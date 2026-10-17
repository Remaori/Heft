# Heft 0.4.1 (draft release notes)

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
