$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$testBase = Join-Path $repoRoot "Intermediate/test-temp/render-gate-publisher"
New-Item -ItemType Directory -Force -Path $testBase | Out-Null
$testRoot = Join-Path $testBase ([Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
$resolvedBase = [IO.Path]::GetFullPath($testBase).TrimEnd('\') + '\'
$resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
if (-not $resolvedTestRoot.StartsWith($resolvedBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe RenderGate publisher test root: $resolvedTestRoot"
}

try {
    . (Join-Path $PSScriptRoot "RenderGateGoldenPublisher.ps1")

    $sourceA = Join-Path $testRoot "source-a.png"
    $sourceB = Join-Path $testRoot "source-b.png"
    $destinationA = Join-Path $testRoot "goldens/a.png"
    $destinationB = Join-Path $testRoot "goldens/b.png"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationA) | Out-Null
    Set-Content -LiteralPath $sourceA -Value "new-a" -NoNewline
    Set-Content -LiteralPath $sourceB -Value "new-b" -NoNewline
    Set-Content -LiteralPath $destinationA -Value "old-a" -NoNewline
    $publisherLockPath = Join-Path $testRoot "publisher.lock"

    $recordA = [PSCustomObject]@{ status = "PENDING"; detail = "" }
    $recordB = [PSCustomObject]@{ status = "PENDING"; detail = "" }
    # Match RunRenderGate.ps1's production collection shape. Wrapping a generic
    # List[object] in @() can fail on Windows PowerShell before parameter binding.
    $candidates = New-Object System.Collections.Generic.List[object]
    $candidates.Add([PSCustomObject]@{ source = $sourceA; destination = $destinationA; record = $recordA })
    $candidates.Add([PSCustomObject]@{ source = $sourceB; destination = $destinationB; record = $recordB })

    $readLockA = Open-RenderGateGoldenReadLock -LockPath $publisherLockPath
    $readLockB = Open-RenderGateGoldenReadLock -LockPath $publisherLockPath
    try {
        $publisherRejectedByReaders = $false
        try {
            Publish-RenderGateGoldenMatrix `
                -Candidates $candidates `
                -TransactionId "reader-contention" `
                -LockPath $publisherLockPath | Out-Null
        }
        catch {
            $publisherRejectedByReaders = $_.Exception.Message -match 'lock unavailable'
        }
        Assert-True $publisherRejectedByReaders "Golden publisher was not excluded by active RenderGate readers."
        Assert-True ((Get-Content -Raw -LiteralPath $destinationA) -eq "old-a") "Reader contention changed the existing golden."
        Assert-True (-not (Test-Path -LiteralPath $destinationB)) "Reader contention created a new golden."
    }
    finally {
        $readLockB.Dispose()
        $readLockA.Dispose()
    }
    $recordA.status = "PENDING"
    $recordB.status = "PENDING"

    $staleArtifact = "$destinationA.rendergate.crashed-run.backup"
    Set-Content -LiteralPath $staleArtifact -Value "recoverable-old-a" -NoNewline
    $staleArtifactRejected = $false
    try {
        Publish-RenderGateGoldenMatrix `
            -Candidates $candidates `
            -TransactionId "different-run" `
            -LockPath $publisherLockPath | Out-Null
    }
    catch {
        $staleArtifactRejected = $_.Exception.Message -match 'unresolved transaction artifact'
    }
    Assert-True $staleArtifactRejected "Publisher ignored an unresolved artifact from a crashed transaction."
    Assert-True ((Get-Content -Raw -LiteralPath $destinationA) -eq "old-a") "Stale-artifact rejection changed the existing golden."
    Assert-True (-not (Test-Path -LiteralPath $destinationB)) "Stale-artifact rejection created a new golden."
    Remove-Item -LiteralPath $staleArtifact -Force
    $recordA.status = "PENDING"
    $recordB.status = "PENDING"

    $injectedFailureObserved = $false
    try {
        Publish-RenderGateGoldenMatrix `
            -Candidates $candidates `
            -TransactionId "rollback" `
            -LockPath $publisherLockPath `
            -FailureAfterPublishCount 2 | Out-Null
    }
    catch {
        $injectedFailureObserved = $true
        $injectedFailureMessage = $_.Exception.Message
    }

    Assert-True $injectedFailureObserved "Expected injected golden publish failure was not observed."
    Assert-True ($injectedFailureMessage -match 'Injected RenderGate golden publish failure') "Rollback test did not reach the injected publication failure."
    Assert-True ((Get-Content -Raw -LiteralPath $destinationA) -eq "old-a") "First golden was not rolled back."
    Assert-True (-not (Test-Path -LiteralPath $destinationB)) "New golden was not removed during rollback."
    Assert-True ($recordA.status -eq "NOT_BLESSED" -and $recordB.status -eq "NOT_BLESSED") "Rollback did not reset all record statuses."
    Assert-True (-not (Get-ChildItem -LiteralPath $testRoot -Recurse -File | Where-Object Name -Match '\.rendergate\.')) "Rollback left transaction artifacts."

    $recordA.status = "PENDING"
    $recordB.status = "PENDING"
    $rollbackFailureObserved = $false
    try {
        Publish-RenderGateGoldenMatrix `
            -Candidates $candidates `
            -TransactionId "preserve-backup" `
            -LockPath $publisherLockPath `
            -FailureAfterPublishCount 1 `
            -FailureDuringRollbackCount 1 | Out-Null
    }
    catch {
        $rollbackFailureObserved = $true
    }

    $preservedBackup = "$destinationA.rendergate.preserve-backup.backup"
    Assert-True $rollbackFailureObserved "Expected injected rollback failure was not observed."
    Assert-True (Test-Path -LiteralPath $preservedBackup) "Failed rollback deleted the only recoverable backup."
    Assert-True ((Get-Content -Raw -LiteralPath $preservedBackup) -eq "old-a") "Preserved backup does not contain the old golden."
    Assert-True ($recordA.detail.Contains($preservedBackup)) "Rollback failure detail does not identify the preserved backup."
    Assert-True ($recordA.status -eq "ROLLBACK_FAILED" -and $recordB.status -eq "ROLLBACK_FAILED") "Incomplete rollback was not reported as ROLLBACK_FAILED."
    Copy-Item -LiteralPath $preservedBackup -Destination $destinationA -Force
    Remove-Item -LiteralPath $preservedBackup -Force

    $heldLock = [IO.File]::Open(
        $publisherLockPath,
        [IO.FileMode]::OpenOrCreate,
        [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::None)
    try {
        $readerRejectedByPublisher = $false
        try {
            $unexpectedReadLock = Open-RenderGateGoldenReadLock -LockPath $publisherLockPath
            $unexpectedReadLock.Dispose()
        }
        catch {
            $readerRejectedByPublisher = $_.Exception.Message -match 'read lock unavailable'
        }
        Assert-True $readerRejectedByPublisher "RenderGate reader was not excluded by an active publisher."

        $recordA.status = "PENDING"
        $recordB.status = "PENDING"
        $contentionObserved = $false
        try {
            Publish-RenderGateGoldenMatrix `
                -Candidates $candidates `
                -TransactionId "contention" `
                -LockPath $publisherLockPath | Out-Null
        }
        catch {
            $contentionObserved = $true
        }
        Assert-True $contentionObserved "Concurrent golden publication was not rejected."
        Assert-True ((Get-Content -Raw -LiteralPath $destinationA) -eq "old-a") "Lock contention changed the first golden."
        Assert-True (-not (Test-Path -LiteralPath $destinationB)) "Lock contention created the second golden."
        Assert-True ($recordA.status -eq "NOT_BLESSED" -and $recordB.status -eq "NOT_BLESSED") "Lock contention did not mark records NOT_BLESSED."
    }
    finally {
        $heldLock.Dispose()
    }

    $recordA.status = "PENDING"
    $recordB.status = "PENDING"
    $cleanupResult = Publish-RenderGateGoldenMatrix `
        -Candidates $candidates `
        -TransactionId "cleanup-failure" `
        -LockPath $publisherLockPath `
        -FailureDuringCleanupCount 1
    $cleanupArtifact = "$destinationA.rendergate.cleanup-failure.backup"
    Assert-True $cleanupResult.committed "Cleanup failure lost the committed publication result."
    Assert-True (-not $cleanupResult.cleanup_complete) "Injected cleanup failure was not reported."
    Assert-True ((Get-Content -Raw -LiteralPath $destinationA) -eq "new-a") "Cleanup failure reverted the first committed golden."
    Assert-True ((Get-Content -Raw -LiteralPath $destinationB) -eq "new-b") "Cleanup failure reverted the second committed golden."
    Assert-True ($recordA.status -eq "BLESSED_CLEANUP_FAILED" -and $recordB.status -eq "BLESSED_CLEANUP_FAILED") "Cleanup failure status hid the committed goldens."
    Assert-True (Test-Path -LiteralPath $cleanupArtifact) "Cleanup failure did not leave the recoverable artifact it reported."
    Remove-Item -LiteralPath $cleanupArtifact -Force
    Set-Content -LiteralPath $destinationA -Value "old-a" -NoNewline
    Remove-Item -LiteralPath $destinationB -Force

    Publish-RenderGateGoldenMatrix `
        -Candidates $candidates `
        -TransactionId "success" `
        -LockPath $publisherLockPath | Out-Null
    Assert-True ((Get-Content -Raw -LiteralPath $destinationA) -eq "new-a") "First golden was not published."
    Assert-True ((Get-Content -Raw -LiteralPath $destinationB) -eq "new-b") "Second golden was not published."
    Assert-True ($recordA.status -eq "BLESSED" -and $recordB.status -eq "BLESSED") "Successful publish did not mark all records BLESSED."
    Assert-True (-not (Get-ChildItem -LiteralPath $testRoot -Recurse -File | Where-Object Name -Match '\.rendergate\.')) "Successful publish left transaction artifacts."

    Write-Host "TestRenderGateGoldenPublisher PASS"
}
finally {
    if (Test-Path -LiteralPath $resolvedTestRoot) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
