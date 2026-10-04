[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Generator = 'Visual Studio 18 2026',
    [string]$Architecture = 'x64',
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDirectory = Join-Path $projectRoot 'build-publish'
$distDirectory = Join-Path $projectRoot 'dist'
$architectureList = 'x86;x64;ARM64;ARM64EC'

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $false)]
        [string[]]$Arguments = @()
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Copy-RequiredFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required publish artifact was not found: $Source"
    }

    $destinationDirectory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
    Copy-Item -Force -LiteralPath $Source -Destination $Destination
}

Push-Location $projectRoot
try {
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    $git = (Get-Command git.exe -ErrorAction Stop).Source

    Invoke-Native $git @('submodule', 'update', '--init', '--recursive')

    if (Test-Path -LiteralPath $distDirectory) {
        Remove-Item -Recurse -Force -LiteralPath $distDirectory
    }
    New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null

    Invoke-Native $cmake @(
        '-S', $projectRoot,
        '-B', $buildDirectory,
        '-G', $Generator,
        '-A', $Architecture,
        '-DFASTPIPE_ENABLE_RUNTIME_STDIO_HOOK=ON',
        '-DFASTPIPE_BUILD_INJECTION_DLLS=ON',
        "-DFASTPIPE_INJECTION_ARCHITECTURES=$architectureList"
    )

    Invoke-Native $cmake @(
        '--build', $buildDirectory,
        '--config', $Configuration,
        '--target', 'fp'
    )

    if (-not $SkipTests) {
        Invoke-Native $cmake @(
            '--build', $buildDirectory,
            '--config', $Configuration,
            '--target', 'fp_tests', 'fp_fixture'
        )
        Invoke-Native 'ctest.exe' @(
            '--test-dir', $buildDirectory,
            '-C', $Configuration,
            '--output-on-failure'
        )
    }

    Copy-RequiredFile `
        (Join-Path $buildDirectory "$Configuration\fp.exe") `
        (Join-Path $distDirectory 'fp.exe')

    foreach ($injectionArchitecture in @('x86', 'x64', 'arm64', 'arm64ec')) {
        $source = Join-Path $buildDirectory "injection-artifacts\$injectionArchitecture\$Configuration\fp_stdio_hook_$injectionArchitecture.dll"
        $destination = Join-Path $distDirectory "fp_stdio_hook_$injectionArchitecture.dll"
        Copy-RequiredFile $source $destination
    }

    $licenseDirectory = Join-Path $distDirectory 'licenses'
    Copy-RequiredFile `
        (Join-Path $projectRoot 'third_party\Detours\LICENSE') `
        (Join-Path $licenseDirectory 'Detours-LICENSE')

    Write-Output "Published fastpipe to $distDirectory"
}
finally {
    Pop-Location
}
