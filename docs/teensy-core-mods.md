# Teensy core modifications — re-apply checklist

These modifications live **outside the repo**, inside the installed Teensy platform, and are
wiped by any `arduino-cli core upgrade`/Boards Manager update of `teensy:avr`. Re-apply this
checklist after every platform update, before rebuilding `OnSpeedTeensy_AHRS`. See
[BUILD-PLAN-macOS26.md](BUILD-PLAN-macOS26.md) §4 for context.

**Applied to:** `teensy:avr@1.62.0`, installed via `arduino-cli core install teensy:avr` on
2026-07-16. Platform directory:
`~/Library/Arduino15/packages/teensy/hardware/avr/1.62.0/`

Do **not** let this platform auto-update until these mods are re-applied to the new version.

## 1. Serial RX buffer size (required for EFIS/boom/wifi ingest)

In `cores/teensy3/serial1.c`, `serial2.c`, `serial3.c`, `serial4.c`, changed:

```c
#define SERIALn_RX_BUFFER_SIZE     64 // number of incoming bytes to buffer
```
to
```c
#define SERIALn_RX_BUFFER_SIZE     8192 // number of incoming bytes to buffer
```
(n = 1, 2, 3, 4 — one edit per file, line ~44/46 in each.)

Status: **applied** to 1.62.0 on 2026-07-16.

## 2. Audio.h SDIO fix (required per `OnSpeedTeensy_AHRS.ino:458`)

In `libraries/Audio/Audio.h`, commented out:

```c
#include "play_sd_raw.h"
#include "play_sd_wav.h"
```

so the Audio library doesn't contend with SdFat for the SDIO peripheral. Verified the OnSpeed
sketch doesn't use `AudioPlaySdRaw`/`AudioPlaySdWav` (it plays voice clips via `AudioPlayMemory`
from PROGMEM, not SD) — safe to exclude.

Status: **applied** to 1.62.0 on 2026-07-16.

## Verifying after a platform update

```bash
TEENSY_DIR=~/Library/Arduino15/packages/teensy/hardware/avr/<new-version>
grep -n 'RX_BUFFER_SIZE     ' "$TEENSY_DIR"/cores/teensy3/serial{1,2,3,4}.c
grep -n 'play_sd_raw\|play_sd_wav' "$TEENSY_DIR"/libraries/Audio/Audio.h
```
If the buffer sizes show `64` or the `play_sd_*` includes are active (not commented), re-apply.
