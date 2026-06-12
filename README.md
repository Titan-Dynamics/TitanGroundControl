<h1 align="center">Titan Ground Control</h1>

<p align="center">
  <a href="https://github.com/Titan-Dynamics/TitanGroundControl/releases">
    <img src="https://img.shields.io/github/v/release/Titan-Dynamics/TitanGroundControl" alt="Latest Release">
  </a>
</p>

<img width="2558" height="1536" alt="image" src="https://github.com/user-attachments/assets/755a6904-bf53-41db-8316-01168edea435" />

**Titan Ground Control** is [Titan Dynamics'](https://github.com/Titan-Dynamics) fork of [QGroundControl](https://github.com/mavlink/qgroundcontrol), the open-source ground control station for MAVLink-enabled drones. It keeps everything QGC offers — full flight control, mission planning, and vehicle setup for PX4 and ArduPilot — and layers on an AI flight assistant, a redesigned Fly View, and a more streamlined day-to-day workflow.

This document covers what's **new in this fork**. For general QGC usage and build instructions, see the [Upstream Resources](#upstream-resources) below.

---

## ✨ What's New in This Fork

### 🤖 Titan AI — natural-language vehicle control

A built-in AI assistant that lets you fly and configure your aircraft by talking to it, in plain English, right from the Fly View.

- **Conversational control** powered by Anthropic's Claude. Ask it to arm, take off, fly to a point, change altitude or heading, set speed, orbit, switch flight modes, run missions, or read and write parameters.
- **Voice in, voice out** — optional speech-to-text (via Groq Whisper) and spoken responses, so you can issue commands hands-free.
- **Firmware- and vehicle-aware** — understands ArduCopter, ArduPlane, QuadPlane/VTOL, and PX4, and picks the right modes and commands for each (e.g. plane takeoff vs. copter guided takeoff, VTOL transitions).
- **Multi-step sequences** — chains actions together ("arm, take off to 30 m, then fly north 100 m"), tracks each step's progress live, and waits for completion before moving on.
- **Points of Interest** — drop POIs on the map and reference them by name ("orbit POI 2").
- **Safety first** — optional confirmation prompts for dangerous commands (arm, takeoff, emergency stop), with disarm blocked while flying.

Configure it under **Application Settings → Titan AI** (Claude API key required; Groq key optional for voice).

### 🛠️ Redesigned Fly View toolbar

- **Arm / Disarm** button right on the toolbar, colored red for visibility.
- **Quick flight-mode buttons** sourced from your `FLTMODE1`–`FLTMODE6` parameters.
- **Guided action confirmations** as a clear centered popup instead of a cramped toolbar control.
- **Frosted-glass blur** and reduced opacity for a cleaner overlay on top of the map and video.
- **Shortened flight-mode names** for fixed-wing / FBW modes so they fit at a glance.
- **Richer battery tooltip** showing current, power draw, and efficiency — plus a fix for the upstream bug that showed a half-full yellow icon at 0%.

### 📹 Picture-in-Picture video

- **Draggable PiP overlay** that snaps to screen edges and stays put when the window resizes.
- **Resizable** with a settings gear and a dropdown of common **aspect ratios**.

### 🗂️ Split-view configuration

- **Split toggle** to expand the Configure/Settings panel to full width, with smooth animations, and the layout is **remembered across launches**.
- **Redesigned parameter editor** with a styled, readable table.
- General polish across the Vehicle Configuration and Parameters pages.

### 🎨 Theming & defaults

- **Orange accent theme** and Titan Dynamics branding throughout.
- Sensible out-of-the-box defaults: instrument panel set to **Horizontal Compass & Attitude**, audio **muted on first run**, and a mobile layout that uses full-width panels and the indoor theme.

---

## Upstream Resources

Titan Ground Control tracks upstream QGroundControl. For everything not specific to this fork, the official QGC documentation applies:

- 📘 [User Manual](https://docs.qgroundcontrol.com/en/)
- 🛠️ [Developer Guide](https://dev.qgroundcontrol.com/en/) and [build instructions](https://dev.qgroundcontrol.com/en/getting_started/)
- 🤝 [Contributing](.github/CONTRIBUTING.md)
- 📜 [License Information](https://github.com/mavlink/qgroundcontrol/blob/master/.github/COPYING.md)

Titan Ground Control is open source under the same license as QGroundControl.
