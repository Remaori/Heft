# Releasing a new version of Heft

A checklist, in order. Nothing is public until the last step: a tag only creates a
**draft** release that nobody else can see.

## 1. Version and notes

- In `CMakeLists.txt`, bump `project(Heft VERSION x.y.z ...)`.
- In [`CHANGELOG.md`](../CHANGELOG.md), move the notes under **Unreleased** to a new
  `## x.y.z` section and leave "Unreleased" with "Nothing yet.". Write them for people
  who use the plugin: what changed and what they'll notice. This section becomes the
  text of the release.
- Commit both and get them onto `main`.

## 2. Tag

```powershell
git tag vx.y.z
git push origin main vx.y.z
```

Wait for GitHub Actions (the **Actions** tab, about 10 minutes). It builds the plugin,
validates it with pluginval (strictness 10), runs HeftMeasure and HeftTests, and creates
the draft release with the zip. If the tag doesn't match the version in `CMakeLists.txt`,
or `CHANGELOG.md` has no section for it, no draft is created and the log says why.

## 3. Install the zip from the draft

1. On GitHub, open **Releases**, then the draft, and download `Heft-vx.y.z-Windows-x64.zip`.
2. Close REAPER.
3. Replace `%USERPROFILE%\VST3\Heft.vst3` with the `Heft.vst3` folder from the zip.
4. Open REAPER again.

## 4. Test by hand in REAPER

- [ ] Load Heft in an existing project: it sounds the same as with the previous version.
- [ ] Save as, Rename, Delete (the file goes to the Recycle Bin), Import and Open presets folder.
- [ ] Type a name in the Save as dialog: the letters go into the field and REAPER doesn't
      react to them (no playback, no shortcuts).
- [ ] Drag a `.heftpreset` file onto Heft's window.
- [ ] Close the plugin window while a dialog is open.
- [ ] Save, close and reopen the project: the preset, the asterisk and both A/B slots come back.
- [ ] Switch between A and B with Level Match on.

## 5. Test in a second host

Repeat loading the project and the A/B check in JUCE's AudioPluginHost (see "Testing in
a second host" in the [README](../README.md)).

## 6. Publish, or start over

- **Everything passed:** on GitHub, edit the draft and press **Publish release**.
- **Something failed:** delete the draft (Releases → the draft → Delete) and the tag:

  ```powershell
  git push --delete origin vx.y.z
  git tag -d vx.y.z
  ```

  Fix the problem and go back to step 1.
