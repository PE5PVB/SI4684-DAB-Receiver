# Předání EPG / UART — 23. 10. 2026

Výchozí HEAD: `e0cb2b60007f6fefe7e97fc2286f8a0432f73e0c`.
Uživatel schválil implementaci parseru, produkčního packet SPI příjmu, UART
replay, tří obrazovek a jejich ovládání před změnami.
**Pouze lokální změny, bez commitu nebo GitHub zápisu.** Předchozí oprava
s pěti DRAM bloky byla se souhlasem nahrána a otestována. Aktuální verze s IRAM a opravou zarovnání Adam7 byla po souhlasu nahrána
přes COM4. HW výsledky a omezení jsou v `EPG_HW_TEST_20261024.md`.

## Výsledek a rozsah

Implementován packet-mode příjem DSRV se skutečným načtením payloadu,
oddělený omezený MOT/SPI parser, omezená cache podle stanice a dne, EPG přehled,
seznam a detail, dlouhé SL a kontextové ovládání, vestavěné skutečné DEMO,
UART protokol E1 a Windows/Python replay s vlastním monitorem.

I²S konfigurace 0x1870, její readback/reuse, pravidla matchesPacketData /
matchesAudioPad, UA discovery, vendor knihovna, platformio.ini a schedulerová
politika včetně max. DSRV burst 4 zůstávají beze změn. Žádná úprava NVS schématu,
GPIO, firmware blobů nebo hardware platformy. Sběr SLS zůstává zachován. Renderer dostal samostatnou funkci pro EPG logo
a větev Adam7 pro prokládaná PNG v EPG i slideshow. Obě používají původní
pracovní paměť PNG/JPEG a validaci před kreslením.

Produkční SPI využívá dosavadního serializovaného vlastníka START/STOP a jeho
omezený cleanup. V kontinuálním režimu nemá 30s konec; změna služby, tune,
ztráta lock, replay a reset vedou přes cleanup. Explicitní DEBUG SPI dočasně
předá vlastnictví původnímu diagnostickému oknu. Full SID/CID se mění pouze
příkazy s plnou identitou, nikoli maskováním bufferu. DSRV čte přesně 24+byteCount
ze zachované odpovědi, payload začíná na SPIbuffer+25, limit 4 071 B.
SPI neprojde legacy SLS/DLS dekodérem ani při nejistém STOP.

Replay je nezávislý na naladění vysílání. Živá SPI větev se po dobu TEST zastaví,
audio a PAD/SLS se nepřesměrují. V EPG je hotová SLS průběžně potvrzována i při
zapnutém autoslideshow, aby se mohl přijímat další obrázek. EPG pouze potlačí
automatické přepnutí obrazovky na slideshow. STOP vrátí živé získávání dat;
cache se znovu naplní, nedrží se druhá plná kopie živých i testovacích objektů.

Použitý RAW log: `_usertesting/device-monitor-261023-160924.log`, SHA-256
`dbdbf20d2238ea023d0a699a887256db17be64af89bb57a292096efd668bbc81`.
Referenční ZIP: `Specification/SI4684_MUX11B_EPG_kompletni_dekodovani.zip`, SHA-256
`aed762cc1f5076dbda47b63101dfeef6ced4b418c9210e7c3c60ba25cc509d1e`.

## Skutečné výsledky hostitelských testů

Použito GCC C++11: `-Wall -Wextra -Werror -pedantic`.

```powershell
python tests/run_epg_tests.py --compiler C:/msys64/ucrt64/bin/g++.exe
pio run -e esp32dev
```

PASS:

- Původní RAW: 3 080 deklarovaných rámců, 1 939 úplných CRC-validních rámců,
  1 141 neúplných/poškozených hex výpisů vynecháno, 1 660 632 B platného payloadu.
- Postupná cílová akvizice přes stejné produkční C++ jádro: 1 adresář + všech
  **70 objektů, 35 PI, 5 SI, 30 PNG a 1 087 záznamů pořadů**.
  Adresář a všech 30 PNG se bajtově shodují s referencí. Nejde o druhý offline
  dekodér nebo o nahrávání hotových JSON do firmware.
- Srovnání PI: bearer, shortId, UTC/duration/LTO, medium/long název a
  popis; níže jsou přesně vymezené odchylky. SI: 55 výskytů služeb v pěti
  objektech, bearer/název/MIME/URL loga a odmítnutí Radio24/Chopin kolize.
- CRC, chybná délka, neznámý DG, duplicity, konfliktní segmenty, pořadí včetně
  posledního segmentu jako prvního, neúplnost/timeout, UTF-8, TLV/token bounds,
  přílišná hloubka, PNG CRC a 2 000 CRC-validních nepřátelských segmentů
  s kontrolními canaries kolem celé arény.
- MJD/UTC, kladný/záporný LTO, krátký/dlouhý čas se sekundami, přechod roku,
  data kolem DST s explicitně přeneseným offsetem, neplatný čas a historické
  datum, čerstvost, deterministická volba NOW při překryvu.
- Skutečné runtime příkazy: nedostatek paměti, DEMO/STATUS/TIME/STATION,
  nejednoznačná služba, STOP/CLEAR, neaktivní UART, změna služby/muxu/FM a reset.
- Celý UART: **52 133 fragmentů** původního RAW, řízení BUSY/ACK, idempotentní
  opakování, session/sequence/CRC/source/CID/length/expiry. Výsledek Trójka,
  12. 11. 2024: **68 položek cache, NOW index 8, NEXT index 9, LIMIT=0**.
- Skutečný `tools/epg_replay.py` proti skutečnému C++ runtime přes simulovaný
  COM/pipe: handshake, úplný RAW, vynucená ztráta prvního ACK, opakování bez
  dvojího zpracování, monitor, konzole, ukončení a STOP. **COM byl simulovaný.**
- Skutečný renderer/ovládací funkce s nahrávajícím TFT mockem: tři stránky,
  devět řádků, font s polštinou, klid bez překreslení, omezený scroll, volume
  overlay/návrat, změna dne, MODE/ROT1 krátce/dlouze bez skenu a nechtěného ladění.
- Skutečné plánovací a DSRV části rádia s transportním mockem: kontinuální
  příjem vs. původní 5/30s diagnostika, předání vlastníka, ztráta lock/replay,
  cleanup failed karanténa, úplné čtení a hraniční velikosti, chyba čtení,
  oddělení legacy audio/SLS.

Původní regrese rovněž PASS: 10 C++ testů `scheduler`, `fm_features`,
`dab_service_switch`, `dab_user_application`, `dab_spi_discovery`,
`dab_spi_probe`, `dab_packet_inventory`, `dab_dsrv_header`, `dab_diagnostics`,
`i2s_audio`; tři integrační Python testy `test_dab_packet_runtime.py`,
`test_dab_xpad_integration.py`, `test_i2s_integration.py`.
Příkazy pro jejich reprodukci jsou v tests/README.md.

Podrobné nové příkazy, návratové kódy a výstupy jsou v
`.pio/epg_acceptance/results.log` a `.json`; runner je znovu vytvoří.
Mockové hodnoty heap=100000/audio=1/SLS=1 v tomto logu **nejsou měřením desky**.

## Odchylky od referenčního Pythonu doložené RAW

Tři binární timePoint mají minutu **60**, kterou původní Python převedl
pomocí timedelta na další hodinu:

| TID | shortId | Přijaté bajty timePoint |
|---|---:|---|
| F629 | 1056087 | `3b34957802` |
| F630 | 1056412 | `3b34d57802` |
| F63E | 1057048 | `3b35557802` |

V krátkém formátu bity 25–30 dávají 60. Firmware tyto časy odmítne jako
neplatné, záznam je ve výsledku dekodéru označen nejistě a nevstoupí do NOW/NEXT.
Počet přenesených programme zůstává 1 087; validní časy se shodují u 1 084.
Původní data ani plán se neposunují, aby vypadal správně.

Deset delších popisů se na výstupu C++ jádra řízeně zkrátilo na nejvýše 511 B
na hranici UTF-8. Test ověřil přesný prefix referenčního popisu; nejde o jinak
vyložené TLV. UI/cache oznamuje LIMIT. Názvy/identity/časy ostatních položek
se shodují. Nulová délka pořadu se nezmění na odhadovaný interval; v cache je
nejistá a není kandidátem NOW/NEXT. Překryvy se zachovávají.

## Paměť a build

`pio run -e esp32dev`: SUCCESS. Statická RAM **66 496 / 327 680 B**;
flash **3 530 841 / 4 128 768 B**. Proti výchozímu buildu 62 272 B / 3 458 437 B
je to +4 224 B statické RAM a +72 404 B flash. Flash rezerva 597 927 B.
Jediné známé upozornění celého buildu je původní TFT_eSPI chybějící TOUCH_CS.

EPG používá pět bloků: metadata v INTERNAL|8BIT mají na ESP32 **13 560 B**,
objekt/adresář/logo/texty v INTERNAL|32BIT|EXEC dohromady **36 864 B**.
Součet je **50 424 B** na ESP32 a **50 720 B** na hostiteli, bez režie alokátoru.
`EPG RAM arena` vypisuje součet; `EPG POOLS` vypisuje volné místo a největší
blok zvlášť pro každou oblast. Jde o dodatečný heap mimo statickou RAM.
Rezerva je 12 000 B v bajtové DRAM a 4 096 B v IRAM. Celkový údaj INTERNAL
nenahrazuje měření těchto oblastí. Částečné selhání vrací všechny bloky.

Buffery jsou dostupné přes `Bytes`/`Buffer`, bez konverze na bajtový pointer.
IRAM používá výhradně zarovnané volatile 32bitové operace; disassembly finálního
ELF potvrdila L32I/S32I v loadWord/storeWord bez bajtových/půlslovových instrukcí.
TFT dostává oddělené kopie názvu a popisu v DRAM (platné do příštího volání
stejného getteru); PNGdec čte logo přes omezené read/seek callbacky. Nový
framebuffer ani kopie celého PNG v DRAM nevzniká. UART rámec 4071 B je statický.
Po úspěšné rezervaci se při segmentech ani STOP/DEMO další bloky nealokují.

V počátečním buildu se kvůli interním bitmapám/fontům objevily duplicitní assety;
finální verze sdílí původní pointery z GUI a hlavního fontu a tyto kopie nelinkuje.

Aplikační binárka: `.pio/build/esp32dev/firmware.bin`, **3 531 456 B**, 0x10000.
SHA-256: `093EEBDF9A3E005F069B81E2B86C02F00124BFF4286852504625CEF37AAB314C`.
Kombinovaná binárka na 0x0 přepisuje NVS; uživatel ji nesmí zaměnit za aplikaci.

## Soubory

- `src/epg_core.h/.cpp`: platformově neutrální CRC, čas, adresář, reassembly,
  postupné TLV, SI/PI, mapování, omezená cache a validace PNG struktury/CRC.
- `src/epg_uart.h/.cpp`: pevné fragmenty E1/session/CRC/ACK/pořadí/timeout.
- `src/epg_runtime.h/.cpp`: lazy arena, skutečné příkazy, DEMO, virtuální čas,
  live kontext, watchdog a stavová diagnostika.
- `src/epg_demo.h`: pouze malé skutečné MSC skupiny z dodaného záznamu.
- `src/epg_ui.h/.cpp`: přehled/seznam/detail, texty, témata a navigace.
- `src/si4684.cpp`: skutečné čtení SPI payloadu, kontinuální vlastník s původním
  cleanup, vazba na reset/tune a zachování opakované UA detekce.
- `SI4684-DAB-Receiver.ino`: pump, modalita EPG, SL press tracker, tlačítka/IR,
  overlay návraty a sdílený font; boot komba/standby zůstávají.
- `src/gui.cpp`: modalita, návrat z menu a sdílení původních bitmap/selektoru.
- `src/slideshow.h/.cpp`: samostatné logo přes existující decoder arena;
  původní SLS dekódovací funkce je zachována.
- `src/comms.cpp`: EPG dispatcher před legacy/ENABLE, původní 128B RX se nemění.
- `tools/epg_replay.py`, `requirements.txt`, `.gitignore`: skutečný PC UART
  klient, pyserial, ochrana proti zahrnutí generovaných pyc.
- `tests/run_epg_tests.py`, `test_epg_core.cpp`, `test_epg_replay.py`,
  `test_epg_errors.cpp`, `test_epg_uart.cpp`, `test_epg_runtime.py`,
  `test_epg_ui.py`, `test_epg_transport.py`, `test_epg_storage.py`, `test_epg_png_adam7.py`, `test_epg_pc.py`: reprodukovatelné testy.
- `tests/README.md`, `Serial_protocol.md`, `docs/EPG_TEST_CZ.md` a tento report:
  workflow, skutečné příkazy a předání. Patch: `tests/EPG_20261023.patch`.

Repo ignoruje nové soubory tests/. Patch je proto obsahuje jako nové soubory
vůči HEAD; běžný git diff by je vynechal. Index/.gitignore projektu se nemění.
Žádné velké uživatelské RAW/ZIP/JSON soubory nejsou zahrnuty.

## Otevřené HW kroky a vědomé limity

**HW stav 24. 10. 2026:** IRAM verze s opravou zarovnání byla nahrána přes COM4.
DEMO, RAW programy, normální PNG a obě Adam7 loga jsou ověřeny; uživatel potvrdil
NOW/NEXT i správný obraz log. Podrobnosti v `EPG_HW_TEST_20261024.md`.
Neověřeno zůstává měření BCK a samostatný poslech,
měření BCK, kontrola TFT/polštiny/displayflip, free/min/largest peak a
měření přírůstků CTS/DSRV overflow/UART drop při skutečném audio/SLS souběhu.
Zdejší PASS nepřebírá Andrzejovo HW ověření staršího routingu jako test nové verze.
Konkrétní postup je v EPG_TEST_CZ.md, včetně DEMO i skutečného COM replay.

- Jde o omezenou cache až osmi výběrů stanice/dne (celkem 96 položek, 8KiB texty), ne sedmidenní
  databázi. Velký adresář/objekt přes pevný limit se neobchází.
- Offline úplnost 70 objektů je postupná akvizice každého TID. Běžný ESP32/UART
  sbírá SI + relevantní PI a logo aktivní služby, ne celý carousel do RAM.
- Čekání na chybějící segment vyžaduje další opakování carouselu, při testu --loop.
  Překročení rozpočtu parseru/live zahození je vidět v busy/timeout/incomplete.
- Obrázky se ověřují před kreslením; EPG logo je do 100×70 bez škálování.
- Šifrované/komprimované MOT objekty/adresář a jiné obsahové profily nejsou
  implementované. X-PAD SPI adaptér je nově implementovaný a synteticky ověřený;
  skutečný X-PAD příjem z čipu dosud ověřený není. Podrobnosti jsou v
  EPG_SYNTHETIC_PATHS_CZ.md.
- Programové události/actual údaje/CRID se dekódují do strukturovaného callbacku;
  omezený seznam prezentuje hlavní programme s shortId, plánem a popisem.
- Diagnostické příznaky audio=1/SLS=1 znamenají stav firmware; souvislý zvuk a
  správně dekódovaný obrázek musí potvrdit uživatel, ne pouhý čítač paketů.

Normativní podklady: [ETSI EN 301 234 V2.1.1](https://www.etsi.org/deliver/etsi_en/301200_301299/301234/02.01.01_60/en_301234v020101p.pdf)
a [ETSI TS 102 371 V3.1.1](https://www.etsi.org/deliver/etsi_ts/102300_102399/102371/03.01.01_60/ts_102371v030101p.pdf).

## Oprava fragmentované paměti po testu COM4

Po jednom schváleném restartu původní aplikace vrátila:
`EPG ERROR MEMORY need=49736 free=82640 largest=25588`.
HELP a STOP fungovaly, START/DEMO/STATUS po první chybě mlčely, protože příznak
allocationReported potlačoval také odpovědi na explicitní příkazy. Běžná
MEMCHK diagnostika navíc vypisovala jiný limit maxblock=53236; pro EPG je
rozhodující INTERNAL|8BIT largest=25588.

Oprava rozděluje rezervaci do výše uvedených pěti bloků bez alokací za segment.
STATUS/START/DEMO vždy oznámí neúspěšnou alokaci, automatická diagnostika
zůstává omezená na první chybu. Regresní test modeluje zvlášť celkovou volnou
paměť 82640 B a limit jednoho bloku 25588 B; ověřuje úspěšné DEMO, plný RAW,
selhání každé z pěti alokací s úplným rollbackem, ochranné bajty a osm opakování
STOP/DEMO bez dalších alokací. Tato pětibloková DRAM verze následně byla nahrána a test znovu selhal;
opravu IRAM popisuje následující část.
Log před opravou: `.pio/epg_acceptance/hardware_after_reset.log`.

## Oprava rozlišení DRAM / IRAM

Při testu nahrané pětiblokové verze každé STATUS/DEMO správně vrátilo chybu
paměti a opakované pokusy neztrácely heap. Startovní diagnostika odhalila
`after splash TFT font free=28360 largest=25588` pro INTERNAL|8BIT. Celkových
82640 B se na bajtové buffery použít nedalo; předchozí PC model pouze omezoval
největší blok a tuto odlišnost nezachytil. Logy jsou v
`.pio/epg_acceptance/hardware_fixed_boot.log` a `hardware_fixed_demo.log`.

Aktuální runtime model měří odděleně 28360 B DRAM a 54280 B word-only IRAM,
modeluje chybu obou oblastí i při vysokém agregovaném volném místě a rollback
každé z pěti alokací. DEMO a plný RAW projdou; osm STOP/DEMO cyklů nepřidává
alokace. Hostitelské jádro používá word-only buffery také při porovnání všech
70 objektů a 1087 programových záznamů s referencí.

`test_epg_storage.py` navíc sestaví skutečný PNGdec z projektové závislosti.
U 28 RAW log porovná RGB565 pixely bajtového vstupu s produkčním IRAM readerem;
dvě interlaced PNG (B385, B389) nová cesta Adam7 vykreslí pixelově shodně
s nezávislým dekodérem Windows System.Drawing (na Linuxu test využije Pillow).
Testuje také logické nezarovnané offsety, překryv přesunů oběma směry,
CRC a konec bufferu. Výpis kontroly cílových instrukcí je v
`.pio/epg_acceptance/word_access_disassembly.log`.

## Adam7 pro EPG i slideshow

Uživatel samostatně schválil doplnění prokládaných PNG. Nový modul
`src/png_adam7.h/.cpp` dekóduje sedm Adam7 průchodů, filtry 0–4, barevné typy
0/2/3/4/6 v platných hloubkách 1/2/4/8/16, PLTE/tRNS a průhlednost přes bílou.
Přijímá bajtový nebo word-only vstup. Pracuje bez heapu ve stávající 76 800B
aréně, se dvěma řádky, pevnou 32KiB zlib dictionary a malým vstupním bufferem;
na hostiteli potřebuje 47 224 B. Před kreslením probíhá úplná validace CRC
chunků, zlib Adler-32 a přesné délky dekomprimovaných dat. Výstup po řádcích
předává souřadnice a krok průchodu; TFT zůstane během neplatné validace beze změny.

Používá existující inflate z PNGdec s předem přiděleným stavem a dictionary,
zalloc vždy odmítá další alokaci a zfree je prázdné. Výstup inflate je omezen
na 128 B za volání. Tím se vyhne optimalizované inflate_fast větvi PNGdec,
která při testu velkého RGB16 vstupu poškodila výstup a skončila chybou Adler;
dodaná knihovna se nemění. Řádkové buffery mají navíc ochranné zarovnávací bajty.

Test `test_epg_png_adam7.py` generuje nezávislým Python encoderem 60 případů
včetně 1×1, lichých rozměrů a 320×240 pro všechny podporované kombinace barev
s bitovou hloubkou; filtry střídá a IDAT dělí i po jediném bajtu. Ověří výstup
přes DRAM i word-only vstup, každou souřadnici právě jednou a ochranné bajty.
Osm chybných streamů (truncation, CRC, Adler, filtr, chybějící/přebývající
rozbalená data, trailing compressed data) se odmítne. `test_epg_storage.py`
navíc porovná dva skutečné Adam7 RAW objekty s OS dekodérem. Testy i ESP32 build
prošly. První HW pokus odhalil chybějící zarovnání workspace; po níže
popsané opravě jsou obě skutečná Adam7 loga ověřena i na displeji.

## HW oprava zarovnání a výsledky 24. 10. 2026

UART diagnostika `[EPG/LOGO]` ukázala `validated=0 stage=workspace` při
správně uloženém PNG. Stávající 76800B blok měl adresu zarovnanou na 4 B,
zatímco `inflate_state` s 64bitovým akumulátorem vyžadoval 8 B. Dekodér
nyní posune začátek uvnitř stejného bloku a odečte padding před kontrolou
kapacity. Nepřiděluje další heap ani nepřistupuje před/za blok.

Regresní test ověřuje posuny 1–7 B s rezervou, přesný obraz i ochranné bajty;
nedostatečný prostor se odmítne. Na desce oba objekty B385/B389 hlásí
`validated=1 stage=ok pad=4` a `draw=1`; uživatel potvrdil správný obraz obou.
Diagnostika zůstává omezená kontrolou volného místa v UART frontě. STATUS
navíc vypisuje `EPG LOGO bytes/interlace`; automatické testy a build prošly.

Úplná sada 1939 RAW rámců byla pokryta ve dvou relacích po výpadku COM4:
1501 potvrzených rámců před odpojením, následně 24 bootstrap skupin a zbývajících
438 rámců. Nejde o souvislý nepřerušený běh. Konečná cache 68, NOW=8, NEXT=9,
PI=2, SI=1 a PNG=1 odpovídá referenci; CRC/invalid/UARTdrop/rxOrder/rxIdentity
zůstaly nulové. NACK BUSY znamenaly řízení toku a byly nástrojem obslouženy.

## Návrat z EPG a indikátor UART testu (24. 10. 2026)

Seznam EPG nyní používá FONT16 již načtený při startu do sdíleného
`FullLineSprite`. Nevolá další load/unload fontu a kreslí s průhledným
pozadím. Původní unload po seznamu přepnul infotext i další dynamické údaje
na základní malý font s barevným pozadím. Regresní test prochází seznam,
detail, nastavení a návrat na hlavní obrazovku a ověřuje zachování fontu.

Indikátor hlavní obrazovky v UART TEST režimu vychází z přijetí úplného
validovaného MOT adresáře, samotný START jej neaktivuje. STOP, timeout nebo
ukončení testu obnoví indikaci podle živé SPI detekce. Testovací data
nemění stav detekce v rádiu. Hostitelský test ověřuje oba živé stavy,
aktivaci z DEMO dat a návrat po STOP/timeoutu.

Build této opravy: RAM 66496 B, flash 3530889 B, firmware.bin 3531504 B.
SHA256: `7DA133E83E2309EF1BD1FF488B459A4B2F332A9EEAFB2D1DD905898EE14C8B88`.
Tato verze byla následně se souhlasem uživatele nahrána přes COM4 na 0x10000;
esptool ověřil hash zápisu. DEMO: cache 39, NOW=12, NEXT=13. Krátký RAW test
Trójky: 24 rámců, PI=2, SI=1, PNG=1, cache 68, NOW=8, NEXT=9. CRC/invalid,
UARTdrop/rxCRC/rxOrder/rxIdentity byly nulové; STOP obnovil prázdný LIVE stav.
Log: `.pio/epg_acceptance/hardware_font_indicator_smoke.log`. Následně bylo
spuštěno DEMO pro vizuální kontrolu uživatelem; uživatel potvrdil správné
texty i aktivní indikátor EPG.
HW výsledky v `EPG_HW_TEST_20261024.md` patří předchozí verzi.


## Čekací panel, seznam a překlady EPG (24. 10. 2026)

Čekání na program používá stejný neutrální panel 270×50 a barvy tématu jako
čekání na slideshow, s vlastní značkou programu. Panel zmizí při příchodu
programových dat. Prázdný program, nejednoznačná služba a nedostatek paměti
mají odpovídající lokalizovaný text.

Pohyb v seznamu obnovuje původní a nově vybraný 20px řádek přes již existující
sprite. Přechod mezi skupinami devíti nebo změna dat obnoví devět řádků;
změna dne/minuty obnoví také patičku. Celá obrazovka seznamu se obnovuje při
vstupu, návratu z překrytí, změně jazyka nebo vynuceném překreslení. Výběr
používá stejný selector, odsazení x=8/y=3 a Primary/Secondary barvy jako
seznam stanic. Nevzniká nový framebuffer ani další alokace fontu.

Systémové texty jsou v epgText v původním language.h pro všech osm jazyků
rádia (EN/NL/EL/DE/FR/ES/PL/RO). Názvy a popisy z vysílání se nepřekládají.
Host test kontroluje všechny diakritické znaky podle skutečných FONT16
metadat, nadpisy i proti FONT28. Nadpis zůstává 28px, delší překlad se vejde
pomocí 16px fontu. Celá hostitelská sada prošla; rozšířený UI test navíc
ověřil dva/devět řádků bez celého pozadí, prázdnou stránku a příchod RAW dat
během zobrazeného čekacího panelu.

Build PASS: RAM 66512 B, flash 3534729 B, firmware.bin 3535344 B.
SHA256: `146265D755A26D9A82C6101975C26B42417D829545C9A58EE0EA8A9C19CA5670`.
Tato verze byla následně se souhlasem uživatele nahrána přes COM4 na 0x10000;
esptool ověřil hash zápisu. Pro vizuální test běží průběžný UART replay:
24 úvodních skutečných skupin F651/F64C/F62B/F632/B390 pro rychlé naplnění,
potom 1939 úplných původních rámců, vše opakovaně ve smyčce. Úvodní data již
naplnila cache 68, NOW=8, NEXT=9, PI=2, SI=1 a PNG=1. Dosud CRC/invalid i
UARTdrop/rxCRC/rxOrder/rxIdentity nulové; audio service aktivní. Tento stav
není potvrzením dokončení celé smyčky. Log průběhu:
`.pio/epg_acceptance/hardware_epg_ui_continuous.log`.
Uživatel potvrdil správný vzhled, plynulé listování a překlady. Následně
byla při testování dne vyprázdněna původní cache. Krátká smyčka 24 skupin
obnovila 68 pořadů a uživatel potvrdil zmizení čekacího panelu; tato smyčka
na původní verzi nyní pokračuje (log hardware_epg_ui_short_loop.log).
Předchozí potvrzení se týká opravy fontu s SHA256 začínajícím 7DA133E8.


## Zachování programu podle stanice a dne (24. 10. 2026)

Cache nově eviduje osm výběrů (úplný bearer + místní den). Každý má vlastní
čas platnosti a značku příjmu. Záznamy i texty sdílejí původní pevné limity
96 pořadů a 8192 B textů; nepřidává se další objektový ani obrazový buffer.
Veřejné count/at/title/description/current/next zpřístupní jen aktuální výběr.
Přepnutí na prázdnou stanici/den nepoužije cizí data. Návrat k uchovanému
výběru obnoví pořady a odstraní čekací panel bez nových paketů.

Při vyčerpání slotů se přednostně uvolní prázdný výběr. Při nedostatku
místa pro pořady/texty se uvolní nejdéle nepoužitý jiný naplněný výběr;
aktuální výběr se nevytlačuje. Textový pool se po uvolnění zkompaktuje,
offsety přeživších názvů a popisů se opraví. Při změně výběru se zruší
rozečtený objekt, aby se nedokončil do jiné cache. Nový adresář, reset muxu
nebo STOP nadále invalidují data. Uchování přes změnu multiplexu není součástí.
Logo má i nadále jediný buffer s kontrolou vlastníka; paměť více log se
nepřidává. Detekční ikona EPG zůstává oddělená od dostupnosti programu.

Testy: celá sada RAW/oracle, chybové vstupy, cache, UART/runtime/UI, transport,
IRAM, Adam7 a PC prošla. Nový test cache ověřuje izolaci stanic/dnů, návrat
bez příjmu, čerstvost podle výběru, limit 96 pořadů, opakované uvolňování
8KiB textů, osm slotů a 66000 změn výběru včetně přetečení značky příjmu.
UI test ověřuje čekání u prázdného dne/stanice a okamžitý návrat devíti řádků
bez nových rámců a bez celého pozadí.

ESP32 sizeof(Core)=13936 B ověřeno cílovým kompilátorem (+376 B metadat).
Celá rezervace EPG je 50800 B; původní čtyři IRAM buffery mají stále 36864 B.
Build PASS: statická RAM 66512 B, flash 3536141 B, firmware.bin 3536768 B.
SHA256: `5980FFFC6A1F39A1DFD34ADA61D686E22D90BD153F939A2CD93D4D1C2F4B74AA`.
Tato verze byla následně se souhlasem uživatele nahrána přes COM4 na 0x10000;
hash zápisu byl ověřen. HW test načetl 24 skutečných skupin Trójky a poté
bez posílání dalších EPG fragmentů třikrát přepnul virtuální stanici na
nenačtené Radio Kraków a zpět, stejně jako datum 13. 11. a zpět na 12. 11.
Prázdné výběry měly cached=0, původní cached=68 a NOW=8/NEXT=9; počet rámců
po celou dobu zůstal 24. CRC/invalid/UARTdrop/rxCRC/rxOrder/rxIdentity byly 0.
Free heap byl stabilně 31744 B, free8=14192 B, největší 8bitový blok 8692 B,
arena=50800 B. Radio audio service zůstalo aktivní; subjektivní poslech tento
test neověřoval. Log: `.pio/epg_acceptance/hardware_cache_return.log`.

Následně se znovu spustila krátká smyčka 24 skupin Trójky pro manuální test.
Uživatel potvrdil okamžitý návrat seznamu po ROT2 přepnutí na nenačtený den
a zpět. Přenos zůstává aktivní; COM4 používá skript run_cache_ui_stream.py.
Log: `.pio/epg_acceptance/hardware_cache_ui_stream.log`.


## Verze v2.5 a tři barvy EPG (24. 10. 2026)

VERSION je v2.5. Indikátor má stavy Absent/Present/Ready: šedá 0x3A4A,
cyan 0x06DF, zelená 0x07E0. Ready vyžaduje neprázdnou cache právě zvolené
stanice/dne, čerstvost do 15 minut a jednoznačnou identitu. Má přednost před
stavem detekce, takže platná uložená data mohou být zelená i během výpadku RF.
Bez platné cache rozhoduje živá SPI detekce nebo adresář UART replay. Ve FM
je nápis skrytý. Stav se přepočítává i při ztrátě platnosti dat, přepnutí dne
či stanice; při nezměněném stavu se nápis znovu nekreslí.

Test runtime vykonává skutečný ShowEpgIcon s registrací barvy a počtu kreseb.
Ověřuje všechny barvy, opakované nezměněné volání, skrytí ve FM, konflikt
identity, zastarání, prázdný den a návrat k uloženému dni. Celá host sada a
build prošly. Build: RAM 66512 B, flash 3536261 B, firmware.bin 3536880 B.
SHA256: `AFB90CE9E85D4F246373B4FDE166695058803BAFF43746CFA2AF578DF4452644`.

Se souhlasem uživatele nahráno přes COM4 na 0x10000, hash zápisu ověřen.
Krátký HW test načetl 24 skupin Trójky, cache 68, NOW=8/NEXT=9, PI=2/SI=1/PNG=1.
Přepnutí na prázdnou virtuální stanici/den a zpět obnovilo cache bez nových
EPG rámců. CRC/invalid/UARTdrop/rxCRC/rxOrder/rxIdentity byly nulové.
Heap free=31744 B, free8=14192 B, arena=50800 B. Log hardware_v25_smoke.log.
Krátký replay 24 skutečných skupin pokračuje ve smyčce přes COM4;
log hardware_v25_stream.log. Uživatel potvrdil zelený nápis EPG na hlavní
obrazovce. Všechny tři barvy jsou navíc ověřeny hostitelským testem kreslení.


## Oprava zalomení popisů Jedynky (24. 10. 2026)

V F629 je např. v popisu pořadu Sygnały dnia už ve vysílaném textovém uzlu
0x01 uvnitř 0x1a doslovný LF před 06:40:00 Po pierwsze ekonomia. Text o čase
6.00 i následující řádky jsou součástí původního popisu, nejde o přetečení
parseru do dalšího pořadu. Zobrazení nahrazovalo LF otazníkem při kontrole
glyfů, ještě před zalamováním. Totéž se týkalo oddělovače názvu a popisu.

Multiline cesta nyní zachovává LF, normalizuje CRLF/CR na jeden nový řádek
a tabulátor na mezeru. Jednořádková pole převádějí tyto oddělovače na mezery.
Zalamování respektuje i prázdné řádky a počítá je pro posouvání. Časy, názvy,
popisy a skutečné otazníky se neodstraňují; neznámé glyfy mají nadále náhradu ?.
Parser, cache a původní vysílané texty zůstávají beze změn.

UI regresní test používá skutečný dekódovaný popis F629/1056061. Ověřuje
zachování upozornění 6.00 i řádku 06:40:00, nepřítomnost náhradního otazníku,
pozice oddělených řádků, posun výřezu, CR/LF/CRLF, tabulátory, skutečný ?,
chybějící glyf a úplné UTF-8 při ořezu. Celá host sada a build prošly.
Po schválení uživatelem je VERSION změněna na v2.5b. Build prošel:
RAM 66512 B, flash 3536361 B, firmware.bin 3536976 B.
SHA256: `3153BC462367D8B18FC170D5E1E3E1CD13EDA1A80C2E02D4DFA7C4CAE9C44FF9`.
Nahráno přes COM4 na 0x10000, hash zápisu ověřen. První zahájení replay
bezprostředně po bootu skončilo ERROR START required; po ověření LIVE stavu
s aktivním audiem byl replay znovu zahájen a běží správně.

Jedynka smyčka obsahuje 27 skutečných skupin F651/F64C/F629/F630/B38D.
Po příjmu obou PI: 92 dekódovaných položek, cache vybraného dne 41,
NOW=12/NEXT=13, PI=2/SI=1/PNG=1, logo 7238 B. Dvě další cache položky proti
samotnému F629 pocházejí z navazujícího PI F630 a překrývají vybraný den.
CRC/invalid/UARTdrop/rxCRC/rxOrder/rxIdentity byly nulové, heap free=31744 B,
free8=14192 B, arena=50800 B. Log hardware_v25b_jedynka_stream.log.
Přenos pokračuje ve smyčce, COM4 používá run_v25b_jedynka_stream.py.
Uživatel potvrdil správné zalomení popisů, zachování časů a odstranění
falešných otazníků v detailu Jedynky.


## Dvousloupcový seznam programu (24. 10. 2026)

EPG PROGRAMME už nepoužívá servicelistbackground se sloupci pro ID, název
a typ stanice. EPG sdílí jednolitý configurationbackground a přikresluje
jediný oddělovač na x=64 mezi časem a názvem. Není přidána další bitmapa ani
framebuffer. Původní seznam stanic a jeho assety zůstávají beze změn.

Řádky jsou na x=8, y=34+20*řádek. Čas začíná na obrazovce x=16, název x=72,
maximální šířka názvu je 232 px. Ořez zachovává UTF-8. Používá se původní
304×20 selector, FONT16 a Primary/Secondary barvy. Oddělovač se kreslí
po selectoru i při obnově jednotlivých řádků; nevznikají výpadky čáry při
posunu výběru, změně stránky ani odstranění čekacího panelu.

UI test kompiluje skutečné funkce výběru assetů z gui.cpp. Ověřuje jednolitý
podklad, souřadnici/výšku oddělovače, jeho obnovu v obou změněných řádcích,
pozice obou textových sloupců, mezery a šířku názvů. Kontroly původního
částečného překreslování a předchozích funkcí nadále procházejí.
Build PASS: RAM 66512 B, flash 3536385 B, firmware.bin 3537008 B, v2.5b.
SHA256: `4AF22EC9A28AF1E6B85E751E1EA3C9031FA5DFA894BFFA20518066B78AA10956`.
Grafická úprava je nahraná přes COM4 na 0x10000, hash zápisu byl ověřen.
Jedynka replay je obnoven ve smyčce 27 skutečných skupin F651/F64C/F629/F630/B38D.
Program se naplnil na 41 položek, PI=2/SI=1, NOW=12/NEXT=13. CRC/invalid,
UARTdrop/rxCRC/rxOrder/rxIdentity byly 0; arena=50800 B, heap free=31744 B.
Přenos používá COM4, skript run_v25b_two_columns_stream.py, log
hardware_v25b_two_columns_stream.log. Uživatel potvrdil správné zarovnání
obou sloupců i souvislý oddělovač při listování.


## Odstranění oddělovací čáry (24. 10. 2026)

Na žádost uživatele byla svislá čára odstraněna z celého pozadí i z řádkového
spritu. Dva sloupce zůstávají zarovnané pouze rozestupem, ostatní vzhled a
částečné překreslování zůstávají zachované. UI regresní test ověřuje původní
souřadnice a nulový počet kreslení svislé čáry; test i build prošly.

V2.5b, RAM 66512 B, flash 3536365 B, firmware.bin 3536992 B.
SHA256: `D42A295CA9EFECA85CCA191FC35BB863D15F13C3436E59BB97D5CCF0B36281A8`.
Nahráno přes COM4 na 0x10000, hash zápisu ověřen. První replay po bootu
ukončila změna služby během dokončování ladění; po opětovném START byl
obnoven. Jedynka data se opakují ve smyčce, log hardware_v25b_no_divider_stream.log.

Po obnovení: cached=41, PI=2/SI=1/PNG=1, NOW=12/NEXT=13; CRC/invalid a
UARTdrop/rxCRC/rxOrder/rxIdentity byly 0. Přenos zůstává spuštěný přes COM4.


## X-PAD EPG adaptér (24. 10. 2026)

Doplněn vstup DATA_SRC=1 pro SPI aplikaci potvrzenou v UA aktuální audio
komponenty, UATYPE=7, přesný nebo povolený krátký audio CID a použitelný
kontext bez přeladění. Chybné DSRV bloky se odmítají. Potvrzené PAD SPI se
předá společnému Core, ne slideshow. SLS/DLS mají původní předání.

PAD má přednost před paketovým vstupem. Runtime neprokládá jejich adresáře
ani shodná TID; změna vstupu/identity zruší rozpracovaná data a cache.
Nové buffery ani alokace nevznikly. Syntetické scénáře MSC i PAD dekódují
stanici, tři pořady a NOW/NEXT; runtime ověřuje přepínání vlastníka a UART
izolaci. Celá sada a build prošly. Následné nahrání a test na desce viz níže.
Podrobný rozsah, standardový podklad, limity a hash buildu:
[EPG_SYNTHETIC_PATHS_CZ.md](EPG_SYNTHETIC_PATHS_CZ.md).


## Syntetický DSRV test na ESP32 (24. 10. 2026)

Doplněn izolovaný E2 UART vstup celé DSRV odpovědi, příkazy INJECT MSC/PAD/NOSPI
 a klient tools/epg_synthetic.py. Používá stávající UART buffer a Core;
UA potvrzení je syntetické, SLS/DLS handoff pouze čítaný. RF tím není ověřeno.
Celá hostitelská sada prošla včetně stejného klienta proti runtime.

Nahráno přes COM4 na 0x10000 a ověřeno následným verify_flash (digest matched).
V2.5b, RAM 66544 B, flash 3538461 B, firmware.bin 3539088 B.
SHA256: `5C4490F195788A74119322DC65A793FF21689C705095EF5555C44B78EB2B65C7`.

Na přijímači prošlo všech devět scénářů: MSC, PAD, duplicity/cache/izolace/
SLS-DLS handoff a šest odmítacích případů (UA, SID, CID, stav, krátká hlavička,
CRC). Závěrečný stav PAD má tři pořady, PI=1/SI=1/dirs=1, NOW=1/NEXT=2,
nulové CRC/invalid i UART chyby. Arena 50800 B, free heap 31712 B, min 31376 B.
Report hardware_synthetic_dsrv.json a .log jsou v .pio/epg_acceptance.
Syntetická PAD data zůstala ve smyčce na COM4 pro kontrolu displeje;
nejde o původní Jedynka replay. Podrobný postup a hranice ověření jsou
v EPG_SYNTHETIC_PATHS_CZ.md.

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
