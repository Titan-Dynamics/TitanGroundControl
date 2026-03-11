#pragma once

// Comprehensive ArduPilot & MAVLink knowledge base for the AI assistant.
// This is appended to the system prompt to give the AI deep domain knowledge.
// It is cached via prompt caching so the token cost is minimal after the first call.

// MSVC limits string literals to 16380 chars, so we split into parts.
static const char* kAIKnowledgeBase1 = R"(

═══════════════════════════════════════════════════════════════
ARDUPILOT & MAVLINK KNOWLEDGE BASE
═══════════════════════════════════════════════════════════════

Use this reference to answer parameter questions, understand
flight behavior, and make informed decisions about commands.

───────────────────────────────────────────────────────────────
K1. ARDUCOPTER FLIGHT MODES
───────────────────────────────────────────────────────────────

Mode 0 — Stabilize
  Self-levels when sticks released. NO altitude hold, NO position hold.
  Throttle directly controls motor output. No GPS needed.
  GCS cannot command movements — switch to Guided.

Mode 1 — Acro
  Rate-controlled, no self-leveling. For aerobatics. No GPS needed.
  GCS cannot command movements.

Mode 2 — Altitude Hold (Alt Hold)
  Like Stabilize but holds altitude via barometer. Throttle stick
  controls climb/descent rate; center = hold. No GPS needed.
  Does NOT hold horizontal position — drifts with wind.
  GCS cannot command movements.

Mode 3 — Auto
  Follows pre-loaded mission waypoints autonomously. Requires GPS.
  GCS can start/pause/jump-to-waypoint. For goto, switch to Guided.

Mode 4 — Guided ★ PRIMARY AI/GCS MODE
  Accepts goto, velocity, and acceleration commands from GCS.
  Holds position when no command active. Requires GPS.
  All navigation commands (goto, fly_heading, change_alt) work here.
  WPNAV_SPEED limits max horizontal speed (default 500 cm/s = 5 m/s).

Mode 5 — Loiter
  GPS position + altitude hold. Pilot sticks command velocity.
  Sticks centered = hold position. Requires GPS.
  GCS cannot command movements — switch to Guided.

Mode 6 — RTL (Return to Launch)
  Copter: Climbs to RTL_ALT → flies home → loiters RTL_LOIT_TIME → lands → disarms.
  RTL_ALT is in centimeters (default 1500 = 15m). 0 = current alt.

Mode 7 — Circle
  Orbits around the point where Circle was entered.
  CIRCLE_RADIUS (cm, default 1000=10m), CIRCLE_RATE (deg/s, default 20).

Mode 9 — Land
  Descends vertically at current position. Auto-detects landing and disarms.
  LAND_SPEED (cm/s, default 50 = 0.5 m/s). GPS optional for position hold.

Mode 11 — Drift
  Car-like controls. Roll steers, pitch controls speed. Altitude hold.

Mode 13 — Sport
  Like Stabilize + altitude hold. Higher lean angles allowed (60 deg).

Mode 14 — Flip
  Performs a flip and returns to previous mode. Needs 10m+ altitude.

Mode 15 — AutoTune
  Auto-tunes PID gains via test maneuvers. Behaves like AltHold between twitches.
  Land in AutoTune to save gains.

Mode 16 — PosHold (Position Hold)
  Hybrid Stabilize + Loiter. Sticks: direct angle control.
  Released: GPS position hold. Requires GPS.

Mode 17 — Brake ⚠
  Emergency stop. Immediately brakes and holds position.
  BLOCKS ALL FLIGHT COMMANDS including from GCS.
  Must switch to Guided before issuing any movement commands.

Mode 18 — Throw
  Arm → throw copter → auto-stabilizes → enters PosHold/AltHold.

Mode 21 — Smart RTL
  Retraces flight path home in reverse. Avoids obstacles.
  Falls back to RTL if path buffer full. SRTL_POINTS (default 300).

Mode 22 — FlowHold
  Position hold via optical flow sensor. No GPS needed.
  Best at low altitude over textured surfaces.

Mode 23 — Follow
  Follows another MAVLink vehicle or GCS. FOLL_ENABLE=1, FOLL_SYSID.

Mode 24 — ZigZag
  Back-and-forth pattern for agricultural spraying.

Mode 27 — AutoRTL
  Returns home using Auto mission system.

Mode 28 — Turtle
  Flips crashed inverted copter back upright using selected motors.

───────────────────────────────────────────────────────────────
K2. ARDUPLANE FLIGHT MODES
───────────────────────────────────────────────────────────────

Mode 0 — Manual
  Zero autopilot intervention. Direct stick-to-surface. Most dangerous.

Mode 1 — Circle
  Circles around entry point. Auto throttle + altitude hold.

Mode 2 — Stabilize
  Wing-leveling. Manual throttle. No altitude hold. No GPS needed.

Mode 3 — Training
  Like Manual but prevents exceeding bank/pitch limits. Safety wrapper.

Mode 4 — Acro
  Rate-controlled for aerobatics. No self-leveling.

Mode 5 — FBWA (Fly-By-Wire A)
  Primary stabilized mode. Stick = desired bank/pitch angle.
  Manual throttle. Enforces ROLL_LIMIT_DEG and PTCH_LIM limits.
  Stall prevention active. No GPS needed.

Mode 6 — FBWB (Fly-By-Wire B)
  Like FBWA but with altitude hold. Pitch stick = climb rate.
  Throttle controls airspeed. Uses TECS.

Mode 7 — Cruise
  FBWB + GPS ground track hold. Wings level = hold heading.
  Banking changes heading. Easiest long-distance mode.
  For QuadPlanes: triggers forward flight transition.

Mode 8 — AutoTune
  Auto-tunes roll/pitch PID. Behaves like FBWA during tuning.

Mode 10 — Auto
  Follows pre-loaded mission. Fully autonomous. Requires GPS.

Mode 11 — RTL ⚠ DOES NOT LAND FOR PLANES
  Flies home and LOITERS (circles). Does NOT land.
  RTL_ALTITUDE controls altitude in meters (default 100).
  For landing, use Autoland (mode 26) instead.

Mode 12 — Loiter
  Circles at current position/altitude. WP_LOITER_RAD controls radius.

Mode 13 — Takeoff
  Automatic takeoff. Detects launch, climbs to TKOFF_ALT.
  Usage: set_flight_mode("Takeoff") → arm.

Mode 15 — Guided ★ PRIMARY AI/GCS MODE FOR PLANES
  Accepts goto commands. Plane flies to target then LOITERS there.
  Cannot hover — always circling or moving. Uses DO_REPOSITION.

Mode 17 — QStabilize (QuadPlane)
  VTOL stabilize. Manual throttle. No altitude/position hold.

Mode 18 — QHover (QuadPlane)
  VTOL altitude hold. Baro-based. No position hold.

Mode 19 — QLoiter (QuadPlane)
  VTOL position + altitude hold. Requires GPS. Like copter Loiter.
  Use only when user wants VTOL hover at a specific spot.

Mode 20 — QLAND (QuadPlane)
  VTOL vertical landing. Handles full landing + disarm.
  Do NOT add wait/disarm after.

Mode 21 — QRTL (QuadPlane) ★
  Return home and land vertically. THE mode for "come home and land"
  for QuadPlanes. Handles everything. No extra actions needed.

Mode 24 — Thermal
  Automatic thermal soaring. Detects updrafts and circles in them.

Mode 26 — Autoland ★
  For fixed-wing: flies home AND lands. Use when user says "come home
  and land" for a plane. Unlike RTL which only loiters.

───────────────────────────────────────────────────────────────
K3. ARDUCOPTER KEY PARAMETERS
───────────────────────────────────────────────────────────────

─── Attitude Control (ATC_*) ───
ATC_ANG_RLL_P      Roll angle P gain (0-12, default 4.5)
ATC_ANG_PIT_P      Pitch angle P gain (0-12, default 4.5)
ATC_ANG_YAW_P      Yaw angle P gain (0-12, default 4.5)
ATC_RAT_RLL_P      Roll rate P (0-0.5, default 0.135) — primary roll tune
ATC_RAT_RLL_I      Roll rate I (0-0.5, default 0.135) — usually = P
ATC_RAT_RLL_D      Roll rate D (0-0.05, default 0.0036)
ATC_RAT_PIT_P/I/D  Pitch rate PID (same ranges as roll)
ATC_RAT_YAW_P      Yaw rate P (0-0.5, default 0.18)
ATC_RAT_YAW_I      Yaw rate I (0-0.5, default 0.018)
ATC_SLEW_YAW       Max yaw rate for pilot (500-18000 cdeg/s, default 6000)
ATC_INPUT_TC        Pilot input time constant (0-1s, default 0.15)
ATC_ACCEL_R_MAX     Max roll accel (0-180000 cdeg/s/s, default 110000)
ATC_ACCEL_P_MAX     Max pitch accel (0-180000 cdeg/s/s, default 110000)
ATC_ACCEL_Y_MAX     Max yaw accel (0-72000 cdeg/s/s, default 27000)

─── Position/Velocity Control (PSC_*) ───
PSC_POSXY_P    Horizontal position P (0.5-2, default 1.0)
PSC_VELXY_P    Horizontal velocity P (0.1-6, default 2.0)
PSC_VELXY_I    Horizontal velocity I (0-1, default 1.0)
PSC_VELXY_D    Horizontal velocity D (0-1, default 0.5)
PSC_POSZ_P     Vertical position P (0-6, default 1.0)
PSC_VELZ_P     Vertical velocity P (1-8, default 5.0)
PSC_ACCZ_P     Vertical accel P (0.2-1.5, default 0.3)
PSC_ACCZ_I     Vertical accel I (0-3, default 1.0)
PSC_JERK_XY    Horizontal jerk limit (1-20, default 5.0)

─── Waypoint Navigation (WPNAV_*) ───
WPNAV_SPEED      Max horizontal speed in guided/auto (20-2000 cm/s, default 500 = 5 m/s)
WPNAV_SPEED_UP   Max climb speed (10-1000 cm/s, default 250 = 2.5 m/s)
WPNAV_SPEED_DN   Max descent speed (10-500 cm/s, default 150 = 1.5 m/s)
WPNAV_RADIUS     Waypoint acceptance radius (5-1000 cm, default 200 = 2m)
WPNAV_ACCEL      Horizontal acceleration (50-500 cm/s/s, default 100)
WPNAV_ACCEL_Z    Vertical acceleration (50-500 cm/s/s, default 100)

─── Loiter (LOIT_*) ───
LOIT_SPEED       Max loiter speed (20-2000 cm/s, default 500 = 5 m/s)
LOIT_ACC_MAX     Max loiter accel (100-981 cm/s/s, default 250)
LOIT_BRK_ACCEL   Brake acceleration (25-250 cm/s/s, default 250)
LOIT_BRK_DELAY   Brake delay (0-2s, default 1.0)

─── Motor/Throttle (MOT_*) ───
MOT_THST_EXPO     Thrust curve expo (0-1, default 0.65)
MOT_THST_HOVER    Hover throttle (0-0.7, default 0.35) — auto-learned
MOT_SPIN_MIN      Min motor spin flying (0-0.5, default 0.15)
MOT_SPIN_MAX      Max motor spin (0.5-1, default 0.95)
MOT_SPIN_ARM      Motor spin when armed (0-0.3, default 0.1)
MOT_SPOOL_TIME    Spool up time (0-2s, default 0.5)
MOT_BAT_VOLT_MAX  Battery voltage compensation max (0-100V)
MOT_BAT_VOLT_MIN  Battery voltage compensation min (0-100V)
MOT_PWM_TYPE      Output type: 0=Normal, 1=OneShot, 4=DShot150, 6=DShot600
MOT_HOVER_LEARN   0=off, 1=learn, 2=learn+save (default 2)
MOT_YAW_HEADROOM  PWM reserved for yaw (0-500, default 200)

─── RTL ───
RTL_ALT         RTL altitude (0-800000 cm, default 1500 = 15m). 0 = current alt
RTL_ALT_FINAL   Descent alt before landing (-1 to 10000 cm, default 0 = land)
RTL_LOIT_TIME   Loiter time at home before landing (0-60000 ms, default 5000)
RTL_SPEED       RTL speed (0-2000 cm/s, 0 = WPNAV_SPEED)

─── Landing ───
LAND_SPEED       Final descent speed (30-200 cm/s, default 50 = 0.5 m/s)
LAND_SPEED_HIGH  Initial descent speed (0-2000 cm/s, 0 = WPNAV_SPEED_DN)
LAND_ALT_LOW     Altitude to switch to slow descent (100-10000 cm, default 1000 = 10m)
LAND_REPOSITION  Allow repositioning during land (0/1, default 1)

─── Failsafes ───
FS_THR_ENABLE   Throttle failsafe: 0=off, 1=RTL, 2=continue auto, 3=Land, 4=SmartRTL/RTL, 5=SmartRTL/Land
FS_THR_VALUE    Throttle PWM threshold (925-1100, default 975)
FS_GCS_ENABLE   GCS failsafe: same values as FS_THR_ENABLE
FS_GCS_TIMEOUT  GCS timeout (2-120s, default 5)
FS_BATT_ENABLE  Battery failsafe: 0=off, 1=Land, 2=RTL, 3=SmartRTL/RTL
FS_EKF_ACTION   EKF failsafe: 1=Land, 2=AltHold, 3=Land even stabilize
FS_EKF_THRESH   EKF variance threshold (0.1-1.0, default 0.8)
FS_CRASH_CHECK  Crash check enable (0/1, default 1)

─── Battery (BATT_*) ───
BATT_MONITOR     Monitor type: 0=off, 3=analog V, 4=analog V+I, 8=UAVCAN
BATT_CAPACITY    Total capacity mAh (0-100000, default 3300)
BATT_LOW_VOLT    Low voltage threshold (V). Triggers low battery action
BATT_CRT_VOLT    Critical voltage threshold (V)
BATT_LOW_MAH     Low remaining mAh threshold
BATT_CRT_MAH     Critical remaining mAh threshold
BATT_FS_LOW_ACT  Low action: 0=none, 1=Land, 2=RTL, 3=SmartRTL/RTL, 4=SmartRTL/Land, 5=Terminate
BATT_FS_CRT_ACT  Critical action: same values
BATT_ARM_VOLT    Min voltage to arm (V, 0=disabled)

─── Fence / Geofence ───
FENCE_ENABLE    Master enable (0/1)
FENCE_TYPE      Bitmask: bit0=max alt, bit1=circle, bit2=polygon, bit3=min alt
FENCE_ACTION    0=report, 1=RTL/Land, 2=Land, 3=SmartRTL/RTL, 4=Brake, 5=SmartRTL/Land
FENCE_ALT_MAX   Max altitude (10-1000m, default 100)
FENCE_ALT_MIN   Min altitude (-100 to 100m, default -10)
FENCE_RADIUS    Circular radius (30-100000m, default 300)
FENCE_MARGIN    Margin before fence (1-10m, default 2)

─── GPS ───
GPS_TYPE        0=none, 1=auto, 2=uBlox, 5=NMEA, 9=SBF, etc.
GPS_TYPE2       Second GPS type (for dual GPS)
GPS_AUTO_SWITCH 0=primary only, 1=auto switch, 2=blend, 3=best
GPS_NAVFILTER   8=airborne<4g (default, best for UAV)

─── EKF3 (Primary) ───
EK3_ENABLE       Master enable (0/1, default 1)
EK3_GPS_TYPE     0=3D vel+pos, 1=2D+vert, 2=2D, 3=no GPS
EK3_SRC1_POSXY   Position source: 1=GPS, 2=beacon, 3=extnav
EK3_SRC1_POSZ    Altitude source: 1=baro, 2=rangefinder, 3=GPS
EK3_SRC1_YAW     Yaw source: 1=compass, 2=GPS, 3=GPS+compass fallback
EK3_MAG_CAL      Mag cal: 0=never, 3=after arm+continuous (default)

─── Arming ───
ARMING_CHECK    Bitmask (1=all). Bits: 2=baro, 4=compass, 8=GPS, 16=INS, 64=RC, 256=battery
ARMING_RUDDER   0=disabled, 1=arm only, 2=arm+disarm (default 2)

─── INS (Sensors) ───
INS_GYRO_FILTER   Gyro LPF cutoff (10-256 Hz, default 20). Critical for tuning
INS_ACCEL_FILTER  Accel LPF cutoff (2-256 Hz, default 20)
INS_HNTCH_ENABLE  Harmonic notch filter (0/1). Critical for noise reduction
INS_HNTCH_FREQ    Notch base frequency (10-400 Hz)
INS_HNTCH_MODE    Tracking: 0=fixed, 1=throttle, 3=ESC telemetry, 4=FFT

─── General ───
ANGLE_MAX        Max lean angle (1000-8000 cdeg, default 3000 = 30 deg)
PILOT_SPEED_UP   Max pilot climb (50-500 cm/s, default 250)
PILOT_SPEED_DN   Max pilot descent (50-500 cm/s, 0=auto)
FRAME_CLASS      0=undefined, 1=quad, 2=hexa, 3=octa, 5=Y6, 6=heli, 7=tri
FRAME_TYPE       0=plus, 1=X, 2=V, 3=H
SYSID_THISMAV    MAVLink system ID (1-255, default 1)
DISARM_DELAY     Auto-disarm delay on ground (0-127s, default 10)

─── Circle ───
CIRCLE_RADIUS    Radius in cm (default 1000 = 10m). 0 = orbit own nose
CIRCLE_RATE      Angular rate deg/s (default 20). Negative = CCW

─── Smart RTL ───
SRTL_ACCURACY    Path accuracy (0-10m, default 2)
SRTL_POINTS      Max breadcrumb points (0-500, default 300)

─── Follow Mode ───
FOLL_ENABLE      Enable (0/1)
FOLL_SYSID       System ID to follow
FOLL_OFS_X/Y/Z   Offset from target (meters, body frame)
FOLL_ALT_TYPE    0=relative to target, 1=AMSL
)";

static const char* kAIKnowledgeBase2 = R"(
───────────────────────────────────────────────────────────────
K4. ARDUPLANE KEY PARAMETERS
───────────────────────────────────────────────────────────────

─── Navigation ───
WP_LOITER_RAD    Loiter radius (m). Positive=CW, negative=CCW (default 60)
                 Also used for RTL loiter and Guided mode loiter.
                 In Guided mode, changing just the param doesn't take effect
                 immediately — the GCS also sends a reposition command.
WP_RADIUS        Waypoint acceptance radius (1-32767m, default 90)
NAVL1_PERIOD     L1 nav period (8-35s, default 17). Lower=tighter tracking
NAVL1_DAMPING    L1 damping (0.6-1.0, default 0.75)

─── Takeoff (TKOFF_*) ───
TKOFF_ALT          Target takeoff altitude (0-5000m, default 50)
TKOFF_THR_MAX      Max throttle during takeoff (0-100%, default 75)
TKOFF_ROTATE_SPD   Rotation airspeed for ground takeoff (m/s)
TKOFF_THR_DELAY    Throttle delay after launch detection (0.1s units, default 2)
TKOFF_GND_PITCH    Pitch during ground roll (deg, default 5)

─── Landing (LAND_*) ───
LAND_FLARE_ALT     Flare altitude (0-30m, default 3)
LAND_FLARE_SEC     Flare time trigger (0-10s, default 2)
LAND_DISARMDELAY   Delay before auto-disarm after landing (0-127s, default 20)
LAND_ABORT_THR     Throttle for go-around (0-100%, 0=use TKOFF_THR_MAX)
LAND_PITCH_DEG     Pitch during flare (degrees, default 0)
LAND_TYPE           0=glide slope, 1=deepstall

─── TECS (Speed/Altitude Control) ───
TECS_CLMB_MAX      Max climb rate (0.1-20 m/s, default 5)
TECS_SINK_MIN      Min sink rate (0.1-10 m/s, default 2)
TECS_SINK_MAX      Max sink rate (0-20 m/s, default 5)
TECS_TIME_CONST    Time constant (3-10s, default 5). Lower=more aggressive
TECS_SPDWEIGHT     Speed vs height priority (0-2, default 1). 0=height, 2=speed
TECS_RLL2THR       Throttle boost in turns (0-20, default 10)
TECS_LAND_ARSPD    Landing approach airspeed (m/s, -1=use AIRSPEED_CRUISE)
TECS_LAND_SINK     Landing sink rate (0.1-2 m/s, default 0.25)
TECS_PITCH_MAX     Max TECS pitch (0-45 deg, 0=use PTCH_LIM_MAX_DEG)
TECS_PITCH_MIN     Min TECS pitch (-45 to 0 deg, 0=use PTCH_LIM_MIN_DEG)
TECS_THR_DAMP      Throttle demand loop damping
TECS_PTCH_DAMP     Pitch demand loop damping (default 0.0)
TECS_INTEG_GAIN    Integrator gain on control loop
TECS_VERT_ACC      Max vertical acceleration (m/s², default 7)

─── Throttle ───
THR_MAX          Max throttle (0-100%, default 75)
THR_MIN          Min throttle (-100 to 100%, default 0). Negative=reverse thrust
TRIM_THROTTLE    Level flight throttle (0-100%, default 45). Critical tuning param
AIRSPEED_CRUISE  Target cruise airspeed (m/s)
THR_SLEWRATE     Max throttle change rate (%/s, 0-127, default 100)

─── Flight Limits ───
PTCH_LIM_MAX_DEG Max pitch up (0-90 deg, default 20)
PTCH_LIM_MIN_DEG Max pitch down (-90 to 0 deg, default -25)
ROLL_LIMIT_DEG   Max bank angle (0-90 deg, default 45)
AIRSPEED_MAX     Max demanded airspeed (5-100 m/s, default 22)
AIRSPEED_MIN     Min demanded airspeed (5-100 m/s, default 9). Must exceed stall speed
AIRSPEED_STALL   Stall airspeed for load factor calculations (5-75 m/s)
STALL_PREVENTION Enable stall prevention (0/1, default 1). Limits bank at low speed
SCALING_SPEED    Airspeed for 1:1 surface deflection (0-50 m/s, default 15)

─── Airspeed Sensor ───
ARSPD_ENABLE     Enable airspeed sensor support (0/1)
ARSPD_TYPE       0=none, 1=analog, 2=MS4525, 3=MS5525
ARSPD_USE        0=don't use, 1=use, 2=use but don't disable on error
ARSPD_AUTOCAL    Auto calibrate ratio (0/1)
ARSPD_RATIO      Calibration ratio (1-4, default ~2)

─── RTL ───
RTL_ALTITUDE     RTL altitude in METERS (default 100). -1 = current alt
RTL_AUTOLAND     0=loiter only, 1=autoland if mission has landing sequence, 2=always use landing
RTL_RADIUS       RTL loiter radius (m, 0=use WP_LOITER_RAD)
RTL_CLIMB_MIN    Initial RTL climb distance (0-30m)

─── Failsafes ───
FS_SHORT_ACTN    Short RC loss: 0=continue, 1=RTL, 2=glide, 3=parachute
FS_SHORT_TIMEOUT Short failsafe timeout (0-100s, default 1.5)
FS_LONG_ACTN     Long loss: 0=continue, 1=RTL, 2=glide, 3=parachute
FS_LONG_TIMEOUT  Long failsafe timeout (0-300s, default 5)
FS_GCS_ENABL     GCS failsafe: 0=off, 1=RTL, 2=only in AUTO
BATT_FS_LOWACT   Low batt: 0=none, 1=RTL, 2=Land, 3=terminate, 4=QLand
BATT_FS_CRTACT   Critical batt: same values

─── Fence ───
FENCE_ENABLE     Master enable (0/1)
FENCE_TYPE       Bitmask: bit0=max alt, bit1=circle, bit2=polygon, bit3=min alt
FENCE_ACTION     0=report, 1=RTL, 6=guided return, 7=guided to rally
FENCE_ALT_MAX    Max altitude (m, default 100)
FENCE_RADIUS     Circle radius (30-100000m, default 300)

─── FBWB / Cruise ───
FBWB_CLIMB_RATE  Max climb rate in FBWB (1-10 m/s, default 2)
CRUISE_SPEED     Cruise mode airspeed (m/s, 0=use AIRSPEED_CRUISE)

─── Terrain ───
TERRAIN_ENABLE   Enable terrain database (0/1)
TERRAIN_FOLLOW   Enable terrain following in AUTO (0/1)
TERRAIN_LOOKAHD  Look-ahead distance (0-10000m, default 2000)

─── Tuning ───
PTCH2SRV_P       Pitch servo P (0.1-3, default 1.0)
PTCH2SRV_I       Pitch servo I (0-0.5, default 0.3)
PTCH2SRV_D       Pitch servo D (0-0.1, default 0.04)
RLL2SRV_P        Roll servo P (0.1-3, default 1.0)
RLL2SRV_I        Roll servo I (0-0.5, default 0.3)
RLL2SRV_D        Roll servo D (0-0.1, default 0.04)

─── Misc ───
KFF_RDDRMIX      Rudder mixing for coordinated turns (0-1, default 0.5)
GROUND_STEER_ALT Ground steering altitude (0-100m, default 5)
USE_REV_THRUST   Reverse thrust: 0=off, 1=in landing, 2=all auto

───────────────────────────────────────────────────────────────
K5. QUADPLANE / VTOL PARAMETERS (Q_*)
───────────────────────────────────────────────────────────────

Q_ENABLE          Enable QuadPlane VTOL (0/1). Requires reboot.
Q_FRAME_CLASS     VTOL frame: 1=quad, 2=hexa, 3=octa, 7=tri, 10=tailsitter
Q_FRAME_TYPE      Motor layout: 0=plus, 1=X
Q_ASSIST_SPEED    Below this airspeed, VTOL motors assist (m/s, 0=off).
                  Critical safety feature. Set 2-3 m/s above stall speed.
Q_ASSIST_ALT      Below this alt, VTOL motors assist (m, 0=off)
Q_ASSIST_ANGLE    Attitude error triggering assist (0-90 deg, default 30)
Q_RTL_MODE        QRTL behavior: 0=VTOL return+land, 1=like plane RTL,
                  2=FW approach then VTOL land, 3=use mission landing
Q_TRANSITION_MS   Max transition time hover→FW (0-120000 ms, default 5000)
Q_TRANS_DECEL     FW→VTOL deceleration (0.5-10 m/s/s, default 2)
Q_TRANS_FAIL      Transition failure action: 0=continue VTOL, 1=QLAND
Q_VFWD_GAIN       Forward motor assist in hover (0-1, default 0)
Q_WVANE_GAIN       Weathervane gain in hover (0-1, default 0). Yaws into wind.
Q_LAND_SPEED      VTOL descent speed (30-200 cm/s, default 50 = 0.5 m/s)
Q_LAND_FINAL_ALT  Final descent altitude (0.1-20m, default 6)
Q_LOIT_SPD        Max VTOL loiter speed (20-3500 cm/s, default 500 = 5 m/s)
Q_WP_SPEED        VTOL waypoint speed (20-2000 cm/s, default 500 = 5 m/s)
Q_WP_SPEED_UP     VTOL climb speed (10-1000 cm/s, default 250)
Q_WP_SPEED_DN     VTOL descent speed (10-500 cm/s, default 150)
Q_ANGLE_MAX       Max lean angle in VTOL (1000-8000 cdeg, default 3000 = 30 deg)

Q_OPTIONS bitmask (key bits):
  bit 0 (1)    = Level transition
  bit 1 (2)    = Allow FW takeoff
  bit 4 (16)   = Disable approach for VTOL landing
  bit 5 (32)   = Reposition in fixed-wing mode
  bit 7 (128)  = Use FW approach for VTOL landing

───────────────────────────────────────────────────────────────
K6. MAVLINK COMMANDS REFERENCE
───────────────────────────────────────────────────────────────

─── Key Navigation Commands ───

MAV_CMD_NAV_WAYPOINT (16)
  For missions. Params: hold_time, accept_radius, pass_radius, yaw, lat, lon, alt.

MAV_CMD_NAV_LOITER_UNLIM (17)
  Loiter indefinitely. Param3: radius (pos=CW, neg=CCW). Fixed-wing only.

MAV_CMD_NAV_LOITER_TURNS (18)
  Loiter for N turns. Param1: turns. Param3: radius.

MAV_CMD_NAV_LOITER_TIME (19)
  Loiter for N seconds. Param1: time(s). Param3: radius.

MAV_CMD_NAV_RETURN_TO_LAUNCH (20)
  Return to launch. No params. Behavior varies by vehicle type.

MAV_CMD_NAV_LAND (21)
  Land. Params: abort_alt, precision_land_mode, yaw, lat, lon, alt.

MAV_CMD_NAV_TAKEOFF (22)
  Takeoff. Params: min_pitch, yaw, lat, lon, target_alt.

─── Key DO Commands ───

MAV_CMD_DO_SET_MODE (176)
  Set flight mode. param1=MAV_MODE_FLAG_CUSTOM_MODE_ENABLED(1),
  param2=ArduPilot custom mode number.

MAV_CMD_DO_CHANGE_SPEED (178)
  param1: speed type (0=airspeed, 1=groundspeed, 2=climb, 3=descent)
  param2: speed m/s (-1=no change). param3: throttle % (-1=no change).

MAV_CMD_DO_SET_HOME (179)
  param1: 1=current location, 0=specified. params5-7: lat, lon, alt.

MAV_CMD_DO_SET_SERVO (183)
  param1: servo channel (1-16). param2: PWM (1000-2000).

MAV_CMD_DO_REPOSITION (192) ★ PRIMARY GUIDED GOTO
  THE command for guided mode navigation.
  param1: ground speed m/s (-1=default)
  param2: bitmask (MAV_DO_REPOSITION_FLAGS_CHANGE_MODE)
  param3: loiter radius (planes, positive only)
  param4: yaw/loiter direction. For planes: 0=CW, 1=CCW
  param5-7: lat, lon, alt
  NaN for unused position params = no change.

MAV_CMD_DO_PAUSE_CONTINUE (193)
  param1: 0=pause, 1=continue.

MAV_CMD_DO_SET_ROI_LOCATION (195)
  Point gimbal/camera at location. params5-7: lat, lon, alt.

MAV_CMD_DO_SET_ROI_NONE (197)
  Cancel ROI.

MAV_CMD_DO_FENCE_ENABLE (207)
  param1: 0=disable, 1=enable, 2=disable floor only.

MAV_CMD_COMPONENT_ARM_DISARM (400)
  param1: 1=arm, 0=disarm.
  param2: 0=normal, 21196=FORCE (bypasses checks, dangerous!).

─── Mission Protocol ───
MISSION_COUNT (44)    Initiate upload/download
MISSION_REQUEST_INT   Autopilot requests item by sequence #
MISSION_ITEM_INT      Single mission item (preferred over MISSION_ITEM)
MISSION_ACK (47)      Confirms completion
MISSION_CURRENT (42)  Current active waypoint
Mission items start at sequence 0 (home position).
)";

static const char* kAIKnowledgeBase3 = R"(
───────────────────────────────────────────────────────────────
K7. FLIGHT BEHAVIOR & SAFETY KNOWLEDGE
───────────────────────────────────────────────────────────────

─── Altitude Reference Systems ───
  Relative:  Height above home position (copter default)
  AMSL:      Above Mean Sea Level (plane default)
  Terrain:   Above terrain (needs terrain data or rangefinder)
  Home:      Same as relative; home altitude = 0

  CRITICAL: Wrong altitude frame can cause crashes. Copter commands
  typically use relative altitude. Plane missions often use AMSL.

─── RTL Behavior Summary ───
  Copter RTL:    Climbs to RTL_ALT → flies home → loiters → LANDS → disarms
  Plane RTL:     Flies to RTL_ALTITUDE → LOITERS (does NOT land). Use Autoland to land.
  QuadPlane QRTL: Flies home (FW if fast) → transitions VTOL → lands vertically
  Smart RTL:     Retraces path. Copter only. Falls back to RTL if buffer full.

─── Loiter Behavior ───
  Copter Loiter: Hovers in place (GPS position hold)
  Plane Loiter:  Circles a point at WP_LOITER_RAD radius
  QLoiter:       VTOL hover (like copter Loiter)
  Plane Guided:  After reaching target, loiters in circles (cannot hover)

─── VTOL Transition ───
  Hover → Forward Flight:
    1. Forward motor spools up, VTOL motors maintain altitude
    2. As airspeed builds, wing generates lift
    3. When airspeed > Q_ASSIST_SPEED, VTOL motors reduce
    4. After Q_TRANSITION_MS, VTOL motors stop
  Forward → Hover:
    1. Forward motor reduces, VTOL motors spool up
    2. Aircraft decelerates, VTOL provides lift
    3. Transition complete when airspeed < Q_ASSIST_SPEED

  Q_ASSIST: If airspeed drops below Q_ASSIST_SPEED during FW flight,
  VTOL motors automatically engage to prevent stall. Active in ALL
  FW modes. Critical safety feature.

─── Speed Units Warning ───
  Copter params: mostly cm/s (WPNAV_SPEED=500 means 5 m/s)
  Plane params: mostly m/s (AIRSPEED_CRUISE in m/s)
  MAVLink commands: always m/s
  Altitude: RTL_ALT (copter) in cm, RTL_ALTITUDE (plane) in meters

─── Parameter Safety Notes ───
  Safe to change in flight: speed params (WPNAV_SPEED, WP_SPEED),
    some PID gains, RTL_ALT, WP_LOITER_RAD
  NEVER change in flight: FRAME_CLASS, FRAME_TYPE, MOT_PWM_TYPE,
    INS_FAST_SAMPLE, EK3_ENABLE, GPS_TYPE, SERIAL protocols
  Params needing reboot: MOT_PWM_TYPE, FRAME_CLASS/TYPE, Q_ENABLE,
    INS_HNTCH_ENABLE, SERIAL*_PROTOCOL, GPS_TYPE

─── Common Mistakes to Avoid ───
  1. Wrong altitude frame in commands — always verify relative vs AMSL
  2. Plane RTL doesn't land — use Autoland or QRTL
  3. Disarming in flight — NEVER (causes crash)
  4. Brake mode blocks commands — switch to Guided first
  5. Too-small loiter radius for planes — causes spiral/overshoot
  6. Forgetting mode switch before goto — Guided required
  7. VTOL transition at low altitude — very risky, needs altitude
  8. Battery in hover — 3-5x more power than forward flight for VTOLs

─── Guided Mode: Copter vs Plane ───
  Copter Guided: Flies to point and HOVERS there
  Plane Guided:  Flies to point and LOITERS (circles) there
  QuadPlane:     Depends on speed — hover if slow, circle if fast

─── DO_REPOSITION for Guided Goto ───
  This is how the GCS sends goto commands (not NAV_WAYPOINT).
  For planes, param3 = loiter radius at destination.
  For copters, loiter radius is irrelevant (they hover).
  The vehicle must be in Guided mode.

─── Failsafe Priority ───
  Battery critical > RC lost > GCS lost > EKF failure > Fence breach
  Each has configurable actions. Battery critical is highest priority
  and can override other failsafe responses.

─── Key Tuning Order (Copter) ───
  1. INS_GYRO_FILTER + notch filters (INS_HNTCH_*)
  2. MOT_THST_HOVER (auto-learned)
  3. Rate PIDs (ATC_RAT_RLL/PIT/YAW P/I/D)
  4. Angle P gains (ATC_ANG_RLL/PIT/YAW_P)
  5. Position/velocity (PSC_*)

─── Key Tuning Order (Plane) ───
  1. AIRSPEED_CRUISE + TRIM_THROTTLE (set for level cruise)
  2. RLL2SRV and PTCH2SRV PIDs
  3. TECS parameters (altitude/speed control)
  4. NAVL1_PERIOD (navigation tracking aggressiveness)
  5. Landing parameters (LAND_FLARE_ALT, LAND_PITCH_DEG)

───────────────────────────────────────────────────────────────
K8. SERVO FUNCTION REFERENCE
───────────────────────────────────────────────────────────────

SERVOx_FUNCTION common values:
  0   = Disabled (available for manual control via set_servo)
  4   = Aileron
  19  = Elevator
  21  = Rudder
  33  = Motor1  (34=Motor2, 35=Motor3, ... 40=Motor8)
  51  = RCIN1 passthrough (52=RCIN2, ...)
  70  = Throttle
  73  = ThrottleLeft
  74  = ThrottleRight
  77  = Landing Gear
  94  = Script1

Only channels with FUNCTION=0 are safe for set_servo commands.
Motor channels (33-40) must NEVER be set manually.

───────────────────────────────────────────────────────────────
K9. UNIT CONVERSION CHEAT SHEET
───────────────────────────────────────────────────────────────

  1 m/s  = 100 cm/s      (WPNAV_SPEED=500 → 5 m/s)
  1 deg  = 100 cdeg      (copter: ANGLE_MAX=3000 → 30 deg)
  1 m    = 100 cm        (RTL_ALT=1500 → 15 m)
  knots to m/s: multiply by 0.5144
  mph to m/s: multiply by 0.4470
  ft to m: multiply by 0.3048

  ArduPilot uses these unit conventions:
    _CD suffix = centidegrees
    _CM suffix = centimeters or cm/s
    No suffix  = usually meters or m/s (check per param)

)";
