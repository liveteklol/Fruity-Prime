# Qt UI port — état et reprise

Objectif : toute l'UI du launcher en QML, 1 pour 1 avec les 26 écrans de référence
(`-uishot` de la version C# Avalonia, 940×528) — valeurs reprises du C++ de Zection
(`src/MphRead.Native/Mods/Launcher/Gui`). Branche `qt-ui`, worktree `~/GIT/fp-qt`.

## Reprendre
- Build : `tools/qt/build-linux.sh`
- Captures Qt : `FP_QT_UISHOT=DIR [FP_QT_UISHOT_ONLY=a,b] ./FruityPrime -launcher`
  (dans `build/linux-qt-release`, avec `LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=wayland`)
- Erreurs QML : `build/linux-qt-release/logs/*-native.txt`
- Références + JSON de positions : `.qt-screenshots/avalonia-reference/` (copie des .json à régénérer
  avec `-uishot` si absents, cf. mémoire)
- Comparaison : script python (ref | qt | diff, % de pixels > 24) — cf. `tools/qt/cmp.py`

## Architecture
- `qml/Page.qml` = UiLayout::Page (fond, feuille, carte, bandeau/titre, corps, marques, note)
- `qml/Main.qml` = pile d'écrans (StartScreen/InGameMenu), `nav.push/pop`
- C++ : `ShellBridge` (contexte `shell`), `PlayModel`, `RowModel` (lignes des réglages),
  `HunterStandItem` (rectangle où le moteur dessine le VRAI modèle 3D — jamais de figurine en blocs),
  `ServerBadgeItem` (drapeaux)
- Tailles de police fractionnaires : `font.pointSize: Theme.pt(px)`

## Fait
- [x] Start (+ téléphone portrait/paysage : barre en colonne, cœur en coin), Pause (+ téléphone sans
      Fullscreen), Confirm, EndPanel
- [x] Play : Online, Offline, Story, Clips, Vote ; échantillon serverbrowser
- [x] Settings : Display, Audio, Controls (Keyboard/Gamepad/Stylus), Profile, Credits — `SettingsModel`
      (C++, un `RowModel` par page) + `SettingsPage.qml`/`SettingsRows.qml` ; capture touches/souris/molette
      (KeyRow), capture manette avec conflits (PadRow), moniteur manette (`GamepadMonitorItem`), calibration,
      mapping manuel, profils manette, aperçu du viseur, partage des logs
- [x] Setup (fichiers du jeu) — `SetupModel`
- [x] CreateServer (+ dedicated, rotation, choix d'hôte) — `CreateServerModel`, `HostPickerPage`, `MapRotationPage`
- [x] Lobby (roster, choix du joueur, brouillon du match sauvé auto, actions propriétaire, chat, choix de
      carte, équipes perso) — `LobbyModel` ; retour au lobby après un match (Shell.cpp EndNetworkMatchToLobby)
- [x] Ligne de version cliquable + vérification de mise à jour (ShellBridge)
- [x] Navigation clavier/manette : `FocusNav` (déplacement spatial du focus, défilement des listes),
      `GamepadUiRouter` dans UiHost (flèches, A=Entrée, B=Échap, gâchettes hautes = onglets, pages)
- [x] Vérifié en vraie fenêtre (Linux + Windows/Arc, FP_QT_DEMO) ; build Windows déployé dans C:\fruityprime
- Écarts : 1–5 % partout (police Inter vs Pixelify des références), start ~13 % (fond C# ≠ C++, voulu),
  play-online ~8 %
- Vérifier une navigation sans écran : `FP_QT_UISHOT_KEYS=Down,Down,Return` avec FP_QT_UISHOT.

## Décisions utilisateur (2026-09-28)
- Police des libellés : **Inter** (`Assets/Fonts/Inter-Variable.ttf`, `Theme.pixel`) — intégrée.
  Pixelify ne reste que pour le logo (`Theme.wordmark`). JetBrains Mono pour champs/chiffres.
  Test d'une autre police sans rebuild : `FP_QT_FONT=<ttf>`.
- Texte : anticrénelage gris + hinting vertical (l'aliasé faisait trop pixelisé, retour utilisateur
  2026-09-28) ; jamais de sous-pixel. `FP_QT_TEXT_AA=0` remet l'aliasé.
  Captures à la taille d'un vrai écran : `FP_QT_UISHOT_SIZE=1920x1080`.
- Pas d'aide « Enter Select / Esc Back » en bas à gauche de l'écran d'accueil (inutile, retirée).
- Le flou ressenti vient surtout de la capture en 940×528 : vérifier en plein écran dans la vraie fenêtre.

## Reste
- Rien de bloquant. Optionnel : régénérer les captures de référence avec Inter pour des écarts proches de 0.

## Manette (fait)
- Clavier virtuel (`ControllerKeyboard.qml`, modal : `navModal` garde le focus dedans) : A sur un champ
  texte (FieldRow, DeckField) via `padAccept()`, appelé par UiHost/FocusNav::PadAccept avant Entrée.
- A sur une touche clavier (KeyRow) ou bouton pressé pendant l'écoute : ouvre la ligne manette
  correspondante et écoute (SettingsModel::KeyToPad), sinon « Keyboard only; configure sticks under Gamepad ».
- Captures : `PadA` dans `FP_QT_UISHOT_KEYS` simule le A de la manette.
