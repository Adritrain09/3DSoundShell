# 🎵 3DSoundShell

Ceci est un lecteur de music pour la 2/3DS inspiré de **3DShell** et de **CTRMUS**, fait avec [claude ai](claude.ai/new) et [Arena](arena.ai). Le lecteur est toujours en test et n'a pas de version fini, mais j'y travail encore malgré les limites de messages imposé par les sites (et le tout se qui est écrit dans le README est se qu'il devait y avoir de base, je le modifirais une fois la V1 faite). Les test sont effectué avec une *New 2DS XL* et maintenant avec une *3DS classique*.

README généré par claude.ai
---

## ✨ Fonctionnalités

- 🎵 **Formats supportés** : MP3, MP2, OGG/Vorbis, FLAC, WAV, AAC, M4A, OPUS, WMA, AIFF
- 🎨 **Thèmes** : Dark, Light, Neon Purple, Forest — et thèmes personnalisés via fichier .ini
- 📁 **Explorateur de fichiers** style 3DShell (tri dossiers/fichiers, navigation complète)
- 📋 **Playlist** avec sauvegarde .m3u, chargement automatique
- 🔀 **Lecture aléatoire** (Fisher-Yates shuffle)
- 🔁 **Répétition** : Désactivée / Une piste / Toute la playlist
- 📊 **Visualiseur audio** : 3 styles (Barres, Onde, Cercle radial)
- 🖼️ **Pochettes d'album** (ID3v2, Vorbis comment, FLAC)
- 🎚️ **Égaliseur 8 bandes** : -12dB à +12dB, 6 présets intégrés
- 💾 **Sauvegarde automatique** (position, piste, réglages)
- ▶️ **Reprise automatique** au démarrage (optionnel)

---

## 🎮 Contrôles

| Bouton | Action |
|---|---|
| **L + R** | Pause / Lecture |
| **◀** | Piste précédente |
| **▶** | Piste suivante |
| **▲ / ▼** | Volume + / - |
| **ZL / ZR** | Reculer / Avancer 10 secondes (New3DS) |
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
name=Mon Thème
bg_primary=0xFF121218
bg_secondary=0xFF1E1E28
accent=0xFF50A0FF
accent2=0xFFFF64B4
text_primary=0xFFE6E6F0
text_secondary=0xFFA0A0B4
text_accent=0xFF64B4FF
progress_fill=0xFF50A0FF
```

Les couleurs sont au format `0xAARRGGBB` (alpha, rouge, vert, bleu en hexadécimal).

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
