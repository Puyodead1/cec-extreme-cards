# Recompile helper programs (i386 ELF in vend-<build>\bin) to vend-<build>\bin\<name>.exe, sharing the runtime objects.
# usage: powershell -ExecutionPolicy Bypass -File buildtool.ps1 [-Builds 152,170] [tgascale tgarot ...]
param([string[]] $Builds = @('152', '170'), [Parameter(ValueFromRemainingArguments)] [string[]] $Tools)
$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot
$env:PATH = "C:\MinGW\bin;$env:PATH"
$CF = @('-O1', '-fno-strict-aliasing', '-fwrapv', '-w', '-g0')
& "$PSScriptRoot\build.ps1" @Builds | Out-Null   # makes sure obj\rt\rt_*.o are current
if (-not $Tools) {   # the image helpers the game runs (not the printer converters tga2*)
    $Tools = 'tgaconv', 'tgascale', 'tgarot', 'tgablend', 'tgabrand', 'tgacconv', 'tgacreat', 'tgacrop', 'tgacswap', 'tgafade',
             'tgafilt', 'tgaflip', 'tgagamma', 'tgamask', 'tgamerge', 'tgamono', 'tgapaste', 'tgaramp', 'tgarepl', 'tgatext',
             'tgawarp', 'voronoi', 'trilate', 'dither', 'art_filter'
}
foreach ($b in $Builds) {
    foreach ($t in $Tools) {
        $elf = "vend-$b\bin\$t"
        if (-not (Test-Path $elf)) { "skip $elf (missing)"; continue }
        $g = "tools\gen\$b\$t"; $ob = "tools\obj\$b\$t"
        New-Item -ItemType Directory -Force $g, $ob | Out-Null
        cmd /c "python recomp.py $elf $g 2>&1" | Select-String "ERROR|done"
        if ($LASTEXITCODE) { "FAILED recomp $elf"; continue }
        Get-ChildItem "$g\*.c", "$g\*.h" | ForEach-Object {
            (Get-Content $_.FullName -Raw).Replace('"../rt/', '"../../../../rt/') | Set-Content -NoNewline -Encoding ascii $_.FullName
        }
        $jobs = Get-ChildItem "$g\*.c" | ForEach-Object {
            $p = Start-Process gcc -ArgumentList ($CF + @('-c', $_.FullName, '-o', "$ob\$($_.BaseName).o")) -NoNewWindow -PassThru
            $null = $p.Handle; $p
        }
        $jobs | ForEach-Object { $_.WaitForExit() }
        if (@($jobs | Where-Object { $_.ExitCode -ne 0 }).Count) { "FAILED compile $elf"; continue }
        & gcc -o "vend-$b\bin\$t.exe" (Get-ChildItem "$ob\*.o").FullName (Get-ChildItem obj\rt\*.o).FullName -lgdi32 -lwinmm -lole32
        if ($LASTEXITCODE) { "FAILED link $b/$t"; continue }
        "built vend-$b\bin\$t.exe"
    }
}
