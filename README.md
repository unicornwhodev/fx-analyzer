# Musique Analyzer

Free Windows x64 audio analyzer from the Musique FX collection.

Musique Analyzer provides spectrum, spectrogram, oscilloscope, loudness and stereo analysis views in a lightweight Standalone/VST3 plugin.

## Download

Ready-to-use builds are intended to be published on the repository **Releases** page:

- Windows x64 installer
- Windows x64 portable package containing Standalone + VST3 + factory presets

## Build from source

Requirements: Windows x64, CMake 3.22+, Visual Studio 2022 with the Desktop development with C++ workload, Git and PowerShell.

```powershell
.\_build_all.ps1 -Configuration Release -BootstrapJuce
```

Or use an existing JUCE 8.0.4 checkout:

```powershell
.\_build_all.ps1 -Configuration Release -JuceDir C:\Dev\JUCE
```

## Create release packages

```powershell
.\_package_release.ps1 -Configuration Release -BootstrapJuce
```

This creates a portable ZIP and, when Inno Setup 6 is installed, a Windows installer in `release/`.

## Repository layout

- `Source/` — plugin source and assets
- `Presets/` — factory presets
- `FXShared/` — small shared runtime/UI headers required by this standalone repository
- `installer/` — Inno Setup definition

Internal DSP test targets, QA reports and monorepo-only tooling are intentionally excluded from the public repository.

## License

The plugin is free to download and use. The source is **source-available**, not open source. Personal inspection, local modification and personal builds are permitted under [LICENSE.md](LICENSE.md). Redistribution, repackaging and commercial source reuse require prior permission.

JUCE is not bundled in the repository and remains subject to its own licence terms.

Copyright © 2026 Charli Billabert / unicorn who dev.
