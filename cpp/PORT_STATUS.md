# Portage C++ / Qt / Vulkan de Fruity Prime — état au 25/09/2026

Branche `cpp-port`, dossier `cpp/` (toujours **hors git** : rien n'est commité).
Le port fait environ **40 200 lignes** de C++, contre environ 263 000 pour le C#.
Il se compile sous Linux (WSL2) et pour Windows (cross-compilation depuis Linux, sans rien installer côté Windows).
L'exe Windows est déployé dans `C:\fruityprime`, avec ses propres fichiers de jeu (`C:\fruityprime\files\AMHP1`, désignés par `paths.txt`) : rien n'est plus lancé depuis `Downloads`.

## Terminé

| Domaine | Détail |
|---|---|
| Build | Linux + Windows cross-compilé, benchmarks C# vs C++ |
| Formats du jeu | modèles, textures, animations, salles, entités, collisions, métadonnées, HUD, effets (`_PS.bin`), graphe de nœuds (`levels/nodeData`), personnalités IA (`aiPersonalityData.bin`) |
| Rendu Vulkan | salles, modèles, animations, particules, traînées de tirs, HUD 2D |
| Joueur | déplacements, collisions, alt forms des 7 chasseurs, caméra, sauts, jump pads, ramassage d'items |
| Animations des chasseurs | jambes et buste séparés (au niveau de `Spine_1`), buste penché selon la visée ; marche, pas de côté, saut, atterrissage, tir, charge, recul quand on est touché, apparition, pose d'attente |
| Armes et combat | les 9 armes (charge, zoom, homing, ricochets, splash, headshots), afflictions (gel, brûlure, perturbation), recul, double dégâts, Omega Cannon |
| Bombes | Samus (bomb jump), Stinglarva de Kanden, Lockjaw de Sylux (arcs, piège à trois bombes) |
| Weavel | Halfturret complète |
| Effets | flashs de tir, charges, impacts, projectiles dessinés en effets, effets de joueurs |
| Mort et réapparition | score, messages de kill, caméra de mort qui recule derrière le corps et regarde le tueur, **particules de mort** le long du squelette, réapparition au point le plus éloigné |
| Retour de dégâts | **flèches de direction des dégâts** sur le HUD, **tremblement de caméra** (coups reçus, bombes, atterrissage lourd, charge complète, choc de Spire) |
| Items | **munitions lâchées à la mort** (Missiles ou UA selon l'arme du tueur, 10 s), items refondus comme en C# (point d'apparition + instance) |
| Camouflage | pickup Cloak, et Trace qui devient invisible à l'arrêt (en alt form ou avec l'Imperialist) |
| HUD | HUD d'origine + mode Pro, radar, messages |
| **Tous les modes** | Battle, Survival, Prime Hunter, **Capture, Bounty, Nodes, Defender**, et les variantes **par équipes** (Battle, Survival, Bounty, Nodes, Defender) |
| Règles de match | Battle (7 points, 7 min), Survival (2 vies de réserve, 15 min), Prime Hunter (1 min 30 de « prime time », 15 min), Capture (5 Octoliths), Bounty (3), Nodes (70 points), Defender (1 min 30 dans l'anneau), 15 min pour tous sauf Battle ; fin sur la limite de points, de temps, ou quand il ne reste qu'un survivant ; « GAME OVER » 3 s, la caméra face au vainqueur et glissant lentement de côté, puis 10 s de résultats (portraits, points ou temps, morts), puis un nouveau match dans la même salle ; tableau des scores sur Tab ; classement et égalités comme `GameState.UpdateStandings` |
| Survival | élimination après la dernière vie (« YOU LOST ALL YOUR LIVES! »), temps de survie (« MAX » pour les survivants), joueur caché près de son point d'apparition révélé aux bots au bout de 10 s (« COWARD DETECTED! », « POSITION REVEALED! »), face-à-face des deux derniers (« FACE OFF! », les bots se voient), arbre de décision IA propre au mode |
| **Prime Hunter** | le premier à tuer devient le Prime Hunter, puis celui qui le tue ; le Prime Hunter court plus vite (0,4), saute plus haut (0,35), tire les versions « affinité » de ses armes avec 50 % de dégâts en plus, perd 1 point d'énergie toutes les 20 images (1/3 s), ne peut pas ramasser de soins ni se soigner au Shock Coil, regagne 70 d'énergie par kill et devient translucide à l'arrêt comme Trace ; son temps compte jusqu'à l'objectif ; classement au temps puis aux kills ; messages « %s IS THE NEW PRIME HUNTER! » et « THE PRIME HUNTER IS DEAD! » ; HUD : « PRIME TIME 0:12/1:30 », icône clignotante et « PRIME HUNTER » qui s'écrit lettre par lettre, colonnes Temps et Kills au tableau ; les bots utilisent l'arbre de décision propre au mode (offset 45220) et visent/traquent le Prime Hunter |
| **Octolith (Capture, Bounty)** | ramassé en passant dessus (pas en alt form), porté sur le dos, lâché à la mort ou en passant en alt form, retour à la base après 20 s au sol ou en tombant hors de la salle ; en Capture on ne marque qu'avec son propre Octolith à la base, toucher le sien le renvoie à la base ; modèles `octolith_ctf`, `flagbase_ctf/bounty/cap` ; messages d'origine (« OCTOLITH RESET! », « RETURN TO BASE », « BOUNTY RECEIVED », etc.) ; icône d'Octolith clignotante sur le HUD pendant le transport |
| **Nœuds (Nodes, Defender)** | Nodes : 10 s sans contestation pour prendre un nœud, 1 point toutes les 5 s par nœud tenu, plus vite avec plusieurs nœuds (bonus « x N ») ; anneau `koth_terminal` qui tourne et clignote, couleurs du propriétaire (bleu/rouge, ou couleur d'équipe) ; « acquiring node », barre de progression, « node stolen », rangée d'icônes NODES. Defender : l'anneau tourne pour l'équipe seule dedans, le temps compte vers l'objectif |
| **Équipes** | 2 à 4 équipes (`--teams`), joueurs répartis comme `TeamRules.ChooseTeam` ; combinaisons orange/verte (recolors 4 et 5) avec la lueur (« emission ») du C#, ajoutée au shader ; pas de tir ami par défaut (`--friendly-fire`) : dégâts annulés, pas de gel/brûlure, tête chercheuse, bombes, Halfturret et vol de vie ignorent les coéquipiers ; totaux et classement par équipe (`CompareTeams`, `UpdateStandings`), vies partagées en Survival, points de réapparition par équipe en Capture ; tableau des scores par équipe (« WINNER: B », lignes « Team A » en couleur) ; « YOU KILLED A TEAMMATE » |
| **Zones (`AreaVolume`)** | gravité (soufflerie de Fuel Stack, gravité faible de Head Shot), dégâts et mort (fosses) ; priorités entre volumes de gravité comme en C# |
| **Icônes de localisation** | flèches du HUD (modèles `hud_icon_player` et `hud_icon_arrow`) : l'icône au-dessus de la cible quand elle est dans le cadre, sinon une flèche au bord du cadre qui pointe vers elle ; rouge sur le Prime Hunter, blanche sur les joueurs révélés en Survival (clignotante au face-à-face) ; icônes `hud_icon_octolith` et `hud_icon_nodes` vers les Octoliths, les bases et les nœuds, aux couleurs du C# |
| Messages | en plus : « YOU KILLED 5 IN A ROW! » / « %s KILLED 5 IN A ROW! » (série de kills) |
| **Début de match** | le match commence sur la caméra d'intro de l'arène (fichiers `cameraEditor/mpNN_intro.bin`, courbes de Bézier entre les images clés comme `CameraSequence`), écran assombri, règles du mode tapées à 30 caractères/s (`HudElements.RulesInfo`, coupure des lignes comme `WrapText`, retraits de Prime Hunter et Nodes), « PRESS FIRE TO BEGIN » ; le joueur apparaît au tir ou à la fin du délai (10 à 30 s selon le nombre de joueurs) ; l'intro revient derrière « GAME OVER » en cas d'égalité ou de vainqueur mort, derrière les résultats, et au match suivant ; `--intro on/off` (par défaut : en jeu dans une fenêtre, pas en `--walk-test` ni avec `--script`) |
| **Téléporteurs** | `TeleporterEntity` des arènes (Landing Bay, Transfer Lock) : modèle `TeleporterMP` qui s'ouvre quand quelqu'un approche et se referme ensuite, téléportation vers `TargetPosition` en gardant la vitesse verticale, un joueur doit quitter le pad avant qu'il se déclenche à nouveau (y compris à l'arrivée), les bots l'utilisent (`Field118`) |
| **Sources de lumière** | `LightSourceEntity` : dans son volume, joueurs et Halfturret prennent ses deux lumières au lieu de celles de la salle, avec le glissement progressif de couleur et de direction de `UpdateLightSources` ; le rendu passe des lumières par modèle au shader |
| **Caméras de morphing** | `MorphCameraEntity` (tunnels de Proving Ground) : caméra fixe tant que l'alt form est dans le volume, directions de roulage gardées jusqu'au relâchement du stick (`AltDirOverride`), impossible de reprendre forme humaine dedans |
| **Son** | moteur des effets de `Sfx.cs` : 128 sons d'au plus 12 échantillons, échantillons IMA-ADPCM de `SNDSAMPLES.DAT`, scripts sonores, sons « DGN » dont volume et hauteur suivent deux valeurs (pas, roulage, glissade, brûlure, tirs continus), sons d'ambiance partagés, voix du commentateur (flux `STRM` de `sound_data.sdat`) ; portée 3D de `SND3DLIST.DAT`, atténuation linéaire bornée et panoramique comme le mixeur logiciel du C# ; sortie par **miniaudio** (WASAPI sous Windows, PulseAudio sous WSLg), rien à installer |
| **Sons du jeu** | joueurs (tirs, charges, trappe des missiles, tirs à vide, rechargement de l'Imperialist, zoom, changement d'arme, saut, atterrissage selon le sol, pas selon le sol, roulage, glissade, boost, attaques alt et leurs impacts, morph, dégâts, mort, apparition, gel, perturbation, brûlure, ramassages, Double Damage et Cloak avec leurs comptes à rebours, alarme d'énergie), tirs (impacts, ricochets, bip du missile à tête chercheuse), bombes, portes, objets et leur bourdonnement, téléporteurs, jump pads, Octoliths et nœuds (sons et voix), voix « 5 kills d'affilée », « Prime Hunter », « un kill pour gagner », « éliminé », « coward detected », alarme des 10 dernières secondes, coupure du son au « GAME OVER », bip des lettres des règles |
| **Musique** | séquenceur DS porté de NcsfPlay (pistes SSEQ, 16 canaux, enveloppes, LFO, portamento, PCM/PSG/bruit, banques `SBNK` et `SWAR` de `sound_data.sdat`) ; gestionnaire de `Music.cs` : musique de l'arène (`ASSIGNMUSIC.DAT`, `INTERMUSICINFO.DAT`), changements mis en file derrière un fondu, pistes qui s'allument et s'éteignent en fondu (Octolith porté, nœud en cours de capture), accélération de la dernière minute, jingle « TIMEOUT » à la fin |
| **Dégâts du sol** | lave (sauf Spire) et faces acides : 1 point toutes les 8 images en y restant, comme `PlayerFlags1.OnLava/OnAcid` |
| **Réseau (client)** | **un client C++ qui rejoint les serveurs dédiés du C#** : même protocole, octet pour octet (`NetConfig.ProtocolVersion` 14 : Hello, Welcome, Identify, Roster, MatchState, SessionState, Intent, SlotIntent, Snapshot avec ses queues `NetMatchTimeSync` et `NetHealthSync`, Chat, Refused, Bye, Ping/Pong, StatusQuery) ; UDP sur un fil à part qui répond aux pings sans attendre l'image (Winsock sous Windows) ; identité des emplacements et des vies comme `NetLifecycleTracker` (générations, vies, refus des états d'une autre vie) ; le serveur décide de tout : apparitions, morts, santé, scores, temps de jeu ; le joueur local se déplace lui-même et envoie son intention chaque image (position, visée, boutons avec l'historique des 8 dernières pressions, arme, munitions, charge verrouillée au relâchement, image vue avec sa fraction pour la compensation de latence) ; les autres joueurs sont lus sur une horloge de lecture interpolée quelques images derrière le dernier instantané (`NetSmoothing`) et leurs intentions relayées les font tirer, se transformer, poser des bombes ; les coups du serveur sont rejoués depuis l'historique de dégâts des instantanés (`NetDamage.Replay` : flèches, recul, tremblement, messages « tué par ») ; la forme des autres réconciliée comme `FormReconciliation` ; objets de soin synchronisés (`NetHealthSync`) ; équipes et couleurs de combinaison depuis la liste des joueurs ; noms des joueurs au tableau des scores ; chat affiché sur le HUD, Entrée pour écrire ; perte de connexion signalée et reconnexion automatique (même emplacement) ; `--connect hôte[:port]`, `--net-status`, `--net-probe` |
| **Serveur dédié (C++)** | `--server` : un serveur sans fenêtre qui **fait tourner le match lui-même**, comme le `-server` du C# (`DedicatedServer`, `ServerSim`) : mêmes paquets, joignable par les clients C++ **et C#** ; admission (Hello/Welcome, reprise de l'emplacement par l'identifiant client, refus plein ou mauvaise version), Identify, chat relayé (limité à 3 lignes d'affilée puis 1 toutes les 2 s), ping mesuré, départs et délais d'attente, réponse aux requêtes d'état ; le `World` tourne en autorité (`World::setAuthority`) : chaque joueur est l'intention d'un client (position annoncée, visée, boutons, arme, munitions, zoom, forme réconciliée comme `FormReconciliation`, charge et Double Damage annoncés), les réapparitions, coups, morts, scores et fin de match sont décidés par le serveur ; instantanés à 60 Hz avec l'historique des 4 derniers coups de chaque joueur (`NetDamage`), les temps (`NetMatchTimeSync`), les soins (`NetHealthSync`) et l'état des générateurs aléatoires ; intentions relayées aux autres (`SlotIntent`) ; **compensation de latence** (`NetUnlagged` : les autres joueurs remis là où le tireur les voyait, à la fraction d'image près, jusqu'à 45 images, puis les nouveaux tirs rattrapent les images de retard) ; numérotation des vies et des occupants (`NetPlayerLifecycle`) ; **rotation** des cartes (fichier du C# : `SALLE | mode | minutes | points`), fin de match puis résultats, carte suivante dès que tout le monde est prêt (14 s) ou au bout de 30 s ; équipes réparties comme `TeamRules.ChooseTeam` (format Auto, deux équipes) ; Ctrl+C prévient les clients |
| **Changement de salle** | à la rotation du serveur, la salle, le monde et toutes les ressources du rendu sont remplacés sans fermer la fenêtre (`SceneRenderer::replaceScene`, `loadMatchRoom`) ; nouveau match dans la même salle quand le serveur en relance un ; fin de match (« GAME OVER », résultats) sur l'ordre du serveur |
| Bots (PlayerAi) | **portage complet des ~11 800 lignes du C#** (≈6 500 lignes C++), **y compris les modes à objectifs** (Octoliths et bases de Capture/Bounty, choix des nœuds à prendre ou reprendre, arbres IA des modes : offsets 32968 et 33012, chemins de l'équipe B en Capture) : arbre de décision pondéré lu dans `aiPersonalityData.bin`, aggro et visibilité, navigation sur le graphe de nœuds, contournement des pièges Lockjaw, esquive des tirs, choix d'arme, charge, zoom de l'Imperialist, bombes et attaques alt, ramassage d'items et d'armes, 4 niveaux (facile, moyen, difficile, Insane) |

### Validé cette session

- Serveur C++ lancé en local (UDP 28191) : deux clients C++ (`SERVER=cpp tools/net-check.sh` : PASS, le kill passe par le serveur C++, la victime rejoue les coups et réapparaît où le serveur la place) ; 51 à 250 tirs compensés par partie, sans raté d'historique ; une image de simulation coûte 0,05 ms en moyenne.
- Un client **C#** (`-netcheck`) et un client C++ sur le serveur C++ (`tools/server-check.sh`) : les mêmes fonctions passent que sur le serveur C# (`SERVER=cs tools/server-check.sh`, la référence) : déplacements, sauts, visée, tirs, alt form dans les deux sens, coups reçus, morts, tableau des scores identique des deux côtés.
- Rotation : Battle sur MP1 (30 s), fin, résultats, Survival sur MP2 HARVESTER, puis retour sur MP1 ; les clients C++ changent de salle à chaque fois et se déclarent prêts à la fin des résultats (la carte suivante arrive en 14 s au lieu de 30).
- Pas de régression : `tools/net-check.sh` contre le serveur C# (PASS), les 27 arènes hors ligne (`tools/smoke-rooms.sh battle 5`), builds Linux et Windows sans warning.

## En cours / reste à faire

Par ordre de priorité suggéré :

1. **Réseau, la suite** : l'hébergement depuis le jeu (le serveur C++ tourne, mais seul, en ligne de commande) ; l'annonce au serveur maître ; la prédiction des coups et les réclamations de coups (`NetHitPrediction`, `NetHitClaims` : aujourd'hui un coup s'affiche au retour du serveur, un aller-retour après le tir) ; le lobby persistant, le vote de carte et le choix de la carte suivante (`MapVote`, `MapPick`, `SessionState`) ; le navigateur de serveurs (serveur maître) ; les démos ; le mode spectateur ; les cartes custom (`MapOffer`/`MapChunk`).
2. **Interface QML** : lanceur, menus, réglages, choix de carte, choix des bots et de leur niveau, **navigateur de serveurs** ; l'**écran de fin** du C# (`Mods/EndScreen`, `Mods/MapPick` : choix du chasseur, vote de la carte suivante, « prêt »). Le changement de salle, dont il dépendait, existe maintenant (utilisé en ligne).
3. **Entrées** : manette, réassignation des touches, aide à la visée, stylet ; le port ne gère que le clavier et la souris.
4. **Rendu** : culling, brouillard, lumières dynamiques, cel shading (partiels) ; fondus au noir (`Scene.SetFade`) ; cartes custom, mode headless, miniatures, Android.
5. **Son, le reste** : sons des menus et du HUD de sélection d'armes (ni menus ni roue d'armes dans le port), réglages de volume (`--mute` seulement).
6. **Objets de salle du mode aventure** : plateformes mobiles (`PlatformEntity`, aucune dans les arènes), objets (`ObjectEntity`, aucun dans les arènes), grands téléporteurs à artefacts, changement de salle par téléporteur ; `AreaVolume` ne répond pas encore aux tirs (aucune arène ne s'en sert).

### Écarts connus avec le C#

- **Réseau** :
  - Pas de prédiction des coups : ses propres coups s'affichent au retour de l'instantané du serveur (un aller-retour), et ne sont pas réclamés au serveur (`HitClaim`) ; le serveur les résout quand même avec sa compensation de latence (l'image vue part dans l'intention).
  - Les objets autres que les soins (munitions, armes, Double Damage...) restent ceux de chaque machine, comme en C#, qui ne les synchronise pas non plus.
  - Les objets des modes (Octoliths, nœuds) sont simulés localement à partir des positions reçues ; seuls les scores et les temps viennent du serveur.
  - Pas de lobby persistant : un serveur en mode lobby (`SessionPolicy.Lobby`) n'est pas géré ; les serveurs « continus » (le cas par défaut) le sont.
  - Pas de correction de la position du joueur local si celle du serveur s'en écarte (`Diverged` du C#).
  - Serveur C++ : pas de lobby (`SessionPolicy.Lobby`, commandes de lobby, `MatchLoaded`), pas de vote ni de choix de carte (`Vote`, `MapPick`, `MapChoices`), pas de réclamations de coups (`HitClaim` ignorés : le serveur les résout seul avec sa compensation de latence), pas d'annonce au serveur maître ni de mise à jour automatique, pas d'hébergement pour d'autres (`HostRequest`), pas de cartes custom ; format d'équipes Auto seulement ; `PickerSlot` des soins toujours vide ; la simulation s'arrête quand le serveur est vide (un nouveau match commence à la première arrivée, comme en C#).
  - Le serveur C++ garde, comme le C#, la position annoncée par chaque client ; la fin du match vient de la règle du `World` (points, temps, dernier survivant) plutôt que d'une horloge à part.
  - `FP_NET_PLACE=x,y,z,yaw` (tests) place le joueur après sa première apparition : le serveur C# prend la position que le client annonce.

- **Modes à objectifs** :
  - Les statistiques de licence (Octoliths lâchés ou arrêtés, nœuds pris ou perdus, kills par arme, tirs amis) ne sont pas tenues.
  - L'IA teste « `defense.OccupiedBy != null` » en C#, toujours vrai (c'est un tableau) ; le port teste si quelqu'un est dans le nœud, ce que le C# fait ailleurs (`FindClosestFriendlyNodeDefense`).
  - Le C# écrit mal deux fichiers de nœuds de Capture (`mp1_CTF_node.bi)`, `mp6_CTF_node.bi)`), ses bots n'ont donc pas de chemins en Capture sur Data Shrine et Head Shot ; le port charge les bons fichiers.
  - Survival par équipes : le C# bloque la réapparition selon `TeamDeaths[SlotIndex]` (le compteur d'une autre équipe dès qu'il y a des équipes) ; le port utilise l'équipe du joueur.
  - Les équipes C et D gardent la combinaison du joueur (comme le C#) ; sans lueur.
- **Fuel Stack** : la soufflerie propulse jusqu'au volume de mort du plafond si l'on reste au centre, comme en C# (aucune limite de vitesse verticale) ; les bots s'y font parfois piéger en allant chercher un objet.

- **Bots** :
  - Samus ne boost pas : c'est un geste tactile sur DS, que le C# ignore aussi.
  - La projection des joueurs à l'écran (utilisée pour l'aggro) utilise le format 4:3 de la DS au lieu de la taille de la fenêtre.
  - Un bug flagrant du C# est corrigé à deux endroits (visée à l'Imperialist, test « cible devant moi ») : la hauteur du centre d'une alt form y était ajoutée en virgule fixe non convertie, donc des milliers d'unités trop haut.
- **Animations** : l'animation de rotation sur place (Turn) n'est pas portée, et les modèles LOD1 (joueurs lointains) ne sont pas utilisés.
- **Mort** : la caméra de mort ne permet pas encore de se déplacer librement, contrairement au C#.
- **Fin de match** :
  - Après les résultats, le C# revient au lanceur ; le port, qui n'a pas encore de lanceur, relance un match dans la même salle.
  - Les joueurs s'appellent « Player1 » à « Player8 » sur le tableau, comme le C# hors ligne, mais les messages de kill gardent le nom du chasseur.

- **Prime Hunter** :
  - « %s IS THE NEW PRIME HUNTER! » donne le nom du chasseur, comme les messages de kill du port, là où le C# donne le pseudo.
  - Les statistiques de licence (kills en tant que Prime Hunter, Prime Hunters tués) ne sont pas tenues : rien ne les affiche hors ligne.
- **Intro** :
  - Pas de fondu au noir au début du match (le C# en fait un de 20/30 s) : les fondus ne sont pas portés.
  - Les séquences de caméra sont lues entièrement, mais seules les parties dont les intros se servent sont jouées : pas d'images clés attachées à une entité, ni de messages, ni de fondus, ni de transition depuis la caméra précédente (aucune intro n'en a).
- **Téléporteurs, caméras de morphing** : la référence de nœud de la caméra (culling des parties de salle) est ignorée, comme le culling en général.
- **Son** :
  - Le placement 3D est celui du mixeur logiciel du C# (Android) : atténuation linéaire bornée et panoramique à puissance constante, sans le rendu HRTF possible d'OpenAL.
  - La musique se charge d'un coup au lieu d'en tâche de fond ; le silence du début (jusqu'à 5 s) est sauté comme dans NcsfPlay.
  - NcsfPlay lit le premier échantillon d'un SWAV ADPCM comme non signé ; le port le lit signé, comme la DS.
  - Les « faders » de pistes reproduisent un détail du C# : une piste éteinte en fondu est remise en sourdine puis réactivée à son dernier petit volume.
- **Icônes de localisation** : dans le cas rarissime où la cible est pile à hauteur des yeux et hors du cadre, le C# garde une hauteur calculée dans la mauvaise unité (la flèche part en haut de l'écran) ; le port la met au milieu du bord.

## Tester

```
# Linux (WSL) : build puis match contre 3 bots, niveau difficile (Battle, 7 points, 7 minutes)
cpp/scripts/build-linux.sh
FP_FILES=~/mph-test/files/AMHP1 QT_QPA_PLATFORM=wayland \
  cpp/build/linux-release/FruityPrime --room "MP3 PROVING GROUND" --bots 3 --bot-level 2

# Sans fenêtre : 1 minute simulée, avec la trace des bots (position, arme, chemin dans l'arbre)
FP_DEBUG_BOTS=1 cpp/build/linux-release/FruityPrime --walk-test --bots 5 --script ".;.;.;.;.;."

# Survival avec 5 bots ; Prime Hunter avec 5 bots
cpp/build/linux-release/FruityPrime --mode survival --bots 5
cpp/build/linux-release/FruityPrime --mode primehunter --bots 5

# Capture (2 équipes) ; Bounty ; Nodes par équipes à 3 équipes
cpp/build/linux-release/FruityPrime --room "MP8 FIRE CONTROL" --mode capture --bots 5
cpp/build/linux-release/FruityPrime --room "AD2 MAGMA VENTS" --mode bounty --bots 3
cpp/build/linux-release/FruityPrime --room "MP12 SIC TRANSIT" --mode nodesteams --teams 3 --bots 5

# Toutes les arènes, 1 minute simulée chacune (les chambres de biodéfense, sans point d'apparition, sont sautées)
FP_FILES=~/mph-test/files/AMHP1 cpp/tools/smoke-rooms.sh primehunter 5

# L'intro puis le match, sans fenêtre : tir au bout d'une demi-seconde
cpp/build/linux-release/FruityPrime --walk-test --intro on --bots 3 --script ".;f;.;."

# Son seul : l'effet 300, la voix 6, la séquence 1 (--sound-wav fichier.wav pour l'enregistrer)
cpp/build/linux-release/FruityPrime --sound "300,v6,s1"

# Windows
FruityPrime.exe --files "C:\...\files\AMHE0" --bots 3 --bot-level 3

# En ligne : rejoindre un serveur dédié (C#), comme Kanden, nommé "Moi"
cpp/build/linux-release/FruityPrime --connect hote:port --name Moi --hunter 1
# Ce que fait un serveur, sans le rejoindre ; puis le rejoindre sans fenêtre et afficher ce qu'il envoie
cpp/build/linux-release/FruityPrime --net-status hote:port
cpp/build/linux-release/FruityPrime --net-probe hote:port --seconds 20
# Un match en ligne sans fenêtre (le script avance au rythme du serveur ; t vise l'adversaire le plus proche, T le poursuit)
cpp/build/linux-release/FruityPrime --walk-test --connect hote:port --seconds 30 --script "tf;t;tf;t"
# Test complet : lance le serveur C# sur le port 28190, deux clients C++, PASS si un kill passe par le serveur
cpp/tools/net-check.sh
# Pareil avec le serveur C++
SERVER=cpp cpp/tools/net-check.sh

# Serveur dédié C++ : une carte (--room, --mode, --time-limit, --point-goal) ou une rotation (fichier du C#)
cpp/build/linux-release/FruityPrime --server --port 27888 --max-players 4 --room "MP3 PROVING GROUND" --mode battle
cpp/build/linux-release/FruityPrime --server --rotation rotation.txt --server-name "Mon serveur" [--friendly-fire]
# Un client C# (-netcheck) et un client C++ sur le serveur C++ ; SERVER=cs pour la référence avec le serveur C#
cpp/tools/server-check.sh
```

Serveur dédié C# local pour les tests (depuis `src/MphRead/bin/Release/net10.0`, avec `DOTNET_ROOT=~/.dotnet DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1`) : `./FruityPrime -server -port 28190 -players 8 -nomaster -noautoupdate [-rotation fichier]`. Depuis Windows, viser l'adresse de WSL, pas 127.0.0.1.

En jeu : **Tab** (maintenu) affiche le tableau des scores ; en ligne, **Entrée** ouvre la ligne de chat (Entrée envoie, Échap annule). Le changement d'arme au clavier se fait avec la molette et les touches 1 à 9 (Tab ne change plus d'arme).

Autres options utiles :

- `FP_DEBUG_NET=1` : le réseau dans la console (vies, coups rejoués, état de la connexion toutes les 5 s).
- `--mute` : sans son. Sous WSL, le son passe par PulseAudio (WSLg) : `scripts/setup-linux-audio.sh` installe libpulse dans `~/.local/sysroot` (sans sudo).
- `FP_DEBUG_SOUND=1` : chaque son, voix et musique lancés s'affichent ; en `--walk-test`, le moteur de son tourne alors sans périphérique.
- `--intro on|off` : commencer sur l'intro de l'arène (par défaut en jeu dans une fenêtre, jamais en `--walk-test` ni avec `--script` sauf `--intro on`).
- `--mode` : `battle`, `survival`, `primehunter`, `capture`, `bounty`, `nodes`, `defender`, `battleteams`, `survivalteams`, `bountyteams`, `nodesteams`, `defenderteams`.
- `--teams N` : nombre d'équipes (2 à 4, Capture toujours 2) ; `--friendly-fire` : les coéquipiers se blessent.
- Arènes par mode : Capture sur 16 arènes (MP1, MP2, MP4 étendue, MP6, MP8, MP9, MP12, MP14, CTF1 étendue, AD1 BT, AD2, les 3 dernières Unit et E3) ; Bounty sur MP4 étendue, MP13, CTF1 étendue, AD1 BT, AD2 Magma Vents, Unit 1 ; Nodes et Defender sur la plupart.
- `--time-goal S` : secondes de « prime time » (Prime Hunter) ou de temps dans l'anneau (Defender) pour gagner, 90 par défaut ; 0 pour aucune limite.
- `FP_PRIME_HUNTER=N` : le joueur N (0 = soi-même) commence le match en Prime Hunter.
- `--point-goal N` : points pour gagner (Battle 7, Capture 5, Bounty 3, Nodes 70) ou vies de réserve (Survival) ; 0 pour aucune limite.
- `--time-limit M` : durée du match en minutes (Battle 7, Survival et Prime Hunter 15) ; 0 pour aucune limite.
- `--targets N` : ajoute des cibles immobiles.
- `--all-weapons` : donne toutes les armes au joueur.
- `--hud pro|stock|off` : choisit le HUD.
- `FP_TARGET_AT=x,y,z` : place la première cible.
- `FP_TARGET_SCRIPT=...` : fait jouer un script à la cible.
- `FP_DEBUG_WORLD=1` : affiche les kills, les items, l'Octolith, les nœuds pris et la fin du match dans la console.
- `FP_DEBUG_BOTS=1` : la trace des bots indique aussi le mouvement de la feuille de l'arbre (`[37/12]` : 37 le long des nœuds, 33 tout droit ; puis la cible).
- `FP_DEBUG_NODES=1`, `FP_DEBUG_AREAS=1` : listent les nœuds de navigation et les `AreaVolume` de la salle.
- `--entities` détaille aussi les `AreaVolume` (messages, priorités, volumes), les téléporteurs (cible), les sources de lumière et les volumes des caméras de morphing.
- `FP_DEBUG_WORLD=1` affiche aussi les téléportations.
- En `--walk-test`, la fin de chaque match s'affiche avec le classement (points, kills, morts, temps de survie) et la position de la caméra du vainqueur.

## Fichiers modifiés cette session

- `src/net/NetServer.*` (nouveau) : le serveur (`DedicatedServer` : pairs, emplacements, générations, messages, rotation `MapRotation`, horloge fixe à 60 Hz de `ServerSim.Advance`) et l'interface `Simulation`.
- `src/game/ServerGame.*` (nouveau) : la simulation du serveur sur le `World` (intentions, placement, forme, dégâts publiés, vies, instantanés, compensation de latence).
- `src/net/NetProtocol.*` : l'écriture des paquets côté serveur (MatchState, Roster, SessionState complet, PlayerState, DamageEvent, SnapshotHeader, ServerStatus, Refused).
- `src/game/World.*` : `World::Authority` / `setAuthority` (crochets avant et après chaque joueur, autour de chaque tir, sur chaque coup ; réapparition immédiate, emplacements inactifs ignorés).
- `src/game/Player.*` : `netSetShotState`, `spawnCount`, `burnTimer`, la poussée d'un coup transmise à son écouteur (`DamageSource::impulse`).
- `src/game/Effects.cpp`, `Rng.h` : l'état des générateurs aléatoires (`rngState1/2`).
- `src/game/NetGame.cpp` : le client se déclare prêt (`ReadyState`) à la fin des résultats.
- `src/app/main.cpp` : `--server`, `--port`, `--max-players`, `--rotation`, `--server-name`.
- `tools/server-check.sh` (nouveau), `tools/net-check.sh` (`SERVER=cpp`). `CMakeLists.txt`.

## Git

`cpp/` n'est toujours pas commité. Une sauvegarde de l'état en début de session (hors `cpp/build/`) existe dans le scratchpad de la session, mais elle est temporaire.
Le commit est recommandé, sans `cpp/build/`.
