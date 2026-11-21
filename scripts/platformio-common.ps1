function Invoke-Checked {
    param([string]$Executable, [string[]]$CommandArguments)
    & $Executable @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Executable $($CommandArguments -join ' ')"
    }
}

function Find-PlatformIOPython {
    # A Python launcher can select a different installation from the one that
    # has PlatformIO. Prefer virtual environments, then check PATH candidates.
    $candidates = @(
        if ($env:VIRTUAL_ENV) { Join-Path $env:VIRTUAL_ENV 'Scripts\python.exe' }
        if ($env:CONDA_PREFIX) { Join-Path $env:CONDA_PREFIX 'python.exe' }
        Join-Path $projectDirectory '.pio\venv\Scripts\python.exe'
        Join-Path $projectDirectory '.venv\Scripts\python.exe'
        Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
        Get-Command python, py -All -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty Source
        Join-Path $env:USERPROFILE 'anaconda3\python.exe'
        Join-Path $env:USERPROFILE 'miniconda3\python.exe'
    )
    $probe = "import importlib.util, sys; ready = all(importlib.util.find_spec(name) is not None for name in ('platformio', 'serial')); print(sys.executable) if ready else None; sys.exit(0 if ready else 1)"
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        try {
            $resolvedPython = & $candidate -c $probe 2>$null
            if ($LASTEXITCODE -eq 0 -and $resolvedPython) {
                return ($resolvedPython | Select-Object -Last 1).Trim()
            }
        }
        catch {
            # Try the next installation if this interpreter cannot start.
        }
    }
    throw 'No Python installation with PlatformIO and pyserial was found. Run python -m pip install platformio pyserial, then retry.'
}

function Read-BoardPort {
    param([string]$Port)
    if ([string]::IsNullOrWhiteSpace($Port)) {
        $Port = Read-Host 'Serial port or number [COM11]'
        if ([string]::IsNullOrWhiteSpace($Port)) { $Port = 'COM11' }
    }
    $Port = $Port.Trim()
    if ($Port -match '^\d+$') { $Port = "COM$Port" }
    $availablePorts = [IO.Ports.SerialPort]::GetPortNames()
    if ($availablePorts -notcontains $Port) {
        throw "Serial port $Port was not found. Available ports: $($availablePorts -join ', ')"
    }
    return $Port
}

