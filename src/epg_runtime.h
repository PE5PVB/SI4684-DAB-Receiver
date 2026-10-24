#pragma once
#include "epg_core.h"
namespace epg {
Core* runtime();
bool ensure(bool reportFailure=false);
bool replayActive();
bool indicatorPresent();
enum class IndicatorState : uint8_t { Absent, Present, Ready };
IndicatorState indicatorState();
bool liveEnabled();
void liveReset();
void liveContext(uint32_t sid, uint16_t eid, uint8_t ecc, uint32_t utc);
void liveEndpoint(uint32_t sid, uint32_t cid);
void livePayload(const uint8_t* data, size_t n);
void poll();
bool command(char* input);
void stopReplay();
}
