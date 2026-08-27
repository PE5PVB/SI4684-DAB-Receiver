#ifndef LANGUAGE_H
#define LANGUAGE_H

#define VERSION "v1.0"

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
    "",		                      // 18
    "PRESS MODE TO RETURN",       // 19
    "CONFIGURATION",              // 20
    "High",                       // 21
    "Low",                        // 22
    "On",                         // 23
    "Off",                        // 24
    "Time-out Timer",             // 25
    "Min.",                       // 26
    "Service Information",        // 27
    "Frequency",                  // 28
    "Ensemblename",               // 29
    "Servicename",                // 30
    "Program type",               // 31
    "Protectionlevel",            // 32
    "Samplerate",                 // 33
    "Bitrate",                    // 34
    "Audio mode",                 // 35
    "Signal information",         // 36
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
    "FM/DAB Receiver",               // 72
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
    "Auto slideshow",                   // 15
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
    "Service informatie",               // 27
    "Frequentie",                       // 28
    "Ensemblenaam",                     // 29
    "Servicenaam",                      // 30
    "Programma type",                   // 31
    "Protectionniveau",                 // 32
    "Bemonst. frequentie",              // 33
    "Bitsnelheid",                      // 34
    "Audio modus",                      // 35
    "Signaal informatie",               // 36
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
    "Gouwe ouwe",                       // 64
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
    "Πληροφορίες υπηρεσίας",                 // 27
    "Συχνότητα",                             // 28
    "Όνομα μπουκέτου",                       // 29
    "Όνομα υπηρεσίας",                       // 30
    "Τύπος προγράμματος",                    // 31
    "Επίπεδο προστασίας",                    // 32
    "Samplerate",                            // 33
    "Ρυθμός bit",                            // 34
    "Λειτουργία ήχου",                       // 35
    "Πληροφορίες σήματος",                   // 36
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
    "Waiting for list",                      // 73
    "Επιλογή υπηρεσίας",                     // 74
    "Συντονισμός...",                        // 75
    "Χωρίς σήμα",                            // 76
    "Το tuner δεν εντοπίστηκε!",             // 77
    "ΑΝΑΜΟΝΗ",                               // 78
    "Ανάπτυξη",                              // 79
    "Γραφικά",                               // 80
    "Περί"                                   // 81,
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
    "Serviceinformationen",             // 27
    "Frequenz",                         // 28
    "Ensemblename",                     // 29
    "Servicename",                      // 30
    "Programmtyp",                      // 31
    "Fehlerschutz",                     // 32
    "Samplingrate",                     // 33
    "Bitrate",                          // 34
    "Audio-Modus",                      // 35
    "Signalinformationen",              // 36
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
    "Français",                     // Français
    "Rotation change",              // 1
    "Relacher le bouton",           // 2
    "Bascule Ecran",                // 3
    "calibrer le compteur",         // 4
    "Relacher le bouton et start",  // 5
    "Encodeur regle optique",       // 6
    "Encodeur sur standard",        // 7
    "SI-DAB Reception",             // 8
    "Logiciel",                     // 9
    "Chargement par defaut",        // 10
    "Liste canaux",                 // 11
    "Language",                     // 12
    "Brillance",                    // 13
    "Theme",                        // 14
    "Auto Diapo",                   // 15
    "Unite de Signal ",             // 16
    "Mode radio",                   // 17
    "",                             // 18
    "Appuyer pour retour",          // 19
    "CONFIGURATION",                //20
    "Haut",                         //21
    "Bas",                          // 22
    "On",                           // 23
    "Off",                          // 24
    "Tempo",                        // 25
    "Min.",                         // 26
    "Service information",          // 27
    "Frequence",                    // 28
    "Nom ensemble",                 // 29
    "Nom de service",               // 30
    "Programme type",               // 31
    "niveau de protection",         // 32
    "Echantillonnage",              // 33
    "Debit",                        // 34
    "Mode audio",                   // 35
    "Information signal",           // 36
    "Inconnu",                      // 37
    "Nouvelles",                    // 38
    "Actualite",                    // 39
    "Information",                  // 40
    "Sport",                        // 41
    "Education",                    // 42
    "Dramatique",                   // 43
    "Culture",                      // 44
    "Science",                      // 45
    "Variete",                      // 46
    "Pop musique",                  // 47
    "Rock",                         // 48
    "Cool",                         // 49
    "Classique leger",              // 50
    "Classique serieux",            // 51
    "Autres musiques",              // 52
    "Meteo",                        // 53
    "Economie",                     // 54
    "Enfants",                      // 55
    "Affaires sociales",            // 56
    "Religion",                     // 57
    "Phoning",                      // 58
    "Voyage",                       // 59
    "Loisir",                       // 60
    "Jazz",                         // 61
    "Country",                      // 62
    "musique national",             // 63
    "Musique ancienne",             // 64
    "Folk",                         // 65
    "Documentaires",                // 66
    "",                             // 67
    "",                             // 68
    "",                             // 69
    "",                             // 70
    "",                             // 71
    "DAB plus",                     // 72
    "Attente liste",                // 73
    "Selectionnez SVP",             // 74
    "Recherche....",                // 75
    "No Signal",                    // 76
    "Pas de Tuner",                 // 77
    "MODE STAND-BY",                // 78
    "Developpement",                // 79
    "Design graphique",             // 80
    "a propos"                      // 81
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
    "Información del servicio",      // 27
    "Frecuencia",                    // 28
    "Nombre del conjunto",           // 29
    "Nombre del servicio",           // 30
    "Tipo de programa",              // 31
    "Nivel de protección",           // 32
    "Muestreo",                      // 33
    "Tasa de bits",                  // 34
    "Modo de audio",                 // 35
    "Información de la señal",       // 36
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
    "Informacje o serwisie",             // 27
    "Częstotliwość",                     // 28
    "Nazwa multipleksu",                 // 29
    "Nazwa stacji",                      // 30
    "Rodzaj programu",                   // 31
    "Poziom ochrony",                    // 32
    "Częst. próbkowania",                // 33
    "Bitrate",                           // 34
    "Tryb audio",                        // 35
    "Informacje o sygnale",              // 36
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
    "Romana",                     		  // Romana
    "Direcția de rotație inversata",   	// 1
    "Vă rugăm să eliberați butonul",	  // 2
    "Ecranul a fost inversat",    		  // 3
    "Calibrarea contorului analogic",	  // 4
    "Eliberați butonul când este gata",	// 5
    "encoder setat optical",     		    // 6
    "encoder setat standard",    		    // 7
    "Receptor SI-DAB",            // 8
    "Software",                   // 9
    "Setari Initiale",            // 10
    "Lista Canale",               // 11
    "Limba",                      // 12
    "Luminozitate",               // 13
    "Tema",                       // 14
    "Derulare Imagini",           // 15
    "Masurare Semnal",            // 16
    "Mod radio",                  // 17
    "",		                      // 18
    "APASA MODE PENTRU IESIRE",   // 19
    "CONFIGURARE",                // 20
    "Sus",                        // 21
    "Jos",                        // 22
    "Pornit",                     // 23
    "Oprit",                      // 24
    "Oprire dupa",                // 25
    "Min.",                       // 26
    "Informatii Program",         // 27
    "Frecventa",                  // 28
    "Nume Multiplex",             // 29
    "Nume Program",               // 30
    "Tip Program",                // 31
    "Nivel Protectie",            // 32
    "Samplerate",                 // 33
    "Bitrate",                    // 34
    "Mod Audio",                  // 35
    "Informatii Semnal",          // 36
    "Necunoscut",                 // 37
    "Stiri",                      // 38
    "Current Affairs",            // 39
    "Informatii",                 // 40
    "Sport",                      // 41
    "Educatie",                   // 42
    "Drama",                      // 43
    "Cultura",                    // 44
    "Stiinta",                    // 45
    "Diverse",                    // 46
    "Pop Music",                  // 47
    "Rock Music",                 // 48
    "Easy Listening",             // 49
    "Light Classical",            // 50
    "Serious Classical",          // 51
    "Other Music",                // 52
    "Vreme",                      // 53
    "Finante",                    // 54
    "Copii",                      // 55
    "Social Affairs",             // 56
    "Religie",                    // 57
    "Phone In",                   // 58
    "Calatorii",                  // 59
    "Leisure",                    // 60
    "Jazz Music",                 // 61
    "Country Music",              // 62
    "National Music",             // 63
    "Oldies Music",               // 64
    "Folk Music",                 // 65
    "Documentare",                // 66
    "",                           // 67
    "",                           // 68
    "",                           // 69
    "",                           // 70
    "",                           // 71
    "Receptor DAB",               // 72
    "Asteptare lista",            // 73
    "Selectare Program",          // 74
    "Cautare...",                 // 75
    "Fara Semnal",                // 76
    "Tuner nedetected!",          // 77
    "STAND-BY MODE",              // 78
    "Dezvoltare",                 // 79
    "Grafica",		              // 80
    "Despre"                      // 81
  }
};

// FM-only labels that have no DAB counterpart in the legacy 82-string table.
static const char* const fmMultipathText[8] PROGMEM = {
  "Multipath", "Multipad", "Πολλαπλή διαδρομή", "Mehrweg", "Trajets multiples",
  "Multitrayecto", "Wielodrogowość", "Propagare multiplă"
};
static const char* const fmStereoBlendText[8] PROGMEM = {
  "Stereo blend", "Stereomix", "Μίξη στερεοφωνίας", "Stereomischung", "Mélange stéréo",
  "Mezcla estéreo", "Miks stereo", "Mix stereo"
};
static const char* const radioModeValueText[2] PROGMEM = {"DAB", "FM"};
static const char* const fmModeText[8] PROGMEM = {"FM", "FM", "FM", "FM", "FM", "FM", "FM", "FM"};
static const char* const fmPiText[8] PROGMEM = {"PI", "PI", "PI", "PI", "PI", "PI", "PI", "PI"};
static const char* const fmPsText[8] PROGMEM = {"PS", "PS", "PS", "PS", "PS", "PS", "PS", "PS"};
static const char* const fmPtyText[8] PROGMEM = {"PTY", "PTY", "PTY", "PTY", "PTY", "PTY", "PTY", "PTY"};
static const char* const fmRdsText[8] PROGMEM = {"RDS", "RDS", "RDS", "RDS", "RDS", "RDS", "RDS", "RDS"};
static const char* const fmSnrText[8] PROGMEM = {"SNR", "SNR", "SNR", "SNR", "SNR", "SNR", "SNR", "SNR"};
static const char* const fmMultipathShortText[8] PROGMEM = {"MP", "MP", "MP", "MP", "MP", "MP", "MP", "MP"};
static const char* const fmBlendShortText[8] PROGMEM = {"BL", "BL", "BL", "BL", "BL", "BL", "BL", "BL"};
static const char* const fmAfcRailText[8] PROGMEM = {"AFCRL", "AFCRL", "AFCRL", "AFCRL", "AFCRL", "AFCRL", "AFCRL", "AFCRL"};
static const char* const fmMonoText[8] PROGMEM = {
  "Mono", "Mono", "Μονοφωνικό", "Mono", "Mono", "Mono", "Mono", "Mono"
};
static const char* const fmStereoText[8] PROGMEM = {
  "Stereo", "Stereo", "Στερεοφωνικό", "Stereo", "Stéréo", "Estéreo", "Stereo", "Stereo"
};

// Slideshow waiting screen. Kept separate from the legacy 82-string table so
// the indexes used by the existing menu and status screens remain unchanged.
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
#endif

