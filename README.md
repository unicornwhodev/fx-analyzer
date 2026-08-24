# Musique Analyzer

Musique Analyzer is a Windows audio-analysis companion for mixing, sound design and final checks. It provides spectrum, spectrogram, oscilloscope, loudness and stereo-field views in a Standalone application or VST3 plug-in.

## Formats

- Windows x64 Standalone
- Windows x64 VST3

## Install a release

1. Download the Windows installer or portable ZIP from this repository's Releases page.
2. Run the installer, or extract the ZIP and copy the complete .vst3 bundle to a VST3 location scanned by your host.
3. Rescan plug-ins in the host, then insert the effect on the track or bus you want to process.

## What you can monitor

- Spectrum and spectrogram detail for tonal balance and transient content.
- Oscilloscope timing, trigger and persistence controls.
- Loudness history, hold and gate controls for session-level monitoring.
- Stereo field, balance, correlation and decay controls for width checks.
- Input and output monitoring workflows, selected through the factory presets.

Use Hold or Freeze when you need to compare a moment in the signal with live audio. Display settings affect the analysis view; they do not replace your host's gain staging or export checks.

## Factory presets

The included bank contains 14 starting points: input and output monitoring, spectrum and sweep focus, spectrogram, oscilloscope, loudness and stereo/correlation views. Select a preset first, then adjust the analysis range, smoothing, visual resolution and the controls specific to the active view.

## Build from source

Requirements: Windows x64, PowerShell, Git, CMake 3.22 or later, Visual Studio 2022 (or Build Tools) with Desktop development with C++, and JUCE 8.0.4.

~~~powershell
.\_build_all.ps1 -Configuration Release -BootstrapJuce
~~~

To use an existing JUCE 8.0.4 checkout:

~~~powershell
.\_build_all.ps1 -Configuration Release -JuceDir C:\Dev\JUCE
~~~

The build produces Standalone and VST3 artefacts.

## Package a local build

~~~powershell
.\_package_release.ps1 -Configuration Release -BootstrapJuce
~~~

The script creates a portable Windows package and, when Inno Setup 6 is installed, a Windows installer. Use the SkipInstaller option when an installer is not required.

## Repository contents

| Path | Purpose |
| --- | --- |
| Source/ | Plug-in source, effect engines and visual assets |
| Presets/ | Factory preset bank |
| FXShared/ | Local shared UI and audio helpers required by this plug-in |
| installer/ | Windows installer definition |

## Licence and support

This project is source-available, not open source. See [LICENSE.md](LICENSE.md) for the permitted use of source and binaries. For a released-build issue, open an issue with the Windows version, host name/version, plug-in format and steps to reproduce it.
