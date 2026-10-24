# EPG / UART / Adam7 — HW test 24. 10. 2026

Výsledek: PASS pro níže vymezené PC a UART/HW testy. Aplikace byla se souhlasem
nahrána na ESP32-D0WD-V3 přes COM4, pouze na adresu 0x10000. NVS, seznamy
stanic a nastavení se nemazaly. Test skončil `EPG TEST STOP`, přijímač je LIVE
s aktivním audio service; COM4 je uvolněný. Bez zápisu na GitHub.

## Finální aplikace

- `.pio/build/esp32dev/firmware.bin`: 3 531 456 B, flash 0x10000.
- SHA256: `093EEBDF9A3E005F069B81E2B86C02F00124BFF4286852504625CEF37AAB314C`.
- Build PASS; statická RAM 66 496 B, flash 3 530 841 / 4 128 768 B.
- Celá PC sada `python tests/run_epg_tests.py`: PASS.

## Ověření na přijímači

| Test | Výsledek |
|---|---|
| DEMO, opakované STOP/DEMO | 42 dekódovaných záznamů, cache 39, NOW=12, NEXT=13; bez postupného úbytku heapu |
| RAW Trójka | PI=2, SI=1, PNG=1, 137 zpracovaných záznamů, cache 68, NOW=8, NEXT=9; shoda s referencí |
| NOW/NEXT na displeji | Uživatel potvrdil „Zapraszamy do Trójki“ a „Serwis Trójki“ |
| Běžné PNG | Logo Trójky B390, 6436 B, potvrzené uživatelem |
| Adam7 B385 | Radio Kraków, 5401 B, 100×70, validated=1, draw=1, pad=4 |
| Adam7 B389 | OFF RadioKraków, 5206 B, 100×70, validated=1, draw=1, pad=4 |
| Adam7 obraz na TFT | Uživatel potvrdil správný obraz obou log po opravě |
| Finální firmware po opravě | Znovu DEMO, Trójka PI/SI/PNG a návrat STOP → LIVE: PASS |

Původní RAW má 3080 deklarovaných paketů, ale jen 1939 úplných CRC-validních
rámců. Přenos v původním pořadí skončil odpojením COM4 po potvrzeném kontrolním
bodu 1501. Po opětovném připojení byla potřebná data obnovena z 24 původních MSC
skupin a přehráno zbývajících 438 rámců (1502–1939). Všechny úplné RAW rámce
jsou tak pokryté; nebyl to jediný nepřerušený běh. Konečný stav odpovídá PC.
CRC/invalid/conflict/UARTdrop/rxCRC/rxOrder/rxIdentity zůstaly nulové. NACK BUSY
při zpracování SI/PI představovaly řízení toku, nástroj fragmenty zopakoval.

## Paměť a opravené závady

První verze vyžadovala souvislých 49736 B v INTERNAL|8BIT, zatímco největší
blok měl 25588 B. Samotné rozdělení na pět DRAM bloků nestačilo: celkových
82640 B INTERNAL zahrnovalo i paměť s omezeným přístupem, pro bajtová data
zbývalo jen 28360 B. Finální EPG drží metadata 13560 B v bajtové DRAM a
buffery 36864 B ve word-only oblasti, přístupné přes zarovnané L32I/S32I.
Celkem 50424 B bez režie alokátoru. Na finálním HW testu free=32136,
free8=14584, min INTERNAL=31796. Capability masky se mohou překrývat;
free8 a free32 se nesčítají.

PC testy Adam7 původně používaly osmibajtově zarovnaný workspace. Na desce
měl stávající blok pouze čtyřbajtové zarovnání. Diagnostika odhalila
`validated=0 stage=workspace`, přestože PNG bylo uložené správně. Dekodér nyní
zarovná začátek uvnitř původní 76800B arény a odečte posun od kapacity.
Na desce oba obrázky potřebovaly pad=4. Žádná další alokace nebo framebuffer
nepřibyl. Nová regrese ověřuje začátky posunuté o 1–7 B, přesný obraz, ochranné
bajty i odmítnutí nedostatečné kapacity.

PC testy porovnaly všech 70 MOT objektů / 1087 programových záznamů a 30 PNG
s referencí. U 28 PNG se shodují pixely s PNGdec, u obou Adam7 log s nezávislým
Windows dekodérem. Adam7 dále prošel 60 kombinacemi barev, hloubek, filtrů,
rozměrů až 320×240 a osmi poškozenými streamy.

## Evidence a rozsah

Logy jsou v `.pio/epg_acceptance/`:

- `hardware_iram_adam7_demo.log`: původní čtyři DEMO cykly.
- `hardware_full_replay.log`: přenos do výpadku COM4.
- `hardware_resume_replay.log`: obnovení a dokončení zbytku RAW.
- `hardware_adam7_diagnostics.log`: reprodukce chyby workspace.
- `hardware_adam7_alignment_fixed.log`: obě úspěšná Adam7 loga po opravě.
- `hardware_final_smoke.log`: finální DEMO/Trójka/STOP → LIVE.
- `hardware_report.json`, `results.log`, `results.json`: souhrn a PC testy.

Na HW je ověřen UART replay a obraz log; samostatný poslech, měření BCK a
živý packet SPI příjem z polského multiplexu v této relaci ověřeny nebyly.
Ovládání seznamu/detailu a tlačítek prošlo hostitelským testem skutečných funkcí,
nikoli novým mechanickým testem všech ovladačů. UI zatím používá hlášku
„EPG nedostupne / zastarale“ i při čekání na první úplný PI; po načtení cache
správně zmizela, což uživatel potvrdil.
