# Pentax 645Z Tether

Application native de **prise de vue connectée (tethering)** pour le **Pentax 645Z**,
pensée pour remplacer *Image Transmitter 2* avec une latence minimale.

- **Pilotage de l'appareil** : le déclenchement se fait depuis l'ordinateur
  (commande PTP `InitiateCapture` via `gp_camera_capture`), pas une simple
  écoute de nouveaux fichiers.
- **Transfert immédiat** : le RAW (~50 Mo) est rapatrié depuis le buffer de
  l'appareil et écrit **directement sur le disque** (zéro tampon intermédiaire
  en RAM), sans nécessiter de carte SD.
- **Stabilité** : reconnexion automatique si le câble est débranché/rebranché.
- **Aperçu intégré** : flux **Live View** de l'appareil
  (`gp_camera_capture_preview`) et affichage de la dernière photo capturée.

Stack : **C++17 · JUCE 8 · libgphoto2** — compilable sur **macOS** (clang) et
**Windows** (MinGW-w64 / MSYS2, voir plus bas pourquoi pas MSVC).

---

## Architecture

```
CMakeLists.txt              Configuration cross-platform (JUCE via FetchContent,
                            libgphoto2 via pkg-config)
Source/
├── Main.cpp                Point d'entrée JUCE (fenêtre principale)
├── MainComponent.h/.cpp    UI : bouton DÉCLENCHER, sélecteur de dossier,
│                           indicateur d'état, journal des transferts
└── CameraController.h/.cpp Moteur : thread USB dédié + wrapper libgphoto2
```

### Pipeline asynchrone (moteur `CameraController`)

libgphoto2 n'est **pas thread-safe** : un unique thread USB haute priorité
(`juce::Thread`) possède la session `Camera*` du début à la fin. L'UI ne fait
que poster des commandes ; les retours vers l'UI passent par
`juce::AsyncUpdater` (toujours sur le message thread).

```
UI (message thread)                    Thread USB (haute priorité)
───────────────────                    ───────────────────────────
triggerCapture()  ──── flag atomique ──▶ gp_camera_capture(GP_CAPTURE_IMAGE)
   (non bloquant)      + notify()            │  obturateur + fichier en buffer
                                             ▼
listeners ◀── AsyncUpdater ───────────  gp_camera_file_get() sur un fd ouvert
 (état, photo enregistrée)                   │  écriture binaire directe disque
                                             ▼
                                        gp_camera_file_delete()  (libère le buffer)
```

Points clés pour la latence :

1. **Session toujours ouverte** — `gp_camera_init` n'est appelé qu'à la
   connexion ; le déclenchement n'a donc aucun coût d'initialisation.
2. **Réveil instantané** — `triggerCapture()` lève un drapeau atomique et
   réveille le thread via `notify()` ; entre deux captures le thread écoute
   `gp_camera_wait_for_event` avec un timeout de 100 ms, ce qui borne la
   latence de prise en compte du clic à ≤ 100 ms (l'essentiel du délai restant
   est mécanique : miroir/obturateur du 645Z).
3. **Écriture directe** — `gp_file_new_from_fd` branche le transfert USB sur
   un descripteur de fichier : les blocs sont écrits sur le disque au fil de
   l'eau, sans copie intermédiaire de 50 Mo en mémoire.
4. **Surveillance passive** — la boucle d'événements détecte immédiatement un
   débranchement (retour d'erreur I/O) et bascule en mode reconnexion (retente
   toutes les 2 s) ; elle récupère aussi les photos déclenchées depuis le
   boîtier (`GP_EVENT_FILE_ADDED`).
5. **Live View** — quand il est activé, le thread USB enchaîne les trames
   `gp_camera_capture_preview` (JPEG basse résolution, décodé hors du message
   thread) ; le déclenchement garde la priorité sur le flux, et un poll
   d'événements ultra-court entre les trames préserve la détection de
   débranchement. Si le driver de l'appareil ne fournit pas d'aperçu, le flux
   se coupe proprement avec un message dans le journal.

### Réglage de l'appareil

Sur le 645Z : **Menu → Préférences → Connexion USB → PTP** (et non MSC).
L'appareil apparaît alors comme périphérique PTP, piloté par le driver `ptp2`
de libgphoto2.

---

## Compilation

### macOS (clang)

Prérequis : Xcode Command Line Tools + [Homebrew](https://brew.sh).

```bash
xcode-select --install
brew install cmake ninja pkg-config libgphoto2

git clone <ce dépôt> && cd pentax
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
open "build/PentaxTether_artefacts/Release/Pentax 645Z Tether.app"
```

#### Créer le DMG (distribution macOS)

```bash
brew install dylibbundler        # en plus des dépendances ci-dessus
./scripts/make_dmg.sh            # -> dist/PentaxTether-1.0.0.dmg
```

Le script compile en Release puis rend le bundle **autonome** : les dylibs
(libgphoto2, libgphoto2_port, libusb, …) sont recopiées dans
`Contents/Frameworks` avec réécriture des chemins de liaison
(`dylibbundler`), et les drivers `camlibs/`/`iolibs/` sont embarqués dans
`Contents/Resources` (l'app les localise au démarrage via `CAMLIBS`/`IOLIBS`,
voir `Source/Main.cpp`). Le DMG fonctionne donc sur un Mac **sans Homebrew**.
La signature est ad-hoc : pour distribuer hors de votre machine sans
avertissement Gatekeeper, re-signez avec votre Developer ID puis notarisez.

Le workflow GitHub Actions `.github/workflows/build-dmg.yml` produit ce même
DMG sur un runner macOS à chaque push (artefact `PentaxTether-dmg` dans
l'onglet *Actions*).

> **Si l'appareil n'est pas détecté sur macOS** : le démon système
> `ptpcamerad` (ou l'app Photos/Transfert d'images) peut monopoliser
> l'appareil PTP. Fermez Photos / Transfert d'images, débranchez puis
> rebranchez le câble, et relancez l'application. Sur les anciens systèmes :
> `killall PTPCamera`.

### Windows (MSYS2 / MinGW-w64)

⚠️ **Pourquoi pas MSVC ?** libgphoto2 (et sa dépendance libusb/libtool) ne se
compile pas avec MSVC — le projet ne fournit aucun support officiel de ce
compilateur. La voie standard et maintenue sous Windows est la chaîne
**MinGW-w64 fournie par MSYS2**, dont les dépôts contiennent un paquet
libgphoto2 prêt à l'emploi. Le binaire produit est une application Windows
native ordinaire (aucune dépendance à MSYS2 à l'exécution, hors DLL à
distribuer avec l'exe).

1. Installer [MSYS2](https://www.msys2.org), puis ouvrir le shell **MINGW64** :

```bash
pacman -Syu
pacman -S git mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf \
          mingw-w64-x86_64-libgphoto2

git clone <ce dépôt> && cd pentax
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/PentaxTether_artefacts/Release/PentaxTether.exe
```

2. **Driver USB** : sous Windows, le 645Z est par défaut associé au driver
   MTP/WPD de Microsoft, invisible pour libgphoto2. Il faut lui associer le
   driver générique **WinUSB** avec [Zadig](https://zadig.akeo.ie) :
   brancher l'appareil → Zadig → *Options ▸ List All Devices* →
   sélectionner le 645Z → installer **WinUSB**. (Opération réversible via le
   Gestionnaire de périphériques.)

3. **Distribution** : pour lancer l'exe hors de l'environnement MSYS2, copier
   à côté de lui les DLL nécessaires (`libgphoto2-*.dll`,
   `libgphoto2_port-*.dll`, `libusb-1.0.dll`, `libgcc_s_seh-1.dll`,
   `libstdc++-6.dll`, `libwinpthread-1.dll`, …) ainsi que les répertoires de
   drivers `camlibs/` et `iolibs/` de libgphoto2 — un
   `ldd PentaxTether.exe | grep mingw64` liste les DLL exactes.

---

## Utilisation

1. Brancher le 645Z en USB (mode **PTP**) et l'allumer — l'indicateur passe au
   **vert** (« Prêt — Pentax 645Z ») dès que la session de contrôle est ouverte.
2. Choisir le **dossier cible** (par défaut : `Images/Pentax645Z`).
3. Cliquer **DÉCLENCHER** (ou appuyer sur la **barre d'espace**) : l'obturateur
   part, puis le fichier apparaît dans le dossier cible et dans le journal.
   La photo capturée s'affiche dans le panneau d'aperçu (JPEG ; les RAW seuls
   ne sont pas décodables par l'aperçu).
   Cocher **Live View** pour afficher le flux vidéo temps réel de l'appareil.
4. Les photos prises **depuis le boîtier** sont également rapatriées
   automatiquement.
5. En cas de câble débranché, l'application repasse en « Déconnecté » et se
   reconnecte seule au rebranchement.

États affichés : 🔴 Déconnecté · 🟡 Connexion · 🟢 Prêt · 🟠 Déclenchement · 🔵 Transfert.

---

## Notes techniques

- Le fichier récupéré est celui produit par le réglage qualité du boîtier
  (RAW PEF/DNG et/ou JPEG). En réglage RAW+, certains firmwares n'annoncent
  qu'un fichier via `gp_camera_capture` ; le second est capté par l'événement
  `GP_EVENT_FILE_ADDED`.
- Après transfert, le fichier est supprimé du buffer de l'appareil
  (`gp_camera_file_delete`) pour éviter la saturation de la mémoire interne.
- Les noms en collision dans le dossier cible sont dédoublonnés
  (`IMGP0001.PEF`, `IMGP0001_2.PEF`, …).
