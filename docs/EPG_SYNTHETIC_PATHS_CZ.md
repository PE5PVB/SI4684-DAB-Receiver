# Syntetický test EPG: paketová MSC cesta a X-PAD

Test nepotřebuje anténu, COM port, původní RAW log ani referenční ZIP. Vytvoří
binární MOT adresář, SI a PI pro `Synthetic EPG One` se třemi pořady, časy a
platnými CRC. Stejný obsah vloží do syntetické DSRV odpovědi jako paketová data
a jako PAD.

**Výsledek:** paketová MSC i nově implementovaná X-PAD cesta dekódují
syntetický program. Report u obou uvádí PASS. Hostitelské testy i syntetická injekce na ESP32 prošly; skutečný
příjem X-PAD z čipu přes RF zatím ověřený není.

## Spuštění bez přijímače

Z kořene projektu:

```powershell
python tests/test_epg_synthetic_paths.py C:/msys64/ucrt64/bin/g++.exe
python tests/test_dab_xpad_integration.py C:/msys64/ucrt64/bin/g++.exe
```

První test přebírá čistě syntetické sestavovací funkce z `test_epg_cache.cpp`
a mock rádia z `test_epg_transport.py`. Jejich testovací programy nespouští.
Výstup je `.pio/epg_synthetic_paths/results.json`; vygenerovaný C++ zdroj je
`paths.cpp` ve stejném adresáři. Druhý příkaz prověří skutečnou inicializaci
X-PAD a diagnostiku s chybami simulovanými na rozhraní čipu.

Obě kontroly jsou také v `python tests/run_epg_tests.py`. Celá dosavadní sada
nadále potřebuje RAW/ZIP pro původní regresní testy, samostatné příkazy výše ne.

## Rozsah ověření

Injekce je na hranici **výstupu Si4684 DSRV**, nikoli RF. Používá se skutečný
`parseDsrvHeader` z vendorizované knihovny Si468x, `dab_probe::route`,
`feedLegacyDecoder`, větev příjmu payloadu z `DAB::finishDabCommand` a
produkční `epg::Core`. Mock čipu vrací připravenou odpověď; parser ani
směrování nenahrazuje napodobenina.

Aktivní endpoint a dokončený výběr služby jsou v testu připravené. FIC scan,
příkazy START/STOP komponenty a RF transport tím nejsou ověřené. Dekodér se
na hostiteli pumpuje do dokončení; nejde o měření časování na ESP32.

| Scénář | Výsledek |
|---|---|
| MSC, DATA_SRC=0, správný SID, plné/krátké CID | Adresář, SI, PI, tři pořady, název stanice, NOW/NEXT |
| Návrat ke stanici/dni | Správná cache bez cizích pořadů |
| Chybný SID/CID, neznámý zdroj | Žádné předání EPG |
| Nadlimitní payload, krátká hlavička | Odmítnutí před dekódováním |
| Probíhající přeladění nebo UART replay | Živá paketová data nevstupují do jádra |
| Chyba čtení odpovědi | Žádná publikace EPG, zvýšení čítače chyby |
| Poškozené MSC CRC | Žádné pořady z vadné skupiny |
| Obrácené pořadí segmentů, opakovaný carousel | Sestavení PI bez duplicit pořadů |
| X-PAD SPI signalizace | UATYPE 7 rozpoznáno jako SPI |
| DATA_SRC=1, UATYPE=7, potvrzená audio UA, plné/krátké CID | Adresář, SI, PI, tři pořady a NOW/NEXT přes EPG callback |
| Nepotvrzená či neaktuální UA, chyba DSRV, jiné DSCTy | Odmítnutí bez předání slideshow |
| SLS/DLS a cizí PAD při sledovaném SPI endpointu | Kontrola předání/odmítnutí bez záměny za paketové EPG |
| X-PAD write/readback, chyba, timeout | Korektní příznaky bez staré hodnoty |

## Implementované směrování

Příjem EPG přes PAD vyžaduje současně DATA_SRC=1, UATYPE=7, DSCTy 0 nebo 60,
aktuální audio SID/CID, použitelný audio kontext, bez přeladění/změny služby,
platný inventář komponent a potvrzenou SPI aplikaci právě této komponenty.
Poškozené, zastavené a fyzicky chybné DSRV bloky se nepředávají.
Syntetický test kompiluje skutečný DAB::xpadSpiPresent, nikoli jen příznak.

SPI kandidát, který podmínky nesplní, se zahodí; nespadne do slideshow.
SLS s UATYPE=2 a DLS se dál předávají původní cestě. Její obsahové dekódování
syntetický test nenapodobuje, kontroluje jen předání. Původní SLS kolektor
ani DLS dekodér se touto změnou neupravovaly.

X-PAD má přednost před souběžnými paketovými daty. Runtime používá jediný Core
bez dalších velkých bufferů; při změně vstupu/jeho identity zruší rozpracované
objekty a cache. Tím se stejné MOT TID z obou přenosů neslepí. Samotný START
paketové komponenty nesmí přepsat aktivní PAD. Po potvrzeném zmizení PAD
aplikace se umožní paketová cesta; neúplná/chybná UA odpověď není důkazem absence.
Rutinní obnova UA během neznámého výsledku příjem PAD jen pozastaví.

Runtime regresní test vykonává skutečné funkce volby zdroje a ověřuje:
packet -> PAD, ignorování souběžných packet dat, změnu PAD komponenty,
PAD -> packet i izolaci vůči UART replay. Při přechodu vstupu se program musí
znovu načíst; cache různých transportů se neuchovávají souběžně.

## Podklad a hranice ověření

[Skyworks AN649 Rev. 2.0](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/an649.pdf)
v DAB tabulce GET_DIGITAL_SERVICE_DATA popisuje DATA_SRC a UATYPE;
DAB_XPAD_ENABLE má EPG na bitu 7. Stávající hodnota 0x0097 tento bit již
obsahuje a nebyla změněna. Kapitoly 7.11 a 7.13 popisují předání MOT datových
skupin a přiřazení PAD k audio identitě. Raw UATYPE v knihovně zůstává raw:
samo o sobě nenahrazuje potvrzení služby ani komponenty.

Skutečné chování vloženého DAB firmware 6.0.9 bez RF vzorku potvrzené není.
Pokud neposkytne UATYPE=7 nebo stejný podporovaný MOT profil, tento adaptér
jej nebude odhadovat z náhodných bajtů. Podpora se týká stávajícího adresářového
MOT/SPI profilu společného Core; jiné profily a komprese tím nepřibyly.
Test neověřuje RF, odbalení PAD uvnitř čipu ani souběh fyzických FIFO.

## Test na přijímači přes COM4 (24. 10. 2026)

Firmware v2.5b obsahuje izolovaný režim `EPG TEST INJECT MSC|PAD [NOSPI]`.
Přijímá E2 fragmenty celé syntetické DSRV odpovědi, používá skutečný parser
hlavičky, sdílená pravidla směrování a produkční Core na ESP32. Potvrzení UA
je v tomto režimu simulováno příkazem; nejde o skutečný FIC scan ani čtení FIFO.
SLS/DLS se ověřují pouze čítačem předání, ne dekódováním obrázku nebo textu.

```powershell
python -u tools/epg_synthetic.py --port COM4 --loop
```

Vstup `groups.bin` vytvoří hostitelský test výše. Klient nejprve provede devět
scénářů, pak ponechá platná PAD data ve smyčce. Ctrl+C ukončí test a obnoví LIVE.
NVS ani naladění nemění. Přeladění rádia test ukončí; při slabém signálu musí
být vypnuté automatické hledání. COM4 během testu vlastní tento klient.

Na desce prošlo MSC a PAD (včetně opačného pořadí segmentů), opakování carouselu,
návrat ke dni z cache bez nových dat, izolace souběžného paketového vstupu
pod PAD a předání SLS/DLS. Odmítnuty byly nepotvrzená UA, cizí SID, cizí CID,
chybový DSRV stav, krátká hlavička a vadné MSC CRC. Očekávané NACK DATA
v negativních scénářích jsou součástí úspěšného testu.

Závěrečný platný PAD stav: cached=3, PI=1, SI=1, dirs=1, NOW=1, NEXT=2,
crcBad=0, invalid=0, UARTdrop=0, rxCRC=0, rxOrder=0, rxIdentity=0.
Arena=50800 B, heap free=31712 B, minimum=31376 B. Stanice je
`Synthetic EPG One`, pořady `station 1 item 0/1/2`, virtuální čas
2024-11-12T01:01:00Z. Tento malý syntetický dataset neobsahuje loga.

Strojový report a sériový log: `.pio/epg_acceptance/hardware_synthetic_dsrv.json`
a `.log`. Celá hostitelská sada včetně stejného E2 klienta proti runtime prošla.
Build: RAM 66544 B, flash 3538461 B, firmware.bin 3539088 B.
Firmware byl nahrán na 0x10000; následné `esptool verify_flash` potvrdilo shodu.
Verze zůstává v2.5b. SHA256:
`5C4490F195788A74119322DC65A793FF21689C705095EF5555C44B78EB2B65C7`.

Uživatel následně potvrdil správné zobrazení syntetického programu
a funkčnost přehledu, seznamu i detailu na displeji přijímače.


## Syntetické logo stanice na přijímači (24. 10. 2026)

Bez změny firmwaru byl původní syntetický program doplněn o SI odkaz na
synthetic.png a odpovídající MOT obrazový objekt. Vygenerované RGB logo
96 × 64 má bílé EPG na tmavomodrém pozadí, cyan rám a žlutý/růžový roh.
Běžné PNG má 267 B, Adam7 varianta stejného obrazu 551 B.

Na COM4 prošly všechny čtyři kombinace MSC/PAD × běžné/Adam7 PNG.
Každá načetla tři pořady, SI=1, PI=1, PNG=1 a přesnou délku loga;
CRC/invalid/UART chyby byly nulové. U obou Adam7 vstupů diagnostika
potvrdila validated=1 stage=ok, w=96 h=64, pad=4 a draw=1.
Běžná PNG větev samostatný čítač úspěšného kreslení nevypisuje;
její načtení do cache bylo ověřeno, vizuální kontrola je zatím neuzavřená.

PAD Adam7 data zůstala ve smyčce. Skript, obě PNG, report a log jsou
v .pio/epg_acceptance/: synthetic_logo_test.py, synthetic_logo_0.png,
synthetic_logo_1.png, hardware_synthetic_logo.json a .log.
Jde nadále o syntetickou DSRV injekci, nikoli ověření příjmu z éteru.

Uživatel potvrdil, že závěrečné PAD Adam7 logo je celé a správně vykreslené.
