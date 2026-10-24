#pragma once
#include "epg_core.h"
struct PngAdam7Info {
  unsigned width=0,height=0,depth=0,type=0;
  const char* stage="entry";
  const char* detail="";
  unsigned pass=0,row=0,input=0,output=0,padding=0;
};
typedef bool (*PngAdam7Row)(void*,unsigned x,unsigned y,unsigned step,const uint16_t* pixels,unsigned count);
typedef void (*PngAdam7Pump)(void*);
// Uses the project's PNGdec inflate engine and caller-owned aligned DRAM only.
// A null row callback validates without drawing. No heap or framebuffer.
// An unaligned caller buffer is aligned internally, reducing usable capacity
// by up to alignof(inflate_state)-1 bytes. WorkspaceBytes() is the aligned size.
size_t PngAdam7WorkspaceBytes();
bool PngAdam7Decode(epg::Bytes input,size_t size,void* workspace,size_t capacity,
                   unsigned maxWidth,unsigned maxHeight,PngAdam7Info& info,
                   PngAdam7Row row=nullptr,void* context=nullptr,PngAdam7Pump pump=nullptr);
