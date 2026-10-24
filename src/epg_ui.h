#pragma once
bool EpgVisible();
void EpgOpen();
void EpgClose(bool redraw=true);
void EpgToggle();
void EpgBack();
void EpgClick();
void EpgMove(int step, bool second=false);
void EpgDraw(bool force=false);
void EpgInvalidate();

void EpgSuspend();
void EpgResume();

#include <stdint.h>
const uint16_t* EpgBackground(bool list);
const uint16_t* EpgSelector();
const uint8_t* EpgFont16();

void DrawEpgWaitingPanel(const char* text);
