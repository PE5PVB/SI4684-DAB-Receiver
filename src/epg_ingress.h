#pragma once
#include "dab_spi_probe.h"
#include "dab_user_application.h"

namespace epg_ingress {
enum class PadRoute : uint8_t { Legacy, Epg, Discard };

// UATYPE is an application discriminator only for non-DLS PAD. It never
// supplies service ownership or replaces the validated component UA answer.
inline PadRoute pad(uint8_t source, uint8_t dscType, uint16_t uatype,
                    uint32_t sid, uint32_t cid,
                    const dab_probe::Endpoint& audio, bool confirmedSpi,
                    bool contextUsable, bool payloadUsable) {
  if(source!=1U || uatype!=dab_ua::SPI) return PadRoute::Legacy;
  if(!contextUsable || !payloadUsable || !confirmedSpi ||
     (dscType!=0U && dscType!=60U) ||
     !(audio.matches(sid,cid) || dab_probe::matchesAudioPad(audio,sid,cid)))
    return PadRoute::Discard;
  return PadRoute::Epg;
}
}
