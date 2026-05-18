[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MSBuildPath,

    [Parameter(Mandatory = $true)]
    [string]$SolutionPath,

    [Parameter(Mandatory = $true)]
    [string]$Target,

    [Parameter(Mandatory = $true)]
    [string]$Configuration,

    [Parameter(Mandatory = $true)]
    [string]$Platform,

    [switch]$MaxCpuCount,

    [string]$Verbosity = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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

function Get-CanonicalPathValue {
    $pathValue = [System.Environment]::GetEnvironmentVariable("PATH", "Process")
    if ([string]::IsNullOrEmpty($pathValue)) {
        $pathValue = [System.Environment]::GetEnvironmentVariable("Path", "Process")
    }
    if ($null -eq $pathValue) {
        return ""
    }
    return $pathValue
}

function Repair-CurrentProcessPathEnvironment {
    $pathValue = Get-CanonicalPathValue
    if ($pathValue.Length -eq 0) {
        return
    }

    [System.Environment]::SetEnvironmentVariable("PATH", $null, "Process")
    [System.Environment]::SetEnvironmentVariable("Path", $null, "Process")
    [System.Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")
}

function Set-SanitizedProcessEnvironment {
    param([System.Diagnostics.ProcessStartInfo]$ProcessStartInfo)

    $pathValue = Get-CanonicalPathValue
    if ($null -eq $ProcessStartInfo.EnvironmentVariables) {
        throw "ProcessStartInfo did not expose an environment collection after PATH normalization."
    }
    $ProcessStartInfo.EnvironmentVariables.Clear()

    $seenNames = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [System.Environment]::GetEnvironmentVariables("Process").GetEnumerator()) {
        $name = [string]$entry.Key
        if ([string]::Equals($name, "Path", [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        if ($seenNames.Add($name)) {
            $ProcessStartInfo.EnvironmentVariables[$name] = [string]$entry.Value
        }
    }

    $ProcessStartInfo.EnvironmentVariables["Path"] = $pathValue
}

try {
    Repair-CurrentProcessPathEnvironment

    if (-not (Test-Path -LiteralPath $MSBuildPath)) {
        throw "MSBuild was not found: $MSBuildPath"
    }
    if (-not (Test-Path -LiteralPath $SolutionPath)) {
        throw "Solution was not found: $SolutionPath"
    }

    $arguments = New-Object System.Collections.Generic.List[string]
    $arguments.Add((Quote-Argument $SolutionPath))
    $arguments.Add("/t:$Target")
    $arguments.Add("/p:Configuration=$Configuration;Platform=$Platform")
    if ($MaxCpuCount) {
        $arguments.Add("/m")
    }
    if (-not [string]::IsNullOrWhiteSpace($Verbosity)) {
        $arguments.Add("/v:$Verbosity")
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $MSBuildPath
    $psi.Arguments = ($arguments -join " ")
    $psi.WorkingDirectory = Split-Path -Parent $SolutionPath
    $psi.UseShellExecute = $false
    Set-SanitizedProcessEnvironment $psi

    $process = [System.Diagnostics.Process]::Start($psi)
    $process.WaitForExit()
    exit $process.ExitCode
}
catch {
    Write-Error ("InvokeMSBuild failed at line {0}: {1}" -f $_.InvocationInfo.ScriptLineNumber, $_.Exception.Message)
    exit 1
}
