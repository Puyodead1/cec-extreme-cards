# Build vjmnupns-<build>.exe (recompiled guest code gen<build>\ + Win32 runtime rt\) with MinGW gcc.
# usage: powershell -ExecutionPolicy Bypass -File build.ps1 [152] [170]     (default: both)
# Run `python recomp.py bin\vjmnupns-<build>.elf gen<build>` first.
param([Parameter(ValueFromRemainingArguments)] [string[]] $Builds)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$env:PATH = "C:\MinGW\bin;$env:PATH"
if (-not $Builds) { $Builds = '152', '170' }
$CF = @('-O1', '-fno-strict-aliasing', '-fwrapv', '-w', '-g0')
$hdrTime = @(Get-ChildItem rt\*.h | ForEach-Object LastWriteTime) | Sort-Object | Select-Object -Last 1

function Compile($srcs, $objdir, $prefix) {
    New-Item -ItemType Directory -Force $objdir | Out-Null
    $jobs = @()
    foreach ($s in $srcs) {
        $o = Join-Path $objdir ($prefix + $s.BaseName + '.o')
        if ((Test-Path $o) -and (Get-Item $o).LastWriteTime -gt $s.LastWriteTime -and (Get-Item $o).LastWriteTime -gt $hdrTime) { continue }
        $p = Start-Process gcc -ArgumentList ($CF + @('-c', "`"$($s.FullName)`"", '-o', "`"$o`"")) -NoNewWindow -PassThru
        $null = $p.Handle   # keeps ExitCode readable
        $jobs += $p
        while (@($jobs | Where-Object { -not $_.HasExited }).Count -ge 12) { Start-Sleep -Milliseconds 100 }
    }
    $jobs | ForEach-Object { $_.WaitForExit() }
    if (@($jobs | Where-Object { $_.ExitCode -ne 0 }).Count) { throw "compile failed in $objdir" }
}

Compile (Get-ChildItem rt\*.c) 'obj\rt' 'rt_'
foreach ($b in $Builds) {
    if (-not (Test-Path "gen$b\dispatch.c")) { throw "gen$b missing: python recomp.py bin\vjmnupns-$b.elf gen$b" }
    Compile (Get-ChildItem "gen$b\*.c") "obj\$b" ''
    & gcc -o "vjmnupns-$b.exe" (Get-ChildItem "obj\$b\*.o").FullName (Get-ChildItem obj\rt\*.o).FullName -lgdi32 -lwinmm -lole32
    if ($LASTEXITCODE) { throw "link $b failed" }
    "built vjmnupns-$b.exe"
}
