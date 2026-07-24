param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('x86', 'x64')]
    [string]$Platform,
    [string]$Configuration = 'Release-static'
)

$ErrorActionPreference = 'Stop'
$test_path = Join-Path $PSScriptRoot "../build/$Platform/$Configuration/ts_sync_condition_test.exe"

if ((Test-Path -LiteralPath $test_path) -eq $false) {
    throw "TS sync condition test was not built: $test_path"
}

& $test_path
if ($LASTEXITCODE -ne 0) {
    throw "TS sync condition test failed. platform: $Platform"
}
