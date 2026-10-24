# Verification status — v2.2.1

## Automated checks performed

- Universal `Si468x` driver commit used by this application:
  `b671d372a4fe5bb4df493ea6147c215577b25dc2`.
- Vendor `Si468x.h` raw SHA-256:
  `A894CF940CBD143D9C0917BF68C3F335370AF28FC06E9D074F0EEA91545FBF6E`.
- Host scheduler policy test: generation rollover/matching, bounded retries,
  phased periodic deadlines, continuous-DSRV fairness, FM tune/RDS/RSQ
  priority, DAB/FM sample/display separation, one-shot time application,
  debug throttling and `millis()` wraparound.
- Host FM feature test: regional frequency wrapping, AF code filtering and
  deduplication, CT/MJD validation, RDS/RBDS PTY lookup, PI/frequency station
  identity, DAB audio-service boundaries and channel wrap, plus Rotary 1
  short/long-press suppression.
- Host DAB service-switch policy test: data-before-audio STOP ordering, latest-
  request-wins supersession, bounded wrap-safe retry/backoff, 200 ms settle
  gates, central data evaluation, deferred audio-only SLS retry and SLS context
  ownership, including audio-PAD SLS without a separate type-3 service.
- Host JPEG tests with GCC C++11, `-Wall -Wextra -Werror -pedantic`:
  baseline grayscale, YCbCr 4:4:4/4:2:2/4:2:0, a complete 320×240 baseline
  4:2:0 decode, and progressive grayscale/4:4:4/4:2:2/4:2:0 decodes including
  DRI/RST, non-MCU-aligned 17×13 dimensions, complete 320×240 output and a
  320×320 progressive/baseline pair reduced to matching 160×160 output.
  Corruption coverage includes premature EOI, malformed DHT/DQT, skipped
  progressive refinement levels, invalid restart sequence, baseline multi-scan
  classification and oversized dimensions.
- The display-sized progressive fixture is 320×240, begins `FF D8 FF E0`, has
  valid SOF2/EOI and is approximately 12 KiB. Its decoded RGB565 output is
  pixel-identical to the matching baseline fixture. It is not the unavailable
  station object with hash `47083CD0`.
- PlatformIO release build with `espressif32@6.9.0`: success for `esp32dev`
  without PSRAM. Reported static usage: 56,120 bytes RAM (17.1%) and
  3,440,053 bytes flash (83.3%). The persistent 50 KiB MOT buffer and 76,800
  byte shared decoder arena are allocated at runtime and therefore are not
  included in that static RAM number.
- Clang is not installed in this environment: **NOT VERIFIED**.
- The installed MinGW GCC lacks ASan/UBSan runtime libraries: sanitizer run is
  **NOT VERIFIED**.

The generated `.pio/build/esp32dev/firmware.bin` is a successful development
build of 3,440,672 bytes with SHA-256
`2C9EB44E8D2778EA127B7E5397C8199B23891E350498F88CB69AB0286E60BB8A`.
It is not copied into `Release/` or represented as a hardware-validated release.
`Release/firmware_v2_0.bin` remains an unchanged historical image.

## Hardware validation still required

1. GPIO12 unconnected/AUTO: verify `hw=Polling`, no IRQ edges, normal DAB/FM,
   manual retune and responsive UI.
2. GPIO12 connected to INTB: verify IRQ-first completion with bounded safety
   polling; a nonzero `ctsPoll` count alone is not a failure.
3. Rapid DAB retune during DSRV/SLS: verify data STOP, audio STOP, at least
   200 ms from the last confirmed STOP, then START/TUNE; no stale multiplex
   metadata or slideshow is published.
4. Real tuner CTS loss: verify bounded data/STOP retries, shared GPIO17 reset,
   `hostAbort` before PRECHECK, successful image upload, and controllable UI if
   recovery fails.
5. Display many baseline JPEG/PNG slides and the 320×240 progressive fixture.
   Also exercise the 320×320 progressive fixture and manually request an
   unsupported/corrupt slide. Confirm complete 160×160 square output, that a
   failed object closes the "Loading slideshow" overlay, acceptable decode
   time, no CTS timeout caused by rendering, PNG alpha appearance and encoder
   responsiveness during validation/render.
6. Repeat FM/RDS, DAB service restore, FM↔DAB switch, presets, light sleep/wake
   and cold boot regression checks.
7. On DAB with continuous MOT, confirm RF status near 1 Hz, signal/Q UI near
   2 Hz, no DSRV regression/overflow, and responsive encoders/buttons.
8. On idle FM, confirm RSQ near 2 Hz, ACF near 1 Hz and signal/multipath UI near
   4 Hz while seek remains fast and the RDS FIFO remains responsive.
9. With `DEBUG` enabled, confirm milestone-only MOT logging and usable UI; use
   `DEBUG SLSV` separately to verify explicit verbose tracing and UART-drop
   summaries. Confirm that repeated NOT_AVAILABLE replies are aggregated.
10. Test erased, missing, partial and malformed direct-NVS records and verify
    safe defaults. Confirm that no legacy EEPROM namespace is read or written.
11. In FM, verify live AUTO seek remains independent of the scanned list.
    Long-press Rotary 1 through a complete band scan, confirm bounded RDS dwell,
    frequency fallback for missing PS, list selection, cancellation preserving
    the previous list, and persistence across reboot.
12. With real RDS/RBDS broadcasts, verify AF list reset on tune/PI change,
    TP/TA transitions, regional PTY names and stable two-sample CT publication.
13. In DAB AUTO, traverse forward/backward inside a multiplex, cross both mux
    boundaries, wrap the Band-III table, reverse/cancel during a search, and
    confirm first/last valid audio service selection without stale list data.
14. Long-press Rotary 1 in DAB through a complete 5A..13F scan. Confirm only
    audio services enter the global list, cancellation preserves the previous
    saved list, the original service is restored, and the list persists across
    reboot. Confirm the progress bar advances through all 38 channels. With no
    completed scan, confirm Channel List stays empty, says `SCAN NOT RUN`, and
    OK starts scanning; after a completed empty scan it says `NO STATIONS FOUND`.
15. During FM full scan confirm the current frequency never paints through the
    scan overlay, and that PS is used only after all four segments are confirmed
    for the current PI; otherwise the list must show the frequency fallback.
    Confirm the compact progress bar advances across the configured regional band.
16. In FM System Information, verify live AF frequencies, TP/TA/PTY, RDS CT
    with half-hour offset, and RSSI/SNR/multipath/blend. Confirm a two-sample
    validated CT updates the main clock to local time without repeatedly
    resetting its seconds.
17. In FM AUTO, verify Rotary 2 traverses the persistent scan list in both
    directions and wraps; with an empty list it must perform no tuning action.
    In DAB, verify Rotary 2 traverses the global list, falls back to the current
    mux only when the global list is absent, and rapidly scrolling commits only
    the final selection. For services sharing a SID, confirm the Component ID
    selects the correct audio component and the UI does not remain on
    `Select service`. Repeat the exact-component check from the Rotary 1 list.
18. With active DAB SLS, rapidly select A -> B -> C and repeat across multiplexes.
    The log must show data STOP, audio STOP, `ServiceSettle`, audio START,
    `AudioSettle`, then at most one data START for the final service. Verify no
    command storm, watchdog/reset, stale slide or stale station label. Inject or
    observe `0x03` rejections and confirm bounded backoff without cold recovery;
    separately interrupt CTS/INTB and confirm only consecutive CTS timeouts invoke
    the existing transport recovery. Repeat in POLL, INTB and AUTO configurations.
19. On an ensemble that emits DSRV/MOT from audio PAD but has no standalone
    type-3 data service, confirm `ResolveData -> Ready`, no continuing
    `staleMOT` growth, and successful slideshow reception. While its manual
    `Loading slideshow` view is visible, turn Rotary 2 and confirm that the
    overlay closes immediately and the adjacent global station is selected.
20. Enable `FM AF`, remain on one PI for at least 10 s and attenuate the RF
    signal below the selected seek-sensitivity thresholds for at least 4 s.
    Verify that probes are muted and non-blocking, wrong/missing PI candidates
    are rejected, a same-PI candidate needs two valid RSQ samples and at least
    +5 dB RSSI without more than 1 dB SNR loss, failures restore the original
    frequency, and 30 s/10 s success/failure cooldowns prevent ping-pong.

## Expected diagnostic sequence examples

```text
[RADIO/RESET] physical reset complete hostAbort=-11 generation=...
[RADIO] PRECHECK result=0 image=...
[RADIO/CTS] late host service gap=... us afterDeadline=... us result=0 (CTS-ready time unknown)
[DAB/STOP] unconfirmed result=-4 retry=1/3 backoffUntil=...
[DAB/SWITCH] state=StopAudio -> WaitAudioStop request=...
[DAB/SWITCH] state=WaitAudioStop -> ServiceSettle request=...
[DAB/DATA] start rejected retry=1/3 until=... result=... reason=0x03
[SLS/JPEG] tid=... size=... hash=... coding=SUPPORTED_PROGRESSIVE_JPEG ...
[SLS/JPEG] render=OK size=... lastMCURow=...
```

`hostAbort=-11` means an old `WaitCts` was cancelled; `hostAbort=0` means the
driver was already idle. Host lateness diagnostics do not prove a tuner fault.
