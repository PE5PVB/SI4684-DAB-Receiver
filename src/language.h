#ifndef LANGUAGE_H
#define LANGUAGE_H

#define VERSION "v1.10 beta"

// [number of languages][number of texts]
// *** means the text is the same as in English
static const char* const myLanguage[5][82] PROGMEM = {
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
    "Channellist", // 11
    "Language", // 12
    "Brightness", // 13
    "Theme", // 14
    "Auto slideshow", // 15
    "Signal units", // 16
    "", // 17
    "", // 18
    "PRESS MODE TO RETURN", // 19
    "CONFIGURATION", // 20
    "High", // 21
    "Low", // 22
    "On", // 23
    "Off", // 24
    "Time-out Timer", // 25
    "Min.", // 26
    "Service Information", // 27
    "Frequency", // 28
    "Ensemblename", // 29
    "Servicename", // 30
    "Program type", // 31
    "Protectionlevel", // 32
    "Samplerate", // 33
    "Bitrate", // 34
    "Audio mode", // 35
    "Signal information", // 36
    "Unknown", // 37
    "News", // 38
    "Current Affairs", // 39
    "Information", // 40
    "Sport", // 41
    "Education", // 42
    "Drama", // 43
    "Culture", // 44
    "Science", // 45
    "Varied", // 46
    "Pop Music", // 47
    "Rock Music", // 48
    "Easy Listening", // 49
    "Light Classical", // 50
    "Serious Classical", // 51
    "Other Music", // 52
    "Weather", // 53
    "Finance", // 54
    "Children's", // 55
    "Social Affairs", // 56
    "Religion", // 57
    "Phone In", // 58
    "Travel", // 59
    "Leisure", // 60
    "Jazz Music", // 61
    "Country Music", // 62
    "National Music", // 63
    "Oldies Music", // 64
    "Folk Music", // 65
    "Documentary", // 66
    "", // 67
    "", // 68
    "", // 69
    "", // 70
    "", // 71
    "DAB Receiver", // 72
    "Waiting for list", // 73
    "Select service", // 74
    "Tuning...", // 75
    "No signal", // 76
    "Tuner not detected!", // 77
    "STAND-BY MODE", // 78
    "Development", // 79
    "Graphic Design", // 80
    "About" // 81
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
    "Kanalenlijst", // 11
    "Taal", // 12
    "Helderheid", // 13
    "Thema", // 14
    "Auto slideshow", // 15
    "Signaal eenheid", // 16
    "", // 17
    "", // 18
    "DRUK MODE OM AF TE SLUITEN", // 19
    "CONFIGURATIE", // 20
    "Hoog", // 21
    "Laag", // 22
    "Aan", // 23
    "Uit", // 24
    "Auto uitschakelen", // 25
    "Min.", // 26
    "Service informatie", // 27
    "Frequentie", // 28
    "Ensemblenaam", // 29
    "Servicenaam", // 30
    "Programma type", // 31
    "Protectionniveau", // 32
    "Bemonst. frequentie", // 33
    "Bitsnelheid", // 34
    "Audio modus", // 35
    "Signaal informatie", // 36
    "Onbekend", // 37
    "Nieuws", // 38
    "Actualiteit", // 39
    "Informatie", // 40
    "Sport", // 41
    "Educatie", // 42
    "Drama", // 43
    "Cultuur", // 44
    "Wetenschap", // 45
    "Varia", // 46
    "Popmuziek", // 47
    "Rockmuziek", // 48
    "Ontspanningsmuziek", // 49
    "Licht klassiek", // 50
    "Klassiek", // 51
    "Overige muziek", // 52
    "Weerbericht", // 53
    "Economie", // 54
    "Kinderen", // 55
    "Maatschappelijk", // 56
    "Religie", // 57
    "Doe mee!", // 58
    "Reizen", // 59
    "Vrije tijd", // 60
    "Jazz", // 61
    "Country", // 62
    "Nat. muziek", // 63
    "Gouwe ouwe", // 64
    "Volksmuziek", // 65
    "Documentaires", // 66
    "", // 67
    "", // 68
    "", // 69
    "", // 70
    "", // 71
    "DAB Ontvanger", // 72
    "Lijst ophalen...", // 73
    "Kies service", // 74
    "Afstemmen....", // 75
    "Geen signaal", // 76
    "Tuner niet verbonden!", // 77
    "STAND-BY MODUS", // 78
    "Ontwikkeling", // 79
    "Grafisch Ontwerp", // 80
    "Over" // 81
  },

  { "Ελληνικά", // Greek
    "Η διεύθυνση του ρότορα άλλαξε", // 1
    "Ελευθερώστε το πλήκτρο", // 2
    "Η οθόνη αναποδογύρισε", // 3
    "Βαθμονόμηση αναλογικού μετρητή", // 4
    "Αφήστε το πλήκτρο όταν είστε έτοιμοι", // 5
    "ο κωδικοποιητής ορίστηκε σε οπτικός", // 6
    "ο κωδικοποιητής ορίστηκε σε στάνταρ", // 7
    "Δέκτης SI-DAB", // 8
    "Λογισμικό", // 9
    "Φορτώθηκαν οι προεπιλογές", // 10
    "Λίστα καναλιών", // 11
    "Γλώσσα", // 12
    "Φωτεινότητα", // 13
    "Θέμα", // 14
    "Αυτόματη παρουσίαση", // 15
    "Μονάδες σήματος", // 16
    "", // 17
    "", // 18
    "ΠΙΕΣΤΕ MODE ΓΙΑ ΕΠΙΣΤΡΟΦΗ", // 19
    "ΡΥΘΜΙΣΕΙΣ", // 20
    "Υψηλό", // 21
    "Χαμηλό", // 22
    "Ενεργό", // 23
    "Ανενεργό", // 24
    "Χρονοδιακόπτης λήξης", // 25
    "λεπτά", // 26
    "Πληροφορίες υπηρεσίας", // 27
    "Συχνότητα", // 28
    "Όνομα μπουκέτου", // 29
    "Όνομα υπηρεσίας", // 30
    "Τύπος προγράμματος", // 31
    "Επίπεδο προστασίας", // 32
    "Samplerate", // 33
    "Ρυθμός bit", // 34
    "Λειτουργία ήχου", // 35
    "Πληροφορίες σήματος", // 36
    "Άγνωστο", // 37
    "Ειδήσεις", // 38
    "Επικαιρότητα", // 39
    "Πληροφορίες", // 40
    "Σπορ", // 41
    "Εκπαίδευση", // 42
    "Δράμα", // 43
    "Κουλτούρα", // 44
    "Επιστήμη", // 45
    "Ποικίλο", // 46
    "Ποπ Μουσική", // 47
    "Ροκ Μουσική", // 48
    "Εύκολη ακρόαση", // 49
    "Ελαφρά Κλασική", // 50
    "Σοβαρή Κλασική", // 51
    "Άλλη Μουσική", // 52
    "Καιρός", // 53
    "Οικονομικά", // 54
    "Παιδικά", // 55
    "Κοινωνικά", // 56
    "Θρησκεία", // 57
    "Τηλεφωνικά", // 58
    "Ταξίδια", // 59
    "Ελεύθερος χρόνος", // 60
    "Τζαζ Μουσική", // 61
    "Κάντρι Μουσική", // 62
    "Εθνική Μουσική", // 63
    "Παλιά Τραγούδια", // 64
    "Παραδοσιακά", // 65
    "Ντοκιμαντέρ", // 66
    "", // 67
    "", // 68
    "", // 69
    "", // 70
    "", // 71
    "Δέκτης DAB", // 72
    "Waiting for list", // 73
    "Επιλογή υπηρεσίας", // 74
    "Συντονισμός...", // 75
    "Χωρίς σήμα", // 76
    "Το tuner δεν εντοπίστηκε!", // 77
    "ΑΝΑΜΟΝΗ", // 78
    "Ανάπτυξη", // 79
    "Γραφικά", // 80
    "Περί" // 81,
  },

  {
    "Deutsch", // Deutsch
    "Drehrichtung geändert", // 1
    "Bitte Taste loslassen", // 2
    "Bildschirm gedreht", // 3
    "Analoge Anzeige kalibrieren", // 4
    "Taste loslassen, wenn bereit..", // 5
    "Encoder auf 'optisch' gesetzt", // 6
    "Encoder auf Standard gesetzt", // 7
    "SI-DAB-Empfänger", // 8
    "Software", // 9
    "Standardeinstellungen geladen", // 10
    "Kanalliste", // 11
    "Sprache", // 12
    "Helligkeit", // 13
    "Thema", // 14
    "Automatische Slideshow", // 15
    "Messeinheiten", // 16
    "", // 17
    "", // 18
    "MODE DRÜCKEN, UM ZURÜCKZUKEHREN", // 19
    "KONFIGURATION", // 20
    "Hoch", // 21
    "Niedrig", // 22
    "An", // 23
    "Aus", // 24
    "Zeit bis Standby", // 25
    "Min.", // 26
    "Serviceinformationen", // 27
    "Frequenz", // 28
    "Ensemblename", // 29
    "Servicename", // 30
    "Programmtyp", // 31
    "Fehlerschutz", // 32
    "Samplingrate", // 33
    "Bitrate", // 34
    "Audio-Modus", // 35
    "Signalinformationen", // 36
    "Unbekannt", // 37
    "Nachrichten", // 38
    "Aktuelle Ereignisse", // 39
    "Information", // 40
    "Sport", // 41
    "Bildung", // 42
    "Drama", // 43
    "Kultur", // 44
    "Wissenschaft", // 45
    "Gemischt", // 46
    "Popmusik", // 47
    "Rockmusik", // 48
    "Leichte Musik", // 49
    "Leichte Klassik", // 50
    "Ernsthafte Klassik", // 51
    "Andere Musik", // 52
    "Wetter", // 53
    "Finanzen", // 54
    "Kinder", // 55
    "Soziale Angelegenheiten", // 56
    "Religion", // 57
    "Call-In Sendung", // 58
    "Reisen", // 59
    "Freizeit", // 60
    "Jazzmusik", // 61
    "Countrymusik", // 62
    "Nationalmusik", // 63
    "Oldies Musik", // 64
    "Folkmusik", // 65
    "Dokumentation", // 66
    "", // 67
    "", // 68
    "", // 69
    "", // 70
    "", // 71
    "DAB-Empfänger", // 72
    "Warte auf Liste", // 73
    "Service auswählen", // 74
    "Suche...", // 75
    "Kein Signal", // 76
    "Tuner nicht erkannt!", // 77
    "STANDBY-MODUS", // 78
    "Entwicklung", // 79
    "Grafikdesign", // 80
    "Über die Software" // 81
  },

  {
    "Français", // Français
    "Rotation change", // 1
    "Relacher le bouton", // 2
    "Bascule Ecran", // 3
    "calibrer le compteur", // 4
    "Relacher le bouton et start", // 5
    "Encodeur regle optique", // 6
    "Encodeur sur standard", // 7
    "SI-DAB Reception", // 8
    "Logiciel", // 9
    "Chargement par defaut", // 10
    "Liste canaux", // 11
    "Language", // 12
    "Brillance", // 13
    "Theme", // 14
    "Auto Diapo", // 15
    "Unite de Signal ", // 16
    "", // 17
    "", // 18
    "Appuyer pour retour", // 19
    "CONFIGURATION", //20
    "Haut", //21
    "Bas", // 22
    "On", // 23
    "Off", // 24
    "Tempo", // 25
    "Min.", // 26
    "Service information", // 27
    "Frequence", // 28
    "Nom ensemble", // 29
    "Nom de service", // 30
    "Programme type", // 31
    "niveau de protection", // 32
    "Echantillonnage", // 33
    "Debit", // 34
    "Mode audio", // 35
    "Information signal", // 36
    "Inconnu", // 37
    "Nouvelles", // 38
    "Actualite", // 39
    "Information", // 40
    "Sport", // 41
    "Education", // 42
    "Dramatique", // 43
    "Culture", // 44
    "Science", // 45
    "Variete", // 46
    "Pop musique", // 47
    "Rock", // 48
    "Cool", // 49
    "Classique leger", // 50
    "Classique serieux", // 51
    "Autres musiques", // 52
    "Meteo", // 53
    "Economie", // 54
    "Enfants", // 55
    "Affaires sociales", // 56
    "Religion", // 57
    "Phoning", // 58
    "Voyage", // 59
    "Loisir", // 60
    "Jazz", // 61
    "Country", // 62
    "musique national", // 63
    "Musique ancienne", // 64
    "Folk", // 65
    "Documentaires", // 66
    "", // 67
    "", // 68
    "", // 69
    "", // 70
    "", // 71
    "DAB plus", // 72
    "Attente liste", // 73
    "Selectionnez SVP", // 74
    "Recherche....", // 75
    "No Signal", // 76
    "Pas de Tuner", // 77
    "MODE STAND-BY", // 78
    "Developpement", // 79
    "Design graphique", // 80
    "a propos" // 81
  }
};
#endif
