# Changelog

What changed in each version of Heft, newest first. Each version's section is also the
text of its release on GitHub, so it's written for people who use the plugin.

## Unreleased

About 40 % less CPU, mono tracks and fixes for presets, projects and broken input. No
change to the sound.

- **About 40 % less CPU.** The oversampling filters, most of Heft's work, now do the same
  calculations without moving data around. The output is identical bit for bit. On the
  development PC, at 48 kHz: 1.9 % of one core with the default settings (was 3.0 %) and
  2.8 % with everything on (was 4.5 %).
- **Works on mono tracks.** Hosts that follow the plugin's channel layouts can now put
  Heft on a mono track (mono in, mono out). Both meter bars show the signal.
- **A broken signal before Heft no longer silences it for good.** If a plugin before Heft
  sent an invalid value (NaN or infinity), Heft kept outputting nothing usable until you
  reloaded it, and an absurdly loud sample could freeze Level Match. Such samples are now
  replaced with silence, and Heft carries on normally.
- **The end of a sound is never cut.** Heft now tells the host that its output goes on for
  about 0.3 s after the input stops (its latency plus a filter's decay). Hosts that pause
  plugins in silence could otherwise cut the last moments of a sound.
- **Rename keeps the capitals you type.** Renaming a preset by changing only its capitals
  (for example "kick" to "Kick") now really renames the file, just like Save as already
  did, so the list and the bar show the same name.
- **A preset caught in an interrupted rename comes back.** If Heft or your DAW closed in
  the middle of renaming a preset, the preset could stay hidden in the presets folder
  under a temporary name. Heft now gives it its name back (or "Name (recovered)" if that
  name is taken) and it shows up in the list again.
- **Safer renaming in a rare case.** If another program deletes a preset at the very
  moment Heft is renaming it, Heft now simply stops instead of attempting a move it
  shouldn't.
- **A project from someone else can't touch files outside your presets folder.** A
  project file could name its preset with a path (like `..\..\Desktop\x`), and Save,
  Rename or Delete would then act on a preset file in that other place. Such a name is
  now ignored and the slot opens as "Untitled", with its settings intact.
- **Odd values in a preset file are read like in a project.** A value that isn't a plain
  number (`abc`, or `62,5` written with a comma) now takes its default, instead of
  reading as 0 or 62.
- **Import can't leave half a preset behind.** Imported presets are now written the same
  safe way as Save (to a temporary file first), so a full disk or a crash in the middle
  can't leave a broken file in the presets folder.

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
