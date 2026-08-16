param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$BuildDir = 'build',
    [string]$JuceDir = '',
    [switch]$BootstrapJuce
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root $BuildDir

if ($JuceDir) {
    $env:JUCE_DIR = (Resolve-Path $JuceDir).Path
} elseif ($BootstrapJuce -and -not (Test-Path (Join-Path $root 'JUCE\CMakeLists.txt'))) {
    git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git (Join-Path $root 'JUCE')
}

cmake -S $root -B $build -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Get-ChildItem $build -Recurse -Filter '*.exe' | Where-Object { $_.FullName -match '_artefacts' } | Select-Object -First 1
$vst = Get-ChildItem $build -Recurse -Filter '*.vst3' | Where-Object { $_.FullName -match '_artefacts' } | Select-Object -First 1
if (-not $exe) { throw 'Standalone executable was not produced.' }
if (-not $vst) { throw 'VST3 bundle was not produced.' }

Write-Host "Standalone: $($exe.FullName)"
Write-Host "VST3: $($vst.FullName)"
