[CmdletBinding()]
param(
    [string]$Python = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $scriptDir "..")).Path
$venvDir = Join-Path $projectRoot ".venv"
$venvPython = Join-Path $venvDir "Scripts\python.exe"
$dcfgen = Join-Path $venvDir "Scripts\dcfgen.exe"
$requirements = Join-Path $scriptDir "requirements-dcfgen-windows.txt"

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')"
    }
}

if (-not (Test-Path -LiteralPath $requirements -PathType Leaf)) {
    throw "Missing requirements file: $requirements"
}

if (-not [string]::IsNullOrWhiteSpace($Python)) {
    if (Test-Path -LiteralPath $Python -PathType Leaf) {
        $pythonCommand = (Resolve-Path -LiteralPath $Python).Path
        $pythonArgs = @()
    }
    else {
        $resolved = Get-Command $Python -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -eq $resolved) {
            throw "Python launcher not found: $Python"
        }
        $pythonCommand = $resolved.Source
        $pythonArgs = @()
    }
}
else {
    $py = Get-Command "py.exe" -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $py) {
        $pythonCommand = $py.Source
        $pythonArgs = @("-3")
    }
    else {
        $python = Get-Command "python.exe" -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -eq $python) {
            throw @"
Python 3 was not found.
Install Python for Windows from:
  https://www.python.org/downloads/windows/
Then rerun this script.
"@
        }
        $pythonCommand = $python.Source
        $pythonArgs = @()
    }
}

Write-Host "Project     : $projectRoot"
Write-Host "Python      : $pythonCommand $($pythonArgs -join ' ')"
Write-Host "Requirements: $requirements"

if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
    Invoke-Checked -FilePath $pythonCommand -Arguments ($pythonArgs + @("-m", "venv", $venvDir))
}

# Keep this helper equivalent to the Windows command sequence that was verified
# on a real host: update pip, force the known-good dependency set, then execute
# dcfgen itself. Do not validate Python packages through an embedded `python -c`
# expression; PowerShell quoting can alter nested quotes before Python sees them.
Invoke-Checked -FilePath $venvPython -Arguments @("-m", "pip", "install", "--upgrade", "pip")
Invoke-Checked -FilePath $venvPython -Arguments @(
    "-m", "pip", "install", "--force-reinstall", "-r", $requirements
)

if (-not (Test-Path -LiteralPath $dcfgen -PathType Leaf)) {
    throw "dcfgen.exe was not installed at: $dcfgen"
}

Invoke-Checked -FilePath $dcfgen -Arguments @("--help")
Write-Host "Ready: .\.venv\Scripts\dcfgen.exe"
Write-Host "Note : dcf-tools 2.4.2 may print a pkg_resources deprecation warning; it is non-fatal if --help succeeds."
