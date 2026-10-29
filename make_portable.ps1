$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path $PSScriptRoot).Path

Write-Host ''
Write-Host 'PageConvert portable builder' -ForegroundColor Cyan
Write-Host 'Finding Qt, MinGW and CMake...'

if (!(Test-Path (Join-Path $repo 'CMakeLists.txt')) -or !(Test-Path (Join-Path $repo 'main.cpp'))) {
    throw 'Keep this script in the same folder as CMakeLists.txt and main.cpp.'
}

# Find the Qt MinGW kit installed by the Qt installer.
$qtRoot = $null
if (Test-Path 'C:\Qt') {
    $versions = Get-ChildItem 'C:\Qt' -Directory -ErrorAction SilentlyContinue
    foreach ($version in $versions) {
        $possible = Join-Path $version.FullName 'mingw_64'
        if ((Test-Path (Join-Path $possible 'bin\windeployqt.exe')) -and
            (Test-Path (Join-Path $possible 'lib\cmake\Qt6\Qt6Config.cmake'))) {
            $qtRoot = $possible
            break
        }
        if ((Test-Path (Join-Path $version.FullName 'bin\windeployqt.exe')) -and
            (Test-Path (Join-Path $version.FullName 'lib\cmake\Qt6\Qt6Config.cmake'))) {
            $qtRoot = $version.FullName
            break
        }
    }
}
if (!$qtRoot) {
    throw 'Qt MinGW 64-bit was not found under C:\Qt. Install Qt 6.12 MinGW 64-bit and Qt PDF using Qt Maintenance Tool.'
}
if (!(Test-Path (Join-Path $qtRoot 'lib\cmake\Qt6Pdf\Qt6PdfConfig.cmake'))) {
    throw "Qt PDF is missing from $qtRoot. Install the matching Qt PDF component using Qt Maintenance Tool."
}

# Prefer MinGW 13.1, the toolchain normally paired with Qt 6.12. Try other installed
# 64-bit MinGW toolchains only if that one is not present.
$toolDirs = @()
if (Test-Path 'C:\Qt\Tools') {
    $toolDirs = @(Get-ChildItem 'C:\Qt\Tools' -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like 'mingw*_64' })
}
$preferred = @($toolDirs | Where-Object { $_.Name -eq 'mingw1310_64' })
if ($preferred.Count -gt 0) {
    $toolDirs = @($preferred) + @($toolDirs | Where-Object { $_.Name -ne 'mingw1310_64' })
}
$compiler = $null
$make = $null
$compilerRoot = $null
foreach ($tool in $toolDirs) {
    $testCompiler = Join-Path $tool.FullName 'bin\g++.exe'
    $testMake = Join-Path $tool.FullName 'bin\mingw32-make.exe'
    if ((Test-Path $testCompiler) -and (Test-Path $testMake)) {
        $compiler = $testCompiler
        $make = $testMake
        $compilerRoot = $tool.FullName
        break
    }
}
if (!$compiler) {
    throw 'MinGW is missing. In Qt Maintenance Tool, install the MinGW 64-bit compiler matching Qt 6.12.'
}

# Find CMake, either from the Qt tools folder or the standard Windows install.
$cmake = $null
$cmakeCandidates = @(
    'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    'C:\Program Files\CMake\bin\cmake.exe'
)
foreach ($candidate in $cmakeCandidates) {
    if (Test-Path $candidate) {
        $cmake = $candidate
        break
    }
}
if (!$cmake) {
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
}
if (!$cmake) {
    throw 'CMake was not found. Install CMake through Qt Maintenance Tool.'
}

$qtBin = Join-Path $qtRoot 'bin'
$compilerBin = Join-Path $compilerRoot 'bin'
$env:PATH = "$qtBin;$compilerBin;$(Split-Path $cmake -Parent);$env:PATH"

Write-Host "Qt: $qtRoot"
Write-Host "Compiler: $compiler"
Write-Host 'Building Release version...' -ForegroundColor Cyan

$buildDir = Join-Path $repo 'build-portable-release'
if (Test-Path $buildDir) { Remove-Item -LiteralPath $buildDir -Recurse -Force }

& $cmake -S $repo -B $buildDir -G 'MinGW Makefiles' "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_MAKE_PROGRAM=$make" "-DCMAKE_PREFIX_PATH=$qtRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake setup failed. See the first error above.' }

& $cmake --build $buildDir --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'The C++ build failed. See the first compiler error above.' }

$exe = Join-Path $buildDir 'PageConvert.exe'
if (!(Test-Path $exe)) { throw 'The build finished but PageConvert.exe was not created.' }

$outDir = Join-Path $repo 'dist\PageConvert'
if (Test-Path $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }
New-Item -ItemType Directory -Path $outDir | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $outDir 'PageConvert.exe')

Write-Host 'Collecting Qt runtime files...' -ForegroundColor Cyan
$windeployqt = Join-Path $qtBin 'windeployqt.exe'
& $windeployqt --release --compiler-runtime --no-translations (Join-Path $outDir 'PageConvert.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt packaging failed. See the error above.' }

Write-Host ''
Write-Host 'SUCCESS: the portable app is here:' -ForegroundColor Green
Write-Host $outDir -ForegroundColor Green
Write-Host 'Copy the entire PageConvert folder to the other PC.'
Write-Host 'Word to PDF still requires LibreOffice to be installed on the other PC.'
