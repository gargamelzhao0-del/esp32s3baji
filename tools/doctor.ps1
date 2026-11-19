param(
    [string]$IdfPath = 'D:/.espressif/v6.1.1/esp-idf',
    [string]$ToolsRoot = 'C:/Espressif'
)
$ErrorActionPreference = 'Stop'
$pythonPath = Join-Path $ToolsRoot 'tools/python/v6.1.1/venv/Scripts/python.exe'
[pscustomobject]@{
    SdkPath = $IdfPath
    SdkExists = Test-Path -LiteralPath (Join-Path $IdfPath 'tools/idf.py')
    PythonPath = $pythonPath
    PythonExists = Test-Path -LiteralPath $pythonPath
    BoardTarget = 'esp32s3'
    ReadOnly = $true
} | Format-List
if (Test-Path -LiteralPath $pythonPath) {
    & $pythonPath -c "import sys,importlib.util; print(sys.version); print({m: bool(importlib.util.find_spec(m)) for m in ['idf_component_manager','serial','jsonschema']})"
    & $pythonPath -m serial.tools.list_ports
}
Write-Output 'A listed COM port is not proof of board identity. This tool does not open ports.'
