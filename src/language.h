#ifndef LANGUAGE_H
#define LANGUAGE_H

#include "FmRegion.h"

#define VERSION "v2.4"

// [number of languages][number of texts]
// *** means the text is the same as in English
static const char* const myLanguage[8][82] PROGMEM = {
  {
    "English",                    // English
    "Rotary direction changed",   // 1
    "Please release button",      // 2
    "Screen flipped",             // 3
    "Calibrate analog meter",     // 4
    "Release button when ready",  // 5
    "encoder set to optical",     // 6
    "encoder set to standard",    // 7
    "SI-DAB receiver",            // 8
    "Software",                   // 9
    "Defaults loaded",            // 10
    "Channel list",               // 11
    "Language",                   // 12
    "Brightness",                 // 13
    "Theme",                      // 14
    "Auto slideshow",             // 15
    "Signal unit",                // 16
    "Radio mode",                 // 17
    "",                           // 18
    "PRESS MODE TO RETURN",       // 19
    "CONFIGURATION",              // 20
    "High",                       // 21
    "Low",                        // 22
    "On",                         // 23
    "Off",                        // 24
    "Time-out Timer",             // 25
    "Min.",                       // 26
    "System info",                          // 27
    "Tuner",                                // 28
    "Radio control",                        // 29
    "GPIO12",                               // 30
    "VDD_SDIO",                             // 31
    "ESP32 / software",                     // 32
    "Uptime",                               // 33
    "Reset reason",                         // 34
    "RAM free / min",                       // 35
    "Heap block / frag.",                   // 36
    "Unknown",                    // 37
    "News",                       // 38
    "Current Affairs",            // 39
    "Information",                // 40
    "Sport",                      // 41
    "Education",                  // 42
    "Drama",                      // 43
    "Culture",                    // 44
    "Science",                    // 45
    "Varied",                     // 46
    "Pop Music",                  // 47
    "Rock Music",                 // 48
    "Easy Listening",             // 49
    "Light Classical",            // 50
    "Serious Classical",          // 51
    "Other Music",                // 52
    "Weather",                    // 53
    "Finance",                    // 54
    "Children's",                 // 55
    "Social Affairs",             // 56
    "Religion",                   // 57
    "Phone In",                   // 58
    "Travel",                     // 59
    "Leisure",                    // 60
    "Jazz Music",                 // 61
    "Country Music",              // 62
    "National Music",             // 63
    "Oldies Music",               // 64
    "Folk Music",                 // 65
    "Documentary",                // 66
    "",                           // 67
    "",                           // 68
    "",                           // 69
    "",                           // 70
    "",                           // 71
    "FM/DAB Receiver",            // 72
    "Waiting for list",           // 73
    "Select service",             // 74
    "Tuning...",                  // 75
    "No signal",                  // 76
    "Tuner not detected!",        // 77
    "STAND-BY MODE",              // 78
    "Development",                // 79
    "Graphic Design",             // 80
    "About"                       // 81
  },

  {
    "Nederlands",                       // Dutch
    "Rotary richting aangepast",        // 1
    "Laat aub de knop los",             // 2
    "Scherm gedraaid",                  // 3
    "Kalibratie analoge meter",         // 4
    "Laat knop los indien gereed",      // 5
    "encoder ingesteld als optisch",    // 6
    "encoder ingesteld als standaard",  // 7
    "SI-DAB ontvanger",                 // 8
    "Software",                         // 9
    "Opnieuw geconfigureerd",           // 10
    "Kanalenlijst",                     // 11
    "Taal",                             // 12
    "Helderheid",                       // 13
    "Thema",                            // 14
    "Auto diavoorstelling",             // 15
    "Signaal eenheid",                  // 16
    "Radiomodus",                       // 17
    "",                                 // 18
    "DRUK MODE OM AF TE SLUITEN",       // 19
    "CONFIGURATIE",                     // 20
    "Hoog",                             // 21
    "Laag",                             // 22
    "Aan",                              // 23
    "Uit",                              // 24
    "Auto uitschakelen",                // 25
    "Min.",                             // 26
    "Systeeminfo",                          // 27
    "Tuner",                                // 28
    "Radiobesturing",                       // 29
    "GPIO12",                               // 30
    "VDD_SDIO",                             // 31
    "ESP32 / software",                     // 32
    "Bedrijfstijd",                         // 33
    "Resetreden",                           // 34
    "RAM vrij / min",                       // 35
    "Heapblok / frag.",                     // 36
    "Onbekend",                         // 37
    "Nieuws",                           // 38
    "Actualiteit",                      // 39
    "Informatie",                       // 40
    "Sport",                            // 41
    "Educatie",                         // 42
    "Drama",                            // 43
    "Cultuur",                          // 44
    "Wetenschap",                       // 45
    "Varia",                            // 46
    "Popmuziek",                        // 47
    "Rockmuziek",                       // 48
    "Ontspanningsmuziek",               // 49
    "Licht klassiek",                   // 50
    "Klassiek",                         // 51
    "Overige muziek",                   // 52
    "Weerbericht",                      // 53
    "Economie",                         // 54
    "Kinderen",                         // 55
    "Maatschappelijk",                  // 56
    "Religie",                          // 57
    "Doe mee!",                         // 58
    "Reizen",                           // 59
    "Vrije tijd",                       // 60
    "Jazz",                             // 61
    "Country",                          // 62
    "Nat. muziek",                      // 63
    "Gouwe ouwe",                      // 64
    "Volksmuziek",                      // 65
    "Documentaires",                    // 66
    "",                                 // 67
    "",                                 // 68
    "",                                 // 69
    "",                                 // 70
    "",                                 // 71
    "DAB Ontvanger",                    // 72
    "Lijst ophalen...",                 // 73
    "Kies service",                     // 74
    "Afstemmen....",                    // 75
    "Geen signaal",                     // 76
    "Tuner niet verbonden!",            // 77
    "STAND-BY MODUS",                   // 78
    "Ontwikkeling",                     // 79
    "Grafisch Ontwerp",                 // 80
    "Over"                              // 81
  },

  {
    "Ελληνικά",                              // Greek
    "Η διεύθυνση του ρότορα άλλαξε",         // 1
    "Ελευθερώστε το πλήκτρο",                // 2
    "Η οθόνη αναποδογύρισε",                 // 3
    "Βαθμονόμηση αναλογικού μετρητή",        // 4
    "Αφήστε το πλήκτρο όταν είστε έτοιμοι",  // 5
    "ο κωδικοποιητής ορίστηκε σε οπτικός",   // 6
    "ο κωδικοποιητής ορίστηκε σε στάνταρ",   // 7
    "Δέκτης SI-DAB",                         // 8
    "Λογισμικό",                             // 9
    "Φορτώθηκαν οι προεπιλογές",             // 10
    "Λίστα καναλιών",                        // 11
    "Γλώσσα",                                // 12
    "Φωτεινότητα",                           // 13
    "Θέμα",                                  // 14
    "Αυτόματη παρουσίαση",                   // 15
    "Μονάδες σήματος",                       // 16
    "Λειτουργία ραδιοφώνου",                 // 17
    "",                                      // 18
    "ΠΙΕΣΤΕ MODE ΓΙΑ ΕΠΙΣΤΡΟΦΗ",             // 19
    "ΡΥΘΜΙΣΕΙΣ",                             // 20
    "Υψηλό",                                 // 21
    "Χαμηλό",                                // 22
    "Ενεργό",                                // 23
    "Ανενεργό",                              // 24
    "Χρονοδιακόπτης λήξης",                  // 25
    "λεπτά",                                 // 26
    "Στοιχεία συστήματος",                  // 27
    "Tuner",                                // 28
    "Έλεγχος radio",                        // 29
    "GPIO12",                               // 30
    "VDD_SDIO",                             // 31
    "ESP32 / λογισμικό",                    // 32
    "Χρόνος λειτουργ.",                     // 33
    "Αιτία reset",                          // 34
    "RAM ελεύθ./ελάχ.",                     // 35
    "Heap block / frag.",                   // 36
    "Άγνωστο",                               // 37
    "Ειδήσεις",                              // 38
    "Επικαιρότητα",                          // 39
    "Πληροφορίες",                           // 40
    "Σπορ",                                  // 41
    "Εκπαίδευση",                            // 42
    "Δράμα",                                 // 43
    "Κουλτούρα",                             // 44
    "Επιστήμη",                              // 45
    "Ποικίλο",                               // 46
    "Ποπ Μουσική",                           // 47
    "Ροκ Μουσική",                           // 48
    "Εύκολη ακρόαση",                        // 49
    "Ελαφρά Κλασική",                        // 50
    "Σοβαρή Κλασική",                        // 51
    "Άλλη Μουσική",                          // 52
    "Καιρός",                                // 53
    "Οικονομικά",                            // 54
    "Παιδικά",                               // 55
    "Κοινωνικά",                             // 56
    "Θρησκεία",                              // 57
    "Τηλεφωνικά",                            // 58
    "Ταξίδια",                               // 59
    "Ελεύθερος χρόνος",                      // 60
    "Τζαζ Μουσική",                          // 61
    "Κάντρι Μουσική",                        // 62
    "Εθνική Μουσική",                        // 63
    "Παλιά Τραγούδια",                       // 64
    "Παραδοσιακά",                           // 65
    "Ντοκιμαντέρ",                           // 66
    "",                                      // 67
    "",                                      // 68
    "",                                      // 69
    "",                                      // 70
    "",                                      // 71
    "Δέκτης DAB",                            // 72
    "Αναμονή λίστας",                        // 73
    "Επιλογή υπηρεσίας",                     // 74
    "Συντονισμός...",                        // 75
    "Χωρίς σήμα",                            // 76
    "Το tuner δεν εντοπίστηκε!",             // 77
    "ΑΝΑΜΟΝΗ",                               // 78
    "Ανάπτυξη",                              // 79
    "Γραφικά",                               // 80
    "Περί"                                   // 81
  },

  {
    "Deutsch",                          // Deutsch
    "Drehrichtung geändert",            // 1
    "Bitte Taste loslassen",            // 2
    "Bildschirm gedreht",               // 3
    "Analoge Anzeige kalibrieren",      // 4
    "Taste loslassen, wenn bereit..",   // 5
    "Encoder auf 'optisch' gesetzt",    // 6
    "Encoder auf Standard gesetzt",     // 7
    "SI-DAB-Empfänger",                 // 8
    "Software",                         // 9
    "Standardeinstellungen geladen",    // 10
    "Kanalliste",                       // 11
    "Sprache",                          // 12
    "Helligkeit",                       // 13
    "Thema",                            // 14
    "Automatische Slideshow",           // 15
    "Messeinheiten",                    // 16
    "Radiomodus",                       // 17
    "",                                 // 18
    "MODE DRÜCKEN, UM ZURÜCKZUKEHREN",  // 19
    "KONFIGURATION",                    // 20
    "Hoch",                             // 21
    "Niedrig",                          // 22
    "An",                               // 23
    "Aus",                              // 24
    "Zeit bis Standby",                 // 25
    "Min.",                             // 26
    "Systeminfo",                           // 27
    "Tuner",                                // 28
    "Radiosteuerung",                       // 29
    "GPIO12",                               // 30
    "VDD_SDIO",                             // 31
    "ESP32 / Software",                     // 32
    "Laufzeit",                             // 33
    "Reset-Grund",                          // 34
    "RAM frei / min",                       // 35
    "Heapblock / Frag.",                    // 36
    "Unbekannt",                        // 37
    "Nachrichten",                      // 38
    "Aktuelle Ereignisse",              // 39
    "Information",                      // 40
    "Sport",                            // 41
    "Bildung",                          // 42
    "Drama",                            // 43
    "Kultur",                           // 44
    "Wissenschaft",                     // 45
    "Gemischt",                         // 46
    "Popmusik",                         // 47
    "Rockmusik",                        // 48
    "Leichte Musik",                    // 49
    "Leichte Klassik",                  // 50
    "Ernsthafte Klassik",               // 51
    "Andere Musik",                     // 52
    "Wetter",                           // 53
    "Finanzen",                         // 54
    "Kinder",                           // 55
    "Soziale Angelegenheiten",          // 56
    "Religion",                         // 57
    "Call-In Sendung",                  // 58
    "Reisen",                           // 59
    "Freizeit",                         // 60
    "Jazzmusik",                        // 61
    "Countrymusik",                     // 62
    "Nationalmusik",                    // 63
    "Oldies Musik",                     // 64
    "Folkmusik",                        // 65
    "Dokumentation",                    // 66
    "",                                 // 67
    "",                                 // 68
    "",                                 // 69
    "",                                 // 70
    "",                                 // 71
    "DAB-Empfänger",                    // 72
    "Warte auf Liste",                  // 73
    "Service auswählen",                // 74
    "Suche...",                         // 75
    "Kein Signal",                      // 76
    "Tuner nicht erkannt!",             // 77
    "STANDBY-MODUS",                    // 78
    "Entwicklung",                      // 79
    "Grafikdesign",                     // 80
    "Über die Software"                 // 81
  },

  {
    "Français",                         // Français
    "Sens de rotation modifié",          // 1
    "Relâchez le bouton",                // 2
    "Écran inversé",                     // 3
    "Calibrer l'indicateur",             // 4
    "Relâchez le bouton quand prêt",     // 5
    "Encodeur réglé optique",            // 6
    "Encodeur réglé standard",           // 7
    "Récepteur SI-DAB",                  // 8
    "Logiciel",                          // 9
    "Réglages par défaut",               // 10
    "Liste des canaux",                  // 11
    "Langue",                            // 12
    "Luminosité",                        // 13
    "Thème",                             // 14
    "Diapo automatique",                 // 15
    "Unité de signal",                   // 16
    "Mode radio",                        // 17
    "",                                  // 18
    "APPUYEZ MODE POUR RETOUR",          // 19
    "CONFIGURATION",                     // 20
    "Haut",                              // 21
    "Bas",                               // 22
    "Marche",                            // 23
    "Arrêt",                             // 24
    "Temporisation",                     // 25
    "Min.",                              // 26
    "Infos système",                        // 27
    "Tuner",                                // 28
    "Contrôle radio",                       // 29
    "GPIO12",                               // 30
    "VDD_SDIO",                             // 31
    "ESP32 / logiciel",                     // 32
    "Durée active",                         // 33
    "Cause reset",                          // 34
    "RAM libre / min",                      // 35
    "Bloc heap / frag.",                    // 36
    "Inconnu",                           // 37
    "Nouvelles",                         // 38
    "Actualité",                         // 39
    "Information",                       // 40
    "Sport",                             // 41
    "Éducation",                         // 42
    "Dramatique",                        // 43
    "Culture",                           // 44
    "Science",                           // 45
    "Variété",                           // 46
    "Musique pop",                       // 47
    "Rock",                              // 48
    "Musique légère",                    // 49
    "Classique léger",                   // 50
    "Classique sérieux",                 // 51
    "Autres musiques",                   // 52
    "Météo",                             // 53
    "Économie",                          // 54
    "Enfants",                           // 55
    "Affaires sociales",                 // 56
    "Religion",                          // 57
    "Téléphone",                         // 58
    "Voyage",                            // 59
    "Loisir",                            // 60
    "Jazz",                              // 61
    "Country",                           // 62
    "Musique nationale",                 // 63
    "Musique ancienne",                  // 64
    "Folk",                              // 65
    "Documentaires",                     // 66
    "",                                  // 67
    "",                                  // 68
    "",                                  // 69
    "",                                  // 70
    "",                                  // 71
    "DAB plus",                          // 72
    "Attente liste",                     // 73
    "Sélectionnez service",              // 74
    "Recherche...",                      // 75
    "Pas de signal",                     // 76
    "Tuner non détecté!",                // 77
    "MODE VEILLE",                       // 78
    "Développement",                     // 79
    "Design graphique",                  // 80
    "À propos"                           // 81
  },

  {
    "Español",                       // Español
    "Cambio de rotación",            // 1
    "Soltar el botón",               // 2
    "Alternar pantalla",             // 3
    "Calibrar el contador",          // 4
    "Soltar el botón y comenzar",    // 5
    "Codificador óptico ajustable",  // 6
    "Codificador en estándar",       // 7
    "Recepción SI-DAB",              // 8
    "Software",                      // 9
    "Carga predeterminada",          // 10
    "Lista de canales",              // 11
    "Idioma",                        // 12
    "Brillo",                        // 13
    "Tema",                          // 14
    "Diapositivas automáticas",      // 15
    "Unidad de señal",               // 16
    "Modo de radio",                 // 17
    "",                              // 18
    "Presionar para volver",         // 19
    "CONFIGURACIÓN",                 // 20
    "Arriba",                        // 21
    "Abajo",                         // 22
    "Encendido",                     // 23
    "Apagado",                       // 24
    "Tiempo",                        // 25
    "Mín.",                          // 26
    "Info del sistema",                     // 27
    "Sintonizador",                         // 28
    "Control radio",                        // 29
    "GPIO12",                               // 30
    "VDD_SDIO",                             // 31
    "ESP32 / software",                     // 32
    "Tiempo activo",                        // 33
    "Causa reinicio",                       // 34
    "RAM libre / mín.",                     // 35
    "Bloque heap / frag.",                  // 36
    "Desconocido",                   // 37
    "Noticias",                      // 38
    "Actualidad",                    // 39
    "Información",                   // 40
    "Deporte",                       // 41
    "Educación",                     // 42
    "Drama",                         // 43
    "Cultura",                       // 44
    "Ciencia",                       // 45
    "Variedades",                    // 46
    "Música pop",                    // 47
    "Rock",                          // 48
    "Relajante",                     // 49
    "Clásica ligera",                // 50
    "Clásica seria",                 // 51
    "Otras músicas",                 // 52
    "Clima",                         // 53
    "Economía",                      // 54
    "Infantil",                      // 55
    "Asuntos sociales",              // 56
    "Religión",                      // 57
    "Llamadas telefónicas",          // 58
    "Viajes",                        // 59
    "Ocio",                          // 60
    "Jazz",                          // 61
    "Country",                       // 62
    "Música nacional",               // 63
    "Música antigua",                // 64
    "Folk",                          // 65
    "Documentales",                  // 66
    "",                              // 67
    "",                              // 68
    "",                              // 69
    "",                              // 70
    "",                              // 71
    "DAB plus",                      // 72
    "Esperando lista",               // 73
    "Seleccione por favor",          // 74
    "Buscando....",                  // 75
    "Sin señal",                     // 76
    "Sin sintonizador",              // 77
    "MODO EN ESPERA",                // 78
    "Desarrollo",                    // 79
    "Diseño gráfico",                // 80
    "Acerca de"                      // 81
  },

  {
    "Polski",                            // Polski
    "Kierunek obrotu zmieniony",         // 1
    "Zwolnij przycisk",                  // 2
    "Ekran obrócony",                    // 3
    "Kalibracja wskaźnika analogowego",  // 4
    "Zwolnij przycisk gdy gotowe",       // 5
    "wybrano enkoder optyczny",          // 6
    "wybrano enkoder standardowy",       // 7
    "Odbiornik SI-DAB",                  // 8
    "Oprogramowanie",                    // 9
    "Ustawienia domyślne załadowane",    // 10
    "Lista kanałów",                     // 11
    "Język",                             // 12
    "Jasność",                           // 13
    "Motyw",                             // 14
    "Automatyczny pokaz slajdów",        // 15
    "Jednostka sygnału",                 // 16
    "Tryb radia",                        // 17
    "",                                  // 18
    "NACIŚNIJ MODE ABY WYJŚĆ",           // 19
    "KONFIGURACJA",                      // 20
    "Wysoki",                            // 21
    "Niski",                             // 22
    "Włącz",                             // 23
    "Wyłącz",                            // 24
    "Czas do wyłączenia",                // 25
    "Min.",                              // 26
    "Info systemowe",                       // 27
    "Tuner",                                // 28
    "Sterowanie radia",                     // 29
    "GPIO12",                               // 30
    "VDD_SDIO",                             // 31
    "ESP32 / program",                      // 32
    "Czas pracy",                           // 33
    "Przyczyna resetu",                     // 34
    "RAM wolna / min",                      // 35
    "Blok heap / frag.",                    // 36
    "Nieznany",                          // 37
    "Wiadomości",                        // 38
    "Aktualności",                       // 39
    "Informacje",                        // 40
    "Sport",                             // 41
    "Edukacja",                          // 42
    "Teatr",                             // 43
    "Kultura",                           // 44
    "Nauka",                             // 45
    "Różne",                             // 46
    "Muzyka pop",                        // 47
    "Muzyka rockowa",                    // 48
    "Muzyka lekka",                      // 49
    "Klasyka lekka",                     // 50
    "Klasyka poważna",                   // 51
    "Inna muzyka",                       // 52
    "Pogoda",                            // 53
    "Finanse",                           // 54
    "Dla dzieci",                        // 55
    "Sprawy społeczne",                  // 56
    "Religia",                           // 57
    "Telefon do studia",                 // 58
    "Podróże",                           // 59
    "Czas wolny",                        // 60
    "Jazz",                              // 61
    "Country",                           // 62
    "Muzyka narodowa",                   // 63
    "Stare przeboje",                    // 64
    "Muzyka folkowa",                    // 65
    "Dokumentalny",                      // 66
    "",                                  // 67
    "",                                  // 68
    "",                                  // 69
    "",                                  // 70
    "",                                  // 71
    "Odbiornik DAB",                     // 72
    "Oczekiwanie na listę",              // 73
    "Wybierz stację",                    // 74
    "Strojenie...",                      // 75
    "Brak sygnału",                      // 76
    "Tuner nie wykryty!",                // 77
    "TRYB CZUWANIA",                     // 78
    "Programowanie",                     // 79
    "Projekt graficzny",                 // 80
    "O programie"                        // 81
  },

  {
    "Romana",                              // Romana
    "Direcția de rotație inversată",       // 1
    "Vă rugăm să eliberați butonul",       // 2
    "Ecranul a fost inversat",             // 3
    "Calibrarea contorului analogic",      // 4
    "Eliberați butonul când este gata",    // 5
    "encoder setat optic",                  // 6
    "encoder setat standard",               // 7
    "Receptor SI-DAB",                      // 8
    "Software",                             // 9
    "Setări inițiale",                      // 10
    "Lista canalelor",                      // 11
    "Limba",                                // 12
    "Luminozitate",                         // 13
    "Tema",                                 // 14
    "Derulare imagini",                     // 15
    "Măsurare semnal",                      // 16
    "Mod radio",                            // 17
    "",                                     // 18
    "APASĂ MODE PENTRU IEȘIRE",             // 19
    "CONFIGURARE",                          // 20
    "Sus",                                  // 21
    "Jos",                                  // 22
    "Pornit",                               // 23
    "Oprit",                                // 24
    "Oprire după",                          // 25
    "Min.",                                 // 26
    "Info sistem",                          // 27
    "Tuner",                                // 28
    "Control radio",                        // 29
    "GPIO12",                               // 30
    "VDD_SDIO",                             // 31
    "ESP32 / software",                     // 32
    "Timp activ",                           // 33
    "Cauză reset",                          // 34
    "RAM liber / min",                      // 35
    "Bloc heap / frag.",                    // 36
    "Necunoscut",                           // 37
    "Știri",                                // 38
    "Actualități",                          // 39
    "Informații",                           // 40
    "Sport",                                // 41
    "Educație",                             // 42
    "Dramă",                                // 43
    "Cultură",                              // 44
    "Știință",                              // 45
    "Diverse",                              // 46
    "Muzică pop",                           // 47
    "Muzică rock",                          // 48
    "Muzică ușoară",                        // 49
    "Clasică ușoară",                       // 50
    "Muzică clasică",                       // 51
    "Altă muzică",                          // 52
    "Vreme",                                // 53
    "Finanțe",                              // 54
    "Copii",                                // 55
    "Probleme sociale",                     // 56
    "Religie",                              // 57
    "Telefon în direct",                    // 58
    "Călătorii",                            // 59
    "Timp liber",                           // 60
    "Muzică jazz",                          // 61
    "Muzică country",                       // 62
    "Muzică națională",                     // 63
    "Muzică veche",                         // 64
    "Muzică folk",                          // 65
    "Documentare",                          // 66
    "",                                     // 67
    "",                                     // 68
    "",                                     // 69
    "",                                     // 70
    "",                                     // 71
    "Receptor DAB",                         // 72
    "Așteptare listă",                      // 73
    "Selectare program",                    // 74
    "Căutare...",                           // 75
    "Fără semnal",                          // 76
    "Tuner nedetectat!",                    // 77
    "MOD AȘTEPTARE",                        // 78
    "Dezvoltare",                           // 79
    "Grafică",                              // 80
    "Despre"                                // 81
  }
};

// Reset reason strings used by System info. Technical fault names such as WDT,
// Brownout and SDIO stay recognizable across languages; ordinary states are
// localized. The table lives in flash and adds no runtime heap allocation.
static const char* const systemResetReasonText[8][9] PROGMEM = {
  {"Power-on", "External", "Software", "Panic", "WDT", "Deep sleep", "Brownout", "SDIO", "Unknown"},
  {"Inschakelen", "Extern", "Software", "Panic", "WDT", "Diepe slaap", "Brownout", "SDIO", "Onbekend"},
  {"Εκκίνηση", "Εξωτερικό", "Λογισμικό", "Panic", "WDT", "Βαθύς ύπνος", "Brownout", "SDIO", "Άγνωστο"},
  {"Einschalten", "Extern", "Software", "Panic", "WDT", "Tiefschlaf", "Brownout", "SDIO", "Unbekannt"},
  {"Mise sous tension", "Externe", "Logiciel", "Panic", "WDT", "Sommeil profond", "Brownout", "SDIO", "Inconnu"},
  {"Encendido", "Externo", "Software", "Panic", "WDT", "Sueño profundo", "Brownout", "SDIO", "Desconocido"},
  {"Zasilanie", "Zewnętrzny", "Programowy", "Panic", "WDT", "Głęboki sen", "Brownout", "SDIO", "Nieznany"},
  {"Pornire", "Extern", "Software", "Panic", "WDT", "Somn profund", "Brownout", "SDIO", "Necunoscut"}
};

// FM-only labels that have no DAB counterpart in the base 82-string table.
static const char* const fmMultipathText[8] PROGMEM = {
  "Multipath", "Multipad", "Πολλαπλή διαδρομή", "Mehrweg", "Trajets multiples",
  "Multitrayecto", "Wielodrogowość", "Propagare multiplă"
};
static const char* const fmStereoBlendText[8] PROGMEM = {
  "Stereo blend", "Stereomix", "Μίξη στερεοφωνίας", "Stereomischung", "Mélange stéréo",
  "Mezcla estéreo", "Miks stereo", "Mix stereo"
};
static const char* const radioModeValueText[2] PROGMEM = {"DAB", "FM"};
static const char* const fmRegionMenuText[8] PROGMEM = {
  "FM Region", "FM-regio", "Περιοχή FM", "FM-Region",
  "Région FM", "Región FM", "Region FM", "Regiune FM"
};
static const char* const fmRegionValueText[8][FM_REGION_COUNT] PROGMEM = {
  {"Europe", "N.America", "Japan"},
  {"Europa", "N.Amerika", "Japan"},
  {"Ευρώπη", "Β. Αμερική", "Ιαπωνία"},
  {"Europa", "N.Amerika", "Japan"},
  {"Europe", "Amér. Nord", "Japon"},
  {"Europa", "N.América", "Japón"},
  {"Europa", "Am. Półn.", "Japonia"},
  {"Europa", "America N.", "Japonia"}
};
static const char* const radioIrqText[8] PROGMEM = {
  "Radio IRQ", "Radio-IRQ", "IRQ ραδιοφώνου", "Radio-IRQ",
  "IRQ radio", "IRQ de radio", "IRQ radia", "IRQ radio"
};
static const char* const fmModeText[8] PROGMEM = {"FM", "FM", "FM", "FM", "FM", "FM", "FM", "FM"};
static const char* const fmPiText[8] PROGMEM = {"PI", "PI", "PI", "PI", "PI", "PI", "PI", "PI"};
static const char* const fmPsText[8] PROGMEM = {"PS", "PS", "PS", "PS", "PS", "PS", "PS", "PS"};
static const char* const fmPtyText[8] PROGMEM = {"PTY", "PTY", "PTY", "PTY", "PTY", "PTY", "PTY", "PTY"};
static const char* const fmRdsText[8] PROGMEM = {"RDS", "RDS", "RDS", "RDS", "RDS", "RDS", "RDS", "RDS"};
static const char* const fmRbdsText[8] PROGMEM = {"RBDS", "RBDS", "RBDS", "RBDS", "RBDS", "RBDS", "RBDS", "RBDS"};
static const char* const fmSnrText[8] PROGMEM = {"SNR", "SNR", "SNR", "SNR", "SNR", "SNR", "SNR", "SNR"};
// Compact scan/status strings use the documented English fallback in every
// language until native translations are supplied; array indexes stay aligned.
static const char* const fmNoScanText[8] PROGMEM = {
  "NO FM SCAN", "NO FM SCAN", "NO FM SCAN", "NO FM SCAN",
  "NO FM SCAN", "NO FM SCAN", "NO FM SCAN", "NO FM SCAN"};
static const char* const fmScanText[8] PROGMEM = {
  "FM SCAN", "FM SCAN", "FM SCAN", "FM SCAN",
  "FM SCAN", "FM SCAN", "FM SCAN", "FM SCAN"};
static const char* const fmSeekMenuText[8] PROGMEM = {
  "FM Seek", "FM Seek", "FM Seek", "FM Seek",
  "FM Seek", "FM Seek", "FM Seek", "FM Seek"};
static const char* const fmAfMenuText[8] PROGMEM = {
  "FM AF", "FM AF", "FM AF", "FM AF",
  "FM AF", "FM AF", "FM AF", "FM AF"};
static const char* const fmSeekValueText[8][3] PROGMEM = {
  {"Weak", "Normal", "Strong"}, {"Weak", "Normal", "Strong"},
  {"Weak", "Normal", "Strong"}, {"Weak", "Normal", "Strong"},
  {"Weak", "Normal", "Strong"}, {"Weak", "Normal", "Strong"},
  {"Weak", "Normal", "Strong"}, {"Weak", "Normal", "Strong"}};
static const char* const noDabServiceText[8] PROGMEM = {
  "NO DAB SERVICE", "NO DAB SERVICE", "NO DAB SERVICE", "NO DAB SERVICE",
  "NO DAB SERVICE", "NO DAB SERVICE", "NO DAB SERVICE", "NO DAB SERVICE"};
static const char* const dabScanText[8] PROGMEM = {
  "DAB SCAN", "DAB SCAN", "DAB SCAN", "DAB SCAN",
  "DAB SCAN", "DAB SCAN", "DAB SCAN", "DAB SCAN"};
static const char* const fmStationListText[8] PROGMEM = {
  "FM STATIONS", "FM STATIONS", "FM STATIONS", "FM STATIONS",
  "FM STATIONS", "FM STATIONS", "FM STATIONS", "FM STATIONS"};
static const char* const dabStationListText[8] PROGMEM = {
  "DAB STATIONS", "DAB STATIONS", "DAB STATIONS", "DAB STATIONS",
  "DAB STATIONS", "DAB STATIONS", "DAB STATIONS", "DAB STATIONS"};
static const char* const currentMuxListText[8] PROGMEM = {
  "CURRENT MUX", "CURRENT MUX", "CURRENT MUX", "CURRENT MUX",
  "CURRENT MUX", "CURRENT MUX", "CURRENT MUX", "CURRENT MUX"};
static const char* const scanNotRunText[8] PROGMEM = {
  "SCAN NOT RUN", "SCAN NOT RUN", "SCAN NOT RUN", "SCAN NOT RUN",
  "SCAN NOT RUN", "SCAN NOT RUN", "SCAN NOT RUN", "SCAN NOT RUN"};
static const char* const noStationsFoundText[8] PROGMEM = {
  "NO STATIONS FOUND", "NO STATIONS FOUND", "NO STATIONS FOUND", "NO STATIONS FOUND",
  "NO STATIONS FOUND", "NO STATIONS FOUND", "NO STATIONS FOUND", "NO STATIONS FOUND"};
static const char* const pressOkScanText[8] PROGMEM = {
  "OK = START SCAN", "OK = START SCAN", "OK = START SCAN", "OK = START SCAN",
  "OK = START SCAN", "OK = START SCAN", "OK = START SCAN", "OK = START SCAN"};
static const char* const fmAfInfoText[8] PROGMEM =
  {"AF list", "AF list", "AF list", "AF list", "AF list", "AF list", "AF list", "AF list"};
static const char* const fmTrafficInfoText[8] PROGMEM =
  {"TP / TA / PTY", "TP / TA / PTY", "TP / TA / PTY", "TP / TA / PTY",
   "TP / TA / PTY", "TP / TA / PTY", "TP / TA / PTY", "TP / TA / PTY"};
static const char* const fmCtInfoText[8] PROGMEM =
  {"RDS clock", "RDS clock", "RDS clock", "RDS clock",
   "RDS clock", "RDS clock", "RDS clock", "RDS clock"};
static const char* const fmQualityInfoText[8] PROGMEM =
  {"FM quality", "FM quality", "FM quality", "FM quality",
   "FM quality", "FM quality", "FM quality", "FM quality"};
static const char* const fmMultipathShortText[8] PROGMEM = {"MP", "MP", "MP", "MP", "MP", "MP", "MP", "MP"};
static const char* const fmBlendShortText[8] PROGMEM = {"BL", "BL", "BL", "BL", "BL", "BL", "BL", "BL"};
static const char* const fmAfcRailText[8] PROGMEM = {"AFCRL", "AFCRL", "AFCRL", "AFCRL", "AFCRL", "AFCRL", "AFCRL", "AFCRL"};
static const char* const fmMonoText[8] PROGMEM = {
  "Mono", "Mono", "Μονοφωνικό", "Mono", "Mono", "Mono", "Mono", "Mono"
};
static const char* const fmStereoText[8] PROGMEM = {
  "Stereo", "Stereo", "Στερεοφωνικό", "Stereo", "Stéréo", "Estéreo", "Stereo", "Stereo"
};

// Slideshow waiting text is kept outside the base 82-string table so existing
// menu and status indexes remain unchanged.
static const char* const slideshowLoadingText[8] PROGMEM = {
  "Loading slideshow...",
  "Slideshow laden...",
  "Φόρτωση παρουσίασης...",
  "Slideshow laden...",
  "Chargement diapo...",
  "Cargando diapositiva...",
  "Ładowanie slajdu...",
  "Se încarcă imaginea..."
};
static const char* const slideshowReceivingText[8] PROGMEM = {
  "MOT reception in progress",
  "MOT-ontvangst actief",
  "Λήψη MOT σε εξέλιξη",
  "MOT-Empfang läuft",
  "Réception MOT en cours",
  "Recepción MOT en curso",
  "Trwa odbiór MOT",
  "Recepție MOT în curs"
};
static const char* const switchingToFmText[8] PROGMEM = {
  "Switching to FM...",
  "Overschakelen naar FM...",
  "Μετάβαση σε FM...",
  "Wechsel zu FM...",
  "Passage en FM...",
  "Cambiando a FM...",
  "Przełączanie na FM...",
  "Comutare la FM..."
};
static const char* const switchingToDabText[8] PROGMEM = {
  "Switching to DAB...",
  "Overschakelen naar DAB...",
  "Μετάβαση σε DAB...",
  "Wechsel zu DAB...",
  "Passage en DAB...",
  "Cambiando a DAB...",
  "Przełączanie na DAB...",
  "Comutare la DAB..."
};
static const char* const radioErrorText[8] PROGMEM = {
  "RADIO ERROR",
  "RADIOFOUT",
  "ΣΦΑΛΜΑ ΡΑΔΙΟΦΩΝΟΥ",
  "RADIOFEHLER",
  "ERREUR RADIO",
  "ERROR DE RADIO",
  "BŁĄD RADIA",
  "EROARE RADIO"
};

// IR Remote UI. Technical labels and action names (GPIO12, IR, TUNE, VOL,
// MODE, SLIDESHOW, STANDBY, protocol names and hexadecimal values) remain
// language-independent; only ordinary user-facing instructions are localized.
static const char* const irRemoteText[8] PROGMEM = {
  "IR Remote", "IR-afstandsbed.", "Χειριστήριο IR", "IR-Fernbed.",
  "Télécommande IR", "Mando IR", "Pilot IR", "Telecomandă IR"
};
static const char* const irLearnText[8] PROGMEM = {
  "Learn", "Leren", "Εκμάθηση", "Lernen",
  "Apprendre", "Aprender", "Nauka", "Învățare"
};
static const char* const irClearText[8] PROGMEM = {
  "Clear", "Verwijder", "Διαγραφή", "Löschen",
  "Effacer", "Borrar", "Usuń", "Șterge"
};
static const char* const irTestText[8] PROGMEM = {
  "Test", "Test", "Δοκιμή", "Test", "Test", "Prueba", "Test", "Test"
};
static const char* const irBackText[8] PROGMEM = {
  "Back", "Terug", "Πίσω", "Zurück", "Retour", "Atrás", "Wstecz", "Înapoi"
};
static const char* const irProfileLearnedText[8] PROGMEM = {
  "Profile: learned", "Profiel: geleerd", "Προφίλ: εκμαθημένο", "Profil: gelernt",
  "Profil : appris", "Perfil: aprendido", "Profil: nauczony", "Profil: învățat"
};
static const char* const irProfileEmptyText[8] PROGMEM = {
  "Profile: empty", "Profiel: leeg", "Προφίλ: κενό", "Profil: leer",
  "Profil : vide", "Perfil: vacío", "Profil: pusty", "Profil: gol"
};
static const char* const irLearnedShortText[8] PROGMEM = {
  "learned", "geleerd", "εκμαθημένο", "gelernt",
  "appris", "aprendido", "nauczony", "învățat"
};
static const char* const irEmptyShortText[8] PROGMEM = {
  "empty", "leeg", "κενό", "leer",
  "vide", "vacío", "pusty", "gol"
};
static const char* const irSetGpio12Text[8] PROGMEM = {
  "Set GPIO12 to IR", "GPIO12 op IR zetten", "GPIO12 σε IR", "GPIO12 auf IR",
  "GPIO12 sur IR", "GPIO12 en IR", "GPIO12 na IR", "GPIO12 pe IR"
};
static const char* const irPressOkText[8] PROGMEM = {
  "Press OK", "Druk op OK", "Πατήστε OK", "OK drücken",
  "Appuyez sur OK", "Pulse OK", "Naciśnij OK", "Apăsați OK"
};
static const char* const irLearningText[8] PROGMEM = {
  "IR Learning", "IR leren", "Εκμάθηση IR", "IR lernen",
  "Apprentissage IR", "Aprendizaje IR", "Nauka IR", "Învățare IR"
};
static const char* const irPressText[8] PROGMEM = {
  "Press", "Druk", "Πατήστε", "Drücken",
  "Appuyez", "Pulse", "Naciśnij", "Apăsați"
};
static const char* const irPhysicalOkCancelsText[8] PROGMEM = {
  "Physical OK cancels", "Fysieke OK annuleert", "Φυσικό OK: ακύρωση", "Physisches OK: Abbruch",
  "OK physique : annuler", "OK físico: cancelar", "Fizyczny OK: anuluj", "OK fizic: anulare"
};
static const char* const irReleaseKeyText[8] PROGMEM = {
  "Release key...", "Laat toets los...", "Αφήστε το πλήκτρο...", "Taste loslassen...",
  "Relâchez la touche...", "Suelte la tecla...", "Puść przycisk...", "Eliberați tasta..."
};
static const char* const irClearLearnedRemoteText[8] PROGMEM = {
  "Clear learned remote?", "Remote wissen?", "Διαγραφή IR;", "IR-Fernbed. löschen?",
  "Effacer télécommande ?", "¿Borrar mando?", "Usunąć pilota?", "Șterge telecomanda?"
};
static const char* const irYesText[8] PROGMEM = {
  "Yes", "Ja", "Ναι", "Ja", "Oui", "Sí", "Tak", "Da"
};
static const char* const irNoText[8] PROGMEM = {
  "No", "Nee", "Όχι", "Nein", "Non", "No", "Nie", "Nu"
};
static const char* const irTestTitleText[8] PROGMEM = {
  "IR Test", "IR-test", "Δοκιμή IR", "IR-Test",
  "Test IR", "Prueba IR", "Test IR", "Test IR"
};
static const char* const irPressRemoteKeyText[8] PROGMEM = {
  "Press remote key", "Druk IR-toets", "Πατήστε πλήκτρο IR", "IR-Taste drücken",
  "Appuyez touche IR", "Pulse tecla IR", "Naciśnij klawisz IR", "Apăsați tasta IR"
};
static const char* const irPhysicalOkExitsText[8] PROGMEM = {
  "Physical OK exits", "Fysieke OK sluit af", "Φυσικό OK: έξοδος", "Physisches OK: Ende",
  "OK physique : quitter", "OK físico: salir", "Fizyczny OK: wyjście", "OK fizic: ieșire"
};
static const char* const irProtocolText[8] PROGMEM = {
  "Protocol", "Protocol", "Πρωτόκολλο", "Protokoll",
  "Protocole", "Protocolo", "Protokół", "Protocol"
};
static const char* const irAddressText[8] PROGMEM = {
  "Address", "Adres", "Διεύθυνση", "Adresse",
  "Adresse", "Dirección", "Adres", "Adresă"
};
static const char* const irCommandText[8] PROGMEM = {
  "Command", "Commando", "Εντολή", "Befehl",
  "Commande", "Comando", "Komenda", "Comandă"
};
static const char* const irActionText[8] PROGMEM = {
  "Action", "Actie", "Ενέργεια", "Aktion",
  "Action", "Acción", "Akcja", "Acțiune"
};
static const char* const irNotAssignedText[8] PROGMEM = {
  "NOT ASSIGNED", "NIET TOEGEWEZEN", "ΧΩΡΙΣ ΑΝΑΘΕΣΗ", "NICHT ZUGEORDNET",
  "NON ATTRIBUÉ", "NO ASIGNADO", "NIEPRZYPISANE", "NEATRIBUIT"
};
static const char* const irRepeatText[8] PROGMEM = {
  "Repeat", "Herhaling", "Επανάληψη", "Wiederholung",
  "Répétition", "Repetición", "Powtórzenie", "Repetare"
};
static const char* const irMemoryErrorText[8] PROGMEM = {
  "IR memory error", "IR-geheugenfout", "Σφάλμα μνήμης IR", "IR-Speicherfehler",
  "Erreur mémoire IR", "Error memoria IR", "Błąd pamięci IR", "Eroare memorie IR"
};
static const char* const restartingText[8] PROGMEM = {
  "Restarting...", "Herstarten...", "Επανεκκίνηση...", "Neustart...",
  "Redémarrage...", "Reiniciando...", "Restart...", "Repornire..."
};

static const char* const i2sAudioMenuText[8] PROGMEM = {
  "I²S Audio", "I²S-audio", "Ήχος I²S", "I²S-Audio",
  "Audio I²S", "Audio I²S", "Dźwięk I²S", "Audio I²S"
};

#endif
