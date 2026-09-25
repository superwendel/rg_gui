# Install the Windows CI dependencies without a package manager.
# SDL_shadercross has no published releases. Build its pinned CLI and
# SPIRV-Cross dependency, using upstream's hash-verified DXC binary download.
# No LLVM/DXC source build or expiring GitHub Actions artifacts are needed.
[CmdletBinding()]
param(
    [string]$Destination = '',
    [string]$Generator = 'Visual Studio 17 2022'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Windows PowerShell 5.1 uses the .NET Framework download implementation.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
if (-not $Destination) {
    if (-not $env:RUNNER_TEMP) {
        throw 'Pass -Destination for a local install, or run on GitHub Actions.'
    }
    $Destination = Join-Path $env:RUNNER_TEMP 'rg-gui-windows-deps'
}
$Destination = [System.IO.Path]::GetFullPath($Destination)
if ($Destination -match '[\r\n]') { throw 'The dependency path contains a newline.' }
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

# Tested CI/setup default; build.bat also accepts a user's own SDL3 installation.
$sdlVersion = '3.4.14'
$shadercrossRevision = '1ca46e0ef7a9e50c706e7be6ef73ce467bac3b2e'
# This is the SPIRV-Cross gitlink in the shadercross revision above.
$spirvCrossRevision = 'd1d4adbefd411fc4721a2fece15a7f4aaa3dcdfa'

function Get-VerifiedArchive {
    param([string]$Name, [string]$Url, [string]$Sha256)
    $archive = Join-Path $Destination $Name
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        Write-Host "Downloading $Name"
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            try {
                Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $archive
                break
            }
            catch {
                if ($attempt -eq 3) { throw }
                Start-Sleep -Seconds 2
            }
        }
    }
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actual -ne $Sha256) {
        throw "SHA256 mismatch for $archive. Expected $Sha256, got $actual."
    }
    return $archive
}

function Invoke-CMake {
    param([string[]]$Arguments)
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
}

function Assert-File {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-Item -LiteralPath $Path).Length -eq 0) {
        throw "Required dependency file is missing or empty: $Path"
    }
}

function Assert-X64Binary {
    param([string]$Path)
    Assert-File $Path
    $reader = [System.IO.BinaryReader]::new([System.IO.File]::OpenRead($Path))
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE binary: $Path" }
        $reader.BaseStream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 0 -or $offset -gt $reader.BaseStream.Length - 6) {
            throw "Invalid PE header: $Path"
        }
        $reader.BaseStream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "Expected an x64 Windows binary: $Path"
        }
    }
    finally { $reader.Dispose() }
}

# Official SDL release asset digest:
# https://github.com/libsdl-org/SDL/releases/expanded_assets/release-3.4.14
$sdlArchive = Get-VerifiedArchive "SDL3-devel-$sdlVersion-VC.zip" `
    "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVersion/SDL3-devel-$sdlVersion-VC.zip" `
    '2fe279e70d426e9c644b625acb3083eb3cfb263a92f2c5718aff18d24a8b6e96'
$shadercrossArchive = Get-VerifiedArchive "SDL_shadercross-$shadercrossRevision.zip" `
    "https://github.com/libsdl-org/SDL_shadercross/archive/$shadercrossRevision.zip" `
    '6e44f63b3343a046adeebe7b35cdc635bc9efc7b0c0f0f68b5feac0ff67d7320'
$spirvCrossArchive = Get-VerifiedArchive "SPIRV-Cross-$spirvCrossRevision.zip" `
    "https://github.com/KhronosGroup/SPIRV-Cross/archive/$spirvCrossRevision.zip" `
    '186c9f04b3a2b7283d57c322faa2ea28f3fc6f99a395548a93e9bd0016168514'

# Preserve each complete package, including its DLLs, CMake files and licenses.
$sources = Join-Path $Destination 'sources'
Expand-Archive -LiteralPath $sdlArchive -DestinationPath $sources -Force
Expand-Archive -LiteralPath $shadercrossArchive -DestinationPath $sources -Force
Expand-Archive -LiteralPath $spirvCrossArchive -DestinationPath $sources -Force
$sdlRoot = Join-Path $sources "SDL3-$sdlVersion"
$shadercrossSource = Join-Path $sources "SDL_shadercross-$shadercrossRevision"
$spirvCrossSource = Join-Path $sources "SPIRV-Cross-$spirvCrossRevision"
$sdlInclude = Join-Path $sdlRoot 'include'
$sdlLib = Join-Path $sdlRoot 'lib\x64'
$sdlBin = $sdlLib
Assert-File (Join-Path $sdlInclude 'SDL3\SDL.h')
Assert-File (Join-Path $sdlLib 'SDL3.lib')
Assert-File (Join-Path $sdlRoot 'cmake\SDL3Config.cmake')
Assert-X64Binary (Join-Path $sdlBin 'SDL3.dll')
Assert-File (Join-Path $shadercrossSource 'CMakeLists.txt')
Assert-File (Join-Path $spirvCrossSource 'CMakeLists.txt')

# The immutable upstream script downloads only the Windows DXC package here:
# https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2602
# dxc_2026_02_20.zip, SHA256:
# a1e89031421cf3c1fca6627766ab3020ca4f962ac7e2caa7fab2b33a8436151e
Push-Location $Destination
try {
    Invoke-CMake @('-DCMAKE_SYSTEM_NAME=Windows', '-P',
        (Join-Path $shadercrossSource 'build-scripts\download-prebuilt-DirectXShaderCompiler.cmake'))
}
finally { Pop-Location }

# Select target AND compiler-host architecture explicitly; do not inherit an
# x86 developer prompt or build the much larger vendored DXC/LLVM dependency.
$spirvCrossBuild = Join-Path $Destination 'spirv-cross-build'
$spirvCrossPrefix = Join-Path $Destination 'spirv-cross'
Invoke-CMake @('-S', $spirvCrossSource, '-B', $spirvCrossBuild,
    '-G', $Generator, '-A', 'x64', '-T', 'host=x64',
    '-DSPIRV_CROSS_SHARED=ON', '-DSPIRV_CROSS_STATIC=OFF',
    '-DSPIRV_CROSS_CLI=OFF', '-DSPIRV_CROSS_ENABLE_TESTS=OFF',
    "-DCMAKE_INSTALL_PREFIX=$spirvCrossPrefix")
Invoke-CMake @('--build', $spirvCrossBuild, '--config', 'Release', '--parallel', '2')
Invoke-CMake @('--install', $spirvCrossBuild, '--config', 'Release')

$shadercrossBuild = Join-Path $Destination 'shadercross-build'
$shadercrossPrefix = Join-Path $Destination 'shadercross'
Invoke-CMake @('-S', $shadercrossSource, '-B', $shadercrossBuild,
    '-G', $Generator, '-A', 'x64', '-T', 'host=x64',
    "-DSDL3_DIR=$(Join-Path $sdlRoot 'cmake')",
    "-Dspirv_cross_c_shared_DIR=$(Join-Path $spirvCrossPrefix 'share\spirv_cross_c_shared\cmake')",
    "-DCMAKE_INSTALL_PREFIX=$shadercrossPrefix",
    '-DSDLSHADERCROSS_SHARED=ON', '-DSDLSHADERCROSS_STATIC=OFF',
    '-DSDLSHADERCROSS_SPIRVCROSS_SHARED=ON', '-DSDLSHADERCROSS_VENDORED=OFF',
    '-DSDLSHADERCROSS_DXC=ON', '-DSDLSHADERCROSS_CLI=ON', '-DSDLSHADERCROSS_TESTS=OFF',
    '-DSDLSHADERCROSS_INSTALL=ON', '-DSDLSHADERCROSS_INSTALL_RUNTIME=ON',
    '-DSDLSHADERCROSS_INSTALL_CPACK=OFF', '-DSDLSHADERCROSS_INSTALL_MAN=OFF')
Invoke-CMake @('--build', $shadercrossBuild, '--config', 'Release', '--parallel', '2')
Invoke-CMake @('--install', $shadercrossBuild, '--config', 'Release')

$shadercrossBin = Join-Path $shadercrossPrefix 'bin'
$shadercrossExe = Join-Path $shadercrossBin 'shadercross.exe'
Assert-X64Binary $shadercrossExe
foreach ($dll in @('SDL3.dll', 'SDL3_shadercross.dll', 'spirv-cross-c-shared.dll', 'dxcompiler.dll', 'dxil.dll')) {
    Assert-X64Binary (Join-Path $shadercrossBin $dll)
}
if ((Get-FileHash -LiteralPath (Join-Path $shadercrossBin 'SDL3.dll')).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $sdlBin 'SDL3.dll')).Hash) {
    throw 'The shadercross runtime does not contain the selected SDL3 DLL.'
}

$paths = [ordered]@{
    SDL3_DIR = $sdlRoot
    SDL3_INCLUDE_DIR = $sdlInclude
    SDL3_LIB_DIR = $sdlLib
    SDL3_BIN_DIR = $sdlBin
    SHADERCROSS_EXE = $shadercrossExe
}
foreach ($entry in $paths.GetEnumerator()) {
    [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    if ($env:GITHUB_ENV) {
        Add-Content -LiteralPath $env:GITHUB_ENV -Value "$($entry.Key)=$($entry.Value)" -Encoding utf8
    }
}
$env:PATH = "$shadercrossBin;$sdlBin;$env:PATH"
if ($env:GITHUB_PATH) {
    Add-Content -LiteralPath $env:GITHUB_PATH -Value $sdlBin -Encoding utf8
    Add-Content -LiteralPath $env:GITHUB_PATH -Value $shadercrossBin -Encoding utf8
}
& $shadercrossExe --help
if ($LASTEXITCODE -ne 0) { throw "shadercross could not start: exit code $LASTEXITCODE" }
Write-Host "Installed SDL $sdlVersion and SDL_shadercross $shadercrossRevision (MSVC x64 Release)."
