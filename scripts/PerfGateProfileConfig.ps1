function Get-PerfGateObjectProperty {
    param(
        [object]$Object,
        [string]$Name
    )

    if ($null -eq $Object) {
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function Import-PerfGateProfileCatalog {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProfilesPath,
        [Parameter(Mandatory = $true)]
        [string]$BaselinePath
    )

    if (-not (Test-Path -LiteralPath $ProfilesPath -PathType Leaf)) {
        throw "Perf gate profile configuration does not exist: $ProfilesPath"
    }
    if (-not (Test-Path -LiteralPath $BaselinePath -PathType Leaf)) {
        throw "Perf gate baseline configuration does not exist: $BaselinePath"
    }

    $profileDocument = Get-Content -Raw -LiteralPath $ProfilesPath | ConvertFrom-Json
    $baselineDocument = Get-Content -Raw -LiteralPath $BaselinePath | ConvertFrom-Json
    if ([int](Get-PerfGateObjectProperty $profileDocument "schema_version") -ne 1) {
        throw "Unsupported perf gate profile schema_version in '$ProfilesPath'."
    }

    $profiles = [ordered]@{}
    $sources = [ordered]@{}
    $newProfiles = Get-PerfGateObjectProperty $profileDocument "profiles"
    if ($null -ne $newProfiles) {
        foreach ($property in @($newProfiles.PSObject.Properties)) {
            $profiles[$property.Name] = $property.Value
            $sources[$property.Name] = "profiles"
        }
    }

    $legacyProfiles = Get-PerfGateObjectProperty $baselineDocument "profiles"
    if ($null -ne $legacyProfiles) {
        foreach ($property in @($legacyProfiles.PSObject.Properties)) {
            if (-not $profiles.Contains($property.Name)) {
                $profiles[$property.Name] = $property.Value
                $sources[$property.Name] = "baseline.profiles"
            }
        }
    }

    return [PSCustomObject]@{
        profiles_path = $ProfilesPath
        baseline_path = $BaselinePath
        profile_document = $profileDocument
        baseline = $baselineDocument
        profiles = $profiles
        sources = $sources
    }
}

function Get-PerfGateProfileNames {
    param([Parameter(Mandatory = $true)][object]$Catalog)

    return @($Catalog.profiles.Keys | ForEach-Object { [string]$_ })
}

function Get-PerfGateProfileConfig {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Catalog,
        [Parameter(Mandatory = $true)]
        [string]$Profile
    )

    if (-not $Catalog.profiles.Contains($Profile)) {
        throw "Unknown perf gate profile '$Profile'."
    }
    return $Catalog.profiles[$Profile]
}

function Get-PerfGateProfileSource {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Catalog,
        [Parameter(Mandatory = $true)]
        [string]$Profile
    )

    if (-not $Catalog.sources.Contains($Profile)) {
        throw "Unknown perf gate profile '$Profile'."
    }
    return [string]$Catalog.sources[$Profile]
}

function Resolve-PerfGateConfiguration {
    param(
        [Parameter(Mandatory = $true)]
        [object]$ProfileConfig,
        [string]$RequestedConfiguration = ""
    )

    $profileConfiguration = [string](Get-PerfGateObjectProperty $ProfileConfig "configuration")
    if ([string]::IsNullOrWhiteSpace($profileConfiguration)) {
        throw "Perf gate profile has no configuration."
    }

    $resolved = if ([string]::IsNullOrWhiteSpace($RequestedConfiguration)) {
        $profileConfiguration
    }
    else {
        $RequestedConfiguration
    }
    if ($resolved -notin @("Debug", "Release")) {
        throw "Unsupported perf gate configuration '$resolved'."
    }

    $lockedValue = Get-PerfGateObjectProperty $ProfileConfig "configuration_locked"
    $locked = $null -ne $lockedValue -and [bool]$lockedValue
    if ($locked -and -not [string]::Equals($resolved, $profileConfiguration, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Perf gate profile configuration is locked to $profileConfiguration; requested $resolved."
    }
    return $resolved
}
