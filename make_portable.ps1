# One-click Release build + portable deployment for PageConvert on Windows.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path $PSScriptRoot).Path
$parent = Split-Path $repo -Parent

function Read-CacheValue([string]$text, [string]$key) {
    $line = $text -split "`r?`n" | Where-Object { $_ -match ("^{0}:[^=]*=" -f [regex]::Escape($key)) } | Select-Object -First 1
    if ($line) { return ($line -replace "^[^=]*=", "") }
    return $null
}

function Find-QtRoot([string]$text) {
    $prefix = Read-CacheValue $text 'CMAKE_PREFIX_PATH'
    $qt6Dir = Read-CacheValue $text 'Qt6_DIR'
    $starts = @()
    if ($prefix) { $starts += ($prefix -split ';') }
    if ($qt6Dir) { $starts += $qt6Dir }

    foreach ($start in $starts) {
        if (-not $start) { continue }
        $p = $start.Trim('"')
        if (Test-Path $p -PathType Leaf) { $p = Split-Path $p -Parent }
        for ($i = 0; $i -lt 7 -and $p; $i++) {
            if (Test-Path (Join-Path $p 'bin\windeployqt.exe')) { return $p }
            $next = Split-Path $p -Parent
            if (-not $next -or $next -eq $p) { break }
            $p = $next
        }
    }

    $usual = @('C:\Qt\6.12.0\mingw_64','C:\Qt\6.12.0\msvc2022_64')
    foreach ($p in $usual) {
        if (Test-Path (Join-Path $p 'bin\windeployqt.exe')) { return $p }
    }
    return $null
}

Write-Host "PageConvert portable builder" -ForegroundColor Cyan
Write-Host "Finding the Qt project and its compiler..."

$candidates = @()
$candidates += Get-ChildItem -Path $repo -Directory -ErrorAction SilentlyContinue
$candidates += Get-ChildItem -Path $parent -Directory -ErrorAction SilentlyContinue
$initialCandidates = @($candidates)
foreach ($d in $initialCandidates) {
    $candidates += Get-ChildItem -Path $d.FullName -Directory -ErrorAction SilentlyContinue
}
$candidates = $candidates | Sort-Object FullName -Unique

$cacheText = $null
foreach ($dir in $candidates) {
    $possible = Join-Path $dir.FullName 'CMakeCache.txt'
    if (-not (Test-Path $possible)) { continue }
    $txt = Get-Content -LiteralPath $possible -Raw
    $projectHome = Read-CacheValue $txt 'CMAKE_HOME_DIRECTORY'
    if ($projectHome -and [IO.Path]::GetFullPath($projectHome).TrimEnd('\') -ieq $repo.TrimEnd('\')) {
        $cacheText = $txt
        break
    }
}
if (-not $cacheText) {
    throw "Couldn't find the project's CMake setup. Open CMakeLists.txt in Qt Creator, select Desktop Qt 6.12 MinGW 64-bit, and configure the project once. Then double-click this file again."
}

$qtRoot = Find-QtRoot $cacheText
if (-not $qtRoot) {
    throw "Couldn't find Qt's windeployqt tool. Check that Qt 6.12.0 MinGW 64-bit and the Qt PDF component are installed in the Qt Maintenance Tool."
}

$compiler = Read-CacheValue $cacheText 'CMAKE_CXX_COMPILER'
if (-not $compiler -or -not (Test-Path $compiler)) {
    # Qt Creator caches can omit the compiler path; discover the installed MinGW compiler instead.
    $toolRoots = @()
    if (Test-Path 'C:\Qt\Tools') {
        $toolRoots = Get-ChildItem 'C:\Qt\Tools' -Directory -Filter 'mingw*' -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
    }
    foreach ($tool in $toolRoots) {
        $candidateCompiler = Join-Path $tool.FullName 'bin\g++.exe'
        if (Test-Path $candidateCompiler) { $compiler = $candidateCompiler; break }
    }
}
if (-not $compiler -or -not (Test-Path $compiler)) {
    throw "I couldn't find the MinGW C++ compiler. Open Qt Maintenance Tool, install the MinGW toolchain that matches Qt 6.12.0, then rerun this script."
}

$compilerBin = Split-Path $compiler -Parent
$makeProgram = Join-Path $compilerBin 'mingw32-make.exe'
if (-not (Test-Path $makeProgram)) {
    $cachedMake = Read-CacheValue $cacheText 'CMAKE_MAKE_PROGRAM'
    if ($cachedMake -and (Test-Path $cachedMake) -and ((Split-Path $cachedMake -Leaf) -match 'mingw32-make|make\.exe')) {
        $makeProgram = $cachedMake
    } else {
        throw "The MinGW compiler was found, but mingw32-make.exe is missing from $compilerBin. Install the matching MinGW toolchain in Qt Maintenance Tool."
    }
}

$cmake = Read-CacheValue $cacheText 'CMAKE_COMMAND'
if (-not $cmake -or -not (Test-Path $cmake)) {
    $cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
}
if (-not $cmake -or -not (Test-Path $cmake)) {
    throw "CMake wasn't found. It is normally included with Qt Creator."
}

$qtBin = Join-Path $qtRoot 'bin'
$cmakeBin = Split-Path $cmake -Parent
$env:PATH = "$qtBin;$compilerBin;$(Split-Path $makeProgram -Parent);$cmakeBin;$env:PATH"

$buildDir = Join-Path $repo 'build-portable-release'
if (Test-Path $buildDir) { Remove-Item -LiteralPath $buildDir -Recurse -Force }
$configureArgs = @('-S', $repo, '-B', $buildDir, '-G', 'MinGW Makefiles',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_CXX_COMPILER=$compiler",
    "-DCMAKE_MAKE_PROGRAM=$makeProgram", "-DCMAKE_PREFIX_PATH=$qtRoot")

Write-Host "Building the Release app..." -ForegroundColor Cyan
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed. See the first error above." }
& $cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "The C++ build failed. See the first compiler error above." }

$exe = Join-Path $buildDir 'PageConvert.exe'
if (-not (Test-Path $exe)) { throw "The build finished but PageConvert.exe was not created." }

$outDir = Join-Path $repo 'dist\PageConvert'
if (Test-Path $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }
New-Item -ItemType Directory -Path $outDir | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $outDir 'PageConvert.exe')

Write-Host "Adding the Qt runtime files..." -ForegroundColor Cyan
$windeployqt = Join-Path $qtBin 'windeployqt.exe'
& $windeployqt '--release' '--compiler-runtime' '--no-translations' (Join-Path $outDir 'PageConvert.exe')
if ($LASTEXITCODE -ne 0) { throw "Qt packaging failed. See the error above." }

Write-Host ""
Write-Host "DONE. Copy this entire folder to the other PC:" -ForegroundColor Green
Write-Host $outDir -ForegroundColor Green
Write-Host "Run PageConvert.exe from inside that folder. The other PC does not need Qt installed."
Write-Host "Word -> PDF still requires LibreOffice on that PC."
