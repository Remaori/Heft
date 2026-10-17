# Changelog

What changed in each version of Heft, newest first. Each version's section is also the
text of its release on GitHub, so it's written for people who use the plugin.

## Unreleased

Nothing yet.

## 0.4.1

Small fixes only: no new features and no change to the sound. Projects and presets
made with 0.4.0 open exactly as before.

- **Smoother window when Documents is on OneDrive or a network drive.** Heft used to
  check the presets folder on disk many times per second just to update the Save
  button. Now it checks only when you open a menu or save something.
- **No more "broken" presets after an interrupted save.** If Heft or your DAW closed in
  the middle of saving a preset, a leftover temporary file could appear in the preset
  list as a preset that wouldn't load. Temporary files now have a name the list never
  shows, and old leftovers are cleaned up automatically.
- **Save as keeps the capitals you type.** Saving over an existing preset whose name
  differs only in capitals (for example, saving "Kick" over "kick") now renames the
  file as well, so the preset list and the bar always show the same name.

## 0.4.0

Presets and an A/B comparator, built into the plugin so they work the same in any DAW.
The sound doesn't change, and projects made with 0.3.x open with the same knobs.

- **Presets.** A bar at the top with previous/next arrows, a list of factory and user
  presets, Save, and a menu with Save as, Rename, Delete (to the Recycle Bin), Import and
  Open presets folder. An amber asterisk shows when the knobs no longer match the preset.
- **Your presets are files** in `Documents\Heft\Presets`, so you can share them by
  sending the file. You can also drop them onto Heft's window to import them.
- **A/B comparator.** Two complete settings to switch between, plus Copy. Turn on Level
  Match to compare them at the same loudness.
- **Saved with your project:** the current preset, its asterisk and both A/B slots come
  back when you reopen the project.
- One provisional factory preset, *Parallel*.

## 0.3.1

- Heft now shows up as **"Heft (Remaori)"** in your DAW's plugin list (re-scan plugins
  to see the new name). Projects that already use Heft keep working.

## 0.3.0

- **Mix knob** (dry/wet) for parallel processing. The original signal stays perfectly in
  line with the processed one, so blending them doesn't dull the top end.
- **Cleaner Clipper.** It now works at 4x oversampling, which means less harshness when
  you clip hard and also catches peaks that fall between samples. The output still never
  goes above 0 dBFS.
- **Fairer Level Match.** It now compares how loud things sound (like LUFS) instead of
  their raw energy, so bass-heavy material is matched more fairly.
- **No click on bypass.** Switching the DAW's bypass fades over 30 ms.
- Latency is now 122 samples (2.5 ms at 48 kHz); your DAW compensates for it.
- The window is a little taller to fit the Mix knob.

## 0.2.1

- The download now includes `THIRD_PARTY_NOTICES.txt` with the licences of the code
  built into the plugin.
- The plugin file is smaller (7.7 MB instead of 9.3 MB). No change in sound.

## 0.2.0

The first version published in this repository.

- **Heft**, a VST3 fattener for Windows: compression and 4x-oversampled saturation
  controlled by two knobs, **Fatness** (how much) and **Color** (dark and warm to bright
  and aggressive), plus Output, a soft Clipper and Level Match for honest comparisons.
- **New interface:** a big Fatness knob, input, output and gain-reduction meters, a curve
  that shows the saturation, tooltips, double-click to reset a knob, and a resizable
  window that remembers its size in your project.
