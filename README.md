# 🎵 3DSoundShell

Ceci est un lecteur de music pour la 2/3DS inspiré de **3DShell** et de **CTRMUS**, fait avec [claude ai](claude.ai/new) et [Arena](arena.ai). Le lecteur est toujours en test et n'auras pas de version réellement fini, mais j'y travail en améliorant l'app ou en ajoutant des fonctionnalitées. Les test sont effectué avec une *New 2DS XL* et une *3DS classique* et une *2DS classique*, pour etre sur que l'IA ne fasse pas de bétise.

[site officiel de 3DSoundShell](https://3dsoundshell.hosten.uk/)
[Discord officiel de 3DSoundShell](https://discord.gg/GMYRY7vdzt)

README généré par claude.ai et modifié par.. bas.. moi:
---

## ✨ Fonctionnalités

- 🎵 **Formats supportés** : MP3, MP2, OGG, FLAC, WAV, OPUS, AIFF.
- 🎨 **Thèmes** : Dark, Light, Neon Purple, Forest — et 16 thèmes personnalisés via fichier .ini
- 📁 **Explorateur de fichiers** style 3DShell (tri alphabétique, dossiers/fichiers, navigation complète)
- 🔀 **Lecture aléatoire** (Fisher-Yates shuffle)
- 🔁 **Répétition** : Désactivée / Une piste / Toute la playlist
- 📊 **Visualiseur audio** : 4 styles (Barres, Onde, Cercle radial, EQ Pro)
- 🖼️ **Pochettes d'album** (sur les formats compatible)
- 🎚️ **Égaliseur 8 bandes** : -12dB à +12dB, 6 présets intégrés
- 💾 **Sauvegarde automatique** (piste, réglages, equalizer)
- ▶️ **Reprise automatique** au démarrage (optionnel)

---

## 🎮 Contrôles

| Bouton | Action |
|---|---|
| **L + R** | Pause / Lecture |
| **◀** | Piste précédente |
| **▶** | Piste suivante |
| **▲ / ▼** | Volume + / - |
| **ZL / ZR** | Reculer / Avancer 10 secondes (New modèle) |
| **A** | Confirmer / Entrer dans le dossier |
| **B** | Retour / Remonter dans les dossiers |
| **X** | Ajouter à la playlist |
| **Y** | Activer/désactiver lecture aléatoire |
| **R** | Changer le style de visualiseur |
| **Start** | Aller au lecteur |
| **Select** | Aller à l'explorateur de fichiers |
| **Écran tactile** | Seek dans la barre de progression |

---

## 📦 Compilation

### Prérequis

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) installé
- `bannertool` et `makerom` dans le PATH
- Connexion internet (pour télécharger les headers tiers)

### Instructions rapides

```bash
# 1. Cloner / décompresser le projet
cd 3DSoundShell

# 2. Lancer le script tout-en-un
chmod +x build_cia.sh
./build_cia.sh
```

Le script va :
- Vérifier les outils
- Télécharger les headers `dr_flac.h`, `dr_wav.h`, `stb_vorbis.h`
- Installer les paquets devkitPro nécessaires
- Compiler le projet
- Générer `3DSoundShell.cia`

### Compilation manuelle

```bash
# Installer les dépendances
dkp-pacman -S 3ds-dev 3ds-citro2d 3ds-citro3d \
              3ds-libmpg123 3ds-libvorbisidec \
              3ds-flac 3ds-opusfile 3ds-libopus

# Compiler
make all

# Ou juste le .3dsx (sans makerom)
make 3dsx
```

---

## 📲 Installation sur 3DS

1. Copiez `3DSoundShell.cia` sur votre carte SD
2. Ouvrez **FBI** sur la 3DS
3. `SD` → naviguez jusqu'au fichier → **Install CIA**
4. L'application apparaît dans le menu HOME

### Alternative : .3dsx (Homebrew Launcher)

```
Copiez 3DSoundShell.3dsx dans /3ds/ sur la carte SD
Lancez via Homebrew Launcher
```

---

## 🎨 Thèmes personnalisés

Créez un fichier `.ini` dans `sdmc:/3DSoundShell/themes/` :

```ini
; Theme Cyber Green — 3DSoundShell
; Format: 0xRRGGBBFF

name=Cyber Green
bg_primary=0x1a1a1aff
bg_secondary=0x121212ff
bg_header=0x0a0a0aff
bg_selected=0x2b2b2bff
bg_playing=0x222222ff
text_primary=0x39ff14ff
text_secondary=0x28b80eff
text_accent=0x00ffffff
text_disabled=0x1a5e11ff
accent=0x0d0d0dff
accent2=0x404040ff
border=0x333333ff
scrollbar=0x262626ff
progress_bg=0x0d0d0dff
progress_fill=0x39ff14ff
eq_bar=0x39ff14ff
eq_handle=0xd8ffd1ff
eq_bar_selected=0xffd700ff
eq_bar_positive=0x00cc44ff
eq_bar_negative=0xff2244ff
eq_zero_line=0x333333ff
eq_bg=0x050505ff
vis_start=0x39ff14ff
vis_end=0x00ffffff
led_color=0x00e204ff
```

Les couleurs sont au format `0xRRGGBBFF`.

---

## 📁 Structure des fichiers sur la carte SD

```
sdmc:/
├── Music/                    ← Vos fichiers audio ici
│   ├── Album1/
│   │   ├── 01-track.mp3
│   │   └── cover.jpg
│   └── ...
└── 3DSoundShell/
    ├── settings.ini          ← Réglages sauvegardés automatiquement
    ├── playlist.m3u          ← Dernière playlist
    └── themes/
        ├── mon_theme.ini
        └── ...
```

---

## 🔧 Dépendances (header-only, téléchargées automatiquement)

| Lib | Usage |
|---|---|
| `dr_flac.h` | Décodage FLAC |
| `dr_wav.h` | Décodage WAV / AIFF |
| `stb_vorbis.h` | Décodage OGG/Vorbis |
| `libmpg123` (devkitPro) | Décodage MP3 / MP2 / AAC |
| `libopusfile` (devkitPro) | Décodage OPUS |
| `citro2d / citro3d` | Rendu 2D GPU |
| `libctru` | SDK 3DS (NDSP audio, etc.) |

---

## 📝 Notes

- Nécessite **Luma3DS** (custom firmware) pour installer le .cia
- Le fichier `banner.bcwav` (son de démarrage) peut être remplacé par le vôtre
- L'icône `romfs/gfx/icon.png` doit être en **48×48 pixels**
- La bannière `romfs/gfx/banner.png` doit être en **256×128 pixels**
- Les métadonnées (titre, artiste, album, pochette) sont lues depuis les tags ID3v2 / Vorbis Comment / FLAC

---

## Licence

Projet homebrew open-source. Aucune affiliation avec Nintendo.

---

## Truc en plus
Du fait que je ne sais pas codé je dois demandé a des IA, donc ne me demandé pas de codé un truc en plus je ne serais pas faire. Et désolé pour les fautes d'orthographes.
