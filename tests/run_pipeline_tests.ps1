param(
    [Parameter(Mandatory = $true)]
    [string]$Fastpipe,
    [Parameter(Mandatory = $true)]
    [string]$Fixture
)

$ErrorActionPreference = 'Stop'
$size = 20 * 1024 * 1024
$inputFile = Join-Path $PSScriptRoot 'fixture-input.bin'
$outputFile = Join-Path $PSScriptRoot 'fixture-output.bin'

function Invoke-Fastpipe {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$CommandArguments,
        [string]$InputPath,
        [string]$OutputPath
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $Fastpipe
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.Arguments = (($CommandArguments | ForEach-Object { '"' + $_ + '"' }) -join ' ')

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "could not start fastpipe"
    }

    $outputStream = $null
    if ($OutputPath) {
        $outputStream = [System.IO.File]::Create($OutputPath)
    }

    try {
        $outputTask = $null
        if ($outputStream) {
            $outputTask = $process.StandardOutput.BaseStream.CopyToAsync($outputStream)
        } else {
            $outputTask = $process.StandardOutput.ReadToEndAsync()
        }
        $errorTask = $process.StandardError.ReadToEndAsync()

        if ($InputPath) {
            $inputBytes = [System.IO.File]::ReadAllBytes($InputPath)
            $process.StandardInput.BaseStream.Write($inputBytes, 0, $inputBytes.Length)
        }
        $process.StandardInput.Close()
        $process.WaitForExit()
        $null = $outputTask.GetAwaiter().GetResult()
        $errorText = $errorTask.GetAwaiter().GetResult()

        if ($outputStream) {
            $outputStream.Flush()
        }
        if ($errorText) {
            Write-Host $errorText -NoNewline
        }
        return $process.ExitCode
    }
    finally {
        if ($outputStream) {
            $outputStream.Dispose()
        }
        $process.Dispose()
    }
}

try {
    $generateExitCode = Invoke-Fastpipe @($Fixture, 'generate', $size) -OutputPath $inputFile
    if ($generateExitCode -ne 0) { throw "fixture generator failed: $generateExitCode" }

    $singleExitCode = Invoke-Fastpipe @($Fixture, 'copy', '|', $Fixture, 'verify', $size) -InputPath $inputFile -OutputPath $outputFile
    if ($singleExitCode -ne 0) { throw "single pipeline failed: $singleExitCode" }
    if ((Get-Item $outputFile).Length -ne $size) { throw 'single pipeline output size mismatch' }

    $multiExitCode = Invoke-Fastpipe @($Fixture, 'copy', '|', $Fixture, 'copy', '|', $Fixture, 'verify', $size) -InputPath $inputFile -OutputPath $outputFile
    if ($multiExitCode -ne 0) { throw "multi-stage pipeline failed: $multiExitCode" }
    if ((Get-Item $outputFile).Length -ne $size) { throw 'multi-stage pipeline output size mismatch' }

    $verifyExitCode = Invoke-Fastpipe @($Fixture, 'fail', 23, '|', $Fixture, 'verify', 0) -OutputPath $outputFile
    if ($verifyExitCode -ne 0) { throw "last stage exit code propagation failed: $verifyExitCode" }

    $lastExitCode = Invoke-Fastpipe @($Fixture, 'fail', 23, '|', $Fixture, 'fail', 29) -OutputPath $outputFile
    if ($lastExitCode -ne 29) { throw "expected last stage exit code 29, got $lastExitCode" }
}
finally {
    Remove-Item -Force -ErrorAction SilentlyContinue $inputFile, $outputFile
}
