# One-click Release build + portable deployment for the configured Qt Creator MinGW kit.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path $PSScriptRoot).Path
$parent = Split-Path $repo -Parent

function Read-CacheValue([string]$text, [string]$key) {
    $line = $text -split "`r?`n" | Where-Object { $_ -match ("^{0}:[^=]*=" -f [regex]::Escape($key)) } | Select-Object -First 1
    if ($line) { return ($line -replace "^[^=]*=", "") }
    return $null
}

Write-Host "PageConvert portable builder" -ForegroundColor Cyan
Write-Host "Looking for the Qt Creator kit used for this project..."

$candidates = @()
$candidates += Get-ChildItem -Path $repo -Directory -ErrorAction SilentlyContinue
$candidates += Get-ChildItem -Path $parent -Directory -ErrorAction SilentlyContinue
# Only inspect the first layer of each candidate folder; Qt Creator commonly
# creates its build folder beside the source repository.
$initialCandidates = @($candidates)
foreach ($d in $initialCandidates) {
    $candidates += Get-ChildItem -Path $d.FullName -Directory -ErrorAction SilentlyContinue
}
$candidates = $candidates | Sort-Object FullName -Unique

$cachePath = $null
$cacheText = $null
foreach ($dir in $candidates) {
    $possible = Join-Path $dir.FullName 'CMakeCache.txt'
    if (-not (Test-Path $possible)) { continue }
    $txt = Get-Content -LiteralPath $possible -Raw
    $projectHome = Read-CacheValue $txt 'CMAKE_HOME_DIRECTORY'
    if ($projectHome -and [IO.Path]::GetFullPath($projectHome).TrimEnd('\') -ieq $repo.TrimEnd('\')) {
        $cachePath = $possible
        $cacheText = $txt
        break
    }
}
if (-not $cacheText) {
    throw "Couldn't find this project's Qt Creator CMake cache. Open the project in Qt Creator and configure it with the Desktop Qt 6.12 MinGW 64-bit kit once, then double-click this file again."
}

$generator = Read-CacheValue $cacheText 'CMAKE_GENERATOR'
$compiler = Read-CacheValue $cacheText 'CMAKE_CXX_COMPILER'
$makeProgram = Read-CacheValue $cacheText 'CMAKE_MAKE_PROGRAM'
$cmake = Read-CacheValue $cacheText 'CMAKE_COMMAND'
$prefix = Read-CacheValue $cacheText 'CMAKE_PREFIX_PATH'
$qt6Dir = Read-CacheValue $cacheText 'Qt6_DIR'

if (-not $generator -or -not $compiler -or -not (Test-Path $compiler)) {
    throw "The Qt Creator kit cache is missing a usable C++ compiler. In Qt Creator, check the Desktop Qt 6.12 MinGW 64-bit kit."
}
if ($generator -notmatch 'MinGW Makefiles') {
    throw "This one-click script currently expects the MinGW kit shown in your screenshot. Qt Creator's saved generator is '$generator'. Select Desktop Qt 6.12 MinGW 64-bit and configure again."
}
if (-not $cmake -or -not (Test-Path $cmake)) {
    $cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
}
if (-not $cmake -or -not (Test-Path $cmake)) {
    throw "CMake wasn't found. It normally comes with the Qt installation or Qt Creator."
}

$qtRoot = $null
if ($prefix) {
    foreach ($part in ($prefix -split ';')) {
        if (Test-Path (Join-Path $part 'bin\windeployqt.exe')) { $qtRoot = $part; break }
    }
}
if (-not $qtRoot -and $qt6Dir) {
    $qtRoot = Split-Path (Split-Path (Split-Path $qt6Dir -Parent) -Parent) -Parent
}
if (-not $qtRoot -or -not (Test-Path (Join-Path $qtRoot 'bin\windeployqt.exe'))) {
    throw "Couldn't locate windeployqt for this Qt kit. Check that Qt 6.12 MinGW is installed with the Qt PDF component."
}

$compilerBin = Split-Path $compiler -Parent
$qtBin = Join-Path $qtRoot 'bin'
$makeBin = if ($makeProgram -and (Test-Path $makeProgram)) { Split-Path $makeProgram -Parent } else { $compilerBin }
$cmakeBin = Split-Path $cmake -Parent
$env:PATH = "$qtBin;$compilerBin;$makeBin;$cmakeBin;$env:PATH"

$buildDir = Join-Path $repo 'build-portable-release'
if (Test-Path $buildDir) { Remove-Item -LiteralPath $buildDir -Recurse -Force }
$configureArgs = @('-S', $repo, '-B', $buildDir, '-G', $generator,
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_CXX_COMPILER=$compiler",
    "-DCMAKE_PREFIX_PATH=$qtRoot")
if ($makeProgram -and (Test-Path $makeProgram)) {
    $configureArgs += "-DCMAKE_MAKE_PROGRAM=$makeProgram"
}

Write-Host "Building Release version..." -ForegroundColor Cyan
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed. See the error above." }
& $cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed. See the first compiler error above." }

$exe = Join-Path $buildDir 'PageConvert.exe'
if (-not (Test-Path $exe)) { throw "Build finished, but PageConvert.exe wasn't found where expected: $exe" }

$outDir = Join-Path $repo 'dist\PageConvert'
if (Test-Path $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }
New-Item -ItemType Directory -Path $outDir | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $outDir 'PageConvert.exe')

Write-Host "Collecting Qt runtime files..." -ForegroundColor Cyan
$windeployqt = Join-Path $qtBin 'windeployqt.exe'
& $windeployqt '--release' '--compiler-runtime' '--no-translations' (Join-Path $outDir 'PageConvert.exe')
if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed. See the error above." }

Write-Host ""
Write-Host "DONE. Copy this entire folder to the other PC:" -ForegroundColor Green
Write-Host $outDir -ForegroundColor Green
Write-Host "Run PageConvert.exe from inside that folder. The other PC does not need Qt installed."
Write-Host "Word -> PDF still requires LibreOffice installed on that PC."
