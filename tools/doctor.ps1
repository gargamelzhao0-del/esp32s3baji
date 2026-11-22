[CmdletBinding()]
param(
    [string]$IdfPath,
    [string]$ToolsRoot = 'C:/Espressif',
    [string]$PythonEnvironment
)
$ErrorActionPreference = 'Stop'
if (-not $IdfPath) { $IdfPath = Join-Path $ToolsRoot 'frameworks/esp-idf-v5.5.4' }
if (-not $PythonEnvironment) { $PythonEnvironment = Join-Path $ToolsRoot 'python_env/idf5.5_py3.11_env' }

$python = Join-Path $PythonEnvironment 'Scripts/python.exe'
$constraints = Join-Path $ToolsRoot 'espidf.constraints.v5.5.txt'
$paths = [ordered]@{
    'ESP-IDF 5.5.4' = Join-Path $IdfPath 'tools/idf.py'
    'Python' = $python
    'CMake' = Join-Path $ToolsRoot 'tools/cmake/3.30.2/bin/cmake.exe'
    'Ninja' = Join-Path $ToolsRoot 'tools/ninja/1.12.1/ninja.exe'
    'Xtensa GCC' = Join-Path $ToolsRoot 'tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc.exe'
    'ROM ELF files' = Join-Path $ToolsRoot 'tools/esp-rom-elfs/20241011'
    'IDF constraints' = $constraints
}
Write-Output 'Project profile: Waveshare ESP32-S3-Touch-AMOLED-1.75C / esp32s3 / 32 MB flash / 8 MB OPI PSRAM'
$tools = foreach ($item in $paths.GetEnumerator()) {
    [pscustomobject]@{ Tool = $item.Key; Present = Test-Path -LiteralPath $item.Value; Path = $item.Value }
}
$tools | Format-Table -AutoSize -Wrap
if ($tools.Present -contains $false) { throw 'Missing ESP-IDF build prerequisites (see paths above).' }

& $python --version
& $python (Join-Path $IdfPath 'tools/check_python_dependencies.py') --requirements (Join-Path $IdfPath 'tools/requirements/requirements.core.txt') --constraints $constraints
if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF Python dependency check failed.' }
Write-Output 'USB serial port enumeration:'
& $python -m serial.tools.list_ports
if ($LASTEXITCODE -ne 0) { throw 'Serial port enumeration failed.' }
