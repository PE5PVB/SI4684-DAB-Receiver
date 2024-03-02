#ifndef LANGUAGE_H
#define LANGUAGE_H

#define VERSION "v1.00 Alpha"

// [number of languages][number of texts]
// *** means the text is the same as in English
static const char* const myLanguage[2][12] = {
  { "English", // English
    "Rotary direction changed", // 1
    "Please release button", // 2
    "Screen flipped", // 3
    "Calibrate analog meter", // 4
    "Release button when ready", // 5
    "encoder set to optical", // 6
    "encoder set to standard", // 7
    "SI-DAB receiver", // 8
	"Software", // 9
	"Defaults loaded", // 10
	"Channel list" // 11
  },

  { "Nederlands", // Dutch
    "Rotary richting aangepast", // 1
    "Laat aub de knop los", // 2
    "Scherm gedraaid", // 3
    "Kalibratie analoge meter", // 4
    "Laat knop los indien gereed", // 5
    "encoder ingesteld als optisch", // 6
    "encoder ingesteld als standaard", // 7
    "SI-DAB ontvanger", // 8
	"Software", // 9
	"Opnieuw geconfigureerd", // 10
	"Kanalen lijst" // 11
  }
};
#endif
