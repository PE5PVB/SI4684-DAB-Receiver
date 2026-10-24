# EPG a testování přes UART (Windows)

Implementace je pro klasický ESP32 bez PSRAM a packet-mode SPI (`TMID=3`,
`source=0`). Živý příjem i UART replay používají stejné C++ jádro
`src/epg_core.*`: MSC CRC → MOT Directory/objekt → binární SPI → omezená cache.
Nejde o načítání hotového JSON do rádia.

**Stav HW 24. 10. 2026:** aktuální IRAM verze s opravou zarovnání Adam7 byla
nahrána přes COM4 při zachování NVS. DEMO, RAW NOW/NEXT, běžné PNG a obě
prokládaná loga prošly testem; pořady a loga potvrdil i uživatel na displeji.
RAW byl dokončen ve dvou relacích po odpojení COM4. Podrobný výsledek:
[EPG_HW_TEST_20261024.md](EPG_HW_TEST_20261024.md).

## 1. Nahrání a krátký test bez polského vysílání

Sestavení:

```powershell
pio run -e esp32dev
```

Výsledná aplikace je `.pio/build/esp32dev/firmware.bin`, adresa **0x10000**.
Kombinovaný `receiver-complete.bin` na 0x0 přepisuje NVS; pro zachování nastavení
použijte aplikační aktualizaci obvyklým postupem projektu. Samotný build nic nenahrává.

Po nahrání otevřete monitor na **115200 Bd** (váš COM může být jiný):

```powershell
pio device monitor --port COM4 --baud 115200
```

Zadejte postupně:

```text
EPG TEST HELP
EPG TEST DEMO
EPG TEST STATUS
```

Příkazy fungují také bez `ENABLE=1`. DEMO automaticky otevře EPG se štítkem
**TEST**. Z flash po malých krocích přehraje 12 skutečných CRC-validních MSC
skupin (9 922 B) z dodaného záznamu: adresář F651, SI F64C a PI F629.
Použije stanici PR Jedynka a virtuální **12. 11. 2024 04:30 +01:00**.
Očekávaný hostitelsky ověřený výsledek: 1 adresář, 1 SI, 1 PI a 42 dekódovaných
záznamů; cache obsahuje 39 platných položek vybraného dne. DEMO neobsahuje PNG logo.
Zobrazené názvy a časy vznikají parsováním binárních dat, nejsou napevno napsané v GUI.

Další skutečně implementované příkazy:

```text
EPG TEST TIME 2024-11-12T04:30:00+01:00
EPG TEST STATION 40e231533211
EPG TEST STATUS
EPG TEST STOP
EPG TEST CLEAR
```

`STOP` i `CLEAR` zruší test, virtuální čas, testovací metadata a rozečtený UART
rámec, zavřou testovací obrazovku a umožní znovu získat živá data. Nemění NVS,
ladění ani SLS buffer. Pevné bloky EPG zůstávají rezervované pro opakované použití;
nevzniká nová alokace při každém segmentu nebo dalším DEMO.

`TIME` vyžaduje explicitní UTC offset nebo `Z`; datum se nepřepisuje na dnešní.
`STATION` přijímá úplný binární bearer v hexu nebo jednoznačné SID v hexu
**bez prefixu 0x**. SID zadané před SI čeká na mapování; neznámá nebo konfliktní
identita je oznámena. Stanice v TEST se vybírá pouze virtuálně, rádio se nepřelaďuje.

`EPG TEST START` spustí prázdnou testovací relaci pro PC, výchozí vstupní
identita je E23153FE/01F2C00C. Nástroj níže používá variantu
`EPG TEST START SID FULLCID` podle skutečných metadat RAW. Odpověď obsahuje novou
session, kterou musí nést každý datový fragment.

## 2. Úplný replay původního záznamu

Nejdříve **zavřete PlatformIO/Arduino Serial Monitor**. Jediný COM port nemůže
současně vlastnit monitor a Python. Replay nástroj má vlastní živý monitor
s možností zadávat příkazy do jeho konzole.

```powershell
python -m pip install -r tools/requirements.txt
python tools/epg_replay.py --help
python tools/epg_replay.py "_usertesting/device-monitor-261023-160924.log" --dry-run
python tools/epg_replay.py "_usertesting/device-monitor-261023-160924.log" --port COM4 --baud 115200 --speed 1 --station 40e231533233 --time 2024-11-12T04:30:00+01:00 --loop
```

Vstup může být také původní ZIP s jediným `.log`/`.txt`. V tomto pracovním
prostředí je dostupný přímo rozbalený log. Jeho SHA-256:
`dbdbf20d2238ea023d0a699a887256db17be64af89bb57a292096efd668bbc81`.
Velký log, externí ZIP a hotové JSON nejsou součástí firmware ani patche.

Záznam má 3 080 deklarovaných `[SPI/PKT]`, ale pouze **1 939 úplných** rámců
s přesnými offsety, délkou a platným MSC CRC. Zbývajících **1 141** poškozených
či neúplných hex výpisů nástroj vynechá a oznámí; nic nedoplňuje odhadem.
Opakování carouselu přesto umožní zrekonstruovat všechny referenční objekty.
Původní záznam tedy nelze vydávat za přenos bez ztrát.

`--speed` určuje horní tempo paketů (1 = maximálně 20 paketů/s). Skutečnou
rychlost omezí UART a potvrzování, takže `--speed 10` nepřetíží nekonečnou frontu.
`--loop` opakuje celý zachycený stream, což umožní získat data po změně dne
nebo virtuální stanice. U 115200 Bd může celý přenos trvat několik až více minut;
pro prvotní kontrolu slouží DEMO. Nástroj nepřepíná baud přijímače.

Během replay lze do Python konzole napsat:

```text
EPG TEST STATUS
EPG TEST TIME 2024-11-12T10:30:00+01:00
EPG TEST STATION 40e231533211
quit
```

Povoleny jsou HELP/STATUS/TIME/STATION; `quit`, `exit`, `EPG TEST STOP` nebo Ctrl+C
přenos zastaví a pošlou STOP. Pokud nebylo použito `--loop`, nástroj po konci
ponechá port a testovací UI otevřené pro prohlížení a zasílá heartbeat STATUS.
Ztráta portu/ACK nebo NACK končí řízeně; při odpojeném kabelu zruší firmware
neaktivní UART relaci po **30 s**. DEMO tento UART watchdog nemá.

### Co má ukazovat diagnostika

- `EPG MOT tid=... kind=DIR/PI/SI/PNG`: dokončený objekt, počet záznamů a cache.
- `EPG OK STATUS`: rámy, MSC CRC, neplatné délky, duplicity a konflikty.
- `EPG DATA`: dokončené objekty, PI/SI/PNG, cache, LIMIT, nejednoznačnost, NOW/NEXT.
- `EPG RX`: data před adresářem, příliš velké/nepodporované objekty, timeouty,
  stáří dat a skoky continuity indexu. Skok není automaticky CRC chyba.
- `EPG RAM`: skutečná velikost arény, free/min/largest heap z desky, audio/SLS
  příznaky a UART drop/CRC/order/identity čítače.

Při výchozí Trójce a dni 12. 11. 2024 hostitelský úplný UART replay získal
**68 položek cache**, 2 PI, 1 SI a správně přiřazené PNG B390 (6 436 B).
137 zpracovaných položek obou PI zahrnuje i okolní den; to není 137 položek cache.
ESP32 záměrně neskladuje ani nedekóduje všechny stanice zároveň. Offline test
postupně zaměří každé TID stejným omezeným C++ jádrem a ověří všech 70 objektů
proti referenci. Tyto dvě statistiky nejsou zaměnitelné.

## 3. Obrazovky a ovládání

Použity jsou původní bitmapy, selektor, dynamické barvy tématu, FONT16/FONT28 a
stávající řádkový sprite. Sdílení assetů nepřidává další kopie bitmap do flash
ani nový celoplošný framebuffer. FONT16 obsahuje všechny polské znaky; chybějící
jiný znak se nahradí `?` a footer označí `~`. UTF-8 se neořezává uvnitř znaku.

| Ovladač | Akce v EPG |
|---|---|
| SL dlouze, 1 s | Otevřít/zavřít EPG |
| SL krátce | O úroveň zpět; mimo EPG původní slideshow až při uvolnění |
| ROT1 otočení | Výběr pořadu, v detailu posun celého názvu a popisu |
| ROT1 klik | Přehled → seznam → detail → seznam |
| ROT1 dlouze | Nespouští scan |
| ROT2 otočení | V seznamu den ±7; v detailu sousední pořad |
| ROT2 klik | Původní hlasitost; po zavření návrat do stejného EPG |
| MODE krátce | Zpět; z přehledu hlavní rádio, bez změny tune mode |
| MODE dlouze | Nastavení, pak návrat do stejné EPG stránky |
| Standby dlouze | Původní uspání má prioritu |

IR Slideshow mapping zůstává krátkou slideshow/zpět akcí. IR ladicí klávesy
používají kontext EPG; jejich stisk zde neladí ani ve FM replay. Původní dlouhé
IR MODE stále přepíná DAB/FM, tím se test ukončí. Mechanický dlouhý MODE otevírá
nastavení. Bootovací kombinace SL/ROT1 jsou zachovány.

Přehled obsahuje název stanice, čas, NOW, plánovaný interval, průběh a další
časovou položku. Seznam má 9 řádků po 20 px. Detail poskytuje interval/délku,
volitelný actual start a posuv celého názvu/popisů. Časy původního plánu se
neposouvají podle sousedních pořadů ani podle actualTime.

Cache zachovává až osm kombinací stanice/dne v celkovém limitu 96 pořadů
a 8 KiB textů. Návrat k uchovanému výběru zobrazí data ihned; nenačtený nebo
uvolněný výběr čeká na další carousel. Při nedostatku kapacity se uvolňuje
nejdéle nepoužitý jiný výběr. Nejde o sedmidenní archiv; u replay ponechte `--loop`. Při překryvu platných intervalů
NOW deterministicky vybere nejpozdější plánovaný začátek, při shodě menší shortId;
UI zobrazí upozornění na překryv. NEXT je první budoucí plánovaný začátek, může
ležet ještě během delšího souběžného pořadu.

Polskie Radio24/Chopin mají v dodaných SI stejné `40e2315332a3`. Firmware tuto
identitu označí jako nejednoznačnou, nepřiřadí jí náhodné logo ani NOW/NEXT.

## 4. Přesné limity a časová platnost

- Paměť EPG na ESP32: **50 800 B** bez režie alokátoru, pět bloků.
  Metadata a kopie textů pro UI zabírají 13 936 B bajtové DRAM; čtyři buffery
  potřebují 36 864 B 32bitové IRAM (objekt 12 288, adresář 4 096, logo 12 288,
  textový pool 8 192 B). `EPG RAM arena` uvádí součet obou oblastí.
- IRAM se čte a zapisuje pouze zarovnanými 32bitovými instrukcemi přes
  `Bytes`/`Buffer`. Text pro TFT a vstup PNGdec se kopírují do bajtové paměti;
  žádný bajtový pointer do IRAM se nepředává TFT ani PNGdec.
- EPG loga do 100×70 i slideshow do 320×240 podporují Adam7 prokládání přes
  nový `png_adam7` modul. Neprokládaná PNG nadále dekóduje PNGdec.
  Loga B385 a B389 prošla porovnáním všech RGB565 pixelů s nezávislým dekodérem
  Windows. Ostatních 28 log se shoduje s původní PNGdec cestou.
- Adam7 používá stávající 76 800B pracovní paměť PNG/JPEG, žádný nový framebuffer
  ani heap. Podporuje PNG typy 0/2/3/4/6 s příslušnými hloubkami 1/2/4/8/16 bitů,
  PLTE/tRNS, všechny filtry, rozdělené IDAT a bílý podklad pro průhlednost.
  Úplný CRC/zlib/Adam7 validační průchod předchází kreslení. Začátek pracovního
  bloku se uvnitř původní arény zarovná pro 64bitový inflate akumulátor; případný
  posun se odečte od kapacity. Na desce byl potřebný posun 4 B.
- Nejvýše 80 položek adresáře, 16 identit služeb, 96 pořadů celkem ve všech uložených výběrech a
  společný 8KiB textový pool. Eviduje se nejvýše osm kombinací stanice/dne. Název nejvýše 127 B, popis 511 B, se zachováním UTF-8.
- Rezervace vyžaduje zvlášť prostor pro metadata a nejméně 12 000 B rezervy
  v INTERNAL|8BIT a prostor pro buffery s rezervou 4 096 B v INTERNAL|32BIT|EXEC.
  Součet `ESP.getFreeHeap()` se k rozhodování nepoužívá. Chyba vypíše
  `need8/free8/largest8/need32/free32/largest32`; STATUS přidává `EPG POOLS` a `EPG LOGO bytes/interlace`.
  Capability masky se mohou překrývat, údaje free8/free32 se proto nesčítají.
  Částečné selhání vrátí všechny přidělené bloky. Každý explicitní příkaz
  vyžadující EPG vrátí chybu paměti, automatický pokus ji vypíše jednou.
- Reassembly má nejvýše 32 segmentů na entitu. Parser pracuje nejvýše s
  24 úrovněmi a 48 kroky na průchod hlavním loopem.
- UART používá existující 128B linku, nejvýše 32 datových bajtů na fragment,
  121 znaků včetně LF, jeden rozpracovaný rámec do 4 071 B a jediný kredit.
- V1 nese session, sekvenci, offset, celkovou délku, SID/CID/source a CRC32
  ASCII prefixu. Stejný potvrzený fragment se znovu neaplikuje. START/STOP
  přijímače stále používají plné CID; zkrácené je přijato pouze jako source=0.
- ACK timeout 3 s, nejvýše 4 pokusy; BUSY čeká na parser v rámci tohoto timeoutu.
  CRC/order/identity/data NACK přenos ukončí. Částečný UART rámec vyprší po 5 s.
- Reassembly čeká podle deklarované periody carouselu (3 periody + 60 s,
  omezeno 120 s až 30 min; bez periody 300 s). Chybný segment nemění již
  validované bajty ani metadata. Těla jiných objektů nevyhazují cílový objekt.
- NOW/NEXT vyžaduje platný čas, platný interval a data čerstvá do 15 min.
  Živý čas se interpoluje od posledního DAB časového vzorku nejvýše 5 min.
  LTO je přenesené, nikoli dnešní časové pásmo PC. Data z roku 2024 nejsou živá v roce 2026.

LIMIT znamená omezení cache/textu; žádný overflow se nemá maskovat jako úplný
program. Větší nebo nepodporované/šifrované objekty se odmítnou/odloží s čítačem.
Komprimovaný MOT adresář, jiné obsahové profily a X-PAD SPI nejsou v této verzi
implementované. Rozhraní `Core::feed` je připravené pro další adaptér, který dodá
celou MSC skupinu; existující PAD Slideshow se do něj nepřesměrovává.

## 5. Testy na PC a ověření na přijímači

```powershell
python tests/run_epg_tests.py --compiler C:/msys64/ucrt64/bin/g++.exe
pio run -e esp32dev
```

Runner vytváří pouze dočasné výsledky v `.pio/epg_acceptance` a porovná produkční
C++ parser s původním RAW i referenčním ZIP. Testuje také skutečný runtime,
DEMO, fyzické obslužné funkce s TFT mockem, celý UART a skutečný Python nástroj
proti C++ runtime přes simulovaný COM včetně ztraceného ACK.

Na desce je ještě nutné samostatně ověřit:

1. DEMO a plný replay v DAB s audiem/SLS, ve FM a bez signálu. Porovnat výchozí
   a konečné CTS timeout/overflow/UARTdrop a free/min/largest heap, poslouchat zvuk.
2. Nový dokončený JPEG/PNG, DLS a příjem dalších SLS během otevřeného EPG i při
   zapnutém autoslideshow; přehled se nesmí automaticky nahradit obrázkem.
3. Všechna témata, displayflip, enkodéry, volume, nastavení, standby a původní UART.
4. STOP uprostřed objektu, výpadek kabelu, restart ESP32, změna stanice/muxu,
   slabý signál a návrat k živým datům. Následný DEMO/START musí fungovat znovu.
5. Na SPI multiplexu ověřit souběžný produkční START bez STOP SLS a plné CID.
   `DEBUG SPI` dočasně předá vlastníka původnímu 5s/30s diagnostickému testu.
   `DEBUG SPI STOP` příjem pozastaví do dalšího přeladění; nic trvale nezapisuje.

Hostitelské PASS nenahrazuje tyto HW kontroly. Podrobné výsledky a nesoulady s
referenčním Pythonem obsahuje `docs/EPG_IMPLEMENTATION_REPORT.md`.

Při UART replay sleduje hlavní indikátor EPG validovaný MOT adresář v testovacích
datech. Po samotném START zůstane neaktivní; po načtení úplného adresáře se
aktivuje. Po STOP nebo timeoutu znovu ukazuje živou detekci multiplexu. Pro
kontrolu se vraťte z EPG na hlavní obrazovku, aniž byste ukončili test.

Po návštěvě seznamu EPG a návratu na hlavní obrazovku ověřte původní velikost
infotextu, hodin a dalších dynamických textů i jejich průhledné pozadí.
Oprava zachování fontu je ověřena hostitelským testem. Verze s touto opravou
byla nahrána, DEMO i krátký RAW test prošly a uživatel potvrdil správný vzhled
textů a indikátoru. Novější úprava panelu, seznamu a překladů je také nahrána; průběžný UART replay
pro její vizuální ověření je spuštěný.


Čekací hláška EPG nově používá panel ve stylu čekání na slideshow. Seznam
při posunu zvýraznění obnovuje pouze dva řádky; při změně stránky nebo dat
oblast devíti řádků. Systémové texty sledují nastavený jazyk rádia včetně
diakritiky, pořady zůstávají ve vysílaném jazyce. Po nahrání ověřte plynulé
listování, přechod mezi stránkami/dny, čekací panel při prázdných datech,
jeho zmizení po načtení programu a překlady při změně jazyka v nastavení.


Ověření cache a čekacího panelu nové verze: načtěte program pro 12. 11. 2024,
přejděte ROT2 na dosud nenačtený den a zpět. Původní seznam se musí objevit
okamžitě bez čekacího panelu a bez dalšího příjmu. Totéž platí pro návrat
k jiné stanici, jejíž program zůstal v cache. Uvolněné výběry čekají na nový
příjem; svítící detekční EPG neznamená dostupnost každé stanice/dne.
Nová verze cache je nahrána na přijímači a prošla testem tří návratů ze stanice
i dne bez přijetí nových EPG rámců (stále frames=24, cached=68, NOW=8, NEXT=9).
Uživatel potvrdil okamžité obnovení seznamu po přepnutí dne pomocí ROT2.
Krátký UART test pokračuje na nové verzi; přenos používá COM4.


Ve verzi v2.5 používá hlavní indikátor EPG tři stavy:
šedá = EPG nedetekováno; cyan = detekováno, ale chybí platný program
pro zvolenou stanici/den; zelená = program pro aktuální výběr je načtený,
jednoznačný a čerstvý (do 15 minut). Samotný výpadek RF platnou cache nemaže.
Po změně dne/stanice nebo zestárnutí cache se barva přepočítá.

Stanice se přímo enkodéry v EPG nepřepíná. MODE krátce vrací z přehledu na
hlavní obrazovku, kde se vybere stanice; EPG se otevře dlouhým SL.
ROT2 v seznamu mění den, v detailu sousední pořad. UART TEST vybírá virtuální
stanici příkazem EPG TEST STATION, který nepřelaďuje skutečné rádio.


Oprava víceřádkových popisů: v detailu Jedynky má položka 06:40:00 Po pierwsze
rozdělení na vlastní řádek; před časem nesmí být náhradní otazník. Upozornění
pro posluchače na 6.00 zůstává součástí textu. Časy i názvy uvnitř popisů se
nefiltrují. LF/CRLF a prázdné řádky musí fungovat i při posouvání. Tato oprava
je ve v2.5b ověřená hostitelskými testy a buildem a nahraná na přijímači.
Jedynka data běží ve smyčce; uživatel potvrdil správné zalomení i text.


Seznam EPG nově používá dvě oblasti: čas vlevo a název vpravo, s jediným
mezerou mezi sloupci, bez svislé čáry. Při HW kontrole sledujte zarovnání
časů vlevo a názvů vpravo a že se žádná oddělovací čára neobjeví ani při
listování přes zvýrazněný řádek. Grafická úprava je sestavená, prošla
hostitelskými testy a je nahraná na přijímači. Jedynka data se posílají ve
smyčce; uživatel potvrdil správné sloupce i listování.
