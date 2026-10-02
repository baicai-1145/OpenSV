param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [ValidateSet('all', 'OpenSV', 'OpenSVVoice')]
    [string]$Target = 'all',
    [switch]$Fresh,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$juceRoot = Join-Path $projectRoot 'third_party/JUCE'
$juceCommit = 'be29c81492b6151c8ea8d14c840e1311963b3a83'

if (-not (Test-Path -LiteralPath $juceRoot)) {
    & git clone --branch 9.0.3 --depth 1 https://github.com/juce-framework/JUCE.git $juceRoot
    if ($LASTEXITCODE -ne 0) { throw 'Could not fetch JUCE.' }
}

$actualCommit = & git -C $juceRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualCommit.Trim() -ne $juceCommit) {
    throw 'third_party/JUCE must contain the pinned JUCE 9.0.3 commit. Local dependency changes are not overwritten.'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'The MSVC C++ build tools are required.' }
$developerCommand = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
$environmentLines = & $env:ComSpec /d /s /c "`"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialise the MSVC build environment.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

$buildDirectory = Join-Path $projectRoot "build/$Configuration"
$configureArguments = @('-S', $projectRoot, '-B', $buildDirectory, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Configuration")
if ($Fresh) { $configureArguments += '--fresh' }

# CMake and Ninja must decode localised MSVC diagnostics with the same code page.
$originalInputEncoding = [Console]::InputEncoding
$originalOutputEncoding = [Console]::OutputEncoding
try {
    $utf8 = [Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8
    [Console]::OutputEncoding = $utf8
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $buildDirectory --target $Target --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
finally {
    [Console]::InputEncoding = $originalInputEncoding
    [Console]::OutputEncoding = $originalOutputEncoding
}

$executable = Join-Path $buildDirectory "OpenSV_artefacts/$Configuration/OpenSV.exe"
$voiceExecutable = Join-Path $buildDirectory "OpenSVVoice_artefacts/$Configuration/opensv-voice.exe"
if ($Target -ne 'OpenSVVoice') { Write-Output "Built: $executable" }
if ($Target -ne 'OpenSV') { Write-Output "Built: $voiceExecutable" }
if ($Run) {
    if ($Target -eq 'OpenSVVoice') {
        & $voiceExecutable --help
    }
    else {
        Start-Process -FilePath $executable -WorkingDirectory $projectRoot
    }
}
