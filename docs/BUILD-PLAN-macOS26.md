# Build Plan — OnSpeed Gen2 v3.3.7a on macOS 26 (Tahoe), Apple Silicon

*Written 2026-07-16. Plan only — nothing here has been executed. Companion reference:
[SOFTWARE.md](SOFTWARE.md), especially §11 "Build-relevant facts embedded in the code".*

**Goal:** produce a working build (and flash) of the current flyable firmware — Teensy
`OnSpeedTeensy_AHRS` v3.3.7a first, then ESP32 `OnSpeedWifi` v3.3.7 — on this machine, then
merge upstream and rebuild.

**Chosen approach (per David, 2026-07-16): Arduino IDE 2.x + Teensy boards-manager platform.**
Rationale and risks in §2. Fallback paths in §11.

---

## 1. Verified machine state (as found, 2026-07-16)

| Item | State |
|---|---|
| macOS | 26.5.2 (Tahoe), **Apple Silicon (arm64)**, Rosetta 2 installed |
| `/Applications/Arduino.app` | **1.8.7** — Intel-only binary, still Gatekeeper-quarantined, **no Teensyduino installed into it** (relic of the old procedure PDF era; not usable as-is) |
| `/Applications/Teensy.app` | Teensy Loader **1.56** (standalone) |
| `arduino-cli` | 0.33.1 at `~/bin/arduino-cli` |
| `~/Library/Arduino15` | esp32 core **2.0.11** installed; sketchbook currently points at `~/Documents/Arduino` (⚠️ possibly iCloud-synced — must be changed, §5) |
| Python | 3.13.3 (`python3`); no `esptool.py` on PATH; no PlatformIO |
| Repo | `master` @ `5e70410` (v3.3.7a), clean; fork **9 ahead / 12 behind** upstream |
| `local_config.h` (both sketches) | Present, **verified identical to the flown "CURRENT" backups** in the OneDrive DevSetup archive (the alternate variant differs only by `MAX_AOA_CURVES 4` + `HIGHRES_ANALOGREAD`) — the BUILD-mac.md ⚠️ on config variants is resolved |
| OneDrive DevSetup archive | Intact at `~/Library/CloudStorage/OneDrive-Personal/Documents/OnSpeed-Gen2-DevSetup/` (email context, config-backup, wifi `upload_cmd.txt`) |

## 2. Why Arduino IDE 2.x, and what it changes

The repo pins Arduino 1.8.16 + Teensyduino 1.55, but that path is effectively dead on this
machine: 1.8.x is Intel-only, the Td 1.55 Mac installer is the historically broken piece, and
macOS 26 tightens Gatekeeper further. Upstream itself moved on — commit `610ae09` ("ventura
update", Nov 2023) declares the code **"Mac Ventura & Arduino 2.0 ready"** and recommends
Teensyduino 1.58 for Mac.

Consequences of choosing IDE 2.x + the Teensy boards-manager platform:

- **Teensy support installs via Boards Manager** (URL `https://www.pjrc.com/teensy/package_teensy_index.json`),
  platform `teensy:avr` (1.59.x as of writing). Teensy 3.6 is still supported. Teensy Loader
  integration is built into the platform (no separate installer to fight).
- **The two manual core mods (RX buffers, Audio.h) move** to
  `~/Library/Arduino15/packages/teensy/hardware/avr/<version>/…` and are **wiped whenever the
  platform is updated** — treat §4 as a re-apply checklist and pin the platform version once it
  works.
- **Newer GCC than the 1.55 era.** Expect new compiler warnings, possibly stricter errors, on
  the un-merged 3.3.7a code. The known canary: upstream's ventura commit replaced a `sprintf`
  in `LogData.ino:61` with `snprintf` — if 3.3.7a HEAD fails on exactly that class of issue,
  apply that one-line fix (or fast-forward to the merge, §8).
- We deliberately **build HEAD (3.3.7a) unchanged first** to isolate toolchain problems from
  merge problems — same sequencing as BUILD-mac.md.

## 3. Phase 1 — Install the toolchain (Teensy)

1. Download **Arduino IDE 2.x** (latest 2.3.x) — Apple Silicon build — from arduino.cc.
   Install as `Arduino IDE.app` (does not collide with the old `Arduino.app`; leave the old
   one untouched for now, delete after success).
2. First launch: approve Gatekeeper prompt (it's signed/notarized — normal right-click → Open
   should suffice; no `xattr` surgery expected).
3. Settings → Additional boards manager URLs: add
   `https://www.pjrc.com/teensy/package_teensy_index.json`.
4. Boards Manager → install **"Teensy (for Arduino IDE 2.0.4 or later)"** — note the exact
   version installed; it appears at `~/Library/Arduino15/packages/teensy/hardware/avr/<ver>/`.
5. Verify the platform bundles what the sketch needs:
   - `…/avr/<ver>/libraries/i2c_t3/` exists (required; the fake Wire shim points at it).
   - `…/avr/<ver>/libraries/Audio/` exists.
   - `…/avr/<ver>/cores/teensy3/serial1.c … serial4.c` exist.
   If `i2c_t3` is *not* bundled in the platform version installed, copy it from the Teensyduino
   1.58 distribution or nox771/i2c_t3 into `Software/Arduino/libraries/` (decision point — stop
   and note it).

## 4. Phase 2 — Core modifications (re-apply after every platform update)

All paths relative to `~/Library/Arduino15/packages/teensy/hardware/avr/<ver>/`.

1. **Serial RX buffers** (required for EFIS/boom/wifi ingest; per the official programming
   procedure PDF and upstream's ventura note): in `cores/teensy3/serial1.c`, `serial2.c`,
   `serial3.c`, `serial4.c` change

   `#define SERIALn_RX_BUFFER_SIZE 64` → `#define SERIALn_RX_BUFFER_SIZE 8192`

   (n = 1..4; exact macro spelling per file). ~32 KB total RAM cost — acceptable, past builds
   ran ~36 % RAM including this.
2. **Audio library SDIO fix** (required per `OnSpeedTeensy_AHRS.ino:458`): in
   `libraries/Audio/Audio.h`, comment out the `#include "play_sd_raw.h"` and
   `#include "play_sd_wav.h"` lines so the Audio lib doesn't claim SDIO against SdFat.
   *(First attempt may try without this — newer cores changed the SD stack — but if the link
   drags in SD code or the SDIO conflict appears, apply it. The code comment says it's
   required; default to applying it.)*
3. Record `<ver>` + both mods in a note (or a small `docs/teensy-core-mods.md`) so they can be
   re-applied mechanically after any platform update. Do **not** auto-update the Teensy
   platform once the build works.

## 5. Phase 3 — Sketchbook / libraries wiring

1. **Done 2026-07-16** via `arduino-cli config set directories.user
   /Users/david/dev/OnSpeed-Gen2/Software/Arduino` (Arduino IDE 2 reads the same
   `~/Library/Arduino15/arduino-cli.yaml`, so Settings → Sketchbook location will show this
   path too). Do *not* leave it at `~/Documents/Arduino` (iCloud risk; wrong libraries).
2. **Verified**: `Software/Arduino/libraries/Wire/Wire.h` exists and contains only the
   `#include <i2c_t3.h>` redirect. If it's ever missing, restore from git.
3. **Resolved (verified by actual compile, not just assumption)**: the Teensy platform *does*
   bundle its own `SdFat`, and the build log showed "Multiple libraries were found for
   SdFat.h" — but **the platform's copy (2.1.2) won, not the sketchbook's (2.1.0)**, the
   opposite of what was assumed here before compiling. `Wire.h` resolved the other way
   (sketchbook shim wins), so the two headers don't follow the same priority rule — always
   check the compiler's "Multiple libraries" notices rather than assuming sketchbook-always-
   wins. The build was clean at 2.1.2; see SOFTWARE.md §11 point 10. The
   `SdFat.h:438 #warning __has_include(FS.h)` remains benign either way.

## 6. Phase 4 — Pre-flash configuration check

1. `local_config.h` in `Software/OnSpeedTeensy_AHRS/` — already verified byte-identical to the
   flown CURRENT backup. **Remaining human check: confirm the `IMUTYPE_*` define matches the
   IMU actually installed in the box** (`IMUTYPE_LSM9DS1` vs `IMUTYPE_ISM330DHCX`). Wrong
   choice = solid-green LED / "IMU i2c timeout" at boot, not a build error.
2. `Software/OnSpeedWifi/local_config.h` — verified identical to backup; `MAX_AOA_CURVES 5` /
   `MAX_CURVE_COEFF 4` match the Teensy side.
3. Runtime config (`onspeed.cfg` on the SD card) is untouched by any of this — flashing
   firmware does not clobber it.

## 7. Phase 5 — Compile, flash, verify (Teensy)

1. Open `Software/OnSpeedTeensy_AHRS/OnSpeedTeensy_AHRS.ino` in IDE 2.
2. Tools: Board **Teensy 3.6** · CPU **180 MHz** · Optimize **Faster** · USB Type **Serial** ·
   Keyboard **US English**. (Code header suggests "Fastest with LTO"; stick with *Faster* —
   it matches every known-good flown build. Revisit LTO later, separately, if ever.) **Verified
   2026-07-16**: these are the FQBN defaults on `teensy:avr` 1.62.0 (`usb=serial`, `speed=180`,
   `opt=o2std`, `keys=en-us`) — no explicit override needed, though the plan pins them anyway.
3. **Verify/Compile only** first. ✅ **Done 2026-07-16** via
   `arduino-cli compile --fqbn "teensy:avr:teensy36:usb=serial,speed=180,opt=o2std,keys=en-us"`
   against HEAD (`5e70410`) with both `local_config.h` files from Phase 4 (including the
   `MAX_AOA_CURVES` fix). **Result: clean build, zero warnings, zero errors.**
   Flash: 501920/1048576 bytes (**47.9 %**). RAM: 95616/262144 bytes (**36.5 %**) — RAM matches
   the historical ≈36 % target almost exactly; flash is a few points above the historical ≈45 %
   (plausibly the newer platform-bundled SdFat 2.1.2 vs. the code's 2.1.0-era comment, or newer
   GCC codegen — not a concern at this margin). See SOFTWARE.md §11 points 10–11 for the full
   library-resolution and footprint record. Triage guide for failures in §10 (not needed this
   run).
4. **Not yet done — needs the physical Teensy connected.** Upload with the Teensy connected via
   micro-USB: Teensy Loader window opens → **press the physical button on the Teensy** to
   complete the first upload.
5. **Power-cycle the box** (flashing can leave the IMU i2c bus locked).
6. Verify:
   - LED **blinking** (solid green = not running → IMU define, §6.1).
   - Serial Monitor @ **921600** shows the version banner + console help text; `CONFIG!`
     dumps the loaded config.
   - Wi-Fi UI (`onspeed.local` / 192.168.0.1 on the `OnSpeed` AP) shows `Teensy: 3.3.7a` in
     the header; `/logs` lists files; a new `log_N.csv` grows while powered.
7. The `.hex` from the verify-compile is archived outside the repo at
   `<scratchpad>/build-artifacts/OnSpeedTeensy_AHRS.ino.hex` (2026-07-16) — re-export after
   upload once the board is connected, since the upload step re-links nothing new but it's
   good practice to archive the exact bits that were flashed.

## 8. Phase 6 — Merge upstream ⚠️ **plan revised twice during execution, 2026-07-16 — final state below**

**This phase did not go as originally planned above (the plain `git merge upstream/master`
procedure was tried, then abandoned, then redone in full) — the section below is what actually
happened and is now the authoritative record.** Fork was 9 ahead / 12 behind
(`git log master..upstream/master`), 12 commits.

### Round 1: attempted plain merge, found a risk, aborted

`3c0ca39` ("v3.3.7 update", upstream, Aug 2023) substantially rewrites the AHRS smoothing
algorithm in `AHRS.ino` / `MadgwickFusion.cpp` / `OnSpeedTeensy_AHRS.ino`: it drops the
downstream adaptive pitch/roll complementary-smoothing step (`smoothedPitch = -filter.getPitch()`,
no exponential blend against the previous value), raises the Madgwick filter's own `betaDef` gain
(0.0085352 → 0.011617, i.e. the fusion filter itself corrects more aggressively), and replaces
the fixed-window moving-average accel/TAS smoothing (tuned window sizes 192/66, "optimized for
ISM330 IMU") with an exponential-decay scheme using different constants. **Because the code
structure changed enough, git's line-based diff did not flag any of this as a merge conflict** —
a plain merge silently took upstream's version with zero visible warning. Verified directly
against `upstream/master`'s actual file content (not a merge artifact) — this is genuinely
upstream's current code, not a diffing quirk.

Root-cause note on *why* the changelog comments (`3.3.7b` through `3.3.7g`, each individually
described as an incremental "optimization") don't read like a removal: git's commit granularity
is coarser than the in-source changelog's. `3c0ca39` is a single squashed commit (pushed Aug 26
2023) whose own message — "separated serial display smoothing to Lateral & Vertical G" — matches
verbatim the changelog text for `3.3.7f` (dated 7/22/23, over a month earlier). So `3.3.7b`
through `3.3.7f` were local/offline edits on the maintainer's machine that only reached GitHub
once, batched, in `3c0ca39`; the intermediate reasoning was never pushed and isn't recoverable
from git. Only `af9cc08` (`3.3.7g`, the IMU axis fix) maps 1:1 to a real individual commit.

Initial decision (now superseded, see Round 2 below): David confirmed via project email
correspondence that the fork's flown `3.3.7a` tuning came from an **offline patch directly from
the OnSpeed maintainers**, a separate lineage from GitHub's `3.3.7b`–`3.3.7g` progression despite
overlapping version numbers/wording — so Round 1 cherry-picked only 7 of the 12 commits
(`af9cc08`, `610ae09`, `07cf0da`, plus the 4 M5-display commits), explicitly skipping `3c0ca39`
to protect that flown tuning.

### Round 2: David clarified the actual constraint, plan reversed

David then clarified: the box's `3.3.7a` firmware, while physically installed, **was never
actually calibrated or flight-tested in the aircraft** — so "don't regress what's flown" wasn't
actually the right constraint. Directive: **roll forward to the full `.7g` upstream code**
(i.e. take `3c0ca39` too), while **keeping the `local_config.h` externalization mechanism**
intact. This fork's central customization (installation config split out of git-tracked files)
is unaffected by which AHRS algorithm version sits underneath it, so this is straightforward.

**What was actually done: redid as a full merge (not cherry-pick), on a fresh branch
`merge-upstream-full-2026-07` off `master`** (the earlier cherry-pick branch,
`merge-upstream-2026-07`, was left alone/abandoned rather than deleted). `git merge
upstream/master --no-edit` brought in all 12 commits in their correct topological order (a full
merge doesn't have the ordering problems sequential cherry-picking onto an already-partially-
picked branch would have caused). Two rounds of conflict resolution + a follow-up compile fix:

| Issue | Resolution |
|---|---|
| One real conflict, in `OnSpeedTeensy_AHRS.ino`: upstream re-adds `HIGHRES_ANALOGREAD` / `dataSource` / `replayLogFileName` / `configString` / `checksumString` / `configContent` directly in the main `.ino`. | The first three are already owned by `local_config.h` (externalized) — taking upstream's redeclaration would double-define them and fail to compile. **Dropped upstream's redeclaration of those three, kept `local_config.h` as the sole owner** — this is the one thing that must always be preserved on any future upstream sync. |
| `VERSION` line, part of the same conflict block. | This time — since the full `.7g` lineage **is** being adopted — took upstream's `VERSION "3.3.7g"` (previously, in Round 1, this was deliberately kept at `"3.3.7a"`; now correct to relabel since the code underneath actually matches). |
| Compile failure after merge commit: `configString`/`checksumString`/`configContent` "not declared in this scope" in `WifiSerial.ino`. | Upstream's `3c0ca39` refactored `WifiSerial.ino` to assign to these as **globals** (no more per-call-site local declarations) — but the conflict resolution above had dropped the global declarations too, assuming (correctly, in Round 1's narrower cherry-pick) that they were unused. In this full merge they're load-bearing. **Fixed in a follow-up commit**: re-added `String configString="";`, `String checksumString="";`, `String configContent="";` as globals in `OnSpeedTeensy_AHRS.ino`. |

`default_config.h` also picked up upstream's own factory-default calibration values (flap-pot
positions, AOA setpoints/curves, sensor biases, two new schema keys `SETPOINT_STALLAOA` /
`SETPOINT_MANAOA` / `CALWIZ_SOURCE`) — this is only a fallback used when no `onspeed.cfg` exists
on the SD card, so it doesn't affect any actual flown/runtime config. Benign.

Final verification: `local_config.h` (both sketches) confirmed **untouched, still untracked**,
still the sole owner of `IMUTYPE_LSM9DS1`, `MAX_AOA_CURVES 5`, `dataSource`, etc. — no duplicate
declarations anywhere in the merged tree. Rebuilt clean: **505416/1048576 bytes flash (48.2%)**,
**95968/262144 bytes RAM (36.6%)** — a modest increase over the Round-1/HEAD-only builds,
consistent with the larger AHRS/serial-protocol rewrite now included. Same one benign
`-Wformat-truncation` warning as before (see Phase 5 §3), no new ones.
`.hex` archived at `<scratchpad>/build-artifacts-merged/OnSpeedTeensy_AHRS.ino.hex`, re-exported
after the follow-up fix commit — this is the current, buildable state of
`merge-upstream-full-2026-07`.

### Standing guidance for future upstream syncs

Regardless of how much of upstream is taken in any future sync: **always hand-check
`AHRS.ino` / `MadgwickFusion.cpp` / the smoothing constants in `OnSpeedTeensy_AHRS.ino`** before
trusting a clean auto-merge involving those files — the code structure can change enough that
git won't flag a real behavioral change as a conflict. And **always confirm `local_config.h`
remains the sole owner** of the installation-specific keys it currently owns
(`IMUTYPE_*`, `MAX_AOA_CURVES`, `MAX_CURVE_COEFF`, `HIGHRES_ANALOGREAD`, `dataSource`,
`replayLogFileName`, `OAT_AVAILABLE`, `SPHERICAL_PROBE`, `LOGDATA_PRESSURE_RATE`/`LOGDATA_IMU_RATE`,
`NOBOOMCHECKSUM`) — upstream doesn't know about this fork's externalization and will keep trying
to redeclare them directly in the main `.ino`.

**Still NOT done, and requiring explicit sign-off before it happens:**
- The `merge-upstream-2026-07` branch has not been merged into `master`.
- Nothing has been pushed to `origin`.
- No physical flash/reflash/re-verify has occurred (no hardware connected this session — see
  Phase 5 §7 above).
- Before fast-forwarding `master`: diff `local_config-sample.h` (both sketches) against the
  cherry-picked tree for any new keys (none of the 7 cherry-picked commits touched either
  `local_config-sample.h`, so this is believed to be a no-op, but re-verify), then do the full
  Phase 5 flash + LED/serial/Wi-Fi verification on real hardware before trusting this build in
  the aircraft.

## 9. Phase 7 — ESP32 OnSpeedWifi build

Pin: **ESP32 core 1.0.6** (code comment explicitly warns core 2.x slows HTTP log downloads).
The installed 2.0.11 must be switched — Boards Manager allows selecting 1.0.6 from the version
dropdown (URL `https://dl.espressif.com/dl/package_esp32_index.json` in additional boards URLs).
Only one version is active at a time; the M5 display sketch (needs 2.x) is out of scope (huVVer).

1. Boards Manager → esp32 by Espressif → select **1.0.6** → Install (replaces 2.0.11).
2. **macOS python fix** (documented in `OnSpeedWifi.ino` header — core 1.0.6 calls `python`):
   `sed -i -e 's/=python /=python3 /g' ~/Library/Arduino15/packages/esp32/hardware/esp32/1.0.6/platform.txt`
   plus `pip3 install pyserial`. (Core 1.0.6's tools are Intel binaries — Rosetta covers them.)
3. Open `Software/OnSpeedWifi/OnSpeedWifi.ino`; Board **ESP32 Pico Kit**; default partition
   scheme; **Sketch → Export Compiled Binary** (compile only — no cable needed).
4. **Flash via OTA (primary path, no USB, no esptool):** join the `OnSpeed` AP (password
   `angleofattack`) → `http://192.168.0.1/upgrade` → upload the exported
   `OnSpeedWifi.ino.pico32.bin` → module reboots on new firmware. Verify the header shows
   `Wifi: <new version>` and a log download still runs at normal speed.
5. **USB fallback** (first-time programming or bricked module): `pip3 install esptool`; port
   will appear as `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART` (macOS 26 has a built-in
   CP210x driver; install Silicon Labs' only if no port appears). Parameters (from the saved
   `upload_cmd.txt`, Windows→Mac adapted): chip `esp32`, baud 921600 (drop to 460800/115200 if
   flaky), `--before default_reset --after hard_reset write_flash -z --flash_mode dio
   --flash_freq 80m --flash_size detect`, images: `0x1000` bootloader_dio_80m.bin · `0x8000`
   partitions.bin · `0xe000` boot_app0.bin · `0x10000` app .bin (all but the app come from
   `~/Library/Arduino15/packages/esp32/hardware/esp32/1.0.6/tools/…`; the IDE's normal Upload
   does all this itself — manual esptool is only for recovery). If the Teensy holds the shared
   serial line, use the web UI's `/wifireflash` (or console `WIFIREFLASH!`) first.

## 10. Troubleshooting matrix

| Symptom | Cause | Fix |
|---|---|---|
| Link: multiple definition of `i2c0_isr` / `Wire1` / `Wire2` | stock Wire library won over the fake shim | Sketchbook not pointed at `Software/Arduino`; restore `libraries/Wire` from git |
| `i2c_t3.h: No such file or directory` | Teensy platform version doesn't bundle i2c_t3 | Copy i2c_t3 into `Software/Arduino/libraries/` (§3.5) |
| Errors in `play_sd_raw`/`play_sd_wav` or SDIO/SdFat link conflicts | Audio.h mod not applied | §4.2 |
| `SdFat.h:438 #warning __has_include(FS.h)` | SdFat pulled into audio files | Benign — ignore. (The platform's SdFat 2.1.2 is what actually gets linked, confirmed by compile; BUILD-mac.md's "2.1.2" was correct all along.) |
| New compile errors that smell like stricter GCC (format, narrowing, sprintf) | newer toolchain vs 2023 code | Apply upstream `610ae09`-style fixes; expect ~one-liners |
| Compile OK, upload never completes | button press missed | Press the physical Teensy button when Teensy Loader prompts |
| Solid green LED / `IMU i2c timeout` after flash | wrong `IMUTYPE_*` in `local_config.h`, or i2c bus locked by flashing | Fix define; **power-cycle the box** |
| EFIS/boom data drops or garbled after rebuild | RX buffer mod lost (platform updated) | Re-apply §4.1 |
| Wifi log downloads painfully slow | esp32 core 2.x used | Rebuild on core 1.0.6 (§9) |
| esptool `serial` module missing / python errors | core 1.0.6 python assumptions | §9.2 sed fix + `pip3 install pyserial` |
| No `/dev/cu.*` port for ESP32 | CP210x driver | Try built-in first; else Silicon Labs VCP driver |

## 11. Fallbacks if IDE 2.x path stalls

1. **Teensyduino 1.58 all-in-one Mac app** (`https://www.pjrc.com/teensy/td_158/Teensyduino_MacOS_Catalina.zip`,
   the exact build upstream's maintainer endorses) — a single signed app bundling Arduino
   1.8.19 + Teensyduino, no installer to break. Intel binary → Rosetta. Core mods go inside
   `Teensyduino.app/Contents/Java/hardware/teensy/avr/…`. This is the closest-to-maintainer
   configuration that still runs on modern macOS.
2. **arduino-cli** (already at `~/bin`, likely wants updating) with the same Teensy platform
   index — fully scriptable `compile`/`upload`; same core mods apply. Good end-state for
   repeatable builds; can be adopted after the IDE path proves the toolchain.
3. Emergency no-build path: flash the checked-in `OnSpeedTeensy_AHRS.ino.TEENSY36.hex` with
   `/Applications/Teensy.app` (Loader 1.56) + button press — but it bakes in whatever
   `local_config.h` it was built with; only useful to prove the flash path.

## 12. Risks and open items

- **Teensy platform 1.59.x is newer than anything the maintainers tested** with this code
  (they tested Td 1.58). If 1.59.x misbehaves, the boards manager allows selecting an older
  platform version (1.58.x) from the dropdown.
- **The IMU define ↔ installed-IMU check (§6.1) is a physical-hardware fact** only David can
  confirm; it is the #1 post-flash failure mode.
- **Core mods are non-durable** (§4.3) — the plan mitigates with a re-apply checklist and a
  no-auto-update policy, but it stays a footgun.
- **macOS 26 USB behavior with Teensy Loader** (HID-based upload) is expected fine but
  unproven on this exact OS build; first upload will tell. Keep the standalone Teensy.app 1.56
  as a second flash path.
- Teensy 3.6 boards are scarce/fragile — nothing in this plan involves risky flashing modes,
  but keep the spare, and don't experiment with LTO/clock settings on the flight unit.
- The repo `.ino.hex`/`.bin` artifacts are stale after any rebuild — don't commit new ones
  (gitignored anyway).
