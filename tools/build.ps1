[CmdletBinding()]
param(
    [string]$ToolsRoot = 'C:/Espressif',
    [string]$IdfPath,
    [string]$PythonEnvironment,
    [switch]$BoardCheck,
    [ValidateRange(1, 64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
if (-not $IdfPath) { $IdfPath = Join-Path $ToolsRoot 'frameworks/esp-idf-v5.5.4' }
if (-not $PythonEnvironment) { $PythonEnvironment = Join-Path $ToolsRoot 'python_env/idf5.5_py3.11_env' }
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$python = Join-Path $PythonEnvironment 'Scripts/python.exe'
$cmake = Join-Path $ToolsRoot 'tools/cmake/3.30.2/bin/cmake.exe'
$ninja = Join-Path $ToolsRoot 'tools/ninja/1.12.1/ninja.exe'
$compiler = Join-Path $ToolsRoot 'tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc.exe'
$rom = Join-Path $ToolsRoot 'tools/esp-rom-elfs/20241011'
$constraints = Join-Path $ToolsRoot 'espidf.constraints.v5.5.txt'
foreach ($item in @($python, $cmake, $ninja, $compiler, $rom, $constraints, (Join-Path $IdfPath 'tools/idf.py'))) {
    if (-not (Test-Path -LiteralPath $item)) { throw "Required ESP-IDF 5.5.4 tool or file missing: $item" }
}

$names = @('PATH','IDF_PATH','IDF_TOOLS_PATH','IDF_PYTHON_ENV_PATH','IDF_TARGET',
           'IDF_COMPONENT_MANAGER','IDF_CCACHE_ENABLE','ESP_ROM_ELF_DIR','PYTHONUTF8','TEMP','TMP')
$saved = @{}
foreach ($key in $names) { $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
$aliasDrive = $null
$root = $projectRoot
$profileName = if ($BoardCheck) { 'g1' } else { 'pet' }
$buildName = "build-firmware-$profileName-native"
try {
    # Keep CMake, GCC specs and response files on the same ASCII path.
    # idf.py resolves SUBST back to the Unicode source path before invoking CMake.
    if ($projectRoot -match '[^\x00-\x7F]') {
        foreach ($letter in @('Z','Y','X','W','V','U','T','S','R')) {
            $drive = $letter + ':'
            if ([IO.Directory]::GetLogicalDrives() -contains ($drive + '\')) { continue }
            & subst.exe $drive $projectRoot
            if ($LASTEXITCODE -ne 0) { continue }
            $aliasDrive = $drive
            $root = $drive + '/'
            $buildName = "build-firmware-$profileName-" + $letter.ToLowerInvariant()
            break
        }
        if (-not $aliasDrive) { throw 'Cannot map the Unicode project path. No free build drive Z: through R:.' }
    }
    $buildDir = "$($root.TrimEnd('/','\'))/$buildName"
    $tempDir = "$buildDir/tmp"
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
    $toolPath = @((Split-Path $python), (Split-Path $cmake), (Split-Path $ninja), (Split-Path $compiler))
    $env:PATH = ($toolPath -join ';') + ';' + $saved['PATH']
    $env:IDF_PATH = $IdfPath
    $env:IDF_TOOLS_PATH = $ToolsRoot
    $env:IDF_PYTHON_ENV_PATH = $PythonEnvironment
    $env:IDF_TARGET = 'esp32s3'
    $env:IDF_COMPONENT_MANAGER = '0'
    $env:IDF_CCACHE_ENABLE = '0'
    $env:ESP_ROM_ELF_DIR = $rom
    $env:PYTHONUTF8 = '1'
    $env:TEMP = $tempDir
    $env:TMP = $tempDir
    & $python (Join-Path $IdfPath 'tools/check_python_dependencies.py') --requirements (Join-Path $IdfPath 'tools/requirements/requirements.core.txt') --constraints $constraints
    if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF Python dependency check failed.' }

    # The original preparation sdkconfig lives at the repository root. Each
    # build tree owns its generated config, seeded from the 1.75C defaults.
    $defaults = "$($root.TrimEnd('/','\'))/sdkconfig.defaults"
    if ($BoardCheck) { $defaults += ";$($root.TrimEnd('/','\'))/config/sdkconfig.board-check.defaults" }
    & $cmake -G Ninja -S $root -B $buildDir "-DPYTHON=$python" "-DCMAKE_MAKE_PROGRAM=$ninja" `
        -DPYTHON_DEPS_CHECKED=1 -DESP_PLATFORM=1 -DCCACHE_ENABLE=OFF -DIDF_TARGET=esp32s3 `
        "-DSDKCONFIG=$buildDir/sdkconfig" "-DSDKCONFIG_DEFAULTS=$defaults"
    if ($LASTEXITCODE -ne 0) { throw 'Firmware CMake configuration failed.' }
    & $cmake --build $buildDir --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed.' }
    Write-Output ("Firmware build directory: " + (Join-Path $projectRoot $buildName))
} finally {
    try {
        if ($aliasDrive) {
            # Another process may have replaced this mapping. Resolve the DOS
            # device itself rather than removing a drive solely by its letter.
            $targetJson = & $python -c 'import ctypes,json,sys; buf=ctypes.create_unicode_buffer(32768); ctypes.windll.kernel32.QueryDosDeviceW(sys.argv[1],buf,len(buf)); print(json.dumps(buf.value))' $aliasDrive
            if (($targetJson | ConvertFrom-Json) -eq ('\??\' + $projectRoot.TrimEnd('\'))) {
                & subst.exe $aliasDrive /D
            } else {
                Write-Warning "The mapping for $aliasDrive has changed; leaving it in place."
            }
        }
    } finally {
        foreach ($key in $names) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
    }
}
