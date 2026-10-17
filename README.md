# Heft

[![Build](https://github.com/Remaori/Heft/actions/workflows/build.yml/badge.svg)](https://github.com/Remaori/Heft/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/Remaori/Heft)](https://github.com/Remaori/Heft/releases/latest)
[![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue.svg)](LICENSE)
![Format: VST3](https://img.shields.io/badge/format-VST3-orange.svg)
![Platform: Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D6.svg)
[![JUCE 9.0.4](https://img.shields.io/badge/JUCE-9.0.4-8DC63F.svg)](https://juce.com)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)
[![pluginval: strictness 10](https://img.shields.io/badge/pluginval-strictness%2010-brightgreen.svg)](https://github.com/Tracktion/pluginval)

VST3 plugin (Windows, 64-bit, stereo) that makes a sound feel thicker, denser
and louder with just a few controls. Built with JUCE 9.0.4 + CMake.

<p align="center"><img src="docs/screenshot.png" alt="Heft UI" width="640"></p>

| Control | What it does |
|---|---|
| **Fatness** (0–100 %) | Raises compression and saturation together. Transparent at 0 %. |
| **Color** (0–100 %) | Saturation character: 0 % dark/warm, 100 % bright/aggressive (more even harmonics). |
| **Mix** (0–100 %) | Dry/wet: blends the processed sound with the original for parallel processing. 100 % = fully processed. |
| **Output** (−24…+12 dB) | Final level, after the mix. |
| **Clipper** | Soft clipper at the very end, oversampled 4x: linear up to −2.5 dBFS, sample peaks never exceed 0 dBFS. |
| **Level Match** | Matches the loudness of the processed sound to the input (K-weighted, like LUFS) for an honest A/B. |

Signal chain: linked peak compressor → pre-emphasis tilt EQ → asymmetric tanh
saturation (4x oversampled) → de-emphasis → DC blocker → level match → **Mix** with the
dry signal → output gain → soft clipper (4x oversampled) → safety clip.

- **Mix stays phase-aligned**: the dry signal runs through the same oversampling filters
  as the processed one (without the saturation), so blending them never comb-filters.
  A plain delay isn't enough: JUCE's integer-latency correction is an allpass, and with
  only a delay Mix 50 % lost 1.5 dB at 20 kHz. Now it is flat within 0.01 dB.
- **Clipper and inter-sample peaks**: clipping at 4x means less aliasing, and it also
  catches peaks that fall *between* samples. The 4x decimation filter can overshoot a
  little, so a hard safety clip at 0 dBFS follows: sample peaks are guaranteed, and in
  the tests the true peak (4x reconstruction) also stayed at 0.0 dBTP. A clipper running
  at the normal rate would let the classic 12 kHz test signal reach about +3 dBTP.
- **Level Match** applies the ITU-R BS.1770 K-weighting filters before measuring over
  0.4 s, so it is close to momentary loudness (LUFS). Bass-heavy material is matched by
  how loud it sounds, not by its raw energy.
- **Bypass**: when the host bypasses Heft, it crossfades (30 ms) to the time-aligned dry
  signal instead of cutting, so there's no click. The chain keeps running while bypassed.

### Interface

- **Meters**: input and output peak levels (IN / OUT) and the compressor's gain
  reduction (GR) with a numeric readout.
- **Saturation curve**: the transfer curve of the saturator for the current Fatness
  and Color. It shows the shape only (scaled to fit), so you can see how hard it bends
  and how asymmetric it is.
- **Level Match readout**: with Level Match on, the switch shows the correction being applied.
- **Knobs**: double-click (or Alt+click) to reset, click the value to type an exact number.
  Hover any control for a tooltip.
- **Resizable**: drag the bottom-right corner (75 %–200 %). The size is saved with the project.

## Presets and A/B

Heft has its own presets and an A/B comparator in the bar at the top. They don't
depend on your DAW's preset system, so they work the same in REAPER, Ableton Live,
FL Studio, Bitwig, Cubase, Studio One or any other VST3 host.

### The preset bar

| Control | What it does |
|---|---|
| **◀ / ▶** | Previous / next preset (factory, then yours), wrapping around at the ends. |
| **Name** | Click for the list: *Factory* presets, then *User* presets. An amber **\*** means the knobs no longer match the preset; move them back and it goes away. |
| **Save** | Saves your changes into the current user preset. On a factory preset, *Default* or *Untitled*, it asks for a name instead. |
| **⋯** | *Save as…*, *Rename…*, *Delete…* (to the Recycle Bin), *Import…* and *Open presets folder*. Rename and Delete only work on your own presets. |

You can also drop `.heftpreset` files onto Heft's window to import them (some DAWs
keep drops to themselves; then use *Import…*).

### What a preset stores

**Fatness, Color, Mix, Output and Clipper.** Not *Level Match* (it's a listening
tool, so loading a preset never turns it on or off) and not the window size.

### Where presets live and how to share them

- Your presets are files in **`Documents\Heft\Presets`**, one `.heftpreset` per preset.
  The file name is the preset name. *Open presets folder* opens it.
- To share one, send the file. To use a preset someone sent you, *Import…* it (or
  just copy it into the folder; it shows up the next time you open the list).
- The format is plain UTF-8 XML, readable in Notepad. Values are in real units:

  ```xml
  <HeftPreset formatVersion="1" pluginVersion="0.4.0">
    <Param id="fatness" value="62.0"/>
    <Param id="color" value="55.0"/>
    <Param id="mix" value="80.0"/>
    <Param id="output" value="-1.5"/>
    <Param id="clipper" value="0"/>
  </HeftPreset>
  ```

  Missing values take their default, values out of range are clamped, and files
  made by a newer Heft are refused with a message instead of loading wrongly.

### Factory presets

They're built into the plugin, so they appear even if only `Heft.vst3` is copied,
and they can't be overwritten from Heft. **Default** (Fatness 25 %, Color 50 %,
Mix 100 %, Output 0 dB, Clipper off) is always first.

To add one: set Heft up in your DAW, save it as a user preset, copy the file from
`Documents\Heft\Presets` to `Presets/Factory/` in this repository, and rebuild. Name
the files `01 Name.heftpreset`, `02 Name.heftpreset`… to choose the order; the
number isn't shown. *Parallel* is provisional: it will be tuned again by ear,
together with the rest of the factory presets.

> **Presets store knob positions, not sound.** If you change the numbers in
> `Source/Tuning.h` later, every preset (yours, the factory ones and the ones other
> people saved) will sound different, just like projects that use Heft. Once presets
> are out, treat any `Tuning.h` change as a breaking change and say so in the release notes.

### A/B comparator

**A | B** holds two complete settings (the five preset values, plus which preset each
one came from and its asterisk). Click the other letter to switch; **Copy** copies the
active setting over the other one (the tooltip says which way). Loading or saving a
preset only affects the active slot.

- For an honest comparison, turn **Level Match** on first: it stays as it is when you
  switch, so A and B are heard at the same loudness.
- Switching glides over ~50 ms (the normal knob smoothing), so it never clicks.
- **Automation**: switching A/B or loading a preset changes the knobs through the host,
  like moving them by hand. If a knob is automated, the automation wins afterwards (and
  the asterisk appears). If your DAW is *recording* automation (touch, latch or write),
  switching A/B or loading a preset gets recorded like any knob move.
- The A/B slot isn't an automatable parameter and doesn't respond to MIDI.

### Saved with your project

The current preset, its asterisk and both A/B slots are saved with the project (and in
your DAW's own presets of Heft), so everything comes back when you reopen it. Projects
made with 0.3.x open with exactly the same sound and knobs: they show *Default* if the
knobs are at their defaults, otherwise *Untitled*.

## Download

Get the latest version from [**Releases**](https://github.com/Remaori/Heft/releases/latest):
download `Heft-vX.Y.Z-Windows-x64.zip`, unzip it and copy the `Heft.vst3` folder into
your VST3 folder (see [Add the folder to REAPER](#add-the-folder-to-reaper-once)).

Development builds of every push to `main` are available from the
[Build workflow](https://github.com/Remaori/Heft/actions/workflows/build.yml): open the
most recent green run and download **Heft-VST3-Windows-x64** (you need to be signed in
to GitHub; these builds expire after 90 days).

## Build from source

Requirements: Windows 10/11 x64, [Git](https://git-scm.com), and Visual Studio 2019
or later (or the free *Build Tools for Visual Studio*) with the **Desktop development
with C++** workload, which includes CMake. JUCE is downloaded automatically.

```powershell
git clone https://github.com/Remaori/Heft.git
cd Heft
.\build.bat
```

To edit the code in VS Code, install the **C/C++** and **CMake Tools** extensions and
set `"C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools"`. CMake Tools uses
`CMakePresets.json` (same `build` folder as `build.bat`), so IntelliSense sees the same
settings as the real build. Visual Studio picks up the preset automatically.

### Build, validate and install

Double-click **`build.bat`**. It does everything: builds in Release, validates
with pluginval (strictness 10) and installs to `%USERPROFILE%\VST3\Heft.vst3`.
If pluginval fails, it does **not** install.

From a terminal, with options:

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1                 # everything (same as build.bat)
powershell -ExecutionPolicy Bypass -File build.ps1 -Strictness 5   # faster validation
powershell -ExecutionPolicy Bypass -File build.ps1 -Measure        # also measure the sound and run the unit tests
powershell -ExecutionPolicy Bypass -File build.ps1 -SkipValidation # build and install only
powershell -ExecutionPolicy Bypass -File build.ps1 -Clean          # from scratch (re-downloads JUCE)
```

If REAPER is open with the plugin loaded, the script stops and tells you
(Windows won't let you replace a DLL that is in use).

### Publishing a new version

1. Bump the version in `CMakeLists.txt` (`project(Heft VERSION 0.2.0 ...)`) and commit it.
2. Tag the commit and push both:

   ```powershell
   git tag v0.2.0
   git push origin main v0.2.0
   ```

GitHub Actions builds and validates the plugin and publishes a release with the
zipped `Heft.vst3`. If the tag doesn't match the version in `CMakeLists.txt`, the
release step fails and nothing is published.

## Add the folder to REAPER (once)

1. REAPER → **Options → Preferences** (Ctrl+P) → **Plug-ins → VST**.
2. Under **VST plug-in paths**, click **Edit path list… → Add path…** and pick
   your `%USERPROFILE%\VST3` folder, e.g. `C:\Users\<you>\VST3`. (You can also type
   `;C:\Users\<you>\VST3` at the end of the field.)
3. Click **Re-scan** (or *Clear cache/re-scan*).
4. It shows up in the FX browser as **VST3: Heft (Remaori)**.

After rebuilding, just close and reopen REAPER. If you changed parameters
(names, ranges), also do a **Re-scan**.

## Testing without risking your projects

- Work in a **new test project** with copies of your audio (kick, bass, vocals, drum bus).
- **Turn your monitoring down** the first time: with high Fatness, quiet
  material can come up by about +10 dB. The clipper is off by default.
- In the FX browser, right-click Heft → **Run as → Dedicated process**:
  if the plugin crashes, REAPER doesn't go down with it.
- **Honest A/B**: turn on *Level Match*, leave *Mix* at 100 % and *Output* at 0 dB, and
  toggle the FX bypass. After a big change, wait ~1 s (the level matcher uses a 0.4 s window).
- Before using it in a real project, do **File → Save project as…** (a copy).
- If a project ever fails to open because of the plugin: **File → Open project**
  and tick **Open with FX offline (recovery mode)**.
- Don't change the parameter IDs, ranges or meaning in `Source/Parameters.h`
  once you use the plugin in projects: REAPER stores values with them.

## Where everything is

```
CMakeLists.txt            Project: JUCE version (GIT_TAG 9.0.4), plugin name and codes
build.ps1 / build.bat     Build + validate + install
Source/
  Tuning.h                ★ EVERY number that defines the sound
  Parameters.h            Parameters: names, ranges and defaults; presetParamIds (what a preset stores)
  PluginProcessor.cpp     The chain: prepareToPlay, processChunk (3 phases) and
                          updateCoefficients (how Fatness/Color drive each stage)
  PluginEditor.cpp        UI layout, meter ballistics and tooltips
  UI/Palette.h            Colours and fonts of the whole UI
  UI/HeftLookAndFeel.h    How knobs, switches, buttons, menus and dialogs are drawn
  UI/PresetBar.h          The preset bar and A/B in the header (asks the PresetManager, shows dialogs)
  UI/Meters.h             IN / OUT level meters and the GR meter
  UI/CurveDisplay.h       Saturation curve display
  DSP/Compressor.h        1. Linked compressor, peak detector, soft knee, makeup
  DSP/TiltFilter.h        2 and 5. Pre-emphasis tilt EQ and its exact inverse
  DSP/Saturator.h         3. Asymmetric tanh (even harmonics)
                          4. The 4x oversampling is juce::dsp::Oversampling (in PluginProcessor)
  DSP/DcBlocker.h         6. 5 Hz DC-blocking filter
  DSP/LevelMatcher.h      7. Level matcher (K-weighted loudness)
  DSP/KWeighting.h        7. ITU-R BS.1770 K-weighting filters used by the level matcher
  DSP/SoftClipper.h       7. Soft clipper
  Presets/PresetFile.h    Reading/writing .heftpreset files and validating names
  Presets/PresetManager.* Preset list, load/save/rename/delete/import, A/B, project state
Presets/Factory/          Factory presets (.heftpreset), compiled into the plugin
tests/Measure.cpp         HeftMeasure: loads the VST3 and measures it (build.ps1 -Measure)
tests/Tests.cpp           HeftTests: unit tests for presets, A/B and project state (build.ps1 -Measure)
.github/workflows/        GitHub Actions: build + pluginval + HeftMeasure + HeftTests; releases on v* tags
docs/screenshot.png       UI screenshot used in this README
LICENSE                   GNU AGPL v3
THIRD_PARTY_NOTICES.txt   Licenses of the third-party code inside the plugin binary
```

## Which numbers to change (all in `Source/Tuning.h`)

| You want… | Change | Current value → try |
|---|---|---|
| Fatness at max to compress more / less | `compThresholdAtMaxDb`, `compRatioAtMax` | −30 dB, 6:1 → −20 dB, 4:1 (gentler) |
| More punch on transients | `compAttackMs` | 2 → 5–10 ms (lets the attack through) |
| More density / "pumping" | `compReleaseMs` | 80 → 40 ms (denser) or 150 ms (smoother) |
| The compressor to add more or less level | `compMakeupAmount` | 0.5 → 0.3 (less) / 0.7 (more) |
| More or less saturation at Fatness 100 % | `driveAtMax` | 4 → 2 (soft) / 8 (very aggressive) |
| Color to change the character more | `tiltMaxDb` | 6 → 9 dB (more pronounced extremes) |
| Move the tilt "center" | `tiltPivotHz` | 700 → 400 (more body) / 1500 (more presence) |
| Even harmonics (tube warmth) | `biasAtMinColor`, `biasAtMaxColor` | 0 / 0.35 → e.g. 0.1 / 0.5 |
| How much "louder" it sounds as Fatness goes up | `fatnessMakeupAtMaxDb` | 6 → 3 (more honest) / 9 |
| Where the clipper starts clipping | `clipperKnee` | 0.75 (−2.5 dBFS) → 0.9 (harder and later) |
| Level matcher speed | `levelMatchWindowSec` | 0.4 → 1.0 s (steadier, slower; 0.4 = momentary loudness) |
| Oversampling factor (saturation) | `oversamplingFactorLog2` | 2 (4x) → 3 (8x: less aliasing, more CPU and latency) |
| Oversampling factor (clipper) | `clipperOversamplingFactorLog2` | 2 (4x) → 1 (2x: less latency, more aliasing) |
| Bypass crossfade length | `bypassFadeSec` | 0.03 s |

After each change: `build.bat` (or `build.ps1 -Measure` to see the
measurements) and reopen REAPER.

## Reference measurements (HeftMeasure, 48 kHz)

- Latency: **122 samples** (2.54 ms at 48 kHz): 61 for the saturation oversampling and 61
  for the clipper's. Reported to the host; the measured delay is 121.96–121.99 samples on
  the wet, dry and clipper paths.
- Fatness 0 %: level change ≤ 0.01 dB, THD+N < −127 dB; flat up to 21 kHz (−0.37 dB at 22 kHz).
- Mix 50 % at Fatness 0 %: flat within 0.01 dB from 100 Hz to 20 kHz (dry and wet aligned).
  Mix 0 % at Fatness 100 % returns the clean input (THD+N −146 dB).
- 1 kHz sine at −6 dBFS, Fatness 50 %: THD+N ≈ −24 dB (Color 0 %) to −20 dB (Color 100 %);
  H2 goes from absent (Color 0 %) to −21 dB (Color 100 %).
- Kick-like hits at −6 dBFS: RMS +2.8 / +3.4 / +2.9 / +2.1 dB at Fatness 25 / 50 / 75 / 100 %,
  with the peaks rising less (more density). At Fatness 100 %, Mix 25 / 50 / 75 % gives
  +0.4 / +0.9 / +1.5 dB.
- Clipper (Fatness 50 %, Output +12 dB): sample peak ≤ 0 dBFS and true peak 0.0 dBTP,
  including a 12 kHz sine with 45° phase (the classic inter-sample-peak test).
- Level Match: loudness (K-weighted) difference 0.00–0.01 dB in all cases, including
  55 Hz kick hits.
- Bypass: no step larger than the signal's own (no click).
- CPU (one core, 48 kHz, 512-sample blocks): about 3 % with the defaults and 4.5 % with
  everything on (Mix below 100 %, clipper, Level Match). Measured on the development PC;
  your numbers will differ.

## Known limitations

- **Limited real-world testing**: it is validated automatically with pluginval
  (strictness 10) and with HeftMeasure, which loads it like a host, but it has only
  been tried briefly in REAPER and not in other DAWs.
- **The sound is not tuned by ear yet**: the values were set with synthetic signals
  (sines, a synthetic kick). What sounds "right" is up to you, in `Tuning.h`.
- **True peak isn't guaranteed**: the clipper guarantees sample peaks (≤ 0 dBFS). It
  catches most inter-sample peaks too (0.0 dBTP in the tests), but the final safety clip
  works on samples, so a true peak slightly above 0 dBTP is still possible. For strict
  true-peak delivery, put a true-peak limiter after Heft.
- **The compressor is not oversampled**, and with a 2 ms attack it distorts
  the lows a bit (part of the "fat" character, but it is not a clean compressor).
- **Level Match is close to LUFS, not exact**: it uses the BS.1770 K-weighting but an
  exponential 0.4 s average instead of a strict 400 ms window, with no gating beyond
  −60 dB. It follows the music with ~0.4 s of delay, can "breathe" on very dynamic
  material and corrects at most ±24 dB. It matches the processed (wet) signal before
  *Mix* and *Output*, so the A/B is exact with *Output* at 0 dB.
- With high Fatness, quiet material comes up a lot (+9.8 dB measured at Fatness 60 %
  on a sine at −20 dBFS). It's part of the effect, but watch your levels.
- At Color 0 % the saturation is symmetric (no even harmonics); the asymmetry
  grows with Color, by design. If you want even harmonics on the "dark" side too, raise `biasAtMinColor`.
- Fixed latency of 122 samples (2.5 ms at 48 kHz, half that at 96 kHz). REAPER
  compensates for it, but you'll notice it when monitoring live through the plugin.
- Stereo in and out only. No M/S mode and no undo for knob or preset changes.
- Presets: no subfolders or categories, tags, search or favourites, and they aren't
  exposed as the host's "programs" (use Heft's own list). Only one placeholder factory
  preset for now; the real ones come after tuning the sound by ear.
- The preset bar, menus and dialogs have only been checked automatically and with
  screenshots; they still need trying in REAPER and at least one other host.
- The GR meter shows the compressor only; the saturation also lowers peaks, which the
  meter doesn't count.
- The bypass crossfade only applies when the host uses the plugin's own VST3 bypass. If a
  host bypasses by simply not calling the plugin, the switch is up to the host.
- While bypassed, Heft keeps processing (that's what makes the crossfade possible), so it
  uses the same CPU as when active.
- The CPU figures come from HeftMeasure on the development PC, not from inside REAPER.
- Windows x64 only; not code-signed.

## License

Copyright (C) 2026 Remaori.

Heft is free software: you can redistribute it and/or modify it under the terms of
the GNU Affero General Public License, version 3. See [LICENSE](LICENSE).

It is built with [JUCE](https://juce.com), which is used under its AGPLv3 license.
The plugin binary also contains third-party code (the VST 3 SDK and the image and
text libraries bundled with JUCE). Their licenses are reproduced in
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt), which ships inside every release zip.
This software is based in part on the work of the Independent JPEG Group.

VST is a registered trademark of Steinberg Media Technologies GmbH.
