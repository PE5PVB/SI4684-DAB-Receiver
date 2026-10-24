"""Replay synthetic DSRV replies through the receiver's isolated E2 test input."""
from pathlib import Path
import argparse,binascii,json,re,struct,sys,time

ROOT=Path(__file__).resolve().parents[1]
SID=0x3201
CID=0x01FE0005
PACKET_SID=0xE23153FE
PACKET_CID=0x01F2C00C
WHEN='2024-11-12T01:01:00Z'

def groups(path):
    data=path.read_bytes();result=[];pos=0
    while pos<len(data):
        if pos+2>len(data):raise ValueError('Truncated fixture length')
        n=struct.unpack_from('<H',data,pos)[0];pos+=2
        if not n or n>4047 or pos+n>len(data):raise ValueError('Invalid fixture group')
        result.append(data[pos:pos+n]);pos+=n
    if len(result)<4:raise ValueError('Missing directory/SI/PI fixtures')
    return result

def wire(payload,path='PAD',sid=None,cid=None,ua=7,source=None,state=0):
    pad=path=='PAD'
    h=bytearray(24);h[0]=128;h[6]=state;h[7]=((int(pad) if source is None else source)<<6)|60
    struct.pack_into('<IIHH',h,8,SID if pad and sid is None else PACKET_SID if sid is None else sid,
                     CID if pad and cid is None else PACKET_CID if cid is None else cid,ua,len(payload))
    return bytes(h)+payload

class Client:
    def __init__(self,port,log=None):
        self.port=port;self.log=log;self.lines=[];self.session=0;self.sequence=1;self.path='PAD'

    def line(self,timeout=3):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            raw=self.port.readline()
            if not raw:continue
            value=raw.decode('utf-8','replace').strip()
            if not value:continue
            self.lines.append(value)
            if self.log:self.log.write(value+'\n');self.log.flush()
            if not value.startswith('EPG ACK '):print(value,flush=True)
            return value
        raise TimeoutError('Receiver response timed out')

    def command(self,value,expected):
        self.port.write((value+'\n').encode())
        end=time.monotonic()+5
        while time.monotonic()<end:
            answer=self.line()
            if answer.startswith(expected):return answer
            if answer.startswith(('EPG ERROR','EPG NACK')):raise RuntimeError(answer)
        raise TimeoutError(expected)

    def begin(self,path='PAD',confirmed=True):
        command='EPG TEST INJECT '+path+(' NOSPI' if path=='PAD' and not confirmed else '')
        answer=self.command(command,'EPG OK INJECT V2')
        self.session=int(re.search(r'session=([0-9A-Fa-f]+)',answer)[1],16)
        self.sequence=1;self.path=path
        self.command('EPG TEST TIME '+WHEN,'EPG OK TIME')
        self.command('EPG TEST STATION 40e231533201','EPG OK STATION')

    def send(self,data,negative=False):
        pad=self.path=='PAD';sid=SID if pad else PACKET_SID;cid=CID if pad else PACKET_CID
        if len(data)>4071:raise ValueError('E2 frame too large')
        for offset in range(0,len(data),32):
            body=f'E2 {self.session:04X} {self.sequence:08X} {offset:04X} {len(data):04X} {sid:08X} {cid:08X} {int(pad):X} {data[offset:offset+32].hex().upper()}'
            line=f'{body} {binascii.crc32(body.encode()):08X}\n'.encode()
            for attempt in range(100):
                self.port.write(line)
                while True:
                    answer=self.line()
                    if answer.startswith('EPG ACK '):
                        if answer!=f'EPG ACK {self.session:04X} {self.sequence:08X}':raise RuntimeError(answer)
                        break
                    if answer.startswith('EPG NACK BUSY'):break
                    if answer.startswith('EPG NACK DATA') and negative and offset+32>=len(data):return
                    if answer.startswith(('EPG ERROR','EPG NACK')):raise RuntimeError(answer)
                if answer.startswith('EPG ACK '):break
                time.sleep(.01)
            else:raise TimeoutError('Receiver remained busy')
            self.sequence+=1
        if negative:raise AssertionError('Invalid DSRV input unexpectedly accepted')

    def status(self):
        start=len(self.lines);self.command('EPG TEST STATUS','EPG OK STATUS')
        while not self.line().startswith('EPG INJECT '):pass
        text='\n'.join(self.lines[start:])
        if not 'UARTdrop=0 rxCRC=0 rxOrder=0 rxIdentity=0' in text:raise AssertionError(text)
        return {k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',text)}

def loaded(client,data,path='PAD',reverse=False):
    client.begin(path)
    order=data[:2]+list(reversed(data[2:])) if reverse else data
    for i,group in enumerate(order):client.send(wire(group,path,cid=(5 if path=='PAD' else 0xC00C) if i&1 else None))
    s=client.status()
    assert s['cached']==3 and s['PI']==1 and s['SI']==1 and s['dirs']==1 and s['now']==1 and s['next']==2,s
    return s

def suite(client,data):
    results=[]
    for path in ('MSC','PAD'):
        s=loaded(client,data,path,reverse=path=='PAD');results.append({'case':path,'cached':s['cached'],'pass':True})
    # Duplicate carousel, cached date return and suppressed packet data under PAD.
    for group in data:client.send(wire(group))
    client.send(wire(data[0],'MSC'))
    client.send(wire(data[0],ua=2))
    client.send(wire(data[0],source=2))
    s=client.status();assert s['cached']==3 and s['PI']==1 and s['blocked']==1 and s['legacy']==2,s
    frames=s['frames']
    client.command('EPG TEST TIME 2024-11-13T01:01:00Z','EPG OK TIME');assert client.status()['cached']==0
    client.command('EPG TEST TIME '+WHEN,'EPG OK TIME');s=client.status()
    assert s['cached']==3 and s['frames']==frames,s
    results.append({'case':'duplicates/cache/packet-isolation/SLS-DLS-handoff','pass':True})
    cases=[('unconfirmed-UA',wire(data[0]),False),
           ('foreign-SID',wire(data[0],sid=SID+1),True),
           ('foreign-CID',wire(data[0],cid=6),True),
           ('DSRV-error',wire(data[0],state=4),True),
           ('short-header',wire(data[0])[:23],True),
           ('bad-MSC-CRC',wire(data[0][:-1]+bytes([data[0][-1]^1])),True)]
    for name,value,confirmed in cases:
        client.begin('PAD',confirmed);client.send(value,negative=True);s=client.status()
        assert s['cached']==0 and s['rejected']==1,s
        if name=='bad-MSC-CRC':assert s['crcBad']==1,s
        results.append({'case':name,'pass':True})
    loaded(client,data,'PAD')
    print('SYNTHETIC SUITE PASS: MSC and PAD, negative routing, CRC, isolation and cache return.',flush=True)
    return results

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',required=True);p.add_argument('--baud',type=int,default=115200)
    p.add_argument('--input',type=Path,default=ROOT/'.pio/epg_synthetic_paths/groups.bin')
    p.add_argument('--loop',action='store_true');p.add_argument('--report',type=Path,default=ROOT/'.pio/epg_acceptance/hardware_synthetic_dsrv.json')
    args=p.parse_args();data=groups(args.input)
    import serial
    args.report.parent.mkdir(parents=True,exist_ok=True)
    port=serial.Serial(port=None,baudrate=args.baud,timeout=.1,write_timeout=2)
    port.dtr=False;port.rts=False;port.port=args.port
    with args.report.with_suffix('.log').open('w',encoding='utf-8') as log:
        port.open();client=Client(port,log)
        try:
            results=suite(client,data)
            args.report.write_text(json.dumps({'port':args.port,'kind':'MCU synthetic DSRV, not RF','results':results,'final':'PAD synthetic 3 programmes','loop':args.loop},indent=2)+'\n',encoding='utf-8')
            if args.loop:
                print('PAD synthetic data loop active; Ctrl+C stops and restores LIVE.',flush=True)
                while True:
                    for group in data:client.send(wire(group));time.sleep(.1)
                    client.status();time.sleep(1)
        except KeyboardInterrupt:pass
        finally:
            try:port.write(b'EPG TEST STOP\n')
            finally:port.close()

if __name__=='__main__':
    sys.stdout.reconfigure(encoding='utf-8',errors='backslashreplace')
    main()
