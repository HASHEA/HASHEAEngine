function Open-RenderGateGoldenReadLock {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$LockPath
    )

    if ([string]::IsNullOrWhiteSpace($LockPath)) {
        throw "RenderGate golden read lock requires a lock path."
    }
    $resolvedLockPath = [IO.Path]::GetFullPath($LockPath)
    try {
        $lockDirectory = Split-Path -Parent $resolvedLockPath
        New-Item -ItemType Directory -Force -Path $lockDirectory -ErrorAction Stop | Out-Null
        # Readers never write the coordination file, but ReadWrite + ShareReadWrite
        # lets the first reader create it atomically while remaining mutually
        # incompatible with the publisher's FileShare.None handle.
        return [IO.File]::Open(
            $resolvedLockPath,
            [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::ReadWrite)
    }
    catch {
        throw "RenderGate golden read lock unavailable at '$resolvedLockPath': $($_.Exception.Message)"
    }
}

function Publish-RenderGateGoldenMatrix {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Candidates,
        [Parameter(Mandatory = $true)]
        [string]$TransactionId,
        [Parameter(Mandatory = $true)]
        [string]$LockPath,
        [ValidateRange(0, [int]::MaxValue)]
        [int]$FailureAfterPublishCount = 0,
        [ValidateRange(0, [int]::MaxValue)]
        [int]$FailureDuringRollbackCount = 0,
        [ValidateRange(0, [int]::MaxValue)]
        [int]$FailureDuringCleanupCount = 0
    )

    $transactionToken = $TransactionId -replace '[^A-Za-z0-9_-]', '_'
    if ([string]::IsNullOrWhiteSpace($transactionToken)) {
        throw "RenderGate golden transaction id must contain at least one usable character."
    }
    if ($Candidates.Count -eq 0) {
        throw "RenderGate golden publication requires at least one candidate."
    }
    if ([string]::IsNullOrWhiteSpace($LockPath)) {
        throw "RenderGate golden publication requires a lock path."
    }

    $resolvedLockPath = [IO.Path]::GetFullPath($LockPath)
    $lockStream = $null
    try {
        try {
            $lockDirectory = Split-Path -Parent $resolvedLockPath
            New-Item -ItemType Directory -Force -Path $lockDirectory -ErrorAction Stop | Out-Null
            $lockStream = [IO.File]::Open(
                $resolvedLockPath,
                [IO.FileMode]::OpenOrCreate,
                [IO.FileAccess]::ReadWrite,
                [IO.FileShare]::None)
        }
        catch {
            $lockError = $_.Exception.Message
            foreach ($candidate in $Candidates) {
                $candidate.record.status = "NOT_BLESSED"
                $candidate.record.detail = "golden publication lock unavailable: $resolvedLockPath ($lockError)"
            }
            throw "RenderGate golden publication lock unavailable at '$resolvedLockPath': $lockError"
        }

        $entries = New-Object System.Collections.Generic.List[object]
        $destinationKeys = @{}
        $publishedCount = 0
        try {
            foreach ($candidate in $Candidates) {
                foreach ($requiredProperty in @("source", "destination", "record")) {
                    if ($null -eq $candidate -or $null -eq $candidate.PSObject.Properties[$requiredProperty]) {
                        throw "RenderGate golden candidate is missing required property '$requiredProperty'."
                    }
                }
                if ($null -eq $candidate.record.PSObject.Properties["status"] -or
                    $null -eq $candidate.record.PSObject.Properties["detail"]) {
                    throw "RenderGate golden candidate record requires writable status and detail properties."
                }

                $source = [IO.Path]::GetFullPath([string]$candidate.source)
                $destination = [IO.Path]::GetFullPath([string]$candidate.destination)
                if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                    throw "RenderGate golden candidate source is missing: $source"
                }
                if (Test-Path -LiteralPath $destination -PathType Container) {
                    throw "RenderGate golden destination is a directory: $destination"
                }
                $destinationKey = $destination.ToLowerInvariant()
                if ($destinationKeys.ContainsKey($destinationKey)) {
                    throw "RenderGate golden candidates contain duplicate destination: $destination"
                }
                $destinationKeys[$destinationKey] = $true

                $destinationDirectory = Split-Path -Parent $destination
                $artifactPrefix = "$(Split-Path -Leaf $destination).rendergate."
                if (Test-Path -LiteralPath $destinationDirectory -PathType Container) {
                    $unresolvedArtifacts = @(
                        Get-ChildItem -LiteralPath $destinationDirectory -File -ErrorAction Stop |
                            Where-Object { $_.Name.StartsWith($artifactPrefix, [StringComparison]::OrdinalIgnoreCase) }
                    )
                    if ($unresolvedArtifacts.Count -gt 0) {
                        throw "RenderGate golden destination has an unresolved transaction artifact: $($unresolvedArtifacts.FullName -join ', ')"
                    }
                }

                $temporaryGolden = "$destination.rendergate.$transactionToken.tmp"
                $backupGolden = "$destination.rendergate.$transactionToken.backup"

                $entries.Add([PSCustomObject]@{
                    source          = $source
                    destination     = $destination
                    temporary       = $temporaryGolden
                    backup          = $backupGolden
                    had_destination = Test-Path -LiteralPath $destination -PathType Leaf
                    published       = $false
                    record          = $candidate.record
                })
            }

            foreach ($entry in $entries) {
                $destinationDirectory = Split-Path -Parent $entry.destination
                New-Item -ItemType Directory -Force -Path $destinationDirectory -ErrorAction Stop | Out-Null
                Copy-Item -LiteralPath $entry.source -Destination $entry.temporary -ErrorAction Stop
            }

            foreach ($entry in $entries) {
                if ($entry.had_destination) {
                    Copy-Item -LiteralPath $entry.destination -Destination $entry.backup -ErrorAction Stop
                }
            }

            foreach ($entry in $entries) {
                Move-Item -LiteralPath $entry.temporary -Destination $entry.destination -Force -ErrorAction Stop
                $entry.published = $true
                ++$publishedCount
                if ($FailureAfterPublishCount -gt 0 -and $publishedCount -eq $FailureAfterPublishCount) {
                    throw "Injected RenderGate golden publish failure after $publishedCount file(s)."
                }
            }
        }
        catch {
            $publishError = $_.Exception.Message
            $rollbackErrors = New-Object System.Collections.Generic.List[string]
            $rollbackAttemptCount = 0
            foreach ($entry in $entries) {
                $preserveBackup = $false
                if ($entry.published) {
                    try {
                        ++$rollbackAttemptCount
                        if ($FailureDuringRollbackCount -gt 0 -and
                            $rollbackAttemptCount -eq $FailureDuringRollbackCount) {
                            throw "Injected RenderGate golden rollback failure for $($entry.destination)."
                        }

                        if ($entry.had_destination) {
                            if (-not (Test-Path -LiteralPath $entry.backup -PathType Leaf)) {
                                throw "Rollback backup is missing: $($entry.backup)"
                            }
                            Copy-Item -LiteralPath $entry.backup -Destination $entry.destination -Force -ErrorAction Stop
                        }
                        elseif (Test-Path -LiteralPath $entry.destination) {
                            Remove-Item -LiteralPath $entry.destination -Force -ErrorAction Stop
                        }
                    }
                    catch {
                        $preserveBackup = $entry.had_destination -and
                            (Test-Path -LiteralPath $entry.backup -PathType Leaf)
                        $recoveryDetail = if ($preserveBackup) {
                            "backup preserved at $($entry.backup)"
                        }
                        else {
                            "no usable backup is available"
                        }
                        $rollbackErrors.Add("$($entry.destination): $($_.Exception.Message); $recoveryDetail")
                    }
                }

                if (Test-Path -LiteralPath $entry.temporary) {
                    try {
                        Remove-Item -LiteralPath $entry.temporary -Force -ErrorAction Stop
                    }
                    catch {
                        $rollbackErrors.Add("temporary cleanup $($entry.temporary): $($_.Exception.Message)")
                    }
                }
                if (-not $preserveBackup -and (Test-Path -LiteralPath $entry.backup)) {
                    try {
                        Remove-Item -LiteralPath $entry.backup -Force -ErrorAction Stop
                    }
                    catch {
                        $rollbackErrors.Add("backup cleanup $($entry.backup): $($_.Exception.Message)")
                    }
                }
            }

            $rollbackComplete = $rollbackErrors.Count -eq 0
            $rollbackDetail = if ($rollbackComplete) {
                if ($publishedCount -eq 0) { "no files were published" } else { "all published files were rolled back" }
            }
            else {
                "rollback incomplete: $($rollbackErrors -join '; ')"
            }
            foreach ($candidate in $Candidates) {
                $candidate.record.status = if ($rollbackComplete) { "NOT_BLESSED" } else { "ROLLBACK_FAILED" }
                $candidate.record.detail = "golden matrix publish failed ($publishError); $rollbackDetail"
            }
            throw "RenderGate golden matrix publish failed: $publishError; $rollbackDetail"
        }

        # All destinations are committed at this point. Cleanup failures must not
        # pretend that the new goldens were rolled back.
        $cleanupErrors = New-Object System.Collections.Generic.List[string]
        $cleanupAttemptCount = 0
        foreach ($entry in $entries) {
            foreach ($artifact in @($entry.temporary, $entry.backup)) {
                if (Test-Path -LiteralPath $artifact) {
                    try {
                        ++$cleanupAttemptCount
                        if ($FailureDuringCleanupCount -gt 0 -and
                            $cleanupAttemptCount -eq $FailureDuringCleanupCount) {
                            throw "Injected RenderGate golden cleanup failure for $artifact."
                        }
                        Remove-Item -LiteralPath $artifact -Force -ErrorAction Stop
                    }
                    catch {
                        $cleanupErrors.Add("${artifact}: $($_.Exception.Message)")
                    }
                }
            }
        }

        $cleanupComplete = $cleanupErrors.Count -eq 0
        foreach ($entry in $entries) {
            $entry.record.status = if ($cleanupComplete) { "BLESSED" } else { "BLESSED_CLEANUP_FAILED" }
            $entry.record.detail = if ($cleanupComplete) {
                "golden updated: $($entry.destination)"
            }
            else {
                "golden updated: $($entry.destination); transaction cleanup incomplete: $($cleanupErrors -join '; ')"
            }
        }
        return [PSCustomObject]@{
            committed        = $true
            cleanup_complete = $cleanupComplete
            cleanup_errors   = @($cleanupErrors)
        }
    }
    finally {
        if ($null -ne $lockStream) {
            $lockStream.Dispose()
        }
    }
}
