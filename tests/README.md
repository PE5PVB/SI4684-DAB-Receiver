# Host regression tests

The 2026-10-22 I2S framing/readback and packet-mode DSRV CID corrections
are documented in [the I2S/SPI repair report](I2S_SPI_20261022_report.md).
Run the I2S commands below and `test_dab_spi_probe.cpp` from the C++11 suite.
The I2S harness now injects every write/read failure and mismatch, tests
read-only running-image reuse, and retains analog/INTB pins on failure.
The probe fixture covers the Polish MUX 11A short CID through the unchanged
DSRV parser, source/TMID/SID bounds, exact command ownership and late-data quarantine.
The report supersedes older short-packet-CID rejection and 0x1600 format descriptions.

Current XPAD readback and bounded raw UATYPE diagnostics are documented in
[the XPAD/UATYPE report](DAB_XPAD_UATYPE_diagnostics_report.md).

```powershell
g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  tests/test_dab_diagnostics.cpp -o .pio/test_dab_diagnostics.exe
./.pio/test_dab_diagnostics.exe
python tests/test_dab_xpad_integration.py C:/msys64/ucrt64/bin/g++.exe
```

These fixtures test the actual boot function and DSRV diagnostic block with
transport/UART mocks. Simulated raw 0x0007 is not an on-air SPI reception test.

Current P/S, UART-independent discovery and raw DSRV diagnostics are documented in
[the stabilization report](DAB_SPI_fixes_20261009_report.md).

```powershell
g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  tests/test_dab_dsrv_header.cpp -o .pio/test_dab_dsrv_header.exe
./.pio/test_dab_dsrv_header.exe
```

Current packet-mode inventory/discovery behavior is documented in
[the packet-component report](DAB_SPI_packet_components_report.md). It supersedes
the older discovery examples below. Run the additional tests with:

```powershell
g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  tests/test_dab_packet_inventory.cpp -o .pio/test_dab_packet_inventory.exe
./.pio/test_dab_packet_inventory.exe
python tests/test_dab_packet_runtime.py C:/msys64/ucrt64/bin/g++.exe
```


The configurable Si4684 output is documented in
[the I²S audio report](I2S_audio_settings_report.md), including the precise
changed-file list, expected diagnostics and receiver test checklist.

```powershell
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -I tests/stubs/nvs `
  tests/test_i2s_audio.cpp src/nvs_storage.cpp -o .pio/test_i2s_audio.exe
./.pio/test_i2s_audio.exe
python tests/test_i2s_integration.py C:/msys64/ucrt64/bin/g++.exe
```

These tests exercise the production NVS implementation with legacy records,
all mode/rate/pin mappings, cycling and menu indices. The integration test
compiles the actual radio configuration block against a transport mock and
injects failures into each property write. It also checks initialization/menu
wiring and the existing fonts. It does not simulate RF/audio hardware.

Discovery completion regression tests and the receiver verification procedure
are documented in
[the component-scan report](DAB_SPI_discovery_component_scan_report.md).
`test_dab_spi_discovery.cpp` now additionally tests the production `Scan` and
CID-retention helpers: last request issued versus last reply committed,
pending diagnostic output, last-component SPI, three CIDs for one SID,
failed commands, stale generation rejection, duplicate completions, empty
inventories, generation wrap, all 448 slots, and capacity/input bounds.
It also tests recurring detection over 100 consecutive refresh cycles,
deadline wrap, postponement while ineligible and cancellation on invalidation.

For the EPG indicator, verify on hardware that the plain `EPG` artwork below
the codec is grey on the supplied mux with no SPI and cyan on a mux advertising
`0x007`. It should retain its color during automatic rescans, repeat discovery
more than 60 seconds after completion without toggling DEBUG, and disappear
in FM. Check redraw after volume/menu/slideshow overlays, station changes and
theme changes, and verify the codec/PR row remains intact. Repeat the audio,
infotext, SLS and CTS/overflow/UART checks described in the report.

The subsequent display fix uses the actual codec FONT16 for EPG. EID now
repaints x=38..77/y=104..118 and SID x=38..77/y=119..135, so a changed EID
cannot erase the top of a cached SID. Font-metric validation checked all
65,536 four-digit hexadecimal combinations and the E/P/G glyphs against
these crops, including the right edge and the adjacent flag/PR regions.

Dynamic fields remain live while volume/slideshow-loading panels are open.
The subsequent user-approved simplification removes viewport clipping.
Changed main widgets set a shared repaint flag; after all dynamic fields,
ProcessDAB consumes it and repaints the visible panel once. Volume takes
precedence when both panels are requested. Unchanged widgets do not request
repainting, and panel rendering does not set its own repaint flag. The update
does not restart VolumeTimer, touch the amplifier or depend on rssiTimer.

A host smoke mock extracted the actual dirty-flag helper and final composition
block and checked multiple field writes -> one panel redraw, idle passes ->
zero redraws, changes between signal ticks, volume/loading precedence and menu
suppression. Actual TFT flicker must still be checked on the receiver. Change
signal strength during loading and verify numeric RSSI/quality remain live.
Check both floating windows, restore after closing, and change EID with SID
unchanged to verify complete hex values.

`test_dab_service_switch.cpp` now rejects service metadata for an old SID/CID,
pending switches, absent audio and differing components of the same SID. It
also covers rapid A -> B -> C and A -> B -> A selection. `test_fm_features.cpp`
checks that the target name remains selected through debounce, START completion
and delayed service-label arrival for both local/global selection routes.
On hardware, rapidly turn Rotary1 and Rotary2 in both directions, including
across multiplexes. The final selected name should remain until fresh metadata
for that service arrives, without a flash of the previous name. Also check
stored presets, a delayed/failed metadata query and normal audio/SLS reception.

Generate deterministic image fixtures (requires IJG/libjpeg `cjpeg` and
PowerShell `System.Drawing`):

```powershell
./tests/generate_fixtures.ps1
```

Compile and run with GCC C++11 from the repository root:

```powershell
g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  tests/test_scheduler.cpp -o test_scheduler.exe
./test_scheduler.exe

g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  tests/test_fm_features.cpp -o test_fm_features.exe
./test_fm_features.exe

g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  tests/test_dab_service_switch.cpp -o test_dab_service_switch.exe
./test_dab_service_switch.exe

g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  tests/test_dab_user_application.cpp -o test_dab_user_application.exe
./test_dab_user_application.exe

g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  tests/test_dab_spi_discovery.cpp -o test_dab_spi_discovery.exe
./test_dab_spi_discovery.exe

g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  tests/test_dab_spi_probe.cpp -o test_dab_spi_probe.exe
./test_dab_spi_probe.exe

g++ -std=c++11 -Wall -Wextra -Werror -pedantic `
  -I tests/stubs -I src tests/test_jpeg.cpp src/JPEGdecoder.cpp `
  -o test_jpeg.exe
./test_jpeg.exe
```

`test_scheduler.cpp` exercises CTS host-starvation classification and recovery
counting, AUDIO_INFO delays, tune-busy backoff constants, service-list
generations, label quality, the existing generic retry/backoff, phased
deadline, continuous-DSRV fairness, FM work-priority, RF/display-filter,
one-shot time-sample and debug-throttling helpers used by the firmware.
`test_fm_features.cpp` covers regional FM wrapping, AF validation,
deduplication, quality hysteresis and wrap-safe sweep timeout, RDS CT/MJD
parsing and chronological confirmation across minute/day/month/year rollover,
RDS versus RBDS PTY lookup, PI/frequency
station identity, DAB audio-service boundary selection, 38-channel wrap and
short/long Rotary 1 press suppression. It also locks down Rotary 2 routing:
FM AUTO scan-list/no-list behavior, DAB global-list/current-mux fallback,
closing the slideshow wait view, and exact/unique SID-component matching.
`test_dab_service_switch.cpp` covers serialized data/audio teardown, rapid
request supersession, wrap-safe bounded backoff, both settle intervals,
metadata-trigger deduplication, audio-only deferred SLS retries and SLS context
ownership, including audio-PAD SLS without a separate data service.
`test_jpeg.cpp` runs the application JPEG parser and
decoder against baseline grayscale, 4:4:4, 4:2:2, 4:2:0 and restart-marker
fixtures, including a complete 320×240 baseline 4:2:0 decode. Progressive
coverage includes grayscale, 4:4:4, 4:2:2, 4:2:0, DRI/RST, 17×13 edge blocks
and complete 320×240 APP0/SOF2 output. A 320×320 pair verifies identical
baseline/progressive 50%-scaled 160×160 output. Each valid progressive fixture
is compared pixel-for-pixel with the matching baseline decode. The suite also
covers premature EOI, malformed DHT/DQT, skipped refinement levels, a wrong
restart marker, oversized/four-component frames and baseline multi-scan
classification.

The PNG alpha fixture is checked structurally here and is decoded by PNGdec in
the PlatformIO firmware build/runtime path. Pixel appearance, MOT traffic,
actual TFT fading and radio timing still require the hardware plan in
`VERIFICATION.md`.

`test_dab_user_application.cpp` covers one/multiple applications, SLS/TPEG/SPI,
unknown types, LE16 type fields, zero/odd/even data lengths and AN649 padding,
every truncated prefix, trailing bytes, null input, maximum feasible UA count,
the maximum declared count (255), oversized lists, and integration with the
unchanged vendor `DabComponentInfo` parser. It also checks all 65,536
combinations of declared data length and available input length.

## User Application diagnostics and SPI discovery (phases 1–2)

On hardware, boot DAB, tune a multiplex and confirm audio and slideshow first.
Send the existing bare `DEBUG` command to enable diagnostics. Once the service
list and audio context are stable, the lowest-priority UA discovery pass queries
all components, including secondary components omitted from the station table.
Queries are separated by at least 500 ms; output is paced at one short queued
line per 100 ms. Data bytes are printed in chunks of at most 16 bytes. A failed
query is reported with its SID/CID and is not retried in a
tight loop. Toggle DEBUG off/on to request another pass. A changed component
inventory also starts a new pass; an identical periodic list does not.

Illustrative output (values depend on the broadcast):

```text
[DAB/UA] SID=0000ABCD CID=00FE0001 label="Programme info" lang=9 count=1 bytes=4
[DAB/UA]   type=0x007 (SPI) len=1
[DAB/UA]     data[0]=01
```

Unknown types are logged without starting any application. A malformed UA
block produces a local `malformed/unreadable list` diagnostic. Confirm audio,
SLS updates, encoder responsiveness and stable heap throughout the pass, with
no new CTS timeout, UART-drop or DSRV-overflow burst. Retune during the pass and
verify old-multiplex output stops. Test both INTB and POLL/AUTO configurations.
These phases do not start SPI and cannot confirm simultaneous standalone
SLS/SPI support on firmware 6.0.9.

Wire format reference: [Skyworks AN649, DAB_GET_COMPONENT_INFO](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/an649.pdf).
The UA parser consumes the vendor parser's existing raw block; it does not
reinterpret FIG 0/13 directly. Diagnostic inventory storage is fixed at 32
services / 448 component IDs (2,048 bytes on ESP32), plus a 255-byte UA block,
17-byte label and small counters. Phase 2 adds 448 one-byte component identities,
a compact selection result and small bookkeeping fields. No SPI arena is allocated.

SPI discovery also runs with DEBUG disabled; DEBUG controls output and can
request a fresh pass. Each component is classified using the validated UA list
only: `UATYPE == 0x007` means SPI, never a label or payload heuristic. All SPI
candidates are logged with SID/CID when DEBUG is enabled. The result is exposed
through `radio.spiDiscovery()`, separately from existing SLS/audio ownership.
No START/STOP service command is added in this phase.

The selection rule is explicit and independent of reply order:

1. Wait until every component has a valid UA answer.
2. Select a unique SPI component belonging to the current audio service.
3. Otherwise select a single SPI candidate in the whole ensemble.
4. Multiple candidates of the same priority are ambiguous; no candidate is
   selected. A failed/malformed component answer leaves discovery incomplete,
   because it might hide a higher-priority candidate. A DEBUG off/on cycle
   requests another bounded pass.

A unique ensemble candidate is a policy choice, not proof that its programme
objects cover the selected audio service. UA application data are retained in
the diagnostic dump, but profile is explicitly reported as `unknown`; Basic or
Advanced support is not inferred from an arbitrary byte. Profile and schedule
scope need verification in the later SPI/MOT implementation.

On service change the selection becomes pending immediately and in-flight UA
ownership is invalidated. A fresh pass runs after the new audio context is
stable. Retune/reset or changed component inventory clears the identities and
starts a new pass. An identical periodic service-list reply preserves them.

Illustrative additional diagnostics:

```text
[DAB/SPI] confirmed=1 packetMOTUnverified=0 status=confirmed/service selected=1 SID=0000ABCD CID=00FE0001 profile=unknown
[DAB/SPI] confirmed=2 packetMOTUnverified=0 status=ambiguous selected=0 SID=00000000 CID=00000000 profile=unknown
```

Hardware checks: compare candidates against the UA dump, switch audio services
within the mux and verify the selection follows the rule, then retune and
confirm that no prior-mux candidate survives. Test missing SPI, ambiguity and
unavailable component information where broadcasts permit it. Check that radio
and SLS remain responsive with DEBUG both on and off. Actual SLS/SPI coexistence
is still unverified: SPI has not been started.

`test_dab_spi_discovery.cpp` covers UA-based identity, SLS/TPEG/unknown rejection,
duplicate SPI UAs, malformed data following a valid SPI UA, all six candidate
ordering permutations, service priority, ensemble uniqueness, ambiguity,
pending/failed inventory, SID/CID identity, duplicate candidates and saturated
counts, service changes, and fixed storage with maximum component inventory.

## Controlled coexistence probe (phase 3)

`test_dab_spi_probe.cpp` tests the same endpoint router and state policy used
by the firmware: audio/PAD versus standalone SLS versus SPI, unknown sources,
stale/wrong SID/CID, independent components with equal MOT Transport IDs,
missing/shared endpoints, duplicate requests, baseline/observation deadlines,
station change while START is pending, explicit rejection versus unanswered
START, bounded STOP retries, quarantine after failed cleanup, zero-byte SLS
STOP notifications, saturating counters and `millis()` wraparound.

The test uses fixed storage (no SPI object arena) and the existing cooperative
command scheduler. Baseline lasts 5 seconds, observation 30 seconds after CTS
confirms START. It issues START/STOP only for the separate probe SID/CID; audio
and SLS ownership fields are untouched. While a probe SPI context is tracked,
the header router precedes the existing decoder, so SPI or unmatched payload
cannot alter the slideshow. Outside the probe, normal reception uses the
original decoder path with its existing switch/collector protections; exact
identity matching remains diagnostic until verified on the real receiver.
SPI content is discarded at this stage. Route logging is bounded independently
for each of the four route classes; counters still include suppressed packets.

### Real receiver procedure (required before a major SPI/MOT decoder)

1. Boot DAB normally and tune a known mux. Confirm stable audio and several
   slideshow updates. Keep a record of firmware 6.0.9 and INTB/POLL/AUTO mode.
2. Send DEBUG, wait for the complete UA pass and SPI selection. Confirm
   UATYPE=0x007, selected SID/CID and a distinct SLS/audio endpoint. If SPI is
   absent, ambiguous, incomplete or shares a component with SLS/audio, record
   that result; the standalone probe cannot be run in that context.
3. Send DEBUG SPI. Record the five-second baseline, especially SLS/PAD packet
   counts. Verify that no STOP for SLS/audio precedes SPI START. START result 0
   means command success, not proof of concurrent application reception.
4. During the following 30 seconds, listen for interruptions and watch for new
   decoded slides. Look for separate SPI and SLS/PAD route counts, not only an
   occasional UART sample. Save CTS, DSRV overflow and UART-drop deltas. SLS_STOP=1
   means a STOP notification was received for the active standalone SLS path;
   absence of packets alone does not establish firmware replacement.
5. Wait for automatic SPI STOP and final summary. If standalone SLS stopped,
   retune/reselect to restore the existing context. Record the failure rather
   than treating the probe as successful. No fallback is implemented.
6. Repeat with DEBUG SPI STOP during baseline and observation, and with a
   station switch while START is pending. Confirm cleanup precedes normal
   service teardown, no stale SPI payload reaches SLS, and the UI stays usable.
   On cleanup uncertain, retune/reset before another probe.
7. Repeat with standalone SLS and audio-PAD SLS where available, and with
   INTB and POLL/AUTO. Record free/largest heap before/after repeated probes,
   audio, slideshow updates, CTS recovery and overflow/drop trends. Packet
   counters are evidence only; actual audio and decoded SLS must be verified.

Illustrative output only (counts and identities depend on the mux):

```text
[DAB/SPI/TEST] baseline 5s then START SID=0000ABCD CID=00FE0001; observe 30s; audio/SLS kept
[DAB/SPI/TEST] START result=0 reason=00; no SLS STOP issued
[DSRV/ROUTE] route=3 src=0 sid=0000ABCD cid=00FE0001 dscty=60 bytes=512 remain=1 state=0 suppressed=0
[DAB/SPI/TEST] distinct SPI and SLS/PAD traffic observed; verify audio and slideshow on HW
[DAB/SPI/TEST] STOP result=0 reason=00 attempt=1
```

An inconclusive outcome prints `coexistence inconclusive`; explicit tuner
evidence prints `tuner reported SLS service stopped`. Successful host tests and
builds do not validate simultaneous standalone reception on Si4684 FW 6.0.9.

### SLS/DLS regression check outside SPI testing

The receiver log showed every DSRV packet being rejected (`staleMOT` tracked
DSRV 17/17 and 35/35) despite confirmed audio, no detected SPI and zero CTS
timeouts/overflows. The unconditional exact-identity gate has been removed
outside a tracked SPI probe context. This restores the previous decoder path;
it is retained independently of the subsequent bounded audio PAD/DLS identity
comparison described below.

`testLegacyReceptionGate` in `test_dab_spi_probe.cpp` exercises the actual
firmware admission helper for all route/context combinations, mismatched PAD
and DLS identities without SPI, suppression during audio teardown, and strict
quarantine during probing or failed cleanup. The differing CID in the fixture
is illustrative, not a captured hardware identity or an inferred CID mask.

On hardware, boot/tune without DEBUG SPI and verify that infotext and new
slideshow images return. Enable DEBUG and save `[DSRV/IDENTITY]` lines, which
show actual versus expected SID/CID/source and `legacy=1` for restored normal
forwarding. Only one such line per 5 seconds is queued. Switch stations and
verify existing stale-MOT protections still work. Compare CTS, DSRV overflow
and UART-drop trends. SPI testing should wait until normal reception and the
received identity representation have been checked. Audio noise in the reported
run was separately traced by the user to amplifier power.

The subsequent HW capture identifies the mismatch: for SID `00002FD9`, the
service list has CID `01FE002A` and DSRV reports `0000002A` for both `src=1`
(non-DLS PAD) and `src=2` (DLS). Slideshow collection restarted (TID 43629,
expected body 17,858 bytes); the supplied log does not show complete publication.

The audio router now accepts either the exact full CID or, for PAD/DLS on an
audio TMId=0 endpoint only, the zero-extended low 16-bit component field with
the same SID. It does not mask arbitrary incoming CIDs, normalize standalone
SLS/SPI IDs or change the full IDs supplied to tuner commands. The original
normal-reception admission path and switch/collector protections remain.

`testCapturedAudioPadIdentity` uses the actual captured SID/CIDs and both source
values, tests admission during a SPI probe, and rejects different SID, component,
TMId or nonzero mismatching upper bits, invalid endpoints and reserved sources.
It also checks exact standalone routing, rejection of unverified short data
CIDs, and retention of the full command-ownership CID.

On the receiver, repeat normal PAD/DLS reception on SID `00002FD9` and switch
stations. The captured `src=1/2` CID `0000002A` should now classify as audio/PAD
and stop producing identity-mismatch lines for that endpoint. Verify infotext
and a completed/new slideshow on the display. Other identity-mismatch lines
remain useful evidence. This mux reports only Slideshow UA `0x002` in the supplied
dump, so SPI coexistence is still unverified and needs a mux advertising `0x007`.
