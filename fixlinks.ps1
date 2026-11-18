# Create the directory junctions listed in <vend>\.junctions (written by linux\exportvend.sh).
# usage: powershell -ExecutionPolicy Bypass -File fixlinks.ps1 vend-152 [vend-170 ...]
param([Parameter(ValueFromRemainingArguments)] [string[]] $Trees)
$ErrorActionPreference = 'Stop'
if (-not $Trees) { $Trees = 'vend-152', 'vend-170' }
foreach ($tree in $Trees) {
    $V = (Resolve-Path (Join-Path $PSScriptRoot $tree)).Path
    foreach ($line in Get-Content (Join-Path $V '.junctions')) {
        if (-not $line) { continue }
        $k, $t = $line -split "`t"
        $l = Join-Path $V ($k -replace '/', '\'); $tp = Join-Path $V ($t -replace '/', '\')
        $i = Get-Item -Force -LiteralPath $l -ErrorAction SilentlyContinue
        if ($i -and $i.LinkType -eq 'Junction') { continue }
        if ($i) { throw "$l exists and is not a junction" }
        New-Item -ItemType Junction -Path $l -Target $tp | Out-Null
        "$tree`: junc $k -> $t"
    }
}
