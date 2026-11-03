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
- [Documentation](#documentation)
- [Commits](#commits)

## Fork Features and Where They Live

| Feature | Files |
| --- | --- |
| Titan AI chat (Claude + Groq Whisper voice) | `src/AI/` (`AIChatController`, `AISystemPrompt.h`, `AIKnowledgeBase.h`, `POIItem.h`), `src/FlyView/AIChatWidget.qml`, `src/FlyView/FlyViewTopRightColumnLayout.qml` (loader) |
| Titan AI settings | `src/Settings/AISettings.*`, `src/Settings/AI.SettingsGroup.json`, `src/AppSettings/AISettings.qml`, "Titan AI" entry in `src/AppSettings/pages/SettingsPages.json` |
| AI hooks on `Vehicle` | `aiChatController`, `loiterRadiusFact()`, `flightModeFromCustomMode()`, `lastGotoCoordinate()`, `guidedModeGotoLocationSent` signal in `src/Vehicle/Vehicle.*` |
| AI voice replies while muted | `AudioOutput::say(text, mods, ignoreMute, rate)` and `stop()` in `src/Utilities/Audio/AudioOutput.*` |
| Map POIs, goto indicator sync, Add POI | `src/FlyView/FlyViewMap.qml`, `src/FlyView/FlyViewMapClickDropPanel.qml` |
| Smooth (animated, cursor-anchored) mouse-wheel zoom on the Fly/Plan map, half a zoom level a notch | `WheelHandler` and `wheelZoomAnimation` in `src/FlightMap/FlightMap.qml` |
| Telemetry panel: tiles shown as numbers or bars, with caution and warning levels, edited in place | `src/TitanTelemetry/` (`TelemetryPanelController.*`, a layout per vehicle type in INI group `TitanTelemetryPanel`, starting from `kDefaultLayout` (four columns, twelve values, the same for every vehicle type) on a fresh install and Reset, `TelemetryPanelItem.*`, `TelemetryPanel.qml`, `TelemetryTile.qml`, `TelemetryItemEditor.qml`), swapped in for upstream's `TelemetryValuesBar` in `src/FlyView/FlyViewBottomRightRowLayout.qml`; consumption per kilometre on each battery (`mahPerKm`, `whPerKm` in `src/Vehicle/FactGroups/BatteryFactGroupListModel.*` and `BatteryFact.json`, the battery tooltip's formulas); tests in `test/TitanTelemetry/` |
| Fly View toolbar (red Arm/Disarm, FLTMODE1-6 buttons, messages icon + severity pill, every indicator right after the flight modes with GPS, battery and RC first, the Titan Dynamics logo alone at the right end while there is room) | `src/Toolbar/FlyViewToolBar.qml` (`_leadingIndicators`), `src/Toolbar/MainStatusIndicator.qml`, `resources/td_full_logo_black.png` and `td_full_logo_white.png` in `src/Toolbar/CMakeLists.txt` |
| Short FBW mode names | `src/QmlControls/TDHelpers.qml` (singleton), used by `src/Toolbar/FlightModeIndicator.qml` |
| Battery tooltip metrics and 0% icon fix | `src/Toolbar/BatteryIndicator.qml` |
| Centered guided-action confirm popup with "Don't show again", kept across launches as action codes in `flyViewSettings.skippedGuidedConfirmations` and undone from Fly View settings → Guided Commands | `src/FlyView/GuidedActionConfirm.qml` (a `Popup`), `src/FlyView/GuidedActionsController.qml`, `src/QmlControls/GuidedConfirmationsReset.qml` |
| Toolbar frosted-glass blur | `src/FlyView/FlyView.qml` |
| Draggable / resizable PiP, which shrinks into its show button as it collapses and grows back out of it | `src/QmlControls/PipView.qml` (`pipScaler`, `_expansion`), PiP placement in `src/FlyView/FlyView.qml` |
| Split-view Configure/Settings drawer | `src/MainWindow/MainWindow.qml` (`_splitExpanded`, persisted in `flyViewSettings.toolDrawerFullWidth`) |
| Settings / Vehicle Config sidebar and parameter editor polish, settings pages no wider than the view | `src/QmlControls/AppSettings.qml`, `src/AppSettings/SettingsPage.qml`, `src/Vehicle/VehicleSetup/VehicleConfigView.qml`, `src/QmlControls/ParameterEditor.qml` |
| Inline searchable sections for hand-coded settings pages; generated groups hide when all their controls are hidden | `tools/generators/settings_qml/emit.py` (`_group_auto_vis` counts each control's `showWhen`), `model.py` (`"sections"` key in `SettingsPages.json`) |
| Branding | `cmake/CustomOptions.cmake` (`QGC_APP_NAME` = `TitanGroundControl`, `QGC_ANDROID_PACKAGE_NAME` = `aero.titandynamics.titangroundcontrol`), `src/QmlControls/QGCPalette.cc` (orange accent, `brandPrimary`/`brandAccent`), `src/API/QGCCorePlugin.*` (update check and download URLs), icons under `resources/`, `deploy/`, `android/res/` |
| Titan 3D map: a port of Mission Planner's Map3D (TitanPlanner `Controls/Map3D`) on Qt's RHI, separate from the 2D map | `src/TitanMap3D/`: `Map3DItem.*` (QML item, gathers telemetry, mission, ADS-B and settings on the GUI thread), `Map3DRenderer.*` (render-thread frame, terrain engine GPU side, draw passes), `Map3DScene.*` (VehicleTracker, CameraRig, Kalman filter), `Map3DTerrain.*` (quadtree LOD, tile mesh builder), `Map3DTileLoader.*` (imagery + Terrarium elevation through the shared tile cache), `Map3DGeo.*` (UTM world frame, tile maths, frustum), `Map3DOverlays.*` (indicator lines, turn arc, trail), `Map3DClouds.*`, `Map3DModel.*` (STL loading), `shaders/`, `models/*.stl`, `images/` (waypoint markers, the tool strip's cube icon `map3d.svg`), QML `Map3DView.qml` (mouse controls, ADS-B tooltip), `Map3DStatsPanel.qml`, `Map3DState.qml` (singleton), tool strip actions (3D Setup opens the 3D Map settings page); hooks in `src/FlyView/FlyView.qml` (`map3DViewComponent`, `map3DLoader` and `map3DSplitLoader` in `mapHolder`, `_map3DCoversMap` on both 2D map engines, `_map3DSplitWidth` on the full-window items' right margin, `_mapViewportRect`, the PiP's `onPipClicked`) and `src/QmlControls/PipView.qml` (`pipClicked`), `src/FlyView/FlyViewWidgetLayer.qml` (map scale and GeoMap controls hidden while `Map3DState.coversMap`) and `src/FlyView/FlyViewToolStripActionList.qml`; tests in `test/TitanMap3D/` |
| 3D map settings (Mission Planner's set and defaults, except waypoint markers at 25 m rather than 60, plus the fork's Split-screen) | `src/Settings/Map3DSettings.*`, `src/Settings/Map3D.SettingsGroup.json` (INI group `TitanMap3D`), `src/AppSettings/pages/Map3D.SettingsUI.json`, "3D Map" entry in `src/AppSettings/pages/SettingsPages.json` (replaces upstream's deprecated "3D View" page) and `Map3DSettings.qml` in the generated-pages list of `src/AppSettings/CMakeLists.txt` |
| Titan HUD (instrument panel option, video overlay) | `src/TitanHud/` (`HudData.*`, `HudItem.*`, `HudPainter.*`, `TitanHud.qml`, `TitanHudInstrument.qml`, B612 Mono in `fonts/` under the SIL Open Font License, the no-video TV static `TvStatic.qml` with `shaders/tvstatic.frag`), `hudOverlay`, `hudScale` and `hudColor` facts in `Video.SettingsGroup.json`, "Titan HUD" in the `instrumentQmlFile2` enum of `src/Settings/FlyView.SettingsGroup.json`, overlay in `src/FlyView/FlightDisplayViewVideo.qml`; tests in `test/TitanHud/` |
| Vehicle model decimation (TitanPlanner STLs) | `tools/titan/convert_vehicle_models.py` |
| Camera controls hidden unless switched on (Fly View settings → Show Camera Controls; the simple camera option only shows with it) | `showCameraControls` in `src/Settings/FlyView.SettingsGroup.json` and `FlyViewSettings.*`, `src/AppSettings/pages/FlyView.SettingsUI.json`, the `PhotoVideoControl` loaders in `src/FlyView/FlyViewTopRightColumnLayout.qml` and `src/FlyView/FlyViewTopRightPanel.qml` (whose camera page, swiping and page indicator go with it) |
| Internet ADS-B traffic (a port of TitanPlanner's adsb.cs), traffic sent to the autopilot for avoidance, the autopilot's collision threat levels, and an icon for each aircraft category on the Fly View map | `src/TitanAdsb/` (`AdsbInternetSource.*`, `AdsbTrafficSender.*`, `AdsbCategory.*`, `AdsbIconProvider.*`, `TitanAdsbMapItem.qml`), `adsbInternetEnabled`, `adsbInternetServer`, `adsbInternetRadius`, `adsbInternetMaxAircraft` and `adsbSendToVehicle` in `src/Settings/ADSBVehicleManager.SettingsGroup.json` and `ADSBVehicleManagerSettings.*`, the ADSB Server page in `src/AppSettings/pages/ADSBVehicleManager.SettingsUI.json`; hooks in `src/ADSB/` (`ADSB.h` category, type and ground fields, `ADSBVehicle.*` properties, `ADSBVehicleManager.*` sources, echo filter and COLLISION), `MAVLINK_MSG_ID_COLLISION` routed in `src/Vehicle/Vehicle.cc`, the `TitanAdsbMapItem` delegate in `src/FlyView/FlyViewMap.qml`, `AdsbIconProvider` registered in `src/QGCApplication.cc`, and grounded traffic and the tooltip in `src/TitanMap3D/`; tests in `test/TitanAdsb/` |
| Help page: source code and release links, and an updater from the GitHub releases (stable or development) | `src/TitanUpdater/` (`TitanUpdater.*`, a QML singleton with its channel in INI group `TitanUpdater`; `TitanHelpSection.qml`), above upstream's links in `src/AppSettings/HelpSettings.qml`; tests in `test/TitanUpdater/` |
| Video sources listed again when a camera is plugged in or out, and a Refresh button by the source | `refreshVideoSources` and `QMediaDevices::videoInputsChanged` in `src/Settings/VideoSettings.*`, `src/QmlControls/VideoSourceSelector.qml` (the source dropdown and Refresh), used as a component control in `src/AppSettings/pages/Video.SettingsUI.json` |
| USB cameras and capture cards on Android through the app's own UVC driver, where Android's camera service does not list them | `src/TitanUsbCamera/` (`UsbCameraDevices.*`, the sources named "USB Camera: <product>"; `TitanUsbCamera.*`, a QML element feeding a VideoOutput's sink; `UvcStream.*`, libuvc on libusb, both built from CPM sources for Android only), `android/src/org/mavlink/qgroundcontrol/TitanUsbCameraManager.java` (USB permission, file descriptors, plugging in and out) and its keep rule in `android/proguard-rules.pro`; hooks in `UVCReceiver.cc`, `VideoSettings.cc`, `VideoManager.cc` and `FlightDisplayViewUVC.qml` |
| Fork defaults | `src/Settings/*.SettingsGroup.json` (audio muted, 10 km guided max altitude, no go-to distance limit, the compass's additional heading indicators, camera controls hidden, Google maps, aspect-ratio dropdown, the Titan HUD over video), indoor palette on mobile in `QGCCorePlugin.cc` |
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
- The Fly View **telemetry panel is the fork's own** (`TelemetryPanel`), not upstream's `TelemetryValuesBar`: a flat list of
  tiles, each a number or a bar, edited from a pencil button (also right click or press and hold), with drag to reorder
  and a searchable value picker. Upstream's `FactValueGrid` still drives the multi-vehicle cards and the video
  recording subtitles (`SubtitleWriter`), which keep upstream's `TelemetryBarUserSettings`.
- The status text is **clickable only while disconnected** ("Click to manually connect").
- The **updater** (Help page) follows the latest version release on the stable channel, judged by version
  (`QGCVersionCheck::isNewerVersion`), or the `dev` prerelease on the development channel, judged by commit: the
  dev workflow writes `**Commit:** <sha>` into the release body, and the build carries git describe's `-g<sha>`. It
  downloads this platform's release asset, checked against GitHub's SHA-256 digest when the API gives one. The
  Windows installer then runs (through the shell, so it can ask for elevation) and the app closes
  (`finishCloseProcess`); an AppImage is replaced by renaming over it and restarted once this process exits, since
  a second instance would refuse to run; macOS opens the disk image; Android opens the package in the browser.
  Installing is disabled while a vehicle is connected, and an unsaved mission is confirmed first.
- **Every toolbar indicator sits right after the flight-mode buttons**: GPS, battery and RC first
  (`_leadingIndicators` in `FlyViewToolBar.qml`, matched by the end of their URL), then the rest as the app and the
  firmware list them. GPS is one of the app's indicators, the others the vehicle's. Upstream's
  `FlyViewToolBarIndicators.qml`, which docks them at the right end, is not used. The **Titan Dynamics logo** alone
  sits there, black on the light theme and white on the dark one, only while the whole toolbar fits without
  scrolling (`_showLogo`).
- The status drawer has **no Arm/Disarm button** either (arming lives on the toolbar); its Force Arm (opt-in
  setting), Primary Link and Reboot controls are kept.
- Toolbar indicator drawers fade and grow in and out. `fillWindow` drawers (the status drawer) are capped at
  half the window width and height on desktop (`MainWindow.qml`); on mobile they still fill the window.
- In split view the Fly/Plan view stays enabled next to the drawer: `enabled: !toolDrawer.visible || !_splitExpanded`.
- A settings page is no wider than the drawer shows (`SettingsPage.qml`), at least 50 characters: a wrapping
  description's implicit width is its whole text on one line, which ran pages off the edge of the split drawer.
- The PiP can be dragged anywhere, so widgets only make way for it in their own corner: bottom-left
  (`inBottomLeft`) the virtual joysticks rise above it; top-left (`inTopLeft`) the tool strip moves to the
  bottom-left corner; bottom-right (`inBottomRight`) the telemetry panel and instrument row moves to the
  bottom-left corner. They jump between corners (animated anchor states in `FlyViewWidgetLayer.qml`) rather than
  tracking the PiP's edge. The PiP's height uses the configured aspect ratio, falling back to the detected stream
  ratio (setting 0 = Auto) and then 16:9. So Aspect Ratio sits right under the source in the Video settings for
  every source but disabled video, not only for manually configured streams in the Settings group as upstream has
  it, and while a MAVLink camera configures the stream only the source dropdown is disabled, not the group
  (`Video.SettingsUI.json`).
- Map-click "Go to location" only shows the goto indicator up front when executing immediately; the confirm
  popup can be dismissed without cancelling.
- Settings and Vehicle Configuration sidebars use a fixed width of 26 characters, with eliding labels.
- The Windows installer removes the previous version from a hidden NSIS section placed in
  `CPACK_NSIS_INSTALLATION_TYPES` (`cmake/install/CPack/CreateCPackNSIS.cmake`), not in
  `CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS`. Upstream's placement runs after the files are copied, so an
  in-place upgrade deleted the new install. `CPACK_NSIS_DEFINES` is overwritten by CPack and does not work.
- The Android application ID is `aero.titandynamics.titangroundcontrol` (`QGC_ANDROID_PACKAGE_NAME`); the other
  platforms keep upstream's `QGC_PACKAGE_NAME`. Only the Gradle namespace and application ID follow it: the Java sources
  stay in `org.mavlink.qgroundcontrol`, the package the JNI class names (`src/Android/AndroidInterface.h`,
  `AndroidSerial.h`) and `android/proguard-rules.pro` use, as upstream's custom builds do. It is a different app to
  Android, so it installs beside an APK built under the old ID instead of upgrading it.
- The app version comes only from `v<digit>*` tags (`cmake/modules/Git.cmake`), so the moving `dev`
  release tag is never picked up as a version.
- The **3D map is its own view and renderer**, not the GeoMap engine. The tool strip's 3D button shows `Map3DView`
  over the whole Fly View in place of the 2D map: it fills `mapHolder` at z 1, above the full-window item and below
  the widgets and the PiP, so the toolbar blur, tool strip, instruments and PiP work as they do over the 2D map. It
  fades and grows in (200 ms, once its slow first frame is out) and out (150 ms) like the toolbar drawers. While
  the 2D map is the full-window item it is hidden once the 3D map is fully in, so it does not draw
  (`_map3DCoversMap`), and shown again as the 3D map starts to go; with the video swapped into the full window, the
  3D map covers the video and the 2D map stays in the PiP. Clicking the PiP closes a full-screen 3D map, so the
  clicked view takes the full window (`PipView.pipClicked`, emitted before the swap: the 2D map hidden under the 3D
  map must be shown again before it moves into the PiP, where, moved while hidden, it did not draw). The 3D map's
  **Split-screen** setting (off by default) puts it on the right half instead, at the same z, beside the
  full-window item (the 2D map, or the video when swapped in). Only that item narrows to the left half (its
  `anchors.rightMargin`, from `_map3DSplitWidth`, while in PipState's full state), with the Settings split's 250 ms
  animation once the 3D map's first frame is out (drawn just past the right edge); the toolbar, widgets and PiP
  keep the whole view, over both halves, and the 2D map keeps the vehicle within its own half (`_mapViewportRect`).
  A 2 px divider in the palette's window color marks the seam. Full-screen video closes the split. Two other splits
  were tried and dropped at the user's request: the 3D map on the left as the only layout, and narrowing the whole
  Fly View, widgets included. The 2D map and the `useGeoMapEngine` default are upstream's. Upstream's Viewer3D
  stays compiled and tested but is reachable from neither the tool strip nor the settings pages; upstream itself
  deprecates it. GeoMap was tried first and dropped: even its 2D mode is a Qt Quick 3D scene whose per-frame sync
  made the whole Fly View lag.
- The 3D map follows TitanPlanner's `Map3D` line by line where it can: the camera rig (chase 0.8 m behind and
  0.2 m above a model of about 1 m span, FPV kept a metre above the ground, free-look 100 m above the 2D map's
  centre while disconnected), the mouse (drag 0.5 deg/px orbit and 0.005 m/px height within +/-5 m, wheel
  0.1 m per notch within 0.1-10 m, double-click resets the angle, free-look 0.3 deg/px), the Kalman tracker,
  the UTM world frame, the quadtree LOD, skirts, fog, sky, clouds (with .NET's seeded `Random`), the lines,
  trail, markers and ADS-B. The vehicle is drawn at the terrain under it plus its height above home, as in
  Mission Planner; that terrain height loads asynchronously here, so the tracker starts afresh when it arrives
  (`_groundHeightKnown`) instead of easing the vehicle up from sea level. Deliberate differences: elevation is
  Terrarium tiles on the imagery's XYZ grid (QGC's
  Copernicus terrain fetches ~1 km tiles one at a time, far too slow for a 30-150 km view), taken three zoom
  levels above each imagery tile (`kElevationZoomOffset`): still 32 samples per tile, as many as its mesh has
  cells, while one ~80 KB download serves 64 tiles. Imagery and elevation download in parallel, 16 tiles at a
  time; together that took loading from ~19 to ~95 tiles/s on a warm cache. Dead reckoning is
  capped at 5 s, the nav-bearing target is indexed by mission sequence number, pinch zoom is added for touch
  screens, and imagery stops at zoom 22 (`SelectionParams::kMaxImageryZoom`; Mission Planner goes to 24 near the
  ground, but Google's satellite imagery ends at 22 and answers HTTP 400 past it). Mouse adjustments to the chase
  camera stay in `Map3DItem` and reach the settings a second after they stop, as Mission Planner saves them when
  its map closes: every settings write rewrites the INI file (~26 ms in a Debug build), and writing on each mouse
  move held dragging to ~25 fps.
- The renderer runs Mission Planner's frame on the render thread (`QQuickRhiItemRenderer::render`); the GUI
  thread only copies a `FrameInput` in `synchronize`. Pass 2 (mission, vehicle) is kept in front of the terrain
  by drawing into depth range [0, 0.05] with the terrain in [0.05, 1], equivalent to Mission Planner clearing
  depth between passes. Lines are screen-space quads (`line.vert`), since D3D11 and Metal have no wide lines.
  A tile's GPU resources are freed with `QRhiResource::deleteLater` (the `DeleteLater` deleter in `TileGpu`):
  eviction can drop a tile in the frame that queued its texture upload, and QRhi reads the texture only when
  `beginPass` submits the batch, so deleting it at once crashed (`Q_UNREACHABLE` in `textureFormatInfo`).
- Imagery uses the 2D map's provider when it is a satellite type (so cached tiles are shared), the same
  vendor's satellite type otherwise, else Google Satellite. Network tiles are cached only after they decode.
  The loader reads the shared cache database itself, on one thread with its own SQLite connection
  (`readCachedTile`), rather than through QGC's cache worker: that worker serves one request at a time and
  recounts the whole cache (`computeTotals`, ~180 ms on a 180 MB cache) whenever its queue empties, so cached
  tiles took ~200 ms each instead of ~2 ms. Writes still go through the worker.
- The tiles under the camera are requested at every level at once, ahead of everything else
  (`TerrainQuadtree::_requestCameraColumn`). Mission Planner's traversal only asks for a tile's children once
  it has loaded, so the ground below would otherwise arrive one round trip per level.
- The 3D map has no HUD overlay (Mission Planner's `map3d_hud_overlay`); it was removed at the user's request.
- - The HUD is the fork's own fighter-style design and deliberately does not copy Mission Planner's. `HudData`
  decodes NAV_CONTROLLER_OUTPUT, HEARTBEAT, SYS_STATUS, EKF_STATUS_REPORT and AOA_SSA itself because `Vehicle` does
  not keep them. `HudItem` gathers a `HudFrame` on the GUI thread and `HudPainter` paints it with QPainter on the
  item's own thread, shown as a texture when done: a QQuickPaintedItem paints during the scene graph sync and held
  the whole window to ~9 fps. A 1080p frame takes ~12 ms in a Debug build. The symbology is one color (`hudColor`,
  white by default, the 3D map model color's choices but black and gray, at half opacity while no vehicle is
  connected or its link is lost), with a dark halo over video and a glow on
  the synthetic sky; amber and red only mark cautions and warnings (ARMED is red), and nominal system rows recede.
  The layout scales with the HUD's size, and small HUDs leave out secondary text instead of shrinking it past
  reading (`Detail`: full, compact for the instrument panel, minimal for the PiP); the overlay's `hudScale` setting
  (`symbolScale`) only resizes the symbology. Over video the HUD covers just the part of the picture on screen,
  since a fit or fill mode can make the picture overhang the view. Text is B612 Mono Bold, Airbus's cockpit display
  typeface, blitted from cached glyph images. The flight path marker sits at the climb angle and drift (GPS track
  minus heading) once moving faster than 2 m/s. The synthetic sky and ground (the instrument panel, and the overlay
  while waiting for video, under a larger waiting message) carry a ground grid laid out from home that slides by
  with the vehicle; it is never drawn over video.
- **Internet ADS-B** ports TitanPlanner's adsb.cs (ADSB Server settings, Get Traffic From The Internet, off by
  default). While on it replaces the SBS server, whose controls are disabled. It polls adsb.fi
  (`/api/v3/lat/{lat}/lon/{lon}/dist/{nm}`, every 1.1 s) or an ADSBExchange-style server such as adsb.lol
  (`/v2/point`, every 3 s) around the active vehicle, or the Fly View map without a vehicle position
  (`QGroundControlQmlGlobal::flightMapPosition`), within Traffic radius (25 nm by default, rather than
  TitanPlanner's 100 nm growing to 250 nm), and keeps the Maximum aircraft (50 by default) nearest; a listed
  aircraft no longer among them goes at once. Responses are read and sorted on a pool thread (`selectNearest`),
  since a few hundred aircraft stuttered the UI. Errors double the interval up to 10 s and a 429's Retry-After is
  honoured up to a minute. Each aircraft is carried forward along its track, at most 10 s, from when its position
  was measured (`seen_pos` plus the snapshot's age), moving aircraft republished every 500 ms (`ADSBVehicle` passes
  on one update a second) and dropped 30 s after the feed stops listing it. Deliberate differences: geometric
  altitude (`alt_geom`, comparable with GPS altitude) is preferred to TitanPlanner's barometric `alt_baro`, and
  non-ICAO (`~`) addresses get bit 24 set. adsb.fi's data is for personal, non-commercial use.
- **Traffic is sent to the autopilot for avoidance** (Send Traffic To The Vehicle For Avoidance, on by default), as
  TitanPlanner does: once a second, the next of the ten nearest aircraft within 10 km that QGroundControl got
  itself (the internet or the SBS server, `isLocalTraffic`), as ADSB_VEHICLE on the active vehicle's primary link,
  never a high-latency one. The vehicle's own copies of that traffic are ignored, and its COLLISION reports set
  each aircraft's `threatLevel`, which lapses 5 s after the last report: high draws it red on the map, and the
  tooltips say Warning or Danger. ArduPilot acts on it only with its ADS-B and avoidance parameters set
  (`ADSB_TYPE`, `AVD_ENABLE`).
- **Traffic icons are the fork's own** vector silhouettes in QGroundControl's style (`AdsbIconProvider`,
  `image://titanadsb/<icon>/<state>`), drawn once for each icon, state and size and shared by every aircraft, so
  traffic draws as batched textures; a QML Shape for each aircraft lagged with a few hundred, one for each emitter
  category with QGroundControl's airliner for the generic one, rather than TitanPlanner's tar1090 sprites, at the
  user's request. Colors and sizes follow TitanPlanner: orange, red for a high threat or an alert, translucent grey
  on the ground, large 1.25 and heavy 1.5 times; its details show on hover or a tap. Upstream's
  `ADSBVehicleMapItem` stays, for its unit test, but the Fly View map no longer uses it; the GeoMap engine keeps
  upstream's traffic item.
- **USB cameras on Android go through the app's own UVC driver** (`src/TitanUsbCamera/`), as other USB camera apps
  do: Android's camera service, which Qt's `Camera` uses, lists a USB camera only on devices whose camera HAL
  supports external cameras. `TitanUsbCameraManager.java` lists USB video class devices, asks for USB permission
  (Android grants it for video devices only once the app has the camera permission, which `TitanUsbCamera` asks for
  first) and hands the device's file descriptor to libuvc on libusb (`uvc_wrap`, with
  `LIBUSB_OPTION_NO_DEVICE_DISCOVERY`, since an app may not scan `/dev/bus/usb`). `UvcStream` picks the best mode
  up to 1080p, 25 fps or more where it can, uncompressed (YUYV, NV12 and the like) over MJPEG at the same size,
  MJPEG held to 30 fps and decoded with Qt's libjpeg-turbo, which supplies the Huffman tables MJPEG frames leave
  out. libuvc is built with 4 isochronous packets per transfer (`LIBUVC_PACKETS_PER_TRANSFER_MAX`), as some Android
  USB controllers reject more. Only the latest frame waits for the GUI thread, so a busy one skips frames rather
  than queueing them. The sources sit beside Qt's cameras as "USB Camera: <product>"; one chosen before it is
  plugged in opens when it is, as `VideoManager` re-reads the source whenever the list changes (`enumsChanged`),
  and the Qt `Camera` stays off for these (`hasCameraDevice`), as it would otherwise open the default camera.
- The vehicle models are TitanPlanner's STLs decimated to 8000 triangles, kept in Mission Planner's frame
  (millimetres): `Map3DModel` rotates them -90 deg about Z and the model matrix scales to metres, exactly as
  for a custom STL. Regenerate them with `tools/titan/convert_vehicle_models.py` (needs a TitanPlanner checkout,
  `numpy-stl` and `pyfqmr`).

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
- Fork C++ types used from QML are compiled into the app target (`target_sources`), so they register in the
  app's QML module; the fork's QML modules (`TitanHudModule`, `TitanMap3DModule`) are QML-only and use
  `NO_PLUGIN`. A static QML module with C++ types and `NO_PLUGIN` loses its type registrations at link time
  ("X is not a type" at runtime).
- `qt_add_shaders` on the app target from a subdirectory needs absolute paths plus `BASE`; relative paths
  resolve against the top-level directory at finalization.
- MSVC builds only warn at `/W2`, but CI builds Linux, macOS and Android with `-Wall -Wextra -Wshadow` and warnings
  as errors (`cmake/CompilerWarnings.cmake`), so a local shadowing a variable or member, an unused parameter or an
  unused lambda capture builds locally and fails CI. On Windows, Qt's `C:\Qt\Tools\mingw1310_64` g++ and
  `llvm-mingw` clang++ can syntax-check a file with those flags (`-fsyntax-only`), using the include paths and
  defines from the build's `compile_commands.json` with the Qt include directory swapped for that kit's.
- A Debug-build assert `value < maximalPlusOne` in `qnumeric.h` means an infinite size reached `qRound`, usually a
  QML size divided by zero. The UVC camera path did this (`_ar` in `FlightDisplayViewVideo.qml`, now guarded).
- `TitanGroundControl.exe --simple-boot-test --allow-multiple` with `QT_FORCE_STDERR_LOGGING=1` is the quickest
  QML smoke test; without it a GUI build sends Qt messages to the debugger, not stderr. Test against SITL, not a
  second GCS on a real vehicle's link. Debug builds use Qt's unoptimized libraries: judge frame rates in a
  Release or Profile build.
- `JsonResourceAuditTest` passes but takes about two minutes in a Debug build, past ctest's 60 s limit; run
  `TitanGroundControl.exe --unittest:JsonResourceAuditTest --allow-multiple` directly to see it finish.

## Syncing with Upstream

1. `git fetch upstream`, then branch `sync/upstream-YYYY-MM-DD` from `master`.
2. `git -c merge.renameLimit=20000 merge --no-ff --no-commit upstream/master`. Upstream moves directories
   often; fork files added inside a moved directory must follow it (git reports these as "file location"
   conflicts).
3. Resolve conflicts by porting the fork's intent onto upstream's new code, not by taking one side
   wholesale. Check `git log <merge-base>..HEAD -- <file>` for why the fork changed something.
4. **Audit beyond the conflicts.** Silent losses have happened before: an earlier sync dropped the aspect-ratio
   dropdown without a conflict. Diff every fork feature above against the previous fork `HEAD`, including settings
   JSON enums and defaults, resource registration, renamed C++ or QML APIs used by fork code (e.g.
   `Vehicle::flying()` became `underway()` / `airborne()`), and new upstream defaults that override fork choices
   (e.g. `QGC_MACOS_UNIVERSAL_BUILD` defaulting to OFF). Re-check the fork hooks inside upstream files:
   `FlyView.qml` (`map3DViewComponent`, `map3DLoader` and `map3DSplitLoader` in `mapHolder`, `_map3DSplitWidth` on
   the full-window items' right margins, `_mapViewportRect`, `_map3DCoversMap` on both 2D map engines, the PiP's
   `inTopLeft` / `inBottomRight` and `onPipClicked`), `PipView.qml` (`pipClicked`, the collapse animation's
   `pipScaler` and `_expansion`),
   `FlyViewBottomRightRowLayout.qml` (`TelemetryPanel`), `FlyViewWidgetLayer.qml` (map scale and GeoMap controls,
   the tool strip and bottom-right row corner states), `MainStatusIndicator.qml` (messages buttons on the left),
   `FlyViewToolBar.qml` (`_indicators` in place of upstream's `FlyViewToolBarIndicators.qml`: carry over upstream's
   changes to that file), `HelpSettings.qml` (`TitanHelpSection`),
   `VideoSettings.*` (`refreshVideoSources`, `_setVideoSourceList`, `UsbCameraDevices::changed`), `UVCReceiver.cc`
   (`UsbCameraDevices` in the device list), `VideoManager.cc` (`enumsChanged`), `FlightDisplayViewUVC.qml`
   (`TitanUsbCamera`, `hasCameraDevice`), `proguard-rules.pro` (`TitanUsbCameraManager`),
   `FlyViewTopRightColumnLayout.qml` and
   `FlyViewTopRightPanel.qml` (`showCameraControls`), `ADSB.h`, `ADSBVehicle.*` and `ADSBVehicleManager.*`
   (category, ground and threat fields, the internet source, the sender, the echo filter and COLLISION),
   `Vehicle.cc` (COLLISION routed to `ADSBVehicleManager`), `FlyViewMap.qml` (the `TitanAdsbMapItem` delegate),
   `QGCApplication.cc` (`AdsbIconProvider`), `SettingsPage.qml` (no wider than the view), `FlightMap.qml` (the
   smooth wheel zoom), `BatteryFactGroupListModel.*` and `BatteryFact.json` (`mahPerKm`, `whPerKm`),
   `FlyViewToolStripActionList.qml` (`Map3DShowAction`, `Map3DSettingsAction`), `FlightDisplayViewVideo.qml`
   (`TitanHud` and its waiting message, `TvStatic` in place of upstream's deleted `NoVideoBackground.jpg`, the
   `_ar` guard), `src/AppSettings/CMakeLists.txt` (generated pages list), and `HudData.cc` / `Map3DItem.cc` against
   the `Vehicle`, `MissionManager`, `ADSBVehicle`, tile cache (`QGeoFileTileCacheQGC`, `QGeoTileFetcherQGC`,
   `QGCTileCacheDatabase` and its `Tiles` table) and `UrlFactory` APIs they use.
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

- `.version.txt` drives the toolbar's update prompt (`QGCVersionCheck`) and is read from `master`. `td-release`
  does not bump it, so set it to the new tag by hand after each release and merge that to `master`. The Help page's
  updater asks GitHub's API instead and does not depend on it.
- `AIChatWidget.qml` and `AISettings.qml` reference `qgcPal.textHighlight` / `textHighlightForeground`,
  which do not exist in `QGCPalette`. Text selection falls back to default colors and QML logs a warning.
- Only internet traffic says whether it is on the ground, so the 3D map draws only that traffic as Mission Planner
  draws grounded aircraft (small, grey, kept above the terrain already loaded); SBS and vehicle traffic always
  counts as airborne.
- Video recording does not cover the USB cameras read by the app's own UVC driver on Android.

## Documentation

- [README.md](README.md) is the fork's front page for users: a concise list of what the fork adds, a line or two
  for each feature, grouped by area, with no emojis and no internals (those belong here). When a change adds a major
  user-facing feature, or changes or removes one the README lists, update the README in the same change. The
  screenshot at the top, `.github/assets/screenshot.jpg`, is replaced by hand. It is a JPEG because pre-commit
  rejects files over 2000 KB, as a full-size PNG screenshot is.
- This file is for agents. Add each new fork feature to
  [Fork Features and Where They Live](#fork-features-and-where-they-live), each deliberate difference from upstream
  to [Deliberate Fork Behavior](#deliberate-fork-behavior), and each fork hook inside an upstream file to the audit
  list in [Syncing with Upstream](#syncing-with-upstream).

## Commits

Follow the Conventional Commits rules in [AGENTS.md](AGENTS.md). Credit only the human author: do not add
`Co-Authored-By` trailers or any mention of AI tools to commit messages or pull requests.
