param(
    [switch]$Help,
    [switch]$Preview
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    $path = Resolve-Path (Join-Path $PSScriptRoot "..")
    if (-not (Test-Path -LiteralPath (Join-Path $path.Path "AshEngine.sln"))) {
        throw "Could not resolve AshEngine repository root from $PSScriptRoot"
    }
    return $path.Path
}

function Quote-Argument {
    param([string]$Value)

    if ($null -eq $Value -or $Value.Length -eq 0) {
        return '""'
    }
    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

function Join-Arguments {
    param([string[]]$Arguments)

    $quoted = @()
    foreach ($argument in $Arguments) {
        $quoted += Quote-Argument $argument
    }
    return ($quoted -join " ")
}

function Get-ProfileConfig {
    param(
        [object]$Baseline,
        [string]$Profile
    )

    $property = $Baseline.profiles.PSObject.Properties[$Profile]
    if ($null -eq $property) {
        throw "Unknown perf gate profile '$Profile'."
    }
    return $property.Value
}

function Select-MenuOption {
    param(
        [string]$Title,
        [string[]]$Options,
        [int]$DefaultIndex = 0
    )

    if ($Options.Count -eq 0) {
        throw "Menu '$Title' has no options."
    }

    $index = [Math]::Max(0, [Math]::Min($DefaultIndex, $Options.Count - 1))
    while ($true) {
        Clear-Host
        Write-Host "AshEngine Perf Gate" -ForegroundColor Cyan
        Write-Host ""
        Write-Host $Title
        Write-Host ""

        for ($i = 0; $i -lt $Options.Count; ++$i) {
            if ($i -eq $index) {
                Write-Host ("> {0}" -f $Options[$i]) -ForegroundColor Yellow
            }
            else {
                Write-Host ("  {0}" -f $Options[$i])
            }
        }

        Write-Host ""
        Write-Host "Up/Down: select    Enter: confirm    Esc: exit"
        $key = [Console]::ReadKey($true)
        switch ($key.Key) {
            "UpArrow" {
                $index = ($index + $Options.Count - 1) % $Options.Count
                continue
            }
            "DownArrow" {
                $index = ($index + 1) % $Options.Count
                continue
            }
            "Enter" {
                return $index
            }
            "Escape" {
                return -1
            }
        }
    }
}

function New-RunnerArguments {
    param(
        [string]$Profile,
        [string]$Configuration,
        [bool]$UseProfileConfiguration,
        [bool]$SkipBuild,
        [bool]$DryRun,
        [bool]$BlessBaseline
    )

    $runnerArguments = @("-Profile", $Profile)
    if (-not $UseProfileConfiguration) {
        $runnerArguments += @("-Configuration", $Configuration)
    }
    if ($SkipBuild) {
        $runnerArguments += "-SkipBuild"
    }
    if ($DryRun) {
        $runnerArguments += "-DryRun"
    }
    if ($BlessBaseline) {
        $runnerArguments += "-BlessBaseline"
    }
    return $runnerArguments
}

function Write-Usage {
    Write-Host "AshEngine Perf Gate interactive menu"
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\RunPerfGateMenu.ps1"
    Write-Host "  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\RunPerfGateMenu.ps1 -Preview"
    Write-Host ""
    Write-Host "The root RunPerfGate.bat opens this menu when launched without arguments."
}

if ($Help) {
    Write-Usage
    exit 0
}

$repoRoot = Get-RepoRoot
$runnerScript = Join-Path $repoRoot "scripts\RunPerfGate.ps1"
$baselinePath = Join-Path $repoRoot "tools\perf\perf_gate_baselines.json"
$baseline = Get-Content -Raw -LiteralPath $baselinePath | ConvertFrom-Json
$profiles = @($baseline.profiles.PSObject.Properties.Name)
if ($profiles.Count -eq 0) {
    throw "No perf gate profiles are defined in $baselinePath."
}

$defaultProfile = if ($profiles -contains "Standard") { "Standard" } else { $profiles[0] }
$defaultProfileConfig = Get-ProfileConfig -Baseline $baseline -Profile $defaultProfile
$defaultArguments = New-RunnerArguments `
    -Profile $defaultProfile `
    -Configuration $defaultProfileConfig.configuration `
    -UseProfileConfiguration $true `
    -SkipBuild $false `
    -DryRun $false `
    -BlessBaseline $false

if ($Preview) {
    Write-Host ("powershell -NoProfile -ExecutionPolicy Bypass -File {0} {1}" -f (Quote-Argument $runnerScript), (Join-Arguments $defaultArguments))
    exit 0
}

if ([Console]::IsInputRedirected) {
    throw "Interactive menu requires console input. Use RunPerfGate.bat with explicit arguments for non-interactive runs."
}

while ($true) {
    $profileIndex = Select-MenuOption -Title "Profile" -Options $profiles -DefaultIndex ([Array]::IndexOf($profiles, $defaultProfile))
    if ($profileIndex -lt 0) {
        exit 0
    }
    $profile = $profiles[$profileIndex]
    $profileConfig = Get-ProfileConfig -Baseline $baseline -Profile $profile

    $configurationOptions = @(
        "Profile default ($($profileConfig.configuration))",
        "Debug",
        "Release"
    )
    $configurationIndex = Select-MenuOption -Title "Configuration" -Options $configurationOptions -DefaultIndex 0
    if ($configurationIndex -lt 0) {
        exit 0
    }
    $useProfileConfiguration = ($configurationIndex -eq 0)
    $configuration = if ($useProfileConfiguration) { [string]$profileConfig.configuration } else { $configurationOptions[$configurationIndex] }

    $buildIndex = Select-MenuOption -Title "Build" -Options @("Build before run", "Skip build") -DefaultIndex 0
    if ($buildIndex -lt 0) {
        exit 0
    }
    $skipBuild = ($buildIndex -eq 1)

    $modeIndex = Select-MenuOption -Title "Run mode" -Options @("Full gate", "Dry run only", "Full gate and bless baseline") -DefaultIndex 0
    if ($modeIndex -lt 0) {
        exit 0
    }
    $dryRun = ($modeIndex -eq 1)
    $blessBaseline = ($modeIndex -eq 2)

    $runnerArguments = New-RunnerArguments `
        -Profile $profile `
        -Configuration $configuration `
        -UseProfileConfiguration $useProfileConfiguration `
        -SkipBuild $skipBuild `
        -DryRun $dryRun `
        -BlessBaseline $blessBaseline

    $commandLine = "powershell -NoProfile -ExecutionPolicy Bypass -File {0} {1}" -f (Quote-Argument $runnerScript), (Join-Arguments $runnerArguments)
    $summaryOptions = @(
        "Run now",
        "Change options",
        "Exit"
    )

    Clear-Host
    Write-Host "AshEngine Perf Gate" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Selected command:"
    Write-Host $commandLine -ForegroundColor Yellow
    Write-Host ""
    Write-Host ("Profile:       {0}" -f $profile)
    Write-Host ("Configuration: {0}" -f ($(if ($useProfileConfiguration) { "Profile default ($configuration)" } else { $configuration })))
    Write-Host ("Build:         {0}" -f ($(if ($skipBuild) { "Skip build" } else { "Build before run" })))
    Write-Host ("Run mode:      {0}" -f ($(if ($dryRun) { "Dry run only" } elseif ($blessBaseline) { "Full gate and bless baseline" } else { "Full gate" })))
    Write-Host ""
    Write-Host "Press any key to choose an action..."
    [Console]::ReadKey($true) | Out-Null

    $summaryIndex = Select-MenuOption -Title "Action" -Options $summaryOptions -DefaultIndex 0
    if ($summaryIndex -eq 1) {
        continue
    }
    if ($summaryIndex -lt 0 -or $summaryIndex -eq 2) {
        exit 0
    }

    Clear-Host
    Write-Host "Running:"
    Write-Host $commandLine -ForegroundColor Yellow
    Write-Host ""
    & powershell -NoProfile -ExecutionPolicy Bypass -File $runnerScript @runnerArguments
    $exitCode = $LASTEXITCODE
    Write-Host ""
    Write-Host ("RunPerfGate exited with code {0}" -f $exitCode)
    exit $exitCode
}
