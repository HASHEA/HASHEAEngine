Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$checker = Join-Path $repoRoot "scripts\CheckArchBoundary.ps1"

function Invoke-Checker {
    param([string[]]$Arguments = @())
    $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $checker @Arguments 2>&1
    return @{ ExitCode = $LASTEXITCODE; Output = ($output | Out-String) }
}

# 1. 正向：当前仓库必须 PASS（legacy 名单允许 WARN）
$result = Invoke-Checker
if ($result.ExitCode -ne 0) {
    throw "Positive check failed: expected PASS on current repo, got exit $($result.ExitCode).`n$($result.Output)"
}
if ($result.Output -notmatch "RESULT: PASS") {
    throw "Positive check failed: missing PASS marker.`n$($result.Output)"
}

# 2. 负向：注入一条新的 Editor -> Graphics 越界，必须 FAIL
$injected = Join-Path $repoRoot "project\src\editor\__archgate_selftest_violation.cpp"
try {
    Set-Content -LiteralPath $injected -Value '#include "Graphics/DynamicRHI.h"' -Encoding ASCII
    $result = Invoke-Checker
    if ($result.ExitCode -ne 1) {
        throw "Negative check failed: expected exit 1 with injected violation, got $($result.ExitCode).`n$($result.Output)"
    }
    if ($result.Output -notmatch "__archgate_selftest_violation") {
        throw "Negative check failed: violation report does not mention injected file.`n$($result.Output)"
    }
}
finally {
    Remove-Item -LiteralPath $injected -ErrorAction SilentlyContinue
}

# 3. 名单失配：临时规则里塞一条不再命中的 legacy 条目，必须 FAIL
$rulesPath = Join-Path $repoRoot "tools\ai-dev\rules\arch-boundary-rules.json"
$rules = Get-Content -LiteralPath $rulesPath -Raw | ConvertFrom-Json
$rules.legacy_violations += [pscustomobject]@{
    file = "project/src/editor/__no_such_file.cpp"
    include = "Graphics/*"
    reason = "self-test stale entry"
}
$tempRules = Join-Path ([System.IO.Path]::GetTempPath()) "archgate-selftest-rules.json"
try {
    $rules | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $tempRules -Encoding UTF8
    $result = Invoke-Checker -Arguments @("-RulesPath", $tempRules)
    if ($result.ExitCode -ne 1) {
        throw "Stale-entry check failed: expected exit 1, got $($result.ExitCode).`n$($result.Output)"
    }
    if ($result.Output -notmatch "stale allowlist") {
        throw "Stale-entry check failed: missing stale allowlist report.`n$($result.Output)"
    }
}
finally {
    Remove-Item -LiteralPath $tempRules -ErrorAction SilentlyContinue
}

Write-Host "[TestCheckArchBoundary] All checks passed."
exit 0
