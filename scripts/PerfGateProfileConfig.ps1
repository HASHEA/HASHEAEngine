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

function Test-PerfGateSchemaVersionOne {
    param([object]$Value)

    $isJsonInteger = $Value -is [System.Int32] -or $Value -is [System.Int64]
    return $isJsonInteger -and [int64]$Value -eq 1
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
    if (-not (Test-PerfGateSchemaVersionOne (Get-PerfGateObjectProperty $profileDocument "schema_version"))) {
        throw "Unsupported perf gate profile schema_version in '$ProfilesPath'."
    }
    if (-not (Test-PerfGateSchemaVersionOne (Get-PerfGateObjectProperty $baselineDocument "schema_version"))) {
        throw "Unsupported perf gate baseline schema_version in '$BaselinePath'."
    }

    $baselineEntries = Get-PerfGateObjectProperty $baselineDocument "baselines"
    if ($baselineEntries -isnot [System.Management.Automation.PSCustomObject]) {
        throw "Perf gate baseline 'baselines' must be a JSON object in '$BaselinePath'."
    }

    $legacyProfilesProperty = $baselineDocument.PSObject.Properties["profiles"]
    if ($null -ne $legacyProfilesProperty -and
        $legacyProfilesProperty.Value -isnot [System.Management.Automation.PSCustomObject]) {
        throw "Perf gate baseline 'profiles' must be a JSON object in '$BaselinePath'."
    }

    $newProfilesProperty = $profileDocument.PSObject.Properties["profiles"]
    if ($null -ne $newProfilesProperty -and
        $newProfilesProperty.Value -isnot [System.Management.Automation.PSCustomObject]) {
        throw "Perf gate profile 'profiles' must be a JSON object in '$ProfilesPath'."
    }
    $newProfiles = if ($null -eq $newProfilesProperty) { $null } else { $newProfilesProperty.Value }

    $profiles = [ordered]@{}
    $sources = [ordered]@{}
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
