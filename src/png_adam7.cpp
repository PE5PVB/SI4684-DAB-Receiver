#include "png_adam7.h"
#include <PNGdec.h>
#include <string.h>
#include <new>
namespace {
const unsigned maxWidth=320, maxRow=320*8;
struct Workspace {
  inflate_state state;
  uint8_t dictionary[32768], lines[2][maxRow+4], input[512], palette[768], alpha[256];
  uint16_t pixels[maxWidth];
};
static_assert(sizeof(Workspace)+alignof(Workspace)-1<=76800,"Adam7 must fit the existing SLS decoder workspace");
uint32_t big(epg::Bytes p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
bool is(epg::Bytes p,const char* name) { return p.equals(reinterpret_cast<const uint8_t*>(name),4); }
struct Image {
  PngAdam7Info info;
  size_t idatBytes=0;
  unsigned channels=0,paletteCount=0;
  bool transparency=false;
  uint16_t transparent[3]={};
};
bool parse(epg::Bytes data,size_t size,Workspace& w,Image& image,unsigned width,unsigned height,PngAdam7Pump pump,void* context) {
  const uint8_t magic[]={137,80,78,71,13,10,26,10};
  if(!data || size<45 || !data.equals(magic,8)) return false;
  bool header=false,palette=false,trns=false,idat=false,idatEnded=false;
  memset(w.alpha,255,sizeof(w.alpha));
  for(size_t pos=8;pos<=size-12;) {
    const size_t length=big(data+pos);
    if(length>size-pos-12) return false;
    epg::Bytes tag=data+pos+4,p=data+pos+8;
    if(epg::crc32(tag,length+4)!=big(p+length)) return false;
    for(unsigned i=0;i<4;++i) if(!((tag[i]>='A' && tag[i]<='Z') || (tag[i]>='a' && tag[i]<='z'))) return false;
    if(tag[2]&32) return false;
    if(!header && !is(tag,"IHDR")) return false;
    if(is(tag,"IHDR")) {
      if(header || pos!=8 || length!=13 || p[10] || p[11] || p[12]!=1) return false;
      image.info.width=big(p); image.info.height=big(p+4); image.info.depth=p[8]; image.info.type=p[9];
      if(!image.info.width || !image.info.height || image.info.width>width || image.info.width>maxWidth || image.info.height>height || image.info.height>240) return false;
      switch(image.info.type) {
        case 0:image.channels=1;break; case 2:image.channels=3;break;
        case 3:image.channels=1;break; case 4:image.channels=2;break; case 6:image.channels=4;break;
        default:return false;
      }
      const unsigned d=image.info.depth;
      if(image.info.type==3 ? (d!=1 && d!=2 && d!=4 && d!=8) : image.info.type==0 ? (d!=1 && d!=2 && d!=4 && d!=8 && d!=16) : (d!=8 && d!=16)) return false;
      header=true;
    } else if(is(tag,"PLTE")) {
      if(palette || idat || trns || !length || length>768 || length%3 || image.info.type==0 || image.info.type==4) return false;
      image.paletteCount=unsigned(length/3);
      if(image.info.type==3 && image.paletteCount>(1U<<image.info.depth)) return false;
      p.copyTo(w.palette,length);palette=true;
    } else if(is(tag,"tRNS")) {
      if(trns || idat) return false;
      if(image.info.type==3) { if(!palette || !length || length>image.paletteCount) return false;p.copyTo(w.alpha,length); }
      else if(image.info.type==0 || image.info.type==2) {
        const unsigned count=image.info.type==0?1:3;
        if(length!=count*2) return false;
        for(unsigned i=0;i<count;++i) {
          image.transparent[i]=uint16_t(p[i*2])<<8|p[i*2+1];
          if(image.info.depth<16 && image.transparent[i]>=(1U<<image.info.depth)) return false;
        }
        image.transparency=true;
      } else return false;
      trns=true;
    } else if(is(tag,"IDAT")) {
      if(idatEnded || (image.info.type==3 && !palette)) return false;
      idat=true;image.idatBytes+=length;
    } else if(is(tag,"IEND")) {
      return idat && image.idatBytes && !length && pos+12==size;
    } else if(!(tag[0]&32)) return false;
    if(idat && !is(tag,"IDAT")) idatEnded=true;
    pos+=length+12;
    if(pump) pump(context);
  }
  return false;
}
struct Input {
  epg::Bytes data;size_t size,pos=8,remaining=0;
  Input(epg::Bytes bytes,size_t length) : data(bytes),size(length) {}
  bool fill(z_stream& stream,Workspace& w) {
    while(!remaining) {
      if(pos>size-12) return false;
      const size_t n=big(data+pos);
      if(is(data+pos+4,"IDAT")) { remaining=n;pos+=8; if(!n) pos+=4; }
      else pos+=n+12;
    }
    const size_t n=remaining<sizeof(w.input)?remaining:sizeof(w.input);
    (data+pos).copyTo(w.input,n);pos+=n;remaining-=n;if(!remaining) pos+=4;
    stream.next_in=w.input;stream.avail_in=unsigned(n);return true;
  }
};
voidpf noAllocate(voidpf,uInt,uInt) { return nullptr; }
void noFree(voidpf,voidpf) {}
struct Inflater {
  z_stream stream{}; Input input;Workspace& work;bool ended=false,valid=false;
  Inflater(epg::Bytes data,size_t size,Workspace& w) : input(data,size),work(w) {
    stream.state=reinterpret_cast<internal_state*>(&w.state);w.state.window=w.dictionary;
    stream.zalloc=noAllocate;stream.zfree=noFree;valid=inflateInit(&stream)==Z_OK;
  }
  ~Inflater() { if(valid) inflateEnd(&stream); }
  bool exact(uint8_t* out,unsigned bytes) {
    if(!valid || ended) return false;
    // Keep output below inflate_fast's 258-byte threshold. PNGdec's
    // optimized bulk-copy path is unnecessary for small Adam7 pass rows.
    while(bytes) {
      const unsigned part=bytes<128?bytes:128;
      stream.next_out=out;stream.avail_out=part;
      while(stream.avail_out) {
        if(!stream.avail_in && !input.fill(stream,work)) return false;
        const unsigned beforeIn=stream.avail_in,beforeOut=stream.avail_out;
        const int rc=inflate(&stream,Z_NO_FLUSH,1);
        if(rc==Z_STREAM_END) { ended=true;return !stream.avail_out && bytes==part; }
        if(rc!=Z_OK && rc!=Z_BUF_ERROR) return false;
        if(beforeIn==stream.avail_in && beforeOut==stream.avail_out) return false;
      }
      out+=part;bytes-=part;
    }
    return true;
  }
  bool finish(size_t expectedBytes) {
    if(!ended) {
      uint8_t extra=0;
      // One-byte output space consumes the zlib trailer; any output is excess image data.
      stream.next_out=&extra;stream.avail_out=1;
      while(!ended) {
        if(!stream.avail_in && !input.fill(stream,work)) return false;
        const unsigned beforeIn=stream.avail_in;
        const int rc=inflate(&stream,Z_NO_FLUSH,1);
        if(!stream.avail_out) return false;
        if(rc==Z_STREAM_END) ended=true;
        else if((rc!=Z_OK && rc!=Z_BUF_ERROR) || beforeIn==stream.avail_in) return false;
      }
    }
    return stream.total_in==expectedBytes;
  }
};
unsigned paeth(unsigned a,unsigned b,unsigned c) {
  const int p=int(a)+int(b)-int(c);
  const unsigned x=unsigned(p>int(a)?p-int(a):int(a)-p), y=unsigned(p>int(b)?p-int(b):int(b)-p), z=unsigned(p>int(c)?p-int(c):int(c)-p);
  return x<=y && x<=z?a:y<=z?b:c;
}
bool unfilter(uint8_t* line,const uint8_t* previous,unsigned bytes,unsigned stride) {
  const unsigned filter=line[0];if(filter>4) return false;
  for(unsigned i=0;i<bytes;++i) {
    const unsigned left=i>=stride?line[1+i-stride]:0, up=previous[1+i], corner=i>=stride?previous[1+i-stride]:0;
    const unsigned predictor=filter==1?left:filter==2?up:filter==3?(left+up)/2:filter==4?paeth(left,up,corner):0;
    line[i+1]=uint8_t(line[i+1]+predictor);
  }
  return true;
}
unsigned sample(const uint8_t* p,unsigned index,unsigned depth) {
  if(depth==16) return unsigned(p[index*2])<<8|p[index*2+1];
  if(depth==8) return p[index];
  const unsigned bit=index*depth;return (p[bit/8]>>(8-depth-bit%8))&((1U<<depth)-1);
}
unsigned eight(unsigned value,unsigned depth) { return depth==16?value>>8:depth==8?value:value*255/((1U<<depth)-1); }
bool pixel(const uint8_t* p,unsigned x,const Image& image,const Workspace& w,uint16_t& out) {
  unsigned v[4]={};for(unsigned i=0;i<image.channels;++i) v[i]=sample(p,x*image.channels+i,image.info.depth);
  unsigned r,g,b,a=255;
  if(image.info.type==3) {
    if(v[0]>=image.paletteCount) return false;
    r=w.palette[v[0]*3];g=w.palette[v[0]*3+1];b=w.palette[v[0]*3+2];a=w.alpha[v[0]];
  } else {
    r=eight(v[0],image.info.depth);
    if(image.info.type==0 || image.info.type==4) { g=b=r;if(image.info.type==4) a=eight(v[1],image.info.depth); }
    else { g=eight(v[1],image.info.depth);b=eight(v[2],image.info.depth);if(image.info.type==6) a=eight(v[3],image.info.depth); }
    if(image.transparency) {
      bool transparent=true;for(unsigned i=0;i<(image.info.type==0?1U:3U);++i) if(v[i]!=image.transparent[i]) transparent=false;
      if(transparent) a=0;
    }
  }
  r=(r*a+255*(255-a)+127)/255;g=(g*a+255*(255-a)+127)/255;b=(b*a+255*(255-a)+127)/255;
  out=uint16_t((r>>3)<<11|(g>>2)<<5|(b>>3));return true;
}
}
size_t PngAdam7WorkspaceBytes() { return sizeof(Workspace); }
bool PngAdam7Decode(epg::Bytes data,size_t size,void* workspace,size_t capacity,
                   unsigned maxW,unsigned maxH,PngAdam7Info& info,
                   PngAdam7Row row,void* context,PngAdam7Pump pump) {
  info=PngAdam7Info{};info.stage="workspace";
  if(!workspace) return false;
  const size_t padding=(alignof(Workspace)-uintptr_t(workspace)%alignof(Workspace))%alignof(Workspace);
  info.padding=unsigned(padding);
  if(padding>capacity || capacity-padding<sizeof(Workspace)) return false;
  // ESP32 heap blocks may be 4-byte aligned while inflate_state contains an
  // 8-byte aligned accumulator. Use spare bytes inside the shared arena.
  void* aligned=static_cast<uint8_t*>(workspace)+padding;
  Workspace& w=*new(aligned) Workspace{};Image image;
  const bool parsed=parse(data,size,w,image,maxW,maxH,pump,context);
  image.info.padding=unsigned(padding);
  info=image.info;info.stage="header";if(!parsed) return false;
  info.stage="inflate-init";
  Inflater inflater(data,size,w);if(!inflater.valid) return false;
  const unsigned x0[]={0,4,0,2,0,1,0},y0[]={0,0,4,0,2,0,1},dx[]={8,8,4,4,2,2,1},dy[]={8,8,8,4,4,2,2};
  const unsigned bits=image.channels*image.info.depth,stride=(bits+7)/8;
  for(unsigned pass=0;pass<7;++pass) {
    if(image.info.width<=x0[pass] || image.info.height<=y0[pass]) continue;
    const unsigned width=(image.info.width-x0[pass]+dx[pass]-1)/dx[pass];
    const unsigned pitch=(width*bits+7)/8;if(pitch>maxRow) return false;
    uint8_t* current=w.lines[0];uint8_t* previous=w.lines[1];memset(previous,0,pitch+1);
    for(unsigned y=y0[pass];y<image.info.height;y+=dy[pass]) {
      info.pass=pass+1;info.row=y;info.stage="inflate";
      const bool inflated=inflater.exact(current,pitch+1);
      info.input=unsigned(inflater.stream.total_in);info.output=unsigned(inflater.stream.total_out);
      info.detail=inflater.stream.msg?inflater.stream.msg:"";
      if(!inflated) return false;
      info.stage="filter";if(!unfilter(current,previous,pitch,stride)) return false;
      info.stage="pixels";
      for(unsigned x=0;x<width;++x) if(!pixel(current+1,x,image,w,w.pixels[x])) return false;
      info.stage="draw-callback";
      if(row && !row(context,x0[pass],y,dx[pass],w.pixels,width)) return false;
      if(pump) pump(context);
      uint8_t* swap=previous;previous=current;current=swap;
    }
  }
  info.stage="finish";
  const bool finished=inflater.finish(image.idatBytes);
  info.input=unsigned(inflater.stream.total_in);info.output=unsigned(inflater.stream.total_out);
  info.detail=inflater.stream.msg?inflater.stream.msg:"";
  if(!finished) return false;
  info.stage="ok";return true;
}
