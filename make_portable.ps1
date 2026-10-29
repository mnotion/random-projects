# One-click Release build + portable deployment for PageConvert on Windows.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path $PSScriptRoot).Path

function Find-CMake {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($candidate in @(
        'C:\Qt\Tools\CMake_64\bin\cmake.exe',
        'C:\Program Files\CMake\bin\cmake.exe'
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Find-QtRoots {
    $roots = @()
    if ($env:PAGECONVERT_QT_ROOT -and (Test-Path $env:PAGECONVERT_QT_ROOT)) {
        $roots += (Resolve-Path $env:PAGECONVERT_QT_ROOT).Path
    }
    if (Test-Path 'C:\Qt') {
        foreach ($versionDir in (Get-ChildItem 'C:\Qt' -Directory -ErrorAction SilentlyContinue)) {
            foreach ($kitDir in (Get-ChildItem $versionDir.FullName -Directory -ErrorAction SilentlyContinue)) {
                if ((Test-Path (Join-Path $kitDir.FullName 'bin\windeployqt.exe')) -and
                    (Test-Path (Join-Path $kitDir.FullName 'lib\cmake\Qt6\Qt6Config.cmake'))) {
                    $roots += $kitDir.FullName
                }
            }
            # Some Qt installs have the kit contents directly inside a version folder.
            if ((Test-Path (Join-Path $versionDir.FullName 'bin\windeployqt.exe')) -and
                (Test-Path (Join-Path $versionDir.FullName 'lib\cmake\Qt6\Qt6Config.cmake'))) {
                $roots += $versionDir.FullName
            }
        }
    }
    return @($roots | Sort-Object -Unique)
}

function Find-MinGWTools {
    $toolRoots = @()
    if (Test-Path 'C:\Qt\Tools') {
        $toolRoots = Get-ChildItem 'C:\Qt\Tools' -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^mingw.*_64$' }
    }
    # Prefer the MinGW toolchain commonly paired with Qt 6.12, then other installed
    # 64-bit MinGW kits, if the computer has a different compatible kit installed.
    $ordered = @($toolRoots | Sort-Object @{
        Expression = {
            if ($_.Name -match '^mingw1310_64$') { 0 }
            elseif ($_.Name -match '^mingw13') { 1 }
            elseif ($_.Name -match '^mingw12') { 2 }
            elseif ($_.Name -match '^mingw11') { 3 }
            else { 4 }
        }
    }, Name -Descending)
    foreach ($tool in $ordered) {
        $gxx = Join-Path $tool.FullName 'bin\g++.exe'
        $make = Join-Path $tool.FullName 'bin\mingw32-make.exe'
        if ((Test-Path $gxx) -and (Test-Path $make)) {
            return @{ Root = $tool.FullName; Compiler = $gxx; Make = $make }
        }
    }
    return $null
}

Write-Host 'PageConvert portable builder' -ForegroundColor Cyan
Write-Host 'Finding Qt and MinGW directly (no Qt Creator cache required)...'

if (-not (Test-Path (Join-Path $repo 'CMakeLists.txt')) -or
    -not (Test-Path (Join-Path $repo 'main.cpp'))) {
    throw 'The project files CMakeLists.txt and main.cpp must be in the same folder as this script.'
}

$qtRoots = Find-QtRoots
if (-not $qtRoots -or $qtRoots.Count -eq 0) {
    throw 'Could not find an installed Qt 6 kit. Make sure Qt 6.12 MinGW 64-bit is installed under C:\Qt, including the Qt PDF module.'
}

# Prefer Qt 6.12 MinGW 64-bit if present, otherwise use a detected Qt kit with MinGW.
$qtRoot = $qtRoots |
    Where-Object { $_ -match '6\.12(\.0)?[\\/].*mingw_64$' } |
    Select-Object -First 1
if (-not $qtRoot) {
    $qtRoot = $qtRoots | Where-Object { $_ -match 'mingw_64$' } | Select-Object -First 1
}
if (-not $qtRoot) {
    throw ('Found Qt, but not a MinGW 64-bit kit. Detected: ' + ($qtRoots -join ', '))
}

if (-not (Test-Path (Join-Path $qtRoot 'lib\cmake\Qt6Pdf\Qt6PdfConfig.cmake'))) {
    throw "The Qt PDF module is missing from $qtRoot. Install the matching Qt PDF component using Qt Maintenance Tool."
}

$mingw = Find-MinGWTools
if (-not $mingw) {
    throw 'Could not find MinGW g++.exe and mingw32-make.exe under C:\Qt\Tools. Install the MinGW 64-bit toolchain matching your Qt kit via Qt Maintenance Tool.'
}

$cmake = Find-CMake
if (-not $cmake) {
    throw 'Could not find CMake. Open Qt Maintenance Tool and install CMake, or install CMake for Windows.'
}

$qtBin = Join-Path $qtRoot 'bin'
$compilerBin = Join-Path $mingw.Root 'bin'
$env:PATH = "$qtBin;$compilerBin;$(Split-Path $cmake -Parent);$env:PATH"

$buildDir = Join-Path $repo 'build-portable-release'
if (Test-Path $buildDir) { Remove-Item -LiteralPath $buildDir -Recurse -Force }

Write-Host "Qt: $qtRoot"
Write-Host "Compiler: $($mingw.Compiler)"
Write-Host 'Configuring Release build...' -ForegroundColor Cyan

$configureArgs = @(
    '-S', $repo,
    '-B', $buildDir,
    '-G', 'MinGW Makefiles',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_CXX_COMPILER=$($mingw.Compiler)",
    "-DCMAKE_MAKE_PROGRAM=$($mingw.Make)",
    "-DCMAKE_PREFIX_PATH=$qtRoot"
)
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed. See the first error above.' }

Write-Host 'Compiling PageConvert...' -ForegroundColor Cyan
& $cmake --build $buildDir --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'The C++ build failed. See the first compiler error above.' }

$exe = Join-Path $buildDir 'PageConvert.exe'
if (-not (Test-Path $exe)) { throw "Build finished, but PageConvert.exe was not created at $exe." }

$outDir = Join-Path $repo 'dist\PageConvert'
if (Test-Path $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }
New-Item -ItemType Directory -Path $outDir | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $outDir 'PageConvert.exe')

Write-Host 'Collecting Qt runtime DLLs and plugins...' -ForegroundColor Cyan
$windeployqt = Join-Path $qtBin 'windeployqt.exe'
& $windeployqt '--release' '--compiler-runtime' '--no-translations' (Join-Path $outDir 'PageConvert.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed. See the error above.' }

Write-Host ''
Write-Host 'DONE. Copy this entire folder to the other PC:' -ForegroundColor Green
Write-Host $outDir -ForegroundColor Green
Write-Host 'Run PageConvert.exe inside that folder. The other PC does not need Qt installed.'
Write-Host 'Word -> PDF still requires LibreOffice installed on that PC.'
