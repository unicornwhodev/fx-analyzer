# Musique Analyzer

Musique Analyzer is a source-available audio analysis plugin from Unicorn Who Dev.

## Formats

- Windows x64 Standalone
- Windows x64 VST3

## Features

- Spectrum, spectrogram, oscilloscope, loudness and stereo views
- Input/output monitoring
- Factory presets and user presets
- Common Musique FX interface and meters

## Download

Official Windows builds are distributed from the GitHub Releases page of this repository.

## Build from source

Requirements: CMake 3.22+, a C++20 compiler and JUCE 8.0.4. JUCE can be provided locally or fetched automatically by CMake.

```powershell
.\_build_all.ps1 -Configuration Release -BootstrapJuce
```

To prepare the portable ZIP and Windows installer after a successful build:

```powershell
.\_package_release.ps1 -Configuration Release -SkipBuild
```

The repository is self-contained: the small runtime `FXShared` headers required by this plugin are included locally.

## Repository layout

- `Source/` — plugin source and assets
- `FXShared/` — common runtime UI/audio helpers
- `Presets/` — factory preset bank
- `installer/` — Inno Setup definition
- `_build_all.ps1` — Windows build helper
- `_package_release.ps1` — release packaging helper

## License

Source-available, personal-use source license. Official binaries are free to use, including for commercial music/audio production. Redistribution, commercial source reuse and derivative public products are restricted. See [LICENSE.md](LICENSE.md).
