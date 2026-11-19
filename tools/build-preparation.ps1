param(
    [string]$IdfPath = 'D:/.espressif/v6.1.1/esp-idf',
    [string]$ToolsRoot = 'C:/Espressif'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$pythonEnv = Join-Path $ToolsRoot 'tools/python/v6.1.1/venv'
$pythonPath = Join-Path $pythonEnv 'Scripts/python.exe'
$toolDirs = @(
    (Join-Path $pythonEnv 'Scripts'),
    (Join-Path $ToolsRoot 'tools/cmake/4.0.3/bin'),
    (Join-Path $ToolsRoot 'tools/ninja/1.12.1'),
    (Join-Path $ToolsRoot 'tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin')
)
foreach ($toolDir in $toolDirs) {
    if (-not (Test-Path -LiteralPath $toolDir)) { throw "Missing tool directory: $toolDir" }
}
# Changes last only for this script; no global environment or SDK installation is edited.
$oldPath = $env:PATH
$envNames = @('IDF_PATH','IDF_TOOLS_PATH','IDF_PYTHON_ENV_PATH','IDF_COMPONENT_MANAGER','IDF_CCACHE_ENABLE','PYTHONUTF8','TEMP','TMP')
$saved = @{}
foreach ($name in $envNames) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$createdDrive = $null
$buildRoot = $projectRoot
try {
    # GCC 15.2 response/spec files fail with this project's Unicode path.
    # A temporary drive alias keeps all files in the user's chosen directory.
    if ($projectRoot -match '[^\x00-\x7F]') {
        $existingDrives = @([System.IO.Directory]::GetLogicalDrives())
        foreach ($letter in @('Z','Y','X','W','V','U','T','S','R')) {
            $candidate = $letter + ':'
            if ($existingDrives -notcontains ($candidate + '\')) {
                & subst.exe $candidate $projectRoot
                if ($LASTEXITCODE -ne 0) { throw "Could not create temporary path alias $candidate" }
                $createdDrive = $candidate
                $buildRoot = $candidate + '\'
                break
            }
        }
        if (-not $createdDrive) { throw 'No free temporary drive letter in Z: through R:.' }
        if (-not (Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeLists.txt'))) {
            throw 'Temporary path alias does not point to this project.'
        }
        Write-Output "Temporary build alias: $createdDrive -> $projectRoot"
    }
    $env:PATH = ($toolDirs -join ';') + ';' + $oldPath
    $env:IDF_PATH = $IdfPath
    $env:IDF_TOOLS_PATH = Join-Path $ToolsRoot 'tools'
    $env:IDF_PYTHON_ENV_PATH = $pythonEnv
    $env:IDF_COMPONENT_MANAGER = '0'
    $env:IDF_CCACHE_ENABLE = '0'
    $env:PYTHONUTF8 = '1'
    $buildSuffix = if ($createdDrive) { $createdDrive.Substring(0,1).ToLowerInvariant() } else { 'native' }
    $buildDirectory = Join-Path $buildRoot ('build-preparation-' + $buildSuffix)
    $buildTemp = Join-Path $buildDirectory 'tmp'
    New-Item -ItemType Directory -Path $buildTemp -Force | Out-Null
    $env:TEMP = $buildTemp
    $env:TMP = $buildTemp
    & $pythonPath (Join-Path $IdfPath 'tools/check_python_dependencies.py') --requirements (Join-Path $IdfPath 'tools/requirements/requirements.core.txt') --constraints (Join-Path $ToolsRoot 'tools/espidf.constraints.v6.1.txt')
    if ($LASTEXITCODE -ne 0) { throw 'IDF Python dependency check failed.' }
    # idf.py calls realpath() and expands SUBST back to Unicode. Use its standard
    # CMake/Ninja stages directly so source/build paths retain the ASCII alias.
    & cmake.exe -G Ninja -S $buildRoot -B $buildDirectory -DPYTHON_DEPS_CHECKED=1 "-DPYTHON=$pythonPath" -DESP_PLATFORM=1 -DIDF_TARGET=esp32s3 -DCCACHE_ENABLE=OFF
    if ($LASTEXITCODE -ne 0) { throw 'Preparation CMake configuration failed.' }
    & cmake.exe --build $buildDirectory --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Preparation compilation failed.' }
    Write-Output ('Build output: ' + (Join-Path $projectRoot ('build-preparation-' + $buildSuffix)))
} finally {
    if ($createdDrive) {
        # Remove only the mapping created here, never a file or directory.
        $mappingJson = & $pythonPath -c 'import ctypes,json,sys; b=ctypes.create_unicode_buffer(32768); ctypes.windll.kernel32.QueryDosDeviceW(sys.argv[1],b,len(b)); print(json.dumps(b.value,ensure_ascii=True))' $createdDrive
        $mappingTarget = $mappingJson | ConvertFrom-Json
        if ($mappingTarget -eq ('\??\' + $projectRoot.TrimEnd('\'))) {
            & subst.exe $createdDrive /D
        } else {
            Write-Warning "Alias $createdDrive changed unexpectedly; left untouched."
        }
    }
    $env:PATH = $oldPath
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
