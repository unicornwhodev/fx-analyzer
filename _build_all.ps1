param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "build",
    [string]$JuceDir = "",
    [switch]$BootstrapJuce
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSCommandPath
$cmakeFile = Join-Path $repoRoot "CMakeLists.txt"
$cmakeText = Get-Content -LiteralPath $cmakeFile -Raw
$projectMatch = [regex]::Match($cmakeText, 'project\(([^\s\)]+)')
if (-not $projectMatch.Success) { throw "Unable to detect CMake project target." }
$target = $projectMatch.Groups[1].Value

$resolvedJuce = ""
if ($JuceDir) { $resolvedJuce = (Resolve-Path -LiteralPath $JuceDir).Path }
elseif ($BootstrapJuce) {
    $local = Join-Path $repoRoot "JUCE"
    if (-not (Test-Path (Join-Path $local "CMakeLists.txt"))) {
        & git clone --depth 1 --branch 8.0.4 --recurse-submodules https://github.com/juce-framework/JUCE.git $local
        if ($LASTEXITCODE -ne 0) { throw "JUCE bootstrap failed." }
    }
    $resolvedJuce = (Resolve-Path -LiteralPath $local).Path
}

$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$args = @("-S", $repoRoot, "-B", $buildPath, "-Wno-dev")
if ($resolvedJuce) { $args += "-DUWDEVST_JUCE_DIR=$resolvedJuce" }
& cmake @args
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

foreach ($buildTarget in @("${target}_Standalone", "${target}_VST3")) {
    & cmake --build $buildPath --config $Configuration --target $buildTarget
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $buildTarget." }
}

$artifactRoot = Join-Path $buildPath "${target}_artefacts\$Configuration"
if (-not (Get-ChildItem (Join-Path $artifactRoot "Standalone") -File -Filter *.exe -ErrorAction SilentlyContinue | Select-Object -First 1)) { throw "Standalone artifact missing." }
if (-not (Get-ChildItem (Join-Path $artifactRoot "VST3") -Directory -Filter *.vst3 -ErrorAction SilentlyContinue | Select-Object -First 1)) { throw "VST3 artifact missing." }
Write-Host "Build completed: $target ($Configuration)"
