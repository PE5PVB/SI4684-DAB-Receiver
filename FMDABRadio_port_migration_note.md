# FMDABRadio radio-core migration note

This change selectively ports platform-neutral scheduling and diagnostic
policies from `FMDABRadio`. The target UI, controls, persistence and board
recovery architecture remain native to `SI4684-FMDAB-Receiver`.

## Ported

- Fixed 4096-byte non-blocking UART TX ring with 64-byte service chunks,
  queue depth and saturating dropped-byte diagnostics. Boot output remains
  direct; runtime diagnostics and the existing serial-control responses are
  buffered. Serial RX remains direct and non-blocking.
- CTS timeout classification into `NotTimeout`, `HostStarved` and `Genuine`.
  Host starvation does not advance the genuine-timeout recovery watchdog;
  device errors are classified as responses, not CTS loss.
- Delayed and bounded `DAB_GET_AUDIO_INFO` acquisition. Command framing is
  unchanged (`0xBD`, one zero argument).
- Immediate invalidation of DAB audio metadata during tune, service change,
  mode/reset recovery and service-start acquisition.
- DAB/MP2 versus DAB+/HE-AAC presentation from `DAB_GET_SUBCHAN_INFO`, never
  from bitrate.
- Non-blocking `0x18 COMMAND_BUSY` tune retry for the same Band III index.
- Monotonic service-list generations, explicit scan refreshes, bounded retry,
  hard timeout, transactional parsing and preservation of the best service
  records already accumulated in `DabStations`.
- Label-quality policy: an empty refreshed label cannot erase a known label
  for the same SID/CID; a stable SID fallback is generated only when scan
  retries are exhausted.
- Four-second FM scan observation that refuses to persist changing/dynamic PS
  text while keeping the frequency/PI station record.
- Host policy tests for timeout classification, audio retry timing, tune busy
  timing, service-list generations and label-content handling.

## Intentionally not ported

- ST7735 screens, layout, fonts and button semantics.
- AT24C256 record/commit layout and EEPROM scan storage.
- FMDABRadio GPIO assignments, power/amplifier control and independent tuner
  reset sequence.
- External NVSPI firmware addresses or other board-specific boot assumptions.
- SLS/MOT renderer and assembly code.
- A new HELP/STATUS/LIST command family or Settings option. The target's
  existing fixed-buffer serial RX protocol is retained; only its TX path was
  made non-blocking.

The target ILI9341 drawing model, Rotary1/Rotary2 behavior, Settings structure,
NVS schema, GPIO17 shared-reset/TFT restore, GPIO12 AUTO/INTB/IR modes, separate
TFT/radio SPI buses, global FM/DAB lists and fixed SLS arena are unchanged.

## New constants

| Constant | Value |
|---|---:|
| UART TX capacity | 4096 bytes |
| UART service budget | 64 bytes/pass |
| `DAB_HOST_STARVATION_US` | 2000 us |
| `DAB_AUDIO_INFO_INITIAL_DELAY_MS` | 400 ms |
| `DAB_AUDIO_INFO_FAST_RETRIES` | 3 |
| AUDIO_INFO fast retry delays | 500/1000/2000 ms |
| `DAB_AUDIO_INFO_SLOW_RETRY_MS` | 10000 ms |
| `DAB_TUNE_BUSY_BACKOFF_MS` | 40 ms |
| `DAB_TUNE_BUSY_MAX_RETRIES` | 3 |
| Tune busy retry delays | 40/80/120 ms |
| `DAB_SCAN_LIST_SETTLE_MS` | 1000 ms |
| `DAB_SCAN_LIST_RETRY_MS` | 1500 ms |
| `DAB_SCAN_NO_SIGNAL_TIMEOUT_MS` | 2500 ms |
| `DAB_SCAN_LIST_TIMEOUT_MS` | 9000 ms |
| `DAB_SCAN_LIST_MAX_REQUESTS` | 4 |
| `FM_SCAN_PS_STABILITY_MS` | 4000 ms |

## Build result

PlatformIO environment `esp32dev` (`espressif32@6.9.0`) builds successfully:

- RAM: 59,224 / 327,680 bytes (18.1%)
- Flash: 3,444,973 / 4,128,768 bytes (83.4%)

The existing TFT_eSPI `TOUCH_CS` informational warning remains unchanged.

## Hardware verification

1. Connect a 115200-baud terminal, send `DEBUG`, and rapidly scroll Rotary2
   across stations on several multiplexes. Confirm responsive UI, no false
   cold recovery, bounded `[SERIAL]` queue and increasing `host` rather than
   `genuine` if a long host gap occurs.
2. Repeat with automatic SLS enabled and an actively changing JPEG/PNG MOT
   stream. Confirm slideshow reception/rendering and audio switching continue.
3. Change DAB services repeatedly. Confirm `Audio loading...` appears first,
   old bitrate/sample rate/mode/PTY do not flash, then `DAB / MP2` or
   `DAB+ / HE-AAC` and fresh audio data appear.
4. Run a full Band III scan. Confirm no-signal channels advance promptly,
   slow/provisional multiplex lists get refresh attempts, labels improve when a
   newer generation arrives, and the completed global list persists in NVS.
   Cancel one scan as well and confirm the previous NVS list is restored.
5. Exercise or inject `0x18 COMMAND_BUSY`. Confirm the same channel is retried
   after 40/80/120 ms, exhaustion advances a full scan, and no cold reset occurs.
6. To validate genuine recovery, force short-gap CTS timeouts (for example by
   disconnecting/stalling tuner communication without starving the host loop).
   Confirm `genuine` and `consecutive` increase and the existing shared GPIO17
   recovery restores both the tuner and ILI9341 only after the watchdog limit.
