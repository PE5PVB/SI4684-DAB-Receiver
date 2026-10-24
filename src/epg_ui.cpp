#include "epg_ui.h"
#include "epg_runtime.h"
#include "gui.h"
#include "slideshow.h"
#include <stdio.h>
#include <string.h>
namespace {
bool visible=false, suspended=false, dirty=true, fallback=false;
unsigned page=0, selected=0, scroll=0, maxScroll=0;
uint32_t revision=0, renderedMinute=0;
unsigned renderedPage=3, renderedSelected=0, renderedFirst=0, renderedLanguage=8;
int renderedDay=0;
constexpr int listX=8, listTop=34, rowHeight=20;
constexpr int titleX=64, titleWidth=232;
const char* text(EpgText id) { return epgText[language<8?language:0][id]; }
void restore(int x,int y,int width,int height,const uint16_t* background) {
  for(int row=0;row<height;++row)
    tft.pushImage(x,y+row,width,1,background+(y+row)*320+x);
}
void displayText(const char* in,char* out,size_t capacity,bool multiline=false);
void waiting(const char* message) {
  char value[128]; displayText(message,value,sizeof(value));
  // The text area in the shared 270x50 status panel is 210 pixels wide.
  tftPrintFixed(-1,"",76,62,PrimaryColor,BackgroundColor3,16);
  size_t n=strlen(value);
  while(n && tft.textWidth(value)>210) {
    do { --n; } while(n && (uint8_t(value[n])&192)==128);
    value[n]=0;
  }
  DrawEpgWaitingPanel(value);
}
// Do not split UTF-8 when fitting, wrapping or substituting a missing glyph.
size_t character(const char* p) {
  const uint8_t c=uint8_t(*p); return c<128?1:(c&224)==192?2:(c&240)==224?3:(c&248)==240?4:1;
}
bool glyph(uint32_t code) {
  if(code>=32 && code<127) return true;
  const uint32_t count=(uint32_t(pgm_read_byte(EpgFont16()))<<24)|(uint32_t(pgm_read_byte(EpgFont16()+1))<<16)|(uint32_t(pgm_read_byte(EpgFont16()+2))<<8)|pgm_read_byte(EpgFont16()+3);
  for(uint32_t i=0;i<count;++i) {
    const uint8_t* p=EpgFont16()+24+i*28;
    const uint32_t c=(uint32_t(pgm_read_byte(p))<<24)|(uint32_t(pgm_read_byte(p+1))<<16)|(uint32_t(pgm_read_byte(p+2))<<8)|pgm_read_byte(p+3);
    if(c==code) return true;
  }
  return false;
}
void displayText(const char* in,char* out,size_t capacity,bool multiline) {
  size_t used=0;
  while(*in) {
    // Layout controls are not font glyphs. Preserve paragraph boundaries for
    // wrapping, and use spaces for single-line fields and horizontal tabs.
    if(*in=='\n' || *in=='\r' || *in=='\t') {
      if(used+1>=capacity) break;
      out[used++]=multiline && *in!='\t'?'\n':' ';
      if(*in=='\r' && in[1]=='\n') ++in;
      ++in; continue;
    }
    const size_t n=character(in);
    if(strlen(in)<n) break;
    uint32_t code=uint8_t(*in)&(n==1?127:n==2?31:n==3?15:7);
    for(size_t i=1;i<n;++i) code=(code<<6)|(uint8_t(in[i])&63);
    if(glyph(code)) { if(used+n>=capacity) break; memcpy(out+used,in,n); used+=n; }
    else { if(used+1>=capacity) break; out[used++]='?'; fallback=true; }
    in+=n;
  }
  out[used]=0;
}
void line(const char* text,int x,int y,int width,int color) {
  char value[256]; displayText(text,value,sizeof(value));
  tftPrintFixed(-1,"",x,y,color,BackgroundColor3,16); // load existing font
  size_t n=strlen(value);
  while(n && tft.textWidth(value)>width) {
    do { --n; } while(n && (uint8_t(value[n])&192)==128);
    value[n]=0;
  }
  tftPrintFixed(-1,value,x,y,color,BackgroundColor3,16);
}
void clockText(uint32_t utc,int16_t offset,char* out,size_t n) {
  const uint32_t civilSeconds=uint32_t(int64_t(utc)+int32_t(offset)*60);
  snprintf(out,n,"%02u:%02u",unsigned((civilSeconds/3600)%24),unsigned((civilSeconds/60)%60));
}
void interval(const epg::Core& c,size_t index,char* out,size_t n) {
  char first[8],last[8]; const auto& p=c.at(index);
  clockText(p.start,p.lto,first,sizeof(first)); clockText(p.start+p.duration,p.lto,last,sizeof(last));
  const bool overnight=(int64_t(p.start)+p.lto*60)/86400!=(int64_t(p.start)+p.duration+p.lto*60)/86400;
  snprintf(out,n,"%s - %s%s / %u min%s",first,last,overnight?"+1":"",unsigned(p.duration/60),p.uncertain?" ?":"");
}
unsigned wrapped(const char* text,unsigned skip,unsigned lines,int top,int color) {
  char safe[640]; displayText(text,safe,sizeof(safe),true);
  const char* pos=safe; unsigned index=0;
  while(*pos) {
    if(*pos=='\n') { ++pos; ++index; continue; }
    char row[192]; size_t n=0, word=0;
    while(pos[n] && pos[n]!='\n' && n+5<sizeof(row)) {
      const size_t bytes=character(pos+n);
      memcpy(row+n,pos+n,bytes); n+=bytes; row[n]=0;
      if(tft.textWidth(row)>296) { n-=bytes; break; }
      if(pos[n-1]==' ') word=n;
    }
    if(!n) n=character(pos);
    if(pos[n] && pos[n]!='\n' && word>n/2) n=word;
    memcpy(row,pos,n); row[n]=0;
    if(index>=skip && index<skip+lines) line(row,12,top+int(index-skip)*20,296,color);
    pos+=n; while(*pos==' ') ++pos;
    if(*pos=='\n') ++pos;
    ++index;
  }
  return index;
}
}
bool EpgVisible() { return visible; }
void EpgInvalidate() { dirty=true; }
void EpgOpen() {
  if(menu || tuning || seek || (radio.isFm() && !epg::replayActive())) return;
  visible=true; dirty=true; page=selected=scroll=0;
  ChannelListView=DabGlobalListView=SlideShowView=ShowServiceInformation=false;
  slsWaitingView=slsWaitingOverlayVisible=setvolume=false;
  memorystore=false; rotary=rotary2=0;
  epg::ensure(); EpgDraw(true);
}
void EpgClose(bool redraw) { suspended=false; if(!visible) return; visible=false; dirty=true; if(redraw) BuildDisplay(); }
void EpgSuspend() { if(visible) { visible=false; suspended=true; setvolume=false; } }
void EpgResume() { if(suspended) { suspended=false; if(!radio.isFm() || epg::replayActive()) { visible=true; dirty=true; } } }
void EpgToggle() { if(visible) EpgClose(); else EpgOpen(); }
void EpgBack() {
  if(!visible) return;
  if(!page) { EpgClose(); return; }
  --page; scroll=0; dirty=true;
  if(!page && epg::runtime()) epg::runtime()->browseDay(-epg::runtime()->dayOffset());
}
void EpgClick() {
  if(!visible) return;
  if(page==2) { page=1; scroll=0; }
  else { ++page; if(page==1 && epg::runtime()) { const int current=epg::runtime()->current(); selected=current>=0?unsigned(current):0; } }
  dirty=true;
}
void EpgMove(int step,bool second) {
  if(!visible) return;
  epg::Core* c=epg::runtime();
  if(!c) return;
  if(second && page==1) { c->browseDay(step); selected=scroll=0; }
  else if((second && page==2) || (!second && page==1)) {
    const int next=int(selected)+step; if(next>=0 && next<int(c->count())) selected=unsigned(next);
    scroll=0;
  } else if(!second && page==2) { const int next=int(scroll)+step; if(next>=0 && next<=int(maxScroll)) scroll=unsigned(next); }
  dirty=true;
}
void EpgDraw(bool force) {
  if(!visible || menu) return;
  epg::Core* c=epg::runtime();
  if(!force && !dirty && renderedLanguage==language && (!c || (revision==c->revision && renderedMinute==c->now()/60))) return;
  // Keep volume stable; its closing path requests one complete EPG repaint.
  if(setvolume) { dirty=true; return; }
  static uint32_t renderedAt=0;
  if(!force && uint32_t(millis()-renderedAt)<150U) return;
  renderedAt=millis();
  const bool full=force || page!=1 || renderedPage!=page || renderedLanguage!=language;
  const bool dataChanged=!c || revision!=c->revision || renderedDay!=c->dayOffset();
  const bool footerChanged=full || dataChanged || (c && renderedMinute!=c->now()/60);
  const unsigned oldSelected=renderedSelected, oldFirst=renderedFirst;
  dirty=false; fallback=false;
  if(c) { revision=c->revision; renderedMinute=c->now()/60; if(selected>=c->count()) selected=c->count()?unsigned(c->count()-1):0; }
  const uint16_t* bg=EpgBackground(page==1);
  if(full) {
    tft.pushImage(0,0,320,240,bg);
    const char* title=text(page==0?EPG_OVERVIEW:page==1?EPG_PROGRAM:EPG_DETAIL);
    // Preserve the usual heading size; fit longer translated headings.
    tftPrintFixed(0,"",160,4,PrimaryColor,PrimaryColorSmooth,28);
    const unsigned size=tft.textWidth(title)<=304?28:16;
    tftPrintFixed(0,title,160,4,PrimaryColor,PrimaryColorSmooth,size);
  }
  renderedPage=page; renderedLanguage=language;
  renderedSelected=selected; renderedFirst=selected/9*9;
  if(c) renderedDay=c->dayOffset();
  const epg::Service* station=c?c->station():nullptr;
  const char* name=station?station->name:epg::replayActive()?text(EPG_WAIT_SI):_serviceName;
  char info[128];
  if(c) {
    int year; unsigned month,day; epg::calendar(c->localDay(),year,month,day);
    snprintf(info,sizeof(info),"%s %02u.%02u.%04d%s%s%s%s",epg::replayActive()?"TEST":"EPG",day,month,year,c->limited?" ":"",c->limited?text(EPG_LIMIT):"",c->ambiguous()?" ":"",c->ambiguous()?text(EPG_AMBIGUOUS):"");
  } else snprintf(info,sizeof(info),"%s",text(EPG_MEMORY));
  if(c && !c->now()) snprintf(info,sizeof(info),"%s / %s",epg::replayActive()?"TEST":"EPG",text(EPG_NO_TIME));
  if(footerChanged) {
    if(!full) restore(0,220,320,20,bg);
    line(info,8,222,304,SecondaryColor);
  }
  if(!c) { waiting(text(EPG_MEMORY)); return; }
  if(page==1) {
    const unsigned first=selected/9*9;
    // Reuse FONT16 loaded at startup: the main screen shares this sprite.
    FullLineSprite.setTextDatum(TL_DATUM);
    for(unsigned row=0;row<9;++row) {
      const unsigned index=first+row; const int y=listTop+row*rowHeight;
      if(!full && !dataChanged && first==oldFirst &&
         (selected==oldSelected || (index!=selected && index!=oldSelected))) continue;
      FullLineSprite.pushImage(-listX,-y,320,240,bg);
      if(index<c->count() && index==selected) FullLineSprite.pushImage(0,0,304,rowHeight,EpgSelector());
      if(index<c->count()) {
        char clock[8],value[128]; clockText(c->at(index).start,c->at(index).lto,clock,sizeof(clock));
        displayText(c->title(index),value,sizeof(value));
        size_t len=strlen(value);
        while(len && FullLineSprite.textWidth(value)>titleWidth) { do { --len; } while(len && (uint8_t(value[len])&192)==128); value[len]=0; }
        FullLineSprite.setTextColor(SecondaryColor,SecondaryColorSmooth,false);
        FullLineSprite.drawString(clock,8,3);
        FullLineSprite.setTextColor(PrimaryColor,PrimaryColorSmooth,false);
        FullLineSprite.drawString(value,titleX,3);
      }
      FullLineSprite.pushSprite(listX,y);
    }
    if(!c->count() && (full || dataChanged || first!=oldFirst)) waiting(text(c->ambiguous()?EPG_AMBIGUOUS:EPG_WAIT_PROGRAM));
  } else if(page==2) {
    line(name,12,36,200,SecondaryColor);
    if(c->count()) {
      int year; unsigned month,day;
      epg::calendar(uint32_t(int64_t(c->at(selected).start)+c->at(selected).lto*60),year,month,day);
      snprintf(info,sizeof(info),"%02u.%02u.%04d",day,month,year); line(info,218,36,90,SecondaryColor);
      interval(*c,selected,info,sizeof(info)); line(info,12,58,296,ActiveColor);
      const bool actualPresent=c->at(selected).actual!=0;
      if(actualPresent) { char actual[8]; clockText(c->at(selected).actual,c->at(selected).lto,actual,sizeof(actual)); snprintf(info,sizeof(info),text(EPG_ACTUAL),actual); line(info,12,78,296,SecondaryColor); }
      char detail[640]; snprintf(detail,sizeof(detail),"%s\n%s",c->title(selected),c->description(selected));
      const unsigned rows=actualPresent?5:6;
      const unsigned total=wrapped(detail,scroll,rows,actualPresent?100:80,PrimaryColor);
      maxScroll=total>rows?total-rows:0;
    } else waiting(text(EPG_WAIT_PROGRAM));
  } else {
    line(name,118,38,190,PrimaryColor);
    char clock[8]; clockText(c->now(),c->offset(),clock,sizeof(clock)); line(c->now()?clock:text(EPG_NO_TIME),118,63,190,SecondaryColor);
    if(c->logo()) DrawEpgLogo(c->logo(),c->logoSize(),12,34,100,70);
    bool overlap=false; const int current=c->current(&overlap), next=c->next();
    line(text(c->ambiguous()?EPG_AMBIGUOUS:overlap?EPG_OVERLAP:EPG_NOW),12,110,296,ActiveColor);
    if(current>=0) {
      wrapped(c->title(current),0,2,130,PrimaryColor);
      interval(*c,current,info,sizeof(info)); line(info,12,171,200,SecondaryColor);
      const auto& p=c->at(current); const unsigned percent=unsigned((uint64_t(c->now()-p.start)*100)/p.duration);
      tft.drawRect(222,174,85,7,SecondaryColor); tft.fillRect(223,175,int(percent*83/100),5,ActiveColor);
    } else if(!c->count()) waiting(text(c->ambiguous()?EPG_AMBIGUOUS:EPG_WAIT_PROGRAM));
    else line(text(c->fresh()?EPG_UNAVAILABLE:EPG_STALE),12,134,296,PrimaryColor);
    if(next>=0) { char time[8]; clockText(c->at(next).start,c->at(next).lto,time,sizeof(time)); snprintf(info,sizeof(info),"%s %s  %s",text(EPG_NEXT),time,c->title(next)); line(info,12,198,296,ActiveColor); }
  }
  if(fallback) line("~",306,222,8,SecondaryColor);
}
