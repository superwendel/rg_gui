$ErrorActionPreference = 'Stop'

$expected = [ordered]@{
    'examples/assets/inter_medium_16.font' = 'e31e494cee22d7a7b03355d70862e6631993a55dc75f23b3e0aa896bb1369e83'
    'examples/assets/inter_medium_16.rgba' = '9d67b0f6edfe34294fecd4888e8058a9c5e21e1fa2589b8ff873496b6230e626'
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
foreach ($entry in $expected.GetEnumerator()) {
    $path = Join-Path $repositoryRoot $entry.Key
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required demo asset is missing: $($entry.Key)"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $entry.Value) {
        throw "SHA-256 mismatch for $($entry.Key): expected $($entry.Value), got $actual"
    }
}

Write-Host 'Demo asset SHA-256 checks passed.'
