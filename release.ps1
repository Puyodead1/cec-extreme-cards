# Package a ready-to-run Windows release: both exes, each build's pristine data tree with the recompiled helper
# programs, launchers and a short readme, zipped to dist\ExtremeCards-win32.zip.
# The data trees must be clean exports (linux/exportvend.sh from the .pristine trees), not the working vend-*
# folders, which collect logs, counters and camera frames while the game runs.
# usage: powershell -ExecutionPolicy Bypass -File release.ps1 -Pristine152 <dir> -Pristine170 <dir> [-Out dist]
param([Parameter(Mandatory)] [string] $Pristine152, [Parameter(Mandatory)] [string] $Pristine170, [string] $Out = 'dist')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$name = 'ExtremeCards-win32'
$dst = Join-Path $Out $name
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
New-Item -ItemType Directory -Force $dst | Out-Null
$src = @{ '152' = $Pristine152; '170' = $Pristine170 }
foreach ($b in '152', '170') {
    $exe = "vjmnupns-$b.exe"
    if (-not (Test-Path $exe)) { throw "$exe missing: run build.ps1 $b" }
    $p = (Resolve-Path $src[$b]).Path
    if (-not (Test-Path "$p\.junctions")) { throw "$p has no .junctions: not an exportvend.sh export" }
    if (Get-ChildItem $p -Recurse -Force -Attributes ReparsePoint) { throw "$p contains links: export it again without fixlinks" }
    Copy-Item $exe $dst
    robocopy $p "$dst\vend-$b" /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "copy of $p failed" }
    $tools = Get-ChildItem "vend-$b\bin\*.exe"
    if ($tools.Count -lt 25) { throw "only $($tools.Count) helper exes in vend-$b\bin: run buildtool.ps1 -Builds $b (and nsyscfg wps iniconfig)" }
    $tools | Copy-Item -Destination "$dst\vend-$b\bin"
    Remove-Item -Force "$dst\vend-$b\exiterr.dat" -ErrorAction SilentlyContinue
    (Get-Content -Raw release\play.cmd).Replace('@B@', $b) | Set-Content -NoNewline -Encoding ascii "$dst\Play-$b.cmd"
}
Copy-Item release\README.txt $dst
New-Item -ItemType Directory -Force "$dst\prints" | Out-Null
$zip = Join-Path $Out "$name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem   # (Windows' bsdtar crashes partway through vend-170\lib)
[IO.Compression.ZipFile]::CreateFromDirectory((Resolve-Path $dst).Path, (Join-Path (Resolve-Path $Out).Path "$name.zip"),
    [IO.Compression.CompressionLevel]::Optimal, $true)
"{0} ({1:N0} MB)" -f (Resolve-Path $zip), ((Get-Item $zip).Length / 1MB)
exit 0   # not robocopy's "files copied" code
