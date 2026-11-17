# OnSpeed Gen2 — Software Documentation

*Generated 2026-07-16 from the source tree at commit `5e70410` (firmware v3.3.7a); **updated
2026-07-16 to describe branch `merge-upstream-full-2026-07` (firmware v3.3.7g)**, the intended
next `master` pending physical flash/verification — see
[BUILD-PLAN-macOS26.md](BUILD-PLAN-macOS26.md) §Phase 6 for how that branch came to be. Sections
below describe the `.7g` code as it currently stands on that branch; `master` itself is still on
`3.3.7a` until that branch is merged.*

---

## 1. System overview

OnSpeed Gen2 is an avionics-independent Angle-of-Attack (AoA) aural indicator for experimental
aircraft. It reads differential pressure from an AoA pitot probe plus static pressure, computes a
calibrated AoA per flap position, and renders the OnSpeed progressive tone scheme (L/D-max pulses →
solid "onspeed" tone → fast-pulse stall warning) directly into the aircraft audio panel. It logs
all data at 50 Hz to SD, serves an AoA display stream over RS-232/TTL, and exposes a Wi-Fi web UI
for configuration, calibration, and log download.

### Processors and links

| Processor | Firmware | Role |
|---|---|---|
| **Teensy 3.6** (MK66FX1M0, Cortex-M4F) | `OnSpeedTeensy_AHRS` v3.3.7g (on `merge-upstream-full-2026-07`; `master` is still v3.3.7a) | Main computer: sensors, AHRS, AoA, tones, SD logging |
| **ESP32-PICO-KIT-D4** | `OnSpeedWifi` v3.3.7 | Wi-Fi AP + web UI, config/calibration front-end, log download, its own OTA |
| **M5Stack Core (ESP32)** — *optional* | `OnSpeed_M5_display` v3.2.3g | Cockpit display (David runs the **huVVer-AVI** instead — separate firmware, outside this repo) |

Inter-processor links:

- **Teensy `Serial4` ↔ ESP32 `Serial` @ 1,000,000 baud** — command/response protocol
  (`$CMD!` requests, `<TAG>…</TAG>` responses) plus the 10 Hz `$ONSPEED,…` live-data stream.
- **Teensy `Serial5`/`Serial3`/`Serial1` → display @ 115,200 baud** — one-way fixed-width
  80-byte `#1…` telemetry frames (or Garmin G3X format), selected by config.
- **Teensy `Serial3` ← EFIS @ 115,200**, **Teensy `Serial1` ← flight-test boom @ 115,200** — inputs.

### Signal path (main firmware)

```
AoA pitot (Pfwd, P45)      static port           IMU (LSM9DS1 or ISM330DHCX)
      │ i2c ×2                  │ i2c                    │ i2c
      ▼                         ▼                        ▼
 SensorRead (50 Hz) ────► AOAcalc/CurveCalc      IMUread (208–238 Hz)
      │   IAS, coeffP, AOA        │                      │
      │                           ▼                      ▼
      │                    per-flap setpoints      AHRS (Madgwick + Kalman)
      │                           │                pitch/roll/VSI/flightpath
      ▼                           ▼                      │
   LogData ring buffer ◄──── Tones/3DAudio ◄─────────────┘
      │  50 Hz CSV                │ DAC stereo out
      ▼                           ▼
   SD card (SDIO)           audio panel
```

---

## 2. Repository layout

- `Software/OnSpeedTeensy_AHRS/` — main firmware (multi-`.ino` Arduino sketch; §4)
- `Software/OnSpeedWifi/` — ESP32 Wi-Fi firmware (§5)
- `Software/OnSpeed_M5_display/` — optional M5Stack display firmware (§6)
- `Software/OnSpeed_hardware_test/` — Teensy board bring-up test (§7)
- `Software/Arduino/libraries/` — **vendored libraries; the sketchbook must point here** (§10)
- `Software/Matlab/`, `TestFlightData/` — offline analysis and reference data (out of scope here)
- `Hardware/` — Eagle schematics/boards, enclosure files, system diagram
- `Documents/` — user manual, assembly and programming procedures (the programming-procedure PDF
  predates the current toolchain pins; see BUILD-PLAN)
- `BUILD-mac.md`, `CLAUDE.md` — Mac build runbook and project context
- Prebuilt binaries checked in per sketch: `OnSpeedTeensy_AHRS.ino.TEENSY36.hex`,
  `OnSpeedWifi.ino.pico32.bin`, `OnSpeed_M5_display.ino.m5stack_core_esp32.bin`

Arduino build model: all `.ino` files in a sketch folder are **concatenated into one translation
unit** — globals, `#define`s, and functions are shared across every `.ino` in the folder. The only
real header/`.cpp` units in the main sketch are `KalmanFilter`, `MadgwickFusion`, and the
`AudioSample*` data files.

---

## 3. Firmware inventory and versions

| Item | Version | Where defined |
|---|---|---|
| Teensy main firmware | **3.3.7g** on `merge-upstream-full-2026-07` (`master` still 3.3.7a) | `#define VERSION` at `OnSpeedTeensy_AHRS.ino:14` |
| Wifi firmware | **3.3.7** | `String wifi_fw` at `OnSpeedWifi.ino:38` |
| M5 display firmware | 3.2.3g | `#define firmwareVersion` at `OnSpeed_M5_display.ino:24` |
| Hardware test | 1.1 | header comment |
| Settings library | 3.2.20 | `Onspeed-settingsFunctions.h` |
| Declared toolchain (Teensy) | Arduino 1.8.16 + Teensyduino 1.55 | `OnSpeedTeensy_AHRS.ino:1` |
| Declared toolchain (Wifi) | ESP32 core **1.0.6** (core 2.0 explicitly warned against — slow HTTP) | `OnSpeedWifi.ino:1-4` |
| Declared toolchain (M5) | ESP32 core **2.0** required | `OnSpeed_M5_display.ino:1-2` |
| Bundled SdFat | Repo vendors **2.1.0** (`libraries/SdFat-master`), code comment says 2.1.0 — but on `teensy:avr` 1.62.0 the platform's own bundled SdFat **2.1.2** wins the build (verified 2026-07-16, §11 point 10). BUILD-mac.md's "2.1.2" was correct. | `libraries/SdFat-master/library.properties` |

Upstream note: the un-merged upstream commit `610ae09` ("ventura update") declares the code
"Mac Ventura & Arduino 2.0 ready" and recommends Teensyduino 1.58 on Mac.

---

## 4. Main firmware — `Software/OnSpeedTeensy_AHRS/`

### 4.1 `OnSpeedTeensy_AHRS.ino` — main sketch

**Version & config gating.** `VERSION "3.3.7g"` on `merge-upstream-full-2026-07` (line 14,
followed by the full changelog; `master` still reads `"3.3.7a"` at this writing). `DEFAULT_CONFIG`
aliases `default_config`; `#include "local_config.h"` pulls in installation-specific feature
defines (§8.2) — line numbers shifted slightly from the `.7a` baseline due to the merge, exact
offsets not re-verified line-by-line in this pass.

**Debug switches** (all commented out by default, lines 85–94): `SENSORDEBUG`, `EFISDATADEBUG`,
`BOOMDATADEBUG`, `TONEDEBUG`, `SDCARDDEBUG`, `VOLUMEDEBUG`, `VNDEBUG`, `AXISDEBUG`,
`IMUTEMPDEBUG`, `AGEDEBUG`.

**Interrupt priorities** (lines 78–81): `ToneTimer` 96 (highest), `IMUTimer` 128, `SensorTimer`
128, `LogReplayTimer` 240. The heavy real-time work runs off `IntervalTimer` ISRs, not `loop()`.

**Key macros.**
- `PCOEFF(p_fwd,p_45)` — pressure coefficient: `atan2(p_45,p_fwd)` under `SPHERICAL_PROBE`,
  else ratiometric `p_45/p_fwd` (lines 107–112).
- `IASCURVE(x)` — hardcoded Zlin polynomial, spherical-probe only.
- `BOOM_*_CALC` macros are currently identity — boom data is logged as **raw counts** and
  converted on the host side (lines 122–125).
- `AUDIO_3D_CURVE(x) = -92.822x² + 20.025x` (line 202) for stereo panning.

**Notable constants.** `BOOM_PACKET_SIZE 50`, `EFIS_PACKET_SIZE 512`; AOA clamps `AOA_MAX_VALUE
40` / `AOA_MIN_VALUE -20`; tone parameters `HIGH_TONE_STALL_PPS 20`, `HIGH_TONE_PPS_MAX 6.2`,
`HIGH_TONE_PPS_MIN 1.5`, `HIGH_TONE_HZ 1600`, `LOW_TONE_PPS_MAX 8.2`, `LOW_TONE_HZ 400`,
`TONE_RAMP_TIME 15`, `STALL_RAMP_TIME 5`; baud rates `BAUDRATE_CONSOLE 921600`, `BAUDRATE_BOOM
115200`, `BAUDRATE_EFIS 115200`, `BAUDRATE_WIFI 1000000` (comment: 921600 was glitchy on Serial4);
timeouts `EFIS_DATA_TIMEOUT`/`BOOM_DATA_TIMEOUT` 1000 ms, `I2C_COMMUNICATION_TIMEOUT` 2000 µs;
timer intervals `SENSOR_INTERVAL 20000` µs (50 Hz), `IMU_INTERVAL` 4200 µs (LSM9DS1) / 4808 µs
(ISM330DHCX); SD `SD_WRITE_BLOCK_SIZE 512`, `LOG_RINGBUFFER_SIZE 32768`; `GLIMIT_REPEAT_TIMEOUT
3000` ms, `ASYMMETRIC_GYRO_LIMIT 15` °/s; `RANGESWEEP_LOW_AOA 4` / `HIGH_AOA 15` / `STEP 0.1`.

**Hardware pins** (lines 345–376, with a DB15 pinout table in the comments):
`TONE_PIN 30`, `VOLUME_PIN`/`TESTPOT_PIN A20` (DB15-10), `PIN_LED1 13` (onboard), `PIN_LED2 5`
(external AOA status LED), `FLAP_PIN A2` (DB15-7), `OAT_PIN 33` (OneWire bus when
`OAT_AVAILABLE`), `SWITCH_PIN 2` (panel button). Tone modes: `PULSE_TONE 1`, `SOLID_TONE 2`,
`TONE_OFF 3`.

DB15 connector map (from the hardware-test sketch header, Gen2 v3): 1 +14V · 2 EFIS RX
[Serial3] · 3 panel switch · 4 GPS RX [Serial2] · 5 LED+ · 6 audio R · 7 flaps analog ·
8 audio L · 9 OAT / display TTL out [Serial5] · 10 volume analog · 11 sensor 3.3V ·
12 EFIS TX [Serial3] · 13 boom TTL RX [Serial1] · 14 GND · 15 audio GND.

**IMU register blocks** (lines 379–425, gated by IMU type): LSM9DS1 at i2c `0x6B` (accel/gyro),
`aRes 8g/32768`, `gRes 245dps/32768`, 238 Hz sample rate; ISM330DHCX at `0x6A`, `gRes
250dps/32768`, 208 Hz. Exactly one of `IMUTYPE_LSM9DS1` / `IMUTYPE_ISM330DHCX` must be defined in
`local_config.h`.

**Library includes** (lines 443–483 and 774):
- `<i2c_t3.h>` — Teensy 3.x multi-port I2C. **Ships with Teensyduino**, not this repo. The
  bundled `libraries/Wire/Wire.h` is a one-line shim (`#include <i2c_t3.h>`) so any transitive
  `Wire.h` include redirects to i2c_t3 instead of instantiating the stock Wire objects
  (prevents `i2c0_isr`/`Wire1`/`Wire2` multiple-definition link errors).
- `"SdFat.h"` — bundled SdFat 2.1.0; instantiates `SdFat Sd`, `SdCardFactory`, 512-byte
  `sectorBuffer` (used for full-card format).
- `<Audio.h>` — Teensy Audio library. **The include comment requires a locally modified
  `Audio.h` with `play_sd_raw.h` and `play_sd_wav.h` commented out** so the Audio library does
  not fight SdFat for the SDIO peripheral. This modification lives in the installed Teensy core,
  not the repo — see BUILD-PLAN §Core mods.
- `<OneButton.h>`, `<RunningMedian.h>`, `"RunningAverage.h"`, `<SavLayFilter.h>`
  (Savitzky-Golay), `"MadgwickFusion.h"`, `"KalmanFilter.h"`, seven `AudioSample*.h` clips,
  `"default_config.h"`, and (under `OAT_AVAILABLE`) `<OneWire.h>` + `<DallasTemperature.h>`.
- `<Onspeed-settingsFunctions.h>` (line 774) — shared config serializer/parser (§8.3).

**Audio graph.** `AudioSynthWaveformSine → AudioEffectEnvelope → AudioMixer4` (ch0 = tones,
ch2 = voice clips via `AudioPlayMemory`) `→ AudioAmplifier L/R → AudioOutputAnalogStereo` — the
**Teensy 3.6 dual on-chip 12-bit DACs**. This is why the firmware cannot run on a Teensy 4.1
without adding an external DAC.

**Smoothing windows** (lines 210–222, tuned for the ISM330): `aoaSmoothing 20`,
`pressureSmoothing 15`, `accSmoothing 192`, `gyroSmoothing 30`, `iasSmoothing 66`,
`tasSmoothing 10`, `ahrsSmoothing 50`, `serialDisplaySmoothing 10`, `gyroScaleCorrection 1.14`.

**`setup()`** (lines 778–919), in order: optional 13-bit ADC (`HIGHRES_ANALOGREAD`); switch
interrupt; `initI2C()`; `initAccelGyro()`; 2.5 s IMU warm-up; `AudioMemory(16)`;
`Sd.begin(SdioConfig(FIFO_SDIO))`; `LoadConfig()`; `configureAxes()`; `createLogFile()`; initial
baro altitude and pitch/roll seed; OAT init; button callbacks (single click = audio on/off, 1 s
long-press = data mark); serial port bring-up (console, Serial4 wifi, conditional boom/EFIS/
display ports); Madgwick + Kalman init; start `ToneTimer` and `IMUTimer`;
`setDataSourceMode()`; `enableWatchdog()`.

**`loop()`** (lines 924–1017) — cooperative scheduler: continuous `readWifiSerial()`,
`readUSBSerial()`, `readEfisSerial()`, `readBoomSerial()`, `writeSerialData()`; time-gated
`checkSerial()` (1 s), `SensorWriteSD()`, `Check3DAudio()` (100 ms), `checkVolume()` (200 ms),
`checkGlimit()` (100 ms), `checkVnoChime()` (100 ms), decel-rate compute (100 ms),
`SendWifiData()` (~100 ms), `heartBeat()` (300 ms), OAT read (1 s), `switchCheck()`,
`checkWatchdog()`.

### 4.2 Sensor path

**`SensorRead.ino`.** `GetPressurePfwd()` reads differential sensor P2 on `Wire` (pins 18/19,
i2c addr 40, 14-bit); `GetPressureP45()` reads P1 on `Wire2` (pins 3/4, addr 40);
`GetStaticPressure()` reads the Honeywell **SSCSRNN1.6BA7A3** baro on `Wire1` (addr 120),
transfer function `(raw-1638.4)/8192*1000` → millibars. `SensorRead()` is the 50 Hz
`SensorTimer` handler: subtracts biases, gets flaps index, `setAOApoints()`, median + moving-
average smoothing, `calcAOA()`, physics-based IAS `sqrt(2·Pfwd_Pa/1.225)·1.94384` kt with
optional CAS curve correction, sets `newSensorDataAvailable`, logs (at pressure rate), and calls
`updateTones()`.

**`i2c.ino`.** `initI2C()` brings up three separate hardware I2C buses via i2c_t3: `Wire`
(18/19 @ 3 MHz, Pfwd), `Wire2` (3/4 @ 3 MHz, P45), `Wire1` (37/38 @ 400 kHz, IMU + baro), all
`I2C_OP_MODE_ISR`, 2 ms timeout. Read/write helpers operate on `Wire1` (IMU bus); the LSM9DS1
multi-read ORs the register with `0x80` for auto-increment; timeouts print "IMU i2c timeout"
(the boot symptom of a wrong IMU define) and kick the watchdog.

**`IMUread.ino`.** `initAccelGyro()`/`readAccelGyro()` are IMU-type-gated. **The LSM9DS1 path
applies a per-axis 2nd-order temperature-compensation polynomial** (e.g.
`ax = scaleAccel(axRaw) - 2.996e-07*imuTemp² - 2.899e-05*imuTemp - 0.01741`), with the code
comment stating it was "derived from heating **the box** from -25C to 50C and then cooling it
again to 27C" — singular, one physical test unit. **These coefficients are a per-unit
characterization of the maintainer's own bench sample, not a datasheet constant** — MEMS
accel/gyro temperature coefficients vary chip-to-chip within a part's tolerance band, so there is
no guarantee this specific polynomial fits a different physical LSM9DS1 unit; it could equally
undercorrect, overcorrect, or be roughly right, and there's no way to know without characterizing
that specific unit the same way. **The ISM330DHCX path applies no compensation code at all**
(`ax=scaleAccel(axRaw); ...` — raw scaled values only) — consistent with it being a higher
industrial-grade part with factory/internal temperature compensation, so it isn't relying on an
external per-unit polynomial the way the LSM9DS1 path does. `configureAxes()` maps
`PORTS_ORIENTATION` × `BOX_TOP_ORIENTATION` to signed axis labels via a 24-row `axisMapArray` —
**a separate table per IMU type** (ISM330 has X/Y swapped). `ReadIMU()` is the `IMUTimer` ISR:
read accel/gyro, map to aircraft frame, read static pressure at IMU rate (for Kalman VSI),
optional IMU-rate logging, then `processAHRS()`.

**`AHRS.ino`.** `processAHRS()` (rewritten by upstream commit `3c0ca39`, adopted 2026-07-16 —
see §4.7 and BUILD-PLAN §Phase 6 for what changed and why): TAS computed directly (density-
altitude formula with OAT, else 2 %/1000 ft) rather than via a smoothed IAS-diff; TAS-diff
smoothing now **exponential-decay** (`TASdiffSmoothed = iasSmoothing*TASdiff +
(1-iasSmoothing)*TASdiffSmoothed`, `iasSmoothing` now a small decimal alpha, not a moving-average
window size); installation-error rotation (pitchBias/rollBias) unchanged; centripetal-
acceleration compensation from TAS × body rates; accelerometer smoothing also now exponential-
decay (`aFwdSmoothed = accSmoothing*aFwdCorr + (1-accSmoothing)*aFwdSmoothed`, same pattern for
lat/vert) rather than the prior fixed-window `RunningAverage`; Madgwick `updateIMU()`. **Pitch/
roll are now unsmoothed**: `smoothedPitch = -filter.getPitch(); smoothedRoll = -filter.getRoll();`
— the prior adaptive gyro-rate-scaled complementary smoothing step is gone (variable names
persist from the old implementation; the smoothing itself doesn't). Earth-frame vertical G →
Kalman update → `kalmanAlt`, `kalmanVSI` (VSI forced 0 below 25 kt); `flightPath =
asin(kalmanVSI/TAS)`; `derivedAOA = smoothedPitch − flightPath` is computed but **is dead code —
verified 2026-07-16 that nothing else in the sketch reads `derivedAOA`** (not logged, not sent
over WiFi/serial, not used by tones); this has been true both before and after the `.7g` merge.

**`KalmanFilter.cpp/.h`** — altimeter/vario Kalman filter (har-in-air, Jun 2020): state =
altitude, velocity, accel bias; `Configure(7.243, 15.8993, 5.6296e-09, z0, 0, 0)`; dynamic
accel variance `|a|/50`. Unchanged by the `.7g` merge.

**`MadgwickFusion.cpp/.h`** — Madgwick AHRS tuned by OnSpeed: `betaDef` is **0.011617** as of
`.7g` (was `0.0085352` on `3.3.7a`; upstream raised the fusion filter's own correction gain in
the same rewrite that removed the downstream pitch/roll smoothing step above — a higher beta
makes the Madgwick filter itself respond faster to accelerometer correction). Seeded from
initial pitch/roll, 6-DoF `updateIMU()` path used.

### 4.3 AoA computation

**`AOAcalc.ino`.** `calcAOA(Pfwd, P45)`: `coeffP = PCOEFF(...)`; evaluates the per-flap
calibration curve with exponential smoothing (`alpha = 1/aoaSmoothing`); clamps to
[−20, +40]°; NaN-guards. `setAOApoints(flapIndex)` copies per-flap setpoints into the live
`LDmaxAOA / onSpeedAOAfast / onSpeedAOAslow / stallWarningAOA / stallAOA / maneuveringAOA`
globals (note: `stallAOA` is populated from `flapSTALLWARNAOA` here).

**`CurveCalc.ino`.** `curveCalc(x, curve)` evaluates by `curveType`: 1 = polynomial
(`MAX_CURVE_COEFF` terms), 2 = logarithmic `a·log(x)+b`, 3 = exponential `a·e^(bx)`.

**`Flaps.ino`.** `getFlapsIndex()` maps the flap-pot `analogRead` into a flap band using
midpoints between the configured `FLAPPOTPOSITIONS`, supporting ascending or descending wiring.

### 4.4 Audio subsystem

**`Tones.ino`.** `updateTones()` implements the AoA→audio bands: mute below
`MUTE_AUDIO_UNDER_IAS`; `AOA ≥ stallWarningAOA` → 20 pps high tone (1600 Hz);
`> onSpeedAOAslow` → pulsed high tone (rate mapped between bands); `≥ onSpeedAOAfast` →
**solid low tone (onspeed)**; `≥ LDmaxAOA` → pulsed low tone (400 Hz); else silence.
`tonePlayHandler()` is the `ToneTimer` ISR; `setFrequencytone()` drives the sine source and
envelope (pulse vs solid vs stall envelope timing) and obeys the panel switch.

**`3DAudio.ino`.** `Check3DAudio()` pans L/R gain with lateral G through `AUDIO_3D_CURVE`
(0.08 g ≈ one ball width) for slip/skid cueing.

**`VnoChime.ino`** — plays `AudioSampleVnochime` every `VNO_CHIME_INTERVAL` s while IAS > Vno.
**`Volume.ino`** — volume pot → `mixer1` gains (voice channel 10× tone gain).
**`gLimit.ino`** — over-G voice warning against configured load limits, reduced to 66.6 % when
roll/yaw rate exceeds 15 °/s (asymmetric loading); 3 s repeat gate.
**`AudioSample*.cpp/.h`** — seven wav2sketch PCM clips in PROGMEM: Enabled, Disabled, Datamark,
Glimit, Vnochime, Onspeed left/right speaker (channel test).

### 4.5 Data and serial modules

**`LogData.ino`.** `logData()` builds a wide CSV line (timestamp, raw pressures, Palt, IAS,
AOA, flaps, dataMark, optional OAT/TAS, IMU temp, aircraft-frame accel/gyro, pitch/roll,
optional boom columns, optional EFIS columns — VN-300 block for `efisID==1` else generic —
earthVertG, flightpath, VSI, altitude) into the 32 KB ring buffer. `createLogFile()` creates
the next `log_N.csv` with matching header (SENSORS mode only).

**`SD_readwrite.ino`.** `SensorWriteSD()` drains the ring buffer to SD in 512-byte `O_APPEND`
blocks, kicking the watchdog between writes; lines are dropped (not blocked on) if the ring
would overflow.

**`DataSource.ino`.** `setDataSourceMode()` selects the operating mode from the `DATASOURCE`
config string: `SENSORS` (50 Hz SensorTimer), `TESTPOT` (flap pot simulates AoA),
`RANGESWEEP` (AoA sweep audio demo), `REPLAYLOGFILE` (replay a logged CSV through the full
audio pipeline via `LogReplay.ino`; disables logging and the IMU timer).

**`EfisSerial.ino`.** `readEfisSerial()` on `Serial3`, dispatched by `efisID`: VN-300/VN-100
binary (127-byte packets, sync `0xFA 0x19`, CRC), MGL binary, AFS/Dynon SkyView ASCII
fixed-column, Dynon D10, Garmin G5, Garmin G3X. Populates the `efis*` globals used by logging,
wifi live-data, and the display stream.

**`BoomSerial.ino`.** `readBoomSerial()` on `Serial1`: `$`-prefixed lines, data at offset 21,
additive checksum (skippable via `NOBOOMCHECKSUM`), four fields → `boomStatic / boomDynamic /
boomAlpha / boomBeta` (raw counts).

**`DisplaySerial.ino`.** `writeSerialData()` every 100 ms emits either the **G3X** format or
the native **ONSPEED** 80-byte `#1` frame (pitch, roll, IAS, Palt, turn rate, lateral/vertical
G, percent lift, AOA, iVSI, OAT, flightpath, flaps, the four AoA setpoints, G-onset rate, spin
cue, dataMark, 2-hex-digit additive checksum). Routes to `Serial5`, `Serial3`, or `Serial1`
per `SERIALOUTPORT` (auto-begins at 115200). Percent-lift is a piecewise map of AoA across the
setpoint bands (LDmax → 50 %, onspeed 55–66 %, stall warn → 90–100 %).

**`WifiSerial.ino`.** `readWifiSerial()` on `Serial4` (1 Mbaud): `$…!`-framed commands in a
3072-byte buffer. Command set: `$LIST`, `$DELETE`, `$PRINT`, `$STARTLIVEDATA`,
`$STOPLIVEDATA`, `$FORMAT`, `$NOLOAD`, `$REBOOT`, `$SENDCONFIGSTRING`, `$SAVECONFIGSTRING`,
`$SENDDEFAULTCONFIGSTRING`, `$SENSORCONFIG`, `$PFWDBIAS`, `$P45BIAS`, `$PITCHROLLALT`,
`$VOLUME`, `$FLAPS`, `$AOA`, `$AUDIOTEST`, `$VNOCHIMETEST`, `$VERSION`, `$WIFIREFLASH`,
`$CHECKSUM`, `$CONFIG`. File transfer streams 1459-byte payload packets + 1 checksum byte with
per-packet ACK from the ESP32. SD-heavy operations are bracketed by `timersOff()/timersOn()`.

**`USBserial.ino`.** Console parser on USB `Serial` @ 921600: `LIST!`, `DELETE!`, `PRINT!`,
`STOP!`, `START!`, `SENSORS!`, `FORMAT!`, `NOLOAD!`, `WIFIREFLASH!`, `REBOOT!`, `FLAPS!`,
`VOLUME!`, `CONFIG!`, `AUDIOTEST!`, `HELP!`. `displayConsoleHelp()` prints the menu at boot —
seeing this help text on the serial monitor is the standard "firmware is alive" check.

**`SendWifiData.ino`.** 10 Hz `$ONSPEED,…,CRC` telemetry line to the ESP32 (AOA, percent lift,
IAS, Palt, vertical/lateral G, setpoints, flaps, coeffP, dataMark, kalman VSI, flightpath,
pitch rate, decel rate, calSourceID). When `CALWIZ_SOURCE = EFIS`, substitutes EFIS-derived
pitch/roll/IAS/VSI (or VN-300-specific fields for `efisID==1`) into the outbound `wifiPitch` /
`wifiRoll` / etc. fields **sent to the calibration-wizard web page only** — see §4.7, this does
not change what drives tones or any runtime safety logic. `AOA = -100` is the invalid/muted
sentinel.

**`CheckSerial.ino`.** 1 Hz stall detector: restarts `Serial3`/`Serial1` if EFIS/boom data
stops for > 1 s.

### 4.6 Misc modules

- **`HeartBeat.ino`** — blinks the external LED when audio is enabled (the "healthy boot =
  blinking LED" indicator).
- **`Switch.ino`** — OneButton service: single click toggles audio (Enabled/Disabled voice),
  1 s long-press increments `dataMark` and plays the Datamark clip.
- **`Timers.ino`** — `timersOff()/timersOn()` stop/restart IMU/Sensor/Tone timers around
  SD-heavy Wi-Fi operations.
- **`Watchdog.ino`** — programs the Kinetis hardware WDOG directly (5 s timeout);
  `checkWatchdog()` is called pervasively inside long loops. Board-specific register code.
- **`HelperFunctions.ino`** — `mapfloat`, `_softRestart()` (`SCB_AIRCR` write), `freeMemory()`,
  `array2float/array2double`, `configChecksum()` (16-bit additive CRC used by the config
  protocol).

### 4.7 Data source relationships: pressure probe vs. IMU vs. EFIS

Verified by direct code reading 2026-07-16 (not inference from comments/changelog) — three
separate, **never-merged** data paths:

1. **AoA / tones — pressure probe only.** `calcAOA()` (`AOAcalc.ino`) takes only `Pfwd`/`P45`
   (the differential-pressure probe) and produces `AOA`. `IAS` is likewise purely physics-derived
   from `PfwdPascal` (`SensorRead.ino`). `updateTones()` (`Tones.ino`) reads only `AOA`, `IAS`,
   and the flap-indexed setpoints. **Neither function references any IMU or EFIS variable
   anywhere** — the core stall-warning/aural-tone function has zero dependency on IMU quality or
   EFIS presence.
2. **IMU (accel/gyro/Madgwick) — always active, feeds two audio features + display/log.** The
   `IMUTimer` starts unconditionally in `setup()`, independent of EFIS presence or
   `CALWIZ_SOURCE`. Of everything `processAHRS()` produces, `smoothedPitch`/`smoothedRoll`/
   `flightPath`/`kalmanVSI`/`earthVertG` are **display/log/Wi-Fi-telemetry only** (consumed by
   `DisplaySerial.ino`, `SendWifiData.ino`, `LogData.ino` — never by tones). But `aVertCorr`/
   `aLatCorr` (vertical/lateral G) and `gRoll`/`gYaw` (smoothed gyro rates) **are** functionally
   active: `gLimit.ino`'s over-G audio warning reads `aVertCorr` directly, with its threshold
   modulated by `gRoll`/`gYaw`; `3DAudio.ino`'s stereo slip/skid panning reads `aLatCorr`
   directly. These four are the only IMU-derived values with any runtime audio effect.
3. **EFIS (`EfisSerial.ino`, including Garmin G3X, `efisID==5`) — calibration-wizard reference +
   logging only.** Every consumer of `efisPitch`/`efisRoll`/`efisLateralG`/`efisVerticalG`/etc.
   is either the parser itself, a debug `Serial.printf`, or `LogData.ino`'s SD-log line — grepped
   exhaustively, no exceptions. The only operational use is `SendWifiData.ino`'s
   `calSource=="EFIS"` branch (§4.5), which substitutes EFIS values into the packet sent to the
   **calibration-wizard web page** as the "measured pitch" ground-truth reference during
   calibration — a setup-time convenience, not a runtime data path.

**Consequence for IMU-quality-sensitive decisions:** there is no code path — under any
`CALWIZ_SOURCE`/`efisID` setting — that substitutes EFIS-sourced `efisLateralG`/`efisVerticalG`
for the IMU's `aVertCorr`/`aLatCorr` in `gLimit.ino` or `3DAudio.ino`. An EFIS connection (G3X or
otherwise) provides **no runtime redundancy** for the over-G warning or 3D-audio panning if the
onboard IMU is degraded — bridging that would require a firmware change, not a configuration
change. It also provides no benefit to AoA/stall-tone accuracy, since that path never touches
the IMU at all. See §4.2 for the IMU temperature-compensation asymmetry between the two
supported IMU types (LSM9DS1 relies on a per-unit-characterized external polynomial; ISM330DHCX
has no such code, consistent with factory/internal compensation).

---

## 5. Wi-Fi firmware — `Software/OnSpeedWifi/`

Single 2200-line sketch `OnSpeedWifi.ino` plus embedded web assets as C headers. Wi-Fi gateway,
config manager, calibration wizard, and log server for the Teensy.

**Build assumptions** (header comments, lines 1–4): Arduino 1.8.16, **ESP32 core 1.0.6, board
"ESP32 Pico Kit"** — explicitly warns that core 2.0 makes HTTP log downloads "painfully slow".
Includes the macOS python fix: `sed -i -e 's/=python /=python3 /g'
~/Library/Arduino15/packages/esp32/hardware/esp32/*/platform.txt`.

**Radio behavior.** `WIFI_AP_STA` mode. AP SSID **`OnSpeed`** / password **`angleofattack`**,
AP IP **192.168.0.1/24**, mDNS **`onspeed.local`** (plus a captive DNS server answering that
name). TX power deliberately lowered to 2 dBm to limit RF interference. Optional station-mode
join to hangar Wi-Fi via `/wifi` (config keys exist but marked "currently not needed").

**Teensy link.** UART0 @ **1,000,000 baud**, `Serial.setRxBufferSize(8192)`. Sends `$CMD!`
commands (with ~500 µs inter-character throttle), receives `<TAG>…</TAG>` responses. Config
blobs carry a 16-bit additive `<CHECKSUM>`. Live data: forwards any `$ONSPEED,…` line verbatim
to WebSocket clients.

**Servers.** HTTP on port 80; **WebSocket on port 81** (live view; first client connect sends
`$STARTLIVEDATA!`, last disconnect sends `$STOPLIVEDATA!`).

**HTTP routes:**

| Route | Function |
|---|---|
| `/` | Landing page |
| `/logs` | `$LIST!` → SD log index with download links |
| *(any other path)* | Treated as a log filename → streams the file via `$PRINT` with per-packet checksum/ACK flow control (1459-byte packets), served as CSV attachment |
| `/live` | LiveView page (WebSocket AoA/slip display; debug only) |
| `/aoaconfig` / `/aoaconfigsave` / `/aoaconfigupload` | Full system-configuration form; save via `$SAVECONFIGSTRING…!`; upload a `.cfg` |
| `/calwiz` | AoA calibration wizard (steps: intro → decel → flydecel → save) — in-browser Savitzky-Golay smoothing (`sg_filter.h`), polynomial regression (`javascript_regression.h`), Chartist.js plots |
| `/sensorconfig` | Accel/gyro/baro calibration (`$PITCHROLLALT!` + `$SENSORCONFIG…!`) |
| `/defaultconfig` | Load factory defaults |
| `/format` | Format the Teensy SD card (`$FORMAT!`) |
| `/reboot` | Reboot Teensy + ESP32 |
| `/upgrade` + `/upload` | **ESP32 self-OTA**: upload the compiled `.bin`, flashed via the `Update` library, then reboot |
| `/wifireflash` | `$WIFIREFLASH!` — Teensy releases the shared serial line so the ESP32 can be reflashed |
| `/wifi` | Scan/join station networks |
| `/getvalue?name=X` | AJAX single live-value fetch (`$X!`) |

**`local_config.h` keys** (from `local_config-sample.h`): AP `ssid`/`password`, station
`clientwifi_ssid`/`clientwifi_password`, `MAX_AOA_CURVES 5`, `MAX_CURVE_COEFF 4`,
`dataSource`, and calibration-wizard aircraft data (`acGrossWeight`, `acCurrentWeight`,
`acVldmax`, `acGlimit`). **`MAX_AOA_CURVES`/`MAX_CURVE_COEFF` must match the Teensy's
`local_config.h`** — the two firmwares share the config schema.

---

## 6. M5 display firmware — `Software/OnSpeed_M5_display/` *(not used in David's aircraft)*

M5Stack Core (ESP32) cockpit display; **requires ESP32 core 2.0** (won't boot on 1.x) — the
opposite of the Wifi sketch's pin. David runs the huVVer-AVI, whose firmware is maintained
outside this repo, so this sketch is reference-only here.

Highlights: reads the 80-byte `#1` ONSPEED frame on `Serial2` @ 115200 (auto-detects three
wiring variants — TTL 16/17, inverted RS-232 16/17, TTL 22/17 — remembered in NVS); five
display pages cycled by the center button (AoA indexer, attitude, narrow AoA, decel gauge,
1-minute G history); own OTA AP (`OnSpeedDisplay` / `angleofattack`, IP **192.168.0.2**,
`display.local/upgrade`); libraries `GaugeWidgets`, `M5Stack-master` 0.3.6, `SavLayFilter`.
The full frame field map (character offsets and scales) is documented in `SerialRead.ino`.

---

## 7. Hardware test — `Software/OnSpeed_hardware_test/`

Bench bring-up sketch for a newly built Gen2 v3 board (declared toolchain: Arduino 1.8.7 /
Teensyduino 1.47 — historical). Every 5 s runs: panel-switch read, all three pressure sensors
(Pfwd on `Wire`, P45 on `Wire2`, baro on `Wire1`), LSM9DS1 read, UART loopback tests at 115200
(Serial3→Serial2, Serial3→Serial3, Serial5→Serial1), volume/flap pot raw reads, L/R audio voice
clips through the DACs, and `SD.begin(BUILTIN_SDCARD)` — printing PASS/FAIL to USB serial.
Its README documents the fake-`Wire.h` requirement. Uses the same i2c_t3 + Audio stack as the
main firmware, so it needs the same core mods to build.

---

## 8. Configuration system

### 8.1 Three layers

1. **Compile-time feature defines** — `local_config.h` per sketch (§8.2). Gitignored;
   template committed as `local_config-sample.h`.
2. **Compiled-in default runtime config** — `default_config.h` (a PROGMEM `<CONFIG>…</CONFIG>`
   string; the committed default describes an RV-4).
3. **Runtime config on SD** — `onspeed.cfg`, loaded at boot if present, else the compiled
   default. Edited via the Wi-Fi UI (`/aoaconfig`), which round-trips the blob over the
   `$SENDCONFIGSTRING!`/`$SAVECONFIGSTRING!` protocol with a 16-bit additive checksum.

### 8.2 `local_config.h` (Teensy) — compile-time keys

`LOGDATA_PRESSURE_RATE` vs `LOGDATA_IMU_RATE`; `SPHERICAL_PROBE`; **`IMUTYPE_LSM9DS1` vs
`IMUTYPE_ISM330DHCX`** (must match the installed IMU — wrong choice = solid green LED / "IMU
i2c timeout" at boot); `NOBOOMCHECKSUM`; **`MAX_AOA_CURVES`** and **`MAX_CURVE_COEFF`**
(dimension the curve/setpoint arrays — must match the Wifi sketch); `OAT_AVAILABLE`;
`HIGHRES_ANALOGREAD` (13-bit ADC); `dataSource` + `replayLogFileName`.

Status in this checkout (verified 2026-07-16): both `local_config.h` files **match the
"CURRENT" flown backups** in
`~/Library/CloudStorage/OneDrive-Personal/Documents/OnSpeed-Gen2-DevSetup/config-backup/`
(AHRS variant uses `MAX_AOA_CURVES 5`, `HIGHRES_ANALOGREAD` off; the alternate backup variant
`local_config.VARIANT-highres-4curves.h` differs by exactly those two keys).

### 8.3 Runtime config schema (`Onspeed-settingsFunctions.h` v3.2.20, shared library)

Header-only serializer/parser shared by the Teensy and Wifi sketches. Key list:
`AOA_SMOOTHING`, `PRESSURE_SMOOTHING`, `MUTE_AUDIO_UNDER_IAS`, `DATASOURCE`,
`REPLAYLOGFILENAME`, `FLAPDEGREES`, `FLAPPOTPOSITIONS`, `VOLUMECONTROL`,
`VOLUME_HIGH_ANALOG`, `VOLUME_LOW_ANALOG`, `VOLUME_DEFAULT`, `3DAUDIO`, `OVERGWARNING`,
`SETPOINT_LDMAXAOA`, `SETPOINT_ONSPEEDFASTAOA`, `SETPOINT_ONSPEEDSLOWAOA`,
`SETPOINT_STALLWARNAOA`, `SETPOINT_STALLAOA`, `SETPOINT_MANAOA`, `AOA_CURVE_FLAPS0..N`,
`CAS_CURVE`, `CAS_ENABLED`, `PORTS_ORIENTATION`, `BOX_TOP_ORIENTATION`, `EFISTYPE`,
`CALWIZ_SOURCE`, `PFWD_BIAS`, `P45_BIAS`, `PSTATIC_BIAS`, `GX_BIAS`, `GY_BIAS`, `GZ_BIAS`,
`PITCH_BIAS`, `ROLL_BIAS`, `BOOM`, `SERIALEFISDATA`, `LOADLIMITPOSITIVE`,
`LOADLIMITNEGATIVE`, `VNO`, `VNO_CHIME_INTERVAL`, `VNO_CHIME_ENABLED`, `SERIALOUTFORMAT`
(`G3X`|`ONSPEED`), `SERIALOUTPORT` (`NONE`|`Serial1`|`Serial3`|`Serial5`), `SDLOGGING`.

Curve types: 1 = polynomial, 2 = logarithmic, 3 = exponential.
`EFISTYPE → efisID`: `VN-300`→1, `ADVANCED`→2, `DYNOND10`→3, `GARMING5`→4, `GARMING3X`→5,
`MGL`→6.

---

## 9. Serial protocol summary

| Link | Port (Teensy side) | Baud | Framing |
|---|---|---|---|
| USB console | `Serial` | 921600 | `CMD!` commands; help menu at boot |
| Wi-Fi module | `Serial4` | 1,000,000 | `$CMD!` / `<TAG>…</TAG>`; `$ONSPEED,…,CRC` live stream at 10 Hz; file transfer in 1459+1-byte checksummed packets |
| EFIS in | `Serial3` | 115,200 | Per-EFIS binary/ASCII (VN-300, MGL, SkyView, D10, G5, G3X) |
| Boom in | `Serial1` | 115,200 | `$`-prefixed, additive checksum |
| Display out | `Serial5`/`Serial3`/`Serial1` | 115,200 | 80-byte fixed-width `#1…` ONSPEED frame or G3X format, 10 Hz |
| ESP32 → browser | WebSocket :81 | — | `$ONSPEED` lines verbatim |

---

## 10. Bundled libraries — `Software/Arduino/libraries/`

Point the Arduino sketchbook at `Software/Arduino/` so these resolve. **Do not "clean up" or
replace this folder.**

| Library | Version | Used by | Notes |
|---|---|---|---|
| `Wire` | — | Teensy sketches | **Deliberate one-line fake**: `Wire.h` → `#include <i2c_t3.h>`. Removing it causes `i2c0_isr`/`Wire1`/`Wire2` multiple-definition link errors |
| `SdFat-master` | 2.1.0 (vendored, but shadowed at build time by the Teensy platform's own SdFat 2.1.2 — see §11 point 10) | Teensy | Greiman SdFat; source of the benign `SdFat.h:438 #warning` when pulled into Teensy audio files |
| `Onspeed-settingsFunctions` | 3.2.20 | Teensy + Wifi | Shared config schema/serializer |
| `OneButton-master` | — | Teensy | Panel switch click/long-press |
| `RunningAverage`, `RunningMedian` | — | Teensy | Smoothing |
| `SavitzkyGolayFilter-master` | — | Teensy + M5 | `SavLayFilter` (IAS derivative / decel rate) |
| `OneWire`, `DallasTemperature` | — | Teensy (`OAT_AVAILABLE`) | DS18B20 OAT probe |
| `arduinoWebSockets-master` | 2.1.3 | Wifi | WebSocket server on :81 |
| `GaugeWidgets` | — | M5 display | Vector gauge drawing |
| `M5Stack-master` | 0.3.6 | M5 display | — |

**Not in this folder but required (come from the Teensy platform/Teensyduino):** `i2c_t3`,
`Audio` (needs local modification, §11), the Teensy core itself.

---

## 11. Build-relevant facts embedded in the code

These are the things the source *requires* of the toolchain — the build plan is built around
them:

1. **Declared pins:** Arduino 1.8.16 + Teensyduino 1.55 (`OnSpeedTeensy_AHRS.ino:1`); upstream
   commit `610ae09` (merged into `merge-upstream-full-2026-07`, 2026-07-16) updates this to
   "Arduino 2.0 ready, use Teensyduino 1.58 on Mac".
2. **Fake `Wire` shim must be on the library path** (bundled).
3. **Teensy `Audio.h` must be locally modified** — comment out `play_sd_raw.h` and
   `play_sd_wav.h` includes (SDIO conflict with SdFat) (`OnSpeedTeensy_AHRS.ino:458`).
4. **Teensy core serial RX buffers must be enlarged** — the official programming procedure
   (`Documents/OnSpeed Gen2 v3 software programming procedure.pdf`) says: in the Teensy core's
   `serial1.c` … `serial4.c`, change `SERIALn_RX_BUFFER_SIZE` from 64 to **8192**. Upstream's
   ventura commit repeats: "don't forget to modify RX buffers for serial 1-4 after upgrading
   Teensyduino." (The sketch itself contains no core overrides — it relies on this manual mod
   for EFIS/boom/wifi ingest.)
5. **Board settings (Teensy):** Board *Teensy 3.6*, CPU *180 MHz*, Optimize *Faster*, USB Type
   *Serial*, Keyboard *US English*. (The code header suggests "Fastest with LTO" for a 1.6×
   gain; David's known-good build banners used *Faster*.) Expected footprint ≈ 45 % flash /
   36 % RAM.
6. **Teensy 3.6-only features:** dual on-chip DACs (`AudioOutputAnalogStereo`), three i2c_t3
   hardware buses, Kinetis WDOG registers, `SdioConfig(FIFO_SDIO)`, six `IntervalTimer`s.
   **Do not target Teensy 4.1** (no DAC).
7. **ESP32 core 1.0.6 for OnSpeedWifi** (core 2.0 degrades HTTP transfer speed; code comment);
   ESP32 core 2.0 for the M5 display — mutually exclusive installs, switch via Boards Manager
   version selector when needed.
8. **`local_config.h` must exist in both sketch folders** (copy from
   `local_config-sample.h`) with the **IMU define matching the installed IMU** and
   `MAX_AOA_CURVES`/`MAX_CURVE_COEFF` consistent across Teensy and Wifi sketches. See §4.2 for
   why the two supported IMU types are not just "old vs. new" but differ in whether temperature
   compensation is a per-unit external polynomial (LSM9DS1) or built into the chip (ISM330DHCX).
9. **After flashing the Teensy, power-cycle the box** — flashing can leave the IMU i2c bus
   locked. Healthy boot = blinking LED; solid green = not running (almost always IMU define).
10. **Library resolution is per-header, not "sketchbook always wins."** Verified empirically on
    `teensy:avr` 1.62.0 (2026-07-16, `arduino-cli compile`): for `Wire.h`, the sketchbook's fake
    shim wins over the platform's real Wire — correct and required. For `SdFat.h`, the
    **platform's bundled SdFat 2.1.2 wins** over the sketchbook's vendored `SdFat-master` 2.1.0
    — the opposite outcome, despite both being "sketchbook vs. platform" collisions. The build
    compiled clean either way (2.1.2 is source-compatible with the 2.1.0-era code), but don't
    assume sketchbook libraries always shadow platform ones — check the compiler's "Multiple
    libraries were found for ..." notices in the build log for the header names that matter
    (`Wire.h`, `SdFat.h`, `OneWire.h`).
11. **Actual compiled footprint (teensy:avr 1.62.0, `opt=o2std`, 2026-07-16):**
    - HEAD `5e70410` (v3.3.7a, `master`): 501920/1048576 bytes flash (**47.9 %**),
      95616/262144 bytes RAM (**36.5 %**).
    - `merge-upstream-full-2026-07` (v3.3.7g, current): 505416/1048576 bytes flash (**48.2 %**),
      95968/262144 bytes RAM (**36.6 %**) — the larger AHRS/`WifiSerial.ino` rewrite adds ~3.5 KB
      flash / ~350 B RAM over `.7a`.

    Both are a few points above the historical ≈45 % flash figure from pre-Mac-toolchain builds,
    plausibly the newer platform-bundled SdFat 2.1.2 / newer GCC codegen; RAM matches the
    historical ≈36 % target closely in both cases. Not a concern at either margin.

---

## 12. External references

- OnDrive dev-setup archive:
  `~/Library/CloudStorage/OneDrive-Personal/Documents/OnSpeed-Gen2-DevSetup/` — contains
  `OnSpeed-email-context.md` (project correspondence), `config-backup/` (flown local_config
  backups + variants, wifi `upload_cmd.txt` with exact esptool parameters,
  `source-bak-2023-03-31/`), and the original `BUILD-mac.md`/`CLAUDE.md`/bootstrap script.
  **Reference only — never develop inside OneDrive** (CRLF churn, `.git/index.lock` errors).
- `Documents/OnSpeed Gen2 v3 software programming procedure.pdf` — official (older) programming
  procedure; still the authority for the RX-buffer mod and the ESP32 first-flash flow.
- Upstream project: https://github.com/flyonspeed/OnSpeed-Gen2. As of 2026-07-16, all 12
  upstream-only commits (including `3c0ca39`'s AHRS smoothing rewrite) were merged onto branch
  `merge-upstream-full-2026-07` (not yet merged to `master`) — VERSION is now `3.3.7g`. An
  earlier, narrower cherry-pick attempt (branch `merge-upstream-2026-07`, excluding the AHRS
  rewrite) was superseded once it was confirmed the box's prior `3.3.7a` firmware had never
  actually been calibrated/flight-tested, removing the reason to protect it. The `local_config.h`
  externalization mechanism was preserved throughout — it remains the sole owner of
  installation-specific compile-time keys regardless of which upstream AHRS version sits under
  it. See BUILD-PLAN §Phase 6 for the full record, including a required follow-up fix (upstream's
  refactored `WifiSerial.ino` expects `configString`/`checksumString`/`configContent` as globals).
- huVVer-AVI display firmware: maintained by Vern @ huvver.tech, outside this repo.
- Project site: https://flyonspeed.org
