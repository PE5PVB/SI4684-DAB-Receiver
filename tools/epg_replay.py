#!/usr/bin/env python3
"""Replay original Si4684 SPI/PKT+SPI/HEX captures, never decoded JSON.

The serial owner is this program; close PlatformIO/Arduino Monitor first.
Standard-library extraction/dry-run works without pyserial.
"""
import argparse
import binascii
from dataclasses import dataclass
from pathlib import Path
import queue
import math
from datetime import datetime
import re
import struct
import sys
import threading
import time
import zipfile

@dataclass
class Frame:
    number: int
    sid: int
    cid: int
    source: int
    payload: bytes

@dataclass
class Extraction:
    declared: int = 0
    complete: int = 0
    incomplete: int = 0
    crc_bad: int = 0
    malformed: int = 0


def input_bytes(path):
    path = Path(path)
    if path.suffix.lower() == '.zip':
        with zipfile.ZipFile(path) as z:
            files = [n for n in z.namelist() if n.lower().endswith(('.log', '.txt'))]
            if len(files) != 1:
                raise ValueError('ZIP must contain exactly one monitor log')
            return z.read(files[0])
    return path.read_bytes()


def extract(path, stats=None):
    stats = stats if stats is not None else Extraction()
    frames = []
    current = None
    def finish():
        if current is None:
            return
        number, sid, cid, declared, data, valid = current
        if not valid or len(data) != declared:
            stats.incomplete += 1
            return
        stats.complete += 1
        if len(data) < 11 or (binascii.crc_hqx(data[:-2], 0xffff) ^ 0xffff) != int.from_bytes(data[-2:], 'big'):
            stats.crc_bad += 1
            return
        frames.append(Frame(number, sid, cid, 0, bytes(data)))
    for line in input_bytes(path).decode('utf-8', errors='replace').splitlines():
        match = re.fullmatch(r'\[SPI/PKT\] #(\d+) ([0-9a-fA-F]+)/([0-9a-fA-F]+) L=(\d+) R=(\d+)', line)
        if match:
            finish()
            number, sid, cid, length = int(match[1]), int(match[2], 16), int(match[3], 16), int(match[4])
            stats.declared += 1
            current = [number, sid, cid, length, bytearray(), 0 < length <= 4071]
            continue
        if line.startswith('[SPI/PKT]'):
            finish(); current = None; stats.malformed += 1
        if line.startswith('[SPI/HEX'):
            match = re.fullmatch(r'\[SPI/HEX\] \+(\d+) ([0-9a-fA-F]+)', line)
            if current is None:
                continue
            if not match or int(match[1]) != len(current[4]) or len(match[2]) % 2:
                current[5] = False
                continue
            block = bytes.fromhex(match[2])
            if len(current[4]) + len(block) > current[3]:
                current[5] = False
            else:
                current[4].extend(block)
    finish()
    return frames


def fragment_lines(frame, session, first_sequence):
    """V1 ASCII framing: max 121 chars incl LF; 32-byte hex fragments.

    E1 session seq offset total sid cid source hex crc32
    CRC32 covers the ASCII prefix through hex, excluding space+CRC+LF.
    One outstanding fragment; duplicate sequence is ACKed without refeeding.
    """
    for offset in range(0, len(frame.payload), 32):
        seq = first_sequence + offset // 32
        body = (f'E1 {session:04X} {seq:08X} {offset:04X} {len(frame.payload):04X} '
                f'{frame.sid:08X} {frame.cid:08X} {frame.source:X} '
                f'{frame.payload[offset:offset+32].hex().upper()}')
        crc = binascii.crc32(body.encode('ascii'))
        line = f'{body} {crc:08X}\n'.encode('ascii')
        assert len(line) <= 127
        yield seq, line


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path, help='original monitor .zip/.log')
    p.add_argument('--port', help='Windows COM port, e.g. COM4')
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--speed', type=float, default=1.0, help='packet pace multiplier (1 = 20 packets/s maximum)')
    p.add_argument('--station', default='40e231533233', help='full bearer hex or unambiguous SID hex')
    p.add_argument('--time', default='2024-11-12T04:30:00+01:00', help='explicit historical virtual ISO time')
    p.add_argument('--loop', action='store_true')
    p.add_argument('--dry-run', action='store_true')
    args = p.parse_args(argv)
    if not math.isfinite(args.speed) or args.speed <= 0 or args.baud <= 0:
        p.error('speed and baud must be positive')
    try:
        virtual = datetime.fromisoformat(args.time.replace('Z', '+00:00'))
        if virtual.tzinfo is None: raise ValueError('offset required')
    except ValueError:
        p.error('--time must be ISO8601 with Z or explicit UTC offset')
    stats = Extraction()
    frames = extract(args.input, stats)
    print(f'Extraction: {stats}; CRC-valid complete frames={len(frames)}')
    if not frames:
        p.error('no complete CRC-valid frames')
    if args.dry_run:
        print(f'payload bytes={sum(len(f.payload) for f in frames)}, max={max(len(f.payload) for f in frames)}')
        return 0
    if not args.port:
        p.error('--port is required unless --dry-run')
    try:
        import serial
    except ImportError:
        p.error('install pyserial: python -m pip install -r tools/requirements.txt')
    # Avoid asserting DTR/RTS reset lines when taking ownership of an already running board.
    port = serial.Serial(port=None, baudrate=args.baud, timeout=0.05, write_timeout=2)
    port.dtr = False; port.rts = False; port.port = args.port
    port.open()
    received = queue.Queue(maxsize=256)
    commands = queue.Queue(maxsize=8)
    stop = threading.Event()
    def reader():
        pending = bytearray()
        while not stop.is_set():
            try:
                chunk = port.read(min(port.in_waiting, 256) or 1)
            except (OSError, serial.SerialException) as error:
                stop.set(); print(f'COM lost: {error}'); break
            for byte in chunk:
                if byte == 10:
                    line = pending.decode('utf-8', errors='replace').strip(); pending.clear()
                    if line.startswith(('EPG ACK ', 'EPG NACK ', 'EPG OK ', 'EPG ERROR ')):
                        try: received.put_nowait(line)
                        except queue.Full: stop.set()
                    if not line.startswith('EPG ACK '): print(line)
                elif len(pending) < 1024:
                    pending.append(byte)
                else:
                    pending.clear()
    def console():
        while not stop.is_set():
            try: line = input()
            except (EOFError, OSError): return
            try: commands.put_nowait(line)
            except queue.Full: print('Command queue full')
    threading.Thread(target=reader, daemon=True).start()
    threading.Thread(target=console, daemon=True).start()
    def send(line, expected):
        for attempt in range(4):
            if stop.is_set(): raise RuntimeError('serial reader stopped')
            port.write(line)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                try: response = received.get(timeout=0.1)
                except queue.Empty: continue
                if response.startswith(expected): return response
                if response.startswith('EPG ERROR'): raise RuntimeError(response)
                if response.startswith('EPG NACK'):
                    if 'BUSY' in response:
                        time.sleep(0.02); port.write(line); continue
                    raise RuntimeError(response)
            print(f'retry {attempt+1}: {expected}')
        raise RuntimeError('ACK timeout; stopping replay')
    try:
        # Full list identity is preserved when present in the original capture.
        raw_text = input_bytes(args.input).decode('utf-8', errors='replace')
        identities = re.findall(r'SID=([0-9A-Fa-f]{8}).*?CID=([0-9A-Fa-f]{8}) TMID=3', raw_text)
        full = next((int(cid,16) for sid,cid in identities if int(sid,16)==frames[0].sid and (int(cid,16)&65535)==frames[0].cid), None)
        if full is None: raise RuntimeError('no matching full packet CID in capture metadata')
        response = send(f'EPG TEST START {frames[0].sid:08X} {full:08X}\n'.encode(), 'EPG OK START')
        match = re.search(r'session=([0-9A-Fa-f]{4})', response)
        if not match: raise RuntimeError('receiver lacks V1 session handshake')
        session = int(match[1],16)
        send(f'EPG TEST TIME {args.time}\n'.encode(), 'EPG OK TIME')
        send(f'EPG TEST STATION {args.station}\n'.encode(), 'EPG OK STATION')
        sequence = 1
        while True:
            for i, frame in enumerate(frames):
                started = time.monotonic()
                while not commands.empty():
                    command = commands.get_nowait().strip()
                    if command.upper() in ('QUIT', 'EXIT', 'EPG TEST STOP'): return 0
                    # Console shares this owner and the same command/ACK discipline.
                    if not any(command.upper().startswith('EPG TEST '+name) for name in ('STATUS','HELP','TIME ','STATION ')):
                        print('During replay use STATUS, TIME, STATION, HELP, or quit.'); continue
                    send((command + '\n').encode(), 'EPG OK ')
                for seq, line in fragment_lines(frame, session, sequence):
                    send(line, f'EPG ACK {session:04X} {seq:08X}')
                    sequence = seq + 1
                if i % 100 == 0:
                    send(b'EPG TEST STATUS\n', 'EPG OK STATUS')
                    print(f'frames={i+1}/{len(frames)} sequence={sequence}')
                time.sleep(max(0, 0.05/args.speed - (time.monotonic()-started)))
            send(b'EPG TEST STATUS\n', 'EPG OK STATUS')
            if not args.loop: break
        print('Replay complete. Type EPG TEST STATUS/STOP, TIME/STATION, or quit.')
        heartbeat = time.monotonic()
        while not stop.is_set():
            try: command = commands.get(timeout=0.1)
            except queue.Empty:
                if time.monotonic()-heartbeat >= 10:
                    send(b'EPG TEST STATUS\n', 'EPG OK STATUS'); heartbeat=time.monotonic()
                continue
            if command.strip().upper() in ('QUIT', 'EXIT', 'EPG TEST STOP'): break
            send((command.strip()+'\n').encode(), 'EPG OK ')
    except (RuntimeError, OSError, serial.SerialException) as error:
        print(error, file=sys.stderr); return 1
    except KeyboardInterrupt:
        print('Interrupted')
    finally:
        try: port.write(b'EPG TEST STOP\n')
        except (OSError, serial.SerialException): pass
        stop.set(); port.close()
    return 0

if __name__ == '__main__':
    sys.exit(main())
