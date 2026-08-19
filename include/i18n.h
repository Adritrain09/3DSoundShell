// i18n.h — Traductions FR/EN pour 3DSoundShell
// Toutes les cles sont en FRANCAIS (langue par defaut)
// Usage : draw_text(x, y, T("texte en francais"));

#ifndef I18N_H
#define I18N_H

#include "settings.h"
#include <string.h>

#define I18N_COUNT 176

typedef struct {
    const char *fr;
    const char *en;
} I18nEntry;

static const I18nEntry i18n_table[I18N_COUNT] = {
    /* ===== Menu Parametres (labels) ===== */
    {"Volume",                  "Volume"},
    {"Theme",                   "Theme"},
    {"Visualiseur",             "Visualizer"},
    {"Pochette album",          "Album cover"},
    {"Aleatoire",               "Shuffle"},
    {"Repetition",              "Repeat"},
    {"Reprendre au demarrage",  "Resume on start"},
    {"Repertoire de depart",    "Start directory"},
    {"Equalizer audio",         "Audio Equalizer"},
    {"Vitesse lecture",         "Playback speed"},
    {"LED notification",        "LED notification"},
    {"Duree splash",            "Splash duration"},
    {"Langue",                  "Language"},
    {"Version",                 "Version"},

    /* ===== Valeurs ON/OFF ===== */
    {"ON",                      "ON"},
    {"OFF",                     "OFF"},

    /* ===== Repetition ===== */
    {"Non",                     "None"},
    {"x1",                      "x1"},
    {"Tout",                    "All"},

    /* ===== Settings - ecran haut ===== */
    {"3DSoundShell - Parametres", "3DSoundShell - Settings"},
    {"Parametres",              "Settings"},
    {"Utilisez < > pour modifier les valeurs.", "Use < > to change values."},
    {"A = confirmer  B = retour  Start = lecteur  L=Equalizer",
     "A = confirm  B = back  Start = player  L=Equalizer"},
    {"Commande lecteur: L+R = play/pause  < >=piste  ↓↑=volume",
     "Player: L+R = play/pause  < >=track  ↓↑=sound vol"},
    {"ZL/ZR = reculer/avancer 10s (modele New)",
     "ZL/ZR = seek -/+10s (New model)"},
    {"R = changer visualiseur   A = play/Pause",
     "R = change visualizer   A = play/Pause"},
    {"Sauvegarde en cours...",  "Saving..."},
    {"Site officiel: https://3DSoundShell.hosten.uk",
     "Official site: https://3DSoundShell.hosten.uk"},
    {"Credit: Dev app- *Adri / Arena.ai*",
     "Credit: App dev - *Adri / Arena.ai*"},
    {"        Testeur et gerant site - *Snipergeek*",
     "        Tester and site manager - *Snipergeek*"},

    /* ===== Notif MAJ ===== */
    {"Version a jour.",         "Up to date."},
    {"Mise a jour V%s disponible", "Update V%s available"},

    /* ===== Player - ecran haut ===== */
    {"Piste",                   "Track"},
    {"PLAY >",                  "PLAY >"},
    {"PAUSE ||",                "PAUSE ||"},
    {"STOP !",                  "STOP !"},
    {"rep.1",                   "rep.1"},
    {"rep.A",                   "rep.A"},
    {"VOL",                     "VOL"},

    /* ===== Player - ecran bas (controles) ===== */
    {"Controles",               "Controls"},
    {"< glisser ici pour avancer >", "< drag here to seek >"},
    {"+- Volume",               "+- Volume"},
    {"ZL/ZR +-10s",             "ZL/ZR +-10s"},
    {"Aleat ON",                "Shuffle ON"},
    {"Aleat OFF",               "Shuffle OFF"},
    {"Repet: Non",              "Repeat: None"},
    {"Repet: x1",               "Repeat: x1"},
    {"Repet: Tout",             "Repeat: All"},

    /* ===== Player - touches ===== */
    {"A = Play/Pause",          "A = Play/Pause"},
    {"L+R = Play/Pause",        "L+R = Play/Pause"},
    {"X = Playlist",            "X = Playlist"},
    {"Y = Aleatoire",           "Y = Shuffle"},
    {"Select = Fichiers",       "Select = Files"},
    {"R = Changer Viz",         "R = Change Viz"},
    {"B = Retour",              "B = Back"},

    /* ===== Visualiseurs ===== */
    {"Barres",                  "Bars"},
    {"Onde",                    "Wave"},
    {"Cercle",                  "Circle"},
    {"Feu",                     "Fire"},
    {"EQ",                      "EQ"},
    {"EQ Pro",                  "EQ Pro"},

    /* ===== File Browser ===== */
    {"Fichiers",                "Files"},
    {"A = Ouvrir   B = Retour   Select = Reglages",
     "A = Open   B = Back   Select = Settings"},
    {"Start = Lecteur    L = Favori",
     "Start = Player    L = Favorite"},

    /* ===== Playlist ===== */
    {"Aucun favori !",          "No favorites!"},
    {"Ajoute des favoris avec L", "Add favorites with L"},
    {"dans le navigateur de fichiers", "in the file browser"},
    {"Playlist vide",           "Playlist empty"},
    {"En lecture :",            "Playing:"},

    /* ===== Egaliseur ===== */
    {"NIVEAU",                  "LEVEL"},
    {"Egaliseur",               "Equalizer"},
    {"v ^  Gain -1 / +1 dB",   "v ^  Gain -1 / +1 dB"},
    {"A    Preset suivant",     "A    Next preset"},
    {"X    Tout a zero",        "X    Reset all"},
    {"B    Retour",             "B    Back"},

    /* ===== Splash ===== */
    {"3DSoundShell",            "3DSoundShell"},
    {"Music Player",            "Music Player"},
    {"by Adritrain09",          "by Adritrain09"},
    {"START pour passer",       "START to skip"},
    {"Chargement...",           "Loading..."},
    /* Playlist bas */
    {"Aucun favori - appuie sur L", "No favorites - press L"},

    /* Tri fichiers */
    {"Tri",                     "Sort"},
    {"Nom A-Z",                 "Name A-Z"},
    {"Nom Z-A",                 "Name Z-A"},

    /* ===== V0.96 Mode eco energie ===== */
    {"Ecran eco energie",       "Power save screen"},
    {"Delai ecran eco",         "Power save delay"},
    {"Visualiseur ON/OFF",      "Visualizer ON/OFF"},
    {"Couleur texte eco",       "Eco text color"},
    {"Wi-Fi OFF en app",        "Wi-Fi OFF in app"},
    {"Serveur inaccessible",    "Server unreachable"},
    {"Version en avance",       "Version ahead"},
    {"Derniere version connue : V%s", "Last known version: V%s"},
    {"Mode economie d energie", "Power save mode"},
    {"Maintenir B (0.5s) pour quitter le mode eco",
     "Hold B (0.5s) to exit power save"},

    /* ===== V0.97 Sleep timer + Oscillo + Onglets ===== */
    {"Sleep timer",             "Sleep timer"},
    {"Shutdown fin timer",      "Shutdown at end"},
    {"Oscillo",                 "Oscillo"},
    {"Style menu params",       "Menu style"},
    {"Direct",                  "Direct"},
    {"Onglets",                 "Tabs"},
    {"Affichage",               "Display"},
    {"Systeme",                 "System"},
    {"Infos",                   "About"},
    {"X = toggle style menu   R = changer onglet",
     "X = toggle menu style   R = change tab"},

    /* ===== Couleurs presets ===== */
    {"Blanc",                   "White"},
    {"Rouge",                   "Red"},
    {"Vert",                    "Green"},
    {"Bleu",                    "Blue"},
    {"Jaune",                   "Yellow"},
    {"Cyan",                    "Cyan"},
    {"Magenta",                 "Magenta"},
    {"Orange",                  "Orange"},
    {"Rose",                    "Pink"},
    {"Violet",                  "Purple"},

    {"Crossfade",               "Crossfade"},
    {"Duree crossfade",         "Crossfade duration"},
    {"Spectro",                 "Spectro"},
    {"Etoiles",                 "Stars"},
    {"Ripple",                  "Ripple"},
    {"Piano",                   "Piano"},
    {"Waves",                   "Waves"},
    {"Tunnel",                  "Tunnel"},
    {"Pulse",                   "Pulse"},
    {"> LECTURE",               "> PLAYING"},
    {"Mode economie d energie", "Power saving mode"},
    {"Maintenir B (0.5s) pour quitter le mode eco",
     "Hold B (0.5s) to exit eco mode"},
};


static inline const char* T(const char *fr_text) {
    if (g_settings.language == 0) return fr_text;
    for (int i = 0; i < I18N_COUNT; i++) {
        if (i18n_table[i].fr && strcmp(i18n_table[i].fr, fr_text) == 0) {
            return i18n_table[i].en;
        }
    }
    return fr_text;
}

#endif
