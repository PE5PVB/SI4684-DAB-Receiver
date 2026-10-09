# Serial protocol

This document describes the serial protocol exactly as implemented by firmware
**v2.2.1**. It is not a proposal for a future protocol. Current limitations and
implementation-specific behaviour are therefore documented explicitly.

The source of truth for the implementation is `src/comms.cpp`.

## Transport

- Interface: ESP32 UART/USB serial console.
- Speed: 115,200 baud.
- Default Arduino Serial frame: 8 data bits, no parity, 1 stop bit.
- Commands and responses are line-oriented text.
- The firmware accepts `CR`, `LF`, and `CRLF` line endings.
- If a terminal sends no line ending, the firmware commits the command after
  approximately 100 ms without another received byte.
- The receive buffer holds 128 bytes including the terminating `NUL`. A longer
  command produces `#1`.
- Command names are case-insensitive. Leading and trailing whitespace around
  the line, command name, and value is removed.
- Service names and text data are transmitted as UTF-8.
- The protocol has no checksum and no acknowledgement for individual dynamic
  messages.

## Output line types

| Prefix | Meaning |
|---|---|
| `*` | current setting or state acknowledgement |
| `:` | receiver capability information |
| `$` | dynamic runtime message |
| `#` | command result or error |
| `[` | diagnostic text outside the control protocol |

Lines beginning with `[` may appear when diagnostics are enabled. A structured
protocol monitor must ignore them.

## Opening and closing a connection

Structured serial output is disabled after startup. Enable it with:

```text
ENABLE=1
```

Disable it with:

```text
ENABLE=0
```

The enable response is sent in this order:

```text
*ENABLE=1,v2.2.1,<chip type>/<chip firmware>
:MODE=3,3-3
*INTERVAL=<interval>
:FREQ=<count>,<index>:<kHz>,<channel>;...
*SERVICE=<index>                 only when a service is running
*TUNE=<DAB channel index>
$M=SLIDESHOW=0
```

The normal communication loop then publishes the current or changed `$L`,
`$I`, `$D`, `$S`, and, where applicable, `$M` messages.

Example:

```text
*ENABLE=1,v2.2.1,SI4684/4.0.5
:MODE=3,3-3
*INTERVAL=100
:FREQ=38,0:174928,5A;1:176640,5B;...
*TUNE=11
$M=SLIDESHOW=0
```

`ENABLE=0` returns:

```text
*ENABLE=0
```

While the connection is disabled, `ENABLE` is the only regular protocol
command that is processed. Other commands containing `=` are silently ignored.
The service commands `DEBUG`, `DEBUG SLSV`, `DEBUG SPI` and `DEBUG SPI STOP` remain available without an
active connection.

## Commands

### `ENABLE=<0|1>`

Enables or disables the structured protocol.

| Value | Result |
|---:|---|
| `0` | disable; response `*ENABLE=0` |
| `1` | enable and send the complete connection header |
| any other value | `#1` |

Sending `ENABLE=1` again while connected resends the connection header and
invalidates the dynamic output caches, causing the current state to be
published again.

### `INTERVAL=<1..500>`

Sets the `$S` message interval in milliseconds.

Command:

```text
INTERVAL=250
```

Response:

```text
*INTERVAL=250
#0
```

Firmware v2.2.1 does not accept zero; `INTERVAL=0` returns `#1`.

### `TUNE=<0..37>`

Tunes to a DAB Band III channel by its index in the advertised `:FREQ` table.

Command:

```text
TUNE=11
```

Response after accepting the request:

```text
#0
*TUNE=11
$M=SLIDESHOW=0
```

The command stops the current service, clears associated text data, and starts
asynchronous tuning. `#0` acknowledges that the request was accepted; it does
not mean that DAB lock was subsequently acquired.

The command returns `#1` while the receiver is in FM mode.

### `SERVICE=<index>`

Selects a service from the current `$L` list. The valid range is zero through
`COUNT - 1`.

Command:

```text
SERVICE=4
```

Response after accepting the request:

```text
#0
*SERVICE=4
$M=SLIDESHOW=0
```

Service startup is asynchronous. The command returns `#1` in FM mode or when
the index is outside the current service list.

## Diagnostic service commands

These commands do not contain `=` and are not part of the monitor data
interface.

### `DEBUG`

Toggles normal diagnostics. The response is:

```text
[DEBUG] diagnostics ON
```

or:

```text
[DEBUG] diagnostics OFF
```

Disabling normal diagnostics also disables verbose slideshow diagnostics.

### `DEBUG SLSV` or `DEBUG VERBOSE`

Enables normal diagnostics and toggles detailed MOT segment and JPEG scan
diagnostics. The response is:

```text
[DEBUG] SLS per-segment verbose ON
```

or the corresponding `OFF` message.

Diagnostics should normally be disabled while a machine-readable monitor is
connected because diagnostic and protocol lines share the same UART.

### DAB component discovery diagnostics

Enabling `DEBUG` queues the current inventory and any completed summary; it
does not restart or delay the UA discovery pass. `[DAB/SVC]` reports each
SID, P/D, component count and list version; `[DAB/COMP]` reports each exact
32-bit CID, TMID, ASCTy/DSCTy, DG, P/S, CA and UA-valid flags. Packet-mode
MOT additionally produces an `identity=unverified` candidate line. Neither
MOT transport nor UA-valid=0 proves the presence or absence of SPI.
Identical periodic service lists do not repeat the inventory dump; changed
version, P/D or CID topology invalidates the pass.

The independently paced summary reports the published result, for example:

```text
[DAB/INV] services=10 components=10 audio=10 data=0 packetMOT=0 invalid=0 version=7
[DAB/UA] scan complete services=10 components=10 processed=10 failed=0
[DAB/SPI] confirmed=0 packetMOTUnverified=0 status=not signalled selected=0 SID=00000000 CID=00000000 profile=unknown
```

`audio` and `data` count programme/data **services by P/D**, not component
transport modes. `processed` counts successfully parsed UA responses; `failed`
counts failed commands/invalid responses. A completed pass satisfies
`components == processed + failed`. Any failure yields `discovery incomplete`.
`not signalled` means SPI was not found in the available signalling, after a
complete error-free inventory/UA scan with no unverified packet-MOT component.
A malformed service list retains the last good station table, reports
`invalid=1` and `scan incomplete`, and blocks endpoint selection until a new
valid list arrives. Counts in that invalid-inventory report refer to the retained
last valid list; confirmed presence can also retain last valid evidence.

`confirmed` counts validated 0x007 advertisements. `packetMOTUnverified` counts
TMID=3/DSCTy=60 components without confirmed SPI identity. `selected=1` requires
an unambiguous endpoint and complete successful discovery. Confirmed presence
can therefore coexist with `selected=0` (ambiguous or incomplete). The final
asynchronous reply must be committed before publication. UART output is
independent: blocked logging never delays queries, presence or SLS evaluation.

Discovery runs quietly with DEBUG disabled. Diagnostic lines share 100 ms pacing
and a 256-byte UART headroom check. Detailed UA dumps use one best-effort slot;
while it is pending, newer diagnostic copies are skipped, but every UA answer
is processed. Retuning/reconfiguration discards stale dumps and summaries. Detection repeats throughout the DAB session:
60 seconds after each completed UA pass, including failed UA passes, another
low-priority pass is scheduled. Loss of lock, switching or a tracked SPI probe
postpones it until eligible. Invalid service-list structure requires a new valid
service list instead of a UA-only retry. Retuning, changed list/version and audio
selection invalidate in-flight UA ownership. Routine refreshes keep the last
confirmed icon color and do not repeat an unchanged inventory dump.

The main display shows only `EPG` below the codec using the existing cyan/grey
palette. Cyan means validated SPI signal was found, including cases where no
unique endpoint can be selected. Unverified packet-MOT keeps the icon grey.
The indicator retains the last known presence while rescanning or after an
unreadable update, and clears on retuning or a valid topology/version change.
It is hidden in FM and does not assert decoded programme data or schedule coverage.
For details and reproducible tests see
[the packet-component report](tests/DAB_SPI_packet_components_report.md).

For P/S verification, `CID=01FE0002` must report `PS=primary` (ETSI EN 300 401
section 6.3.1). CID and all other classification fields remain unchanged.
A corresponding synthetic `CID=01FC0002` fixture reports `PS=secondary`.

`DAB_XPAD_ENABLE` remains 0x0097. During DAB initialization only, a successful
SET_PROPERTY is followed by one GET_PROPERTY (250 ms timeout) using the existing
boot command API. DEBUG prints the cached result, not a new radio transaction:

```text
[DAB/XPAD] requested=0x0097 setResult=0 readback=0x0097 getResult=0 match=1 fw=6.0.9 rev=unverified
```

`setResult` and `getResult` are separate vendor Result codes; 0 means Ok, -4
means timeout. `setResult=127` means no attempt was recorded. A failed write
skips the read (`getResult=not-attempted`). Failed or absent reads print
`readback=unavailable` without `match`. Successful unequal reads print `match=0`.
Boot/recovery/hardware reset invalidates the cached result; FM never reads B400.
DEBUG toggles may repeat the cached diagnostic line, never the GET_PROPERTY.

`[DSRV/UA]` preserves the raw field and adds `meaning`, source name, routing
context, scan generation and suppressed-sample count. Raw 2/7 at source 0/1
prints SLS-candidate/SPI-candidate; source 2 prints DLS-raw regardless of raw UA,
and source 3 prints reserved. `context` reports audio/sls/spi/unmatched/inactive
using existing endpoint and audio-state rules. Raw interpretation does not
supply endpoint confirmation or change routing.

One line globally per five seconds within a scan generation is allowed, only
with DEBUG and at least 256 bytes of UART headroom. Suppressed counts saturate;
retune/new generation discards the old diagnostic window. No payload is retained
for later logging. `rev=unverified` remains because the firmware UATYPE contract
has not been established. See [the diagnostics report](tests/DAB_XPAD_UATYPE_diagnostics_report.md).

### `DEBUG SPI`

Enables diagnostics and requests one controlled SPI coexistence probe. Requires
stable DAB audio, a complete unambiguous SPI discovery result, an idle probe,
and SPI SID/CID distinct from both audio and the active standalone SLS context.
Shared-component SPI is refused until application-level MOT routing exists.
If discovery has not finished, wait for its result and send the command again;
the request is not silently queued for a later station.

The probe records 5 seconds of baseline DSRV traffic, then schedules a
low-priority START for SPI **without STOP for audio or SLS**. It observes traffic
for 30 seconds after successful START and schedules STOP for the probe's own
SPI SID/CID. SPI payload is counted and discarded, never passed to the SLS
collector. While the probe tracks a SPI context, audio/SLS/PAD packets use
the existing decoder only after matching SID/CID and DATA_SRC; other contexts
are drained/ignored. Outside the probe, the original SLS/DLS path is restored,
including its existing audio-switch and collector stale-MOT protections. An
unmatched diagnostic SID/CID alone does not discard normal SLS/DLS reception.
Failed probe cleanup retains strict routing until retune/reset.

With DEBUG enabled, `[DSRV/IDENTITY]` reports unmatched received SID/CID/source
alongside expected audio/SLS identities and validity flags, even without SPI.
`legacy=1` means the payload is forwarded to the original decoder (which still
applies its own checks); `legacy=0` means it is withheld. Output is limited to
one queued line per 5 seconds with a suppression count and UART-space check.
Hardware captured audio SID `00002FD9` with service-list CID `01FE002A`, while
both PAD (`src=1`) and DLS (`src=2`) report CID `0000002A`. For these two sources
only, an audio TMId=0 endpoint accepts its zero-extended low 16-bit component
field with the same SID. Full CID matches remain accepted. Nonzero mismatching
upper halves, different SID/component fields and unknown sources are rejected
by the router. Standalone SLS/SPI identities still require exact matching;
their short-CID representation has not been verified. START/STOP and other
component commands continue using the full original service-list CID.

`[DSRV/ROUTE]` output contains route, source, SID/CID, DSCTy, byte count, remaining
buffers and service state. Route values: 0 ignored, 1 audio/PAD, 2 standalone
SLS path, 3 SPI. Each route logs at most twice a second; suppressed lines are
counted. Summaries every 5 seconds compare baseline and observation packet
counts, report tuner SLS STOP notifications and CTS/overflow/UART-drop deltas.
Distinct packet traffic alone does not prove uninterrupted audio or decoded
slideshows. Missing packets are inconclusive because of carousel timing.

Changing station/multiplex, changing the component inventory or disabling DEBUG
cancels the probe. A START still awaiting CTS is reconciled before cleanup,
even after a new tune generation. Probe STOP takes priority over normal service
teardown; up to three attempts use cooperative 500 ms retry gaps, with DSRV
draining continuing between attempts. A NOT_AVAILABLE STOP response counts as
already stopped. An unanswered START also triggers conservative STOP.

If cleanup fails, the log says `cleanup uncertain`; the possible SPI context
remains quarantined and another probe is refused until a retune/reset. Normal
CTS recovery rules still apply. This probe does not restore a standalone SLS
service replaced by firmware: note the evidence and retune/reselect the station
after testing to restore normal service selection. No time-sharing fallback is
enabled and simultaneous standalone SLS/SPI remains unverified on FW 6.0.9.

### `DEBUG SPI STOP`

Requests early cancellation and cleanup of the current probe. It does not stop
audio or the SLS component. Like DEBUG SPI, it is available without ENABLE=1.

## Result codes

| Code | Current meaning |
|---|---|
| `#0` | an `INTERVAL`, `TUNE`, or `SERVICE` command was accepted |
| `#1` | value outside the accepted range, or receive-line overflow |
| `#2` | unknown command or command without the expected syntax |

`ENABLE` uses its own state response and does not emit `#0`.

### Current numeric parser behaviour

Firmware v2.2.1 uses `strtol()` without validating the complete input string.
A monitor must account for these properties of the current implementation:

- a non-numeric value is converted to zero;
- a numeric prefix followed by other characters is accepted, for example
  `TUNE=12abc` is interpreted as `12`;
- `TUNE=abc` can be accepted as `TUNE=0`;
- `SERVICE=abc` can select service 0 if that service exists;
- `ENABLE=abc` is interpreted as `ENABLE=0`.

The monitor must therefore validate every integer before transmitting it.

## State and dynamic messages

### Current channel

```text
*TUNE=<index>
```

Sent during connection setup and when the DAB channel is changed through the
protocol or local user interface.

### Current service

```text
*SERVICE=<index>
```

Sent during connection setup if a service is running, and when the running
service subsequently changes.

### Service list `$L`

```text
$L=COUNT=<count>,ENSEMBLE=<EID>,<name>;SERVICES=<index>,<type>,<name>;...
```

Example:

```text
$L=COUNT=3,ENSEMBLE=1234,Example DAB;SERVICES=0,4,Radio One;1,4,Radio Two;2,3,Data
```

The list is sent on the first communication pass after connection and then
only when monitored list data changes. At most 32 services are transmitted. If
the receiver has no signal lock, the services section is:

```text
SERVICES=0
```

Service types used by the firmware:

| Type | Meaning |
|---:|---|
| 0 | AUDIO STREAM SERVICE |
| 1 | DATA STREAM SERVICE |
| 2 | FIDC SERVICE |
| 3 | MSC DATA PACKET SERVICE |
| 4 | DAB+ |
| 5 | DAB |
| 6 | FIC SERVICE |
| 7 | XPAD DATA |
| 8 | NO MEDIA |

The `,`, `;`, and `=` delimiters are not escaped in names. A monitor must not
treat the complete line as unrestricted CSV data.

### Service information `$I`

```text
$I=ID=<component>;SID=<SID>;PTY=<pty>;PROTECTION=<protection>;SAMPLERATE=<Hz>;BITRATE=<kbps>;AUDIO=<mode>
```

If no service is running:

```text
$I=ID=0;SID=0;PTY=0;PROTECTION=0;SAMPLERATE=0;BITRATE=0;AUDIO=0
```

`ID` is the low byte of the selected service component ID. `$I` is sent after
connection and subsequently only when monitored values change.

Protection values:

| Value | Meaning |
|---:|---|
| 1–5 | UEP 1–5 |
| 6–9 | EEP A-1 through A-4 |
| 10–13 | EEP B-1 through B-4 |

Audio modes:

| Value | Meaning |
|---:|---|
| 0 | dual |
| 1 | mono |
| 2 | stereo |
| 3 | joint stereo |

### Dynamic Label/radiotext `$D`

```text
$D=RT=<UTF-8 text>
```

The current text is sent once after connection and again whenever it changes.
An empty text is valid:

```text
$D=RT=
```

The text is not escaped.

### Signal `$S`

```text
$S=SIGNAL=<decimal value>,LOCK=<0|1>,CNR=<value>,FIC=<0..100>
```

Example:

```text
$S=SIGNAL=43.9,LOCK=1,CNR=18,FIC=100
```

The message is periodic according to `INTERVAL`. `SIGNAL` is the internal
display signal level divided by ten, `LOCK` reports DAB synchronization, `CNR`
is the carrier-to-noise ratio, and `FIC` is FIC quality.

### Slideshow `$M`

```text
$M=SLIDESHOW=<type>
```

| Type | Meaning |
|---:|---|
| 0 | no slideshow is available, or it was invalidated by a channel/service change |
| 1 | a JPEG object is ready |
| 2 | a PNG object is ready |
| 3 | the object has an unknown or unsupported signature |

Firmware v2.2.1 does not transmit image bytes or Base64 data. This message only
announces the type of the locally received MOT object.

`$M=SLIDESHOW=0` is always sent during `ENABLE=1`. If no slideshow is available,
the current implementation may send the same line once more immediately. If
an image is available, its type is sent by the following normal communication
pass. A monitor must tolerate duplicate messages.

## DAB frequency table

The complete table is sent in the `:FREQ` line. Firmware v2.2.1 uses this
index mapping:

```text
0=174928/5A   1=176640/5B   2=178352/5C   3=180064/5D
4=181936/6A   5=183648/6B   6=185360/6C   7=187072/6D
8=188928/7A   9=190640/7B  10=192352/7C  11=194064/7D
12=195936/8A 13=197648/8B  14=199360/8C  15=201072/8D
16=202928/9A 17=204640/9B  18=206352/9C  19=208064/9D
20=209936/10A 21=211648/10B 22=213360/10C 23=215072/10D
24=216928/11A 25=218640/11B 26=220352/11C 27=222064/11D
28=223936/12A 29=225648/12B 30=227360/12C 31=229072/12D
32=230784/13A 33=232496/13B 34=234208/13C 35=235776/13D
36=237488/13E 37=239200/13F
```

Frequencies are expressed in kHz.

## FM limitations of the current protocol

Firmware v2.2.1 supports FM through its local user interface, but its serial
interface remains the original DAB protocol:

- `:MODE` always reports `3,3-3`;
- `:FREQ` always contains the DAB table;
- `TUNE` and `SERVICE` return `#1` in FM mode;
- no command is defined for FM frequency, region, or seek;
- no separate PI, PS, RDS/RBDS, stereo, blend, multipath, or AFC-rail messages
  are defined;
- periodic `$S` retains the original DAB-shaped format.

The receiver should be in DAB mode when controlled through this protocol.

## Monitor implementation recommendations

- Send `ENABLE=1` after opening the port and wait for `*ENABLE=1`.
- Do not derive a protocol version from application version `v2.2.1`; this
  firmware does not transmit a separate protocol version.
- Parse complete lines independently and tolerate asynchronous ordering of
  dynamic messages.
- Tolerate repeated state messages, particularly `$M=SLIDESHOW=0`.
- Ignore unknown capability lines beginning with `:` and diagnostic lines
  beginning with `[`.
- Do not assume that `#0` means signal lock or completed service startup.
- Validate command values in the monitor before transmitting them.
- Do not expect Base64 image data after an `$M` message; this firmware does not
  transmit it.

