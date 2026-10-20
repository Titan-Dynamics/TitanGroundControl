# TITAN_AGENTS.md

Fork-specific instructions for AI coding agents working on **Titan Ground Control**, Titan Dynamics'
fork of QGroundControl. Read [AGENTS.md](AGENTS.md) first for the upstream conventions; this file covers
what differs in the fork. It is fork-owned (upstream never touches it), so keep fork knowledge here
rather than in `AGENTS.md` / `CLAUDE.md`, which are upstream files.

## Contents

- [Fork Features and Where They Live](#fork-features-and-where-they-live)
- [Deliberate Fork Behavior](#deliberate-fork-behavior)
- [CI and Releases](#ci-and-releases)
- [Building Locally](#building-locally)
- [Syncing with Upstream](#syncing-with-upstream)
- [Known Issues](#known-issues)
- [Commits](#commits)

## Fork Features and Where They Live

| Feature | Files |
| --- | --- |
| Titan AI chat (Claude + Groq Whisper voice) | `src/AI/` (`AIChatController`, `AISystemPrompt.h`, `AIKnowledgeBase.h`, `POIItem.h`), `src/FlyView/AIChatWidget.qml`, `src/FlyView/FlyViewTopRightColumnLayout.qml` (loader) |
| Titan AI settings | `src/Settings/AISettings.*`, `src/Settings/AI.SettingsGroup.json`, `src/AppSettings/AISettings.qml`, "Titan AI" entry in `src/AppSettings/pages/SettingsPages.json` |
| AI hooks on `Vehicle` | `aiChatController`, `loiterRadiusFact()`, `flightModeFromCustomMode()`, `lastGotoCoordinate()`, `guidedModeGotoLocationSent` signal in `src/Vehicle/Vehicle.*` |
| AI voice replies while muted | `AudioOutput::say(text, mods, ignoreMute, rate)` and `stop()` in `src/Utilities/Audio/AudioOutput.*` |
| Map POIs, goto indicator sync, Add POI | `src/FlyView/FlyViewMap.qml`, `src/FlyView/FlyViewMapClickDropPanel.qml` |
| Fly View toolbar (red Arm/Disarm, FLTMODE1-6 buttons, messages icon + severity pill) | `src/Toolbar/FlyViewToolBar.qml`, `src/Toolbar/MainStatusIndicator.qml` |
| Short FBW mode names | `src/QmlControls/TDHelpers.qml` (singleton), used by `src/Toolbar/FlightModeIndicator.qml` |
| Battery tooltip metrics and 0% icon fix | `src/Toolbar/BatteryIndicator.qml` |
| Centered guided-action confirm popup with "Don't show again" | `src/FlyView/GuidedActionConfirm.qml` (a `Popup`), `src/FlyView/GuidedActionsController.qml` |
| Toolbar frosted-glass blur | `src/FlyView/FlyView.qml` |
| Draggable / resizable PiP | `src/QmlControls/PipView.qml`, PiP placement in `src/FlyView/FlyView.qml` |
| Split-view Configure/Settings drawer | `src/MainWindow/MainWindow.qml` (`_splitExpanded`, persisted in `flyViewSettings.toolDrawerFullWidth`) |
| Settings / Vehicle Config sidebar and parameter editor polish | `src/QmlControls/AppSettings.qml`, `src/Vehicle/VehicleSetup/VehicleConfigView.qml`, `src/QmlControls/ParameterEditor.qml` |
| Inline searchable sections for hand-coded settings pages | `tools/generators/settings_qml/emit.py`, `model.py` (`"sections"` key in `SettingsPages.json`) |
| Branding | `cmake/CustomOptions.cmake` (`QGC_APP_NAME` = `TitanGroundControl`), `src/QmlControls/QGCPalette.cc` (orange accent, `brandPrimary`/`brandAccent`), `src/API/QGCCorePlugin.*` (update check and download URLs), icons under `resources/`, `deploy/`, `android/res/` |
| Fork defaults | `src/Settings/*.SettingsGroup.json` (audio muted, 10 km guided max altitude, no go-to distance limit, Horizontal Compass & Attitude instrument, Google maps, aspect-ratio dropdown), indoor palette on mobile in `QGCCorePlugin.cc` |
| MSVC build flags | `/bigobj` in `CMakeLists.txt`, `/Zc:preprocessor` in `cmake/Toolchain.cmake` |

Fork-only resources are registered through CMake (`QGC_APP_RESOURCES` in `CMakeLists.txt` and the
`qt_add_resources` blocks of each module). There is no `qgcresources.qrc`. A QML reference such as
`/res/foo.svg` only works if the file is listed in one of those blocks.

The Titan AI settings page section indices (`sectionFilter` 0-5 in `AISettings.qml`) must stay in the
same order as its `"sections"` list in `SettingsPages.json`.

## Deliberate Fork Behavior

These differ from upstream on purpose. Do not "fix" them back when merging:

- The toolbar **Arm/Disarm** button is enabled whenever the vehicle is armed (disarm is always offered)
  and otherwise follows the health-and-arming `canArm` check. It does not use the guided controller's
  `showArm` / `showDisarm`, which hide Disarm whenever ArduPilot reports the vehicle airborne.
- The status drawer has **no Emergency Stop button**, and its unread-message **severity pill sits on the
  toolbar messages icon**, not on the status text. The messages icon opens the drawer on its Messages tab.
- The status text is **clickable only while disconnected** ("Click to manually connect").
- The upstream drawer's own Arm/Disarm, Force Arm (opt-in setting), Primary Link and Reboot controls are
  kept.
- In split view the Fly/Plan view stays enabled next to the drawer: `enabled: !toolDrawer.visible || !_splitExpanded`.
- The PiP only constrains the left-side widgets while docked in the bottom-left quadrant (`inBottomLeft`),
  because it can be dragged anywhere. Its height uses the configured aspect ratio, falling back to the
  detected stream ratio (setting 0 = Auto) and then 16:9.
- Map-click "Go to location" only shows the goto indicator up front when executing immediately; the confirm
  popup can be dismissed without cancelling.
- Settings and Vehicle Configuration sidebars use a fixed width of 26 characters, with eliding labels.
- The Windows installer removes the previous version from a hidden NSIS section placed in
  `CPACK_NSIS_INSTALLATION_TYPES` (`cmake/install/CPack/CreateCPackNSIS.cmake`), not in
  `CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS`. Upstream's placement runs after the files are copied, so an
  in-place upgrade deleted the new install. `CPACK_NSIS_DEFINES` is overwritten by CPack and does not work.
- The app version comes only from `v<digit>*` tags (`cmake/modules/Git.cmake`), so the moving `dev`
  release tag is never picked up as a version.

## CI and Releases

- **`td-dev.yml`** runs on pushes to `development` (and manually). It builds Android (on Windows), macOS
  (universal), Windows AMD64/ARM64 (NSIS via the `qgc-package` target) and Linux AppImage (Ubuntu 24.04, as upstream),
  then replaces the `dev` prerelease.
- **`td-release.yml`** is manual. It increments the patch of the latest `v*` tag reachable from `HEAD`,
  pushes the new tag and publishes a release with the same artifacts.
- **Every upstream workflow is `workflow_dispatch`-only**, including ones upstream adds later. Repo-maintenance
  bots (stale, welcome, pr-checks, dependency-review, cache-cleanup-pr, build-results, lupdate, px4-metadata,
  doxygen schedule) are left as upstream has them.
- Both td workflows verify artifacts before publishing, like upstream: Windows/macOS/Linux executables,
  the DMG and AppImage are boot-tested, and the Windows installer goes through install, upgrade into a new
  directory, in-place upgrade, launch and uninstall (`deploy/windows/verify-installer.ps1` with
  `QGC_APP_NAME=TitanGroundControl`). A failing check blocks the release.
- Binary and package names are `TitanGroundControl*`. Keep the renames when upstream touches a workflow.
- The td workflows reuse upstream's composite actions (`build-setup`, `cmake-configure`, `cmake-build`,
  `install-dependencies`). After each sync, check their inputs and outputs still match what the td
  workflows pass and read.

## Building Locally

- Qt version: whatever `.github/build-config.json` names (`qt.version`; 6.12.0 as of the 2026-10-18 sync),
  with the extra modules listed there.
- **CMake 3.28 to 3.x**, matching upstream's `cmake>=3.28,<4` pin. With MSVC, CMake 4 fails zstd's ASM
  detection. Visual Studio's bundled CMake 3.31 works.
- **Ninja** generator. NMake/JOM is not used by upstream presets or CI.
- Python generator dependencies live in `tools/.venv` (gitignored). CMake creates it with `uv`; without
  `uv`, create it with `python -m venv tools/.venv` and install the `scripts` group from `tools/pyproject.toml`.
- In Qt Creator, set the kit's CMake tool to a 3.x CMake and its generator to Ninja *before* first opening
  the project. Otherwise clear `.qtcreator/` and the kit's build directory.

## Syncing with Upstream

1. `git fetch upstream`, then branch `sync/upstream-YYYY-MM-DD` from `master`.
2. `git -c merge.renameLimit=20000 merge --no-ff --no-commit upstream/master`. Upstream moves directories
   often; fork files added inside a moved directory must follow it (git reports these as "file location"
   conflicts).
3. Resolve conflicts by porting the fork's intent onto upstream's new code, not by taking one side
   wholesale. Check `git log <merge-base>..HEAD -- <file>` for why the fork changed something.
4. **Audit beyond the conflicts.** Silent losses have happened before: an earlier sync dropped the
   aspect-ratio dropdown without a conflict. Diff every fork feature above against the previous fork
   `HEAD`, including settings JSON enums and defaults, resource registration, renamed C++ or QML APIs
   used by fork code (e.g. `Vehicle::flying()` became `underway()` / `airborne()`), and new upstream
   defaults that override fork choices (e.g. `QGC_MACOS_UNIVERSAL_BUILD` defaulting to OFF).
5. Re-apply the CI policy to every upstream workflow, including new ones, and re-check the td workflows.
6. Build Debug with tests against the required Qt. Before attributing a test failure to the merge, compare
   with a control build of pure `upstream/master` (a `git worktree` in the gitignored `build/` directory).
   On Windows, about 23 unit tests and every QML UI test fail on pure upstream too: the UI tests abort on a
   Qt `qnumeric.h` debug assertion in offscreen mode.
7. Commit the merge as `chore(sync): merge upstream QGroundControl master`, summarizing the adaptations in
   the body. Put unrelated fixes found along the way in separate commits.
8. Fast-forward `development` to the sync branch and delete the sync branch. Pushing `development` runs
   TD_Dev (dev prerelease); `development` is merged into `master` later. **Never push upstream tags to
   origin** (no `--tags` / `--follow-tags`): upstream's `v5.2.0-dev` marker tag would become the nearest
   `v*` tag and `td-release` would compute the wrong version.

## Known Issues

- `.version.txt` drives the in-app update prompt and is read from `master`. `td-release` does not bump
  it, so set it to the new tag by hand after each release and merge that to `master`.
- `AIChatWidget.qml` and `AISettings.qml` reference `qgcPal.textHighlight` / `textHighlightForeground`,
  which do not exist in `QGCPalette`. Text selection falls back to default colors and QML logs a warning.

## Commits

Follow the Conventional Commits rules in [AGENTS.md](AGENTS.md). Credit only the human author: do not add
`Co-Authored-By` trailers or any mention of AI tools to commit messages or pull requests.
