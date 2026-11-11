# ESPN NFL 2K5 Native — 0.8.1 (Windows hotfix)

A Windows hotfix for what players reported on 0.8.0 (the Android fixes on the
same branch ship in their own APK). Extract the new zip over your game folder:
your settings, saves, Music, mods and your own presentation packages are kept
(see UPDATING in `HOW-TO-INSTALL.txt`).

## Fixed

- **The game no longer closes at start when the Music folder has a song or
  folder whose name has accents or non-English letters.** A file that cannot
  be read is skipped and named in the log; sub-folders are playlists too, and
  songs straight in Music play as a "Music" playlist.
- **Music Player** (F1 > Music Player): picking a folder or a song, Next song,
  sub-folders, non-English names and holding a pick until you are back in the
  menus were checked with recorded sound, with SOFTDRINK v0.9 and with the
  original game. Accented names show without the accent instead of "?".
- **Socks and other small parts drawing the wrong texture** (a grey/yellow
  machine-like pattern): we could not make it happen on our PC yet, but fixed
  two renderer faults that produce exactly this (smaller texture versions
  that were never refreshed, and versions the game never uses). If you still
  see it, please send a screenshot and your session.log.
- **The box in the middle of the screen between plays** (built-in
  widescreen, worst at 21:9): full-screen fills now cover the whole picture.
  A short darkening can still show as a box now and then; we are on it.
- **Shimmering** on yard lines and the crowd at the default render scale on
  1080p screens.
- **Direct3D 12 / Vulkan black screen with sound**: the game checks at start
  that the graphics card and driver can show the picture. If not, or if nothing
  reaches the screen, it says why, switches to Direct3D 11 and starts again.
- **Exclusive fullscreen grey window** on Direct3D 12 / Vulkan: exclusive now
  switches the display to the chosen resolution; if the display has no such
  mode the menu says so and the game uses borderless.
- **"It crashes / won't open"**: the Visual C++ runtime now ships next to the
  game, and the game is built to run with older runtimes too. Instead of
  closing silently the game explains what is wrong: no disc yet (it can unpack
  your .iso by itself), started from inside the .zip, a folder it cannot write
  to (Program Files), missing files. A crash leaves a report in
  `%LOCALAPPDATA%\NFL2K5-PC\logs` and says so.
- **Mod packs**: drag a .2k5patch onto NFL2K5.exe, or copy it into
  `mods\patches` even while the game runs; no extract-xiso or other tool is
  needed. Clearer messages, and a rewritten `HOW-TO-INSTALL.txt`.
- **Rosters and saves**: a save you delete (in the game, or from
  `saves\rosters` or `saves\UDATA`) no longer comes back from its other copy;
  that copy is kept in `saves\backups\deleted`.
- **Presentations**: art a package names but does not have is hidden instead
  of drawing a broken-image icon (and is named in the log). NBC Sunday Night
  Football shows team logos again, from the CBS 2026 package's logos. Updates
  no longer write over your presentation or video settings (they ship as
  `*.default.ini`).

## Changed

- Frame Rate Cap offers 30, 60 or Original. The game simulates at most 60
  frames a second, so "120" and "Unlimited" were always a 60 lock and are
  gone; more than 60 on screen is Frame Generation (Direct3D 12, off by
  default).

## Still known

- Direct3D 12 / Vulkan compile new shaders the first time a scene appears (a
  short stutter in the first minutes); later starts are smoother.
- The CBS, NBC and FOX packages draw with a system font (their fonts are not
  included), and NBC's own plates and branding art are not included.

## If something goes wrong

Send `session.log`, `xbox_kernel.log` and any `crash-*.txt` / `crash-*.dmp`
from `%LOCALAPPDATA%\NFL2K5-PC\logs` (Win+R, paste the path), plus a
screenshot and the exact text of any message.

---

# ESPN NFL 2K5 Native — 0.8.0

Windows, Linux (experimental) and Android. You need a clean dump of your own
USA retail Xbox disc; nothing from the game is included. Install steps are in
`HOW-TO-INSTALL.txt` (Windows), the archive's `README.txt` (Linux) and
`README.md`.

## What's new

### MyCareer home (with SOFTDRINK 2K28 v0.9)

- Your career starts in a 3D apartment you walk around in, instead of a menu.
- Front door: next game or practice. Phone: call your agent or your coach
  (your head coach's own photo). Mirror: Edit Appearance and Edit Attributes.
  Computer: stats, schedule, bank, homes and the Furniture Shop.
- Furniture Shop: free CC0 furniture, plus the Crib's own items, made from your
  own game disc the first time they are needed (Windows). Purchases and your
  weekly pay are saved with your career.
- In-game MyPlayer stat bar with a live game grade, top-right, in a 2K5 look
  or a Modern look (MyCareer Settings > Stat bar look). NBC SNF and CBS move
  their corner graphic out of its way.
- A modern backdrop for SOFTDRINK's Supersim screen.

### Platforms

- Windows: Direct3D 11, Direct3D 12 and Vulkan renderers (F1 > Video >
  Graphics API).
- Linux x86-64: new, experimental, Vulkan only. Tested only in a virtual
  machine with software graphics so far.
- Android: custom Adreno GPU drivers (Mesa Turnip and others) under Options >
  GPU Driver, Phone Settings, touch layout editor, Controls screen, Heat
  Control, Music Player rows. Signed with a new release key: uninstall an
  earlier test APK once first. Your /NFL2K5 folder (saves, settings, mods,
  songs) stays; re-add a custom GPU driver and allow All files access again.

### SOFTDRINK 2K28 v0.9

- Supported as a drop-in `.2k5patch` (use the 128 MB build, not the "-64MB"
  one). Patched ISOs are refused with a message: use your clean dump plus the
  `.2k5patch`. v0.7 still works.
- The audible L/R slide works for the player controlling the quarterback.
- Extras > Mods switches packs; the game asks before it restarts.

### Video

- Render Scale is your choice. Dynamic Resolution Auto may lower the
  resolution when the graphics card falls behind, but never goes above the
  Render Scale you set.
- Menus no longer get stuck in a narrow 4:3 strip after you skip an attract
  demo.

### Music Player

- F1 > Music Player on PC (Options > Music Player on Android): pick a folder or
  a song, skip, see what's playing.

### Sandbox

- Ships as its own pack for Windows (`NFL2K5-Sandbox-pack-0.8.0.zip`). Not on
  Linux.

## Not in this release

- My NFL Eras and the old-school gear: still in development.
- The CBS, NBC and FOX broadcast packages' own fonts (licences unclear); those
  packages draw with a system font instead.
- The Alabama Crimson Tide team's player photos. The team itself ships and
  plays; its players show the game's "no photo" portrait.
- The save converter is the same October build as in earlier releases,
  unchanged.

## Known issues

- Menus can hitch briefly now and then.
- Direct3D 12 can stall for a moment under load; Direct3D 11 and Vulkan were
  smoother in testing.
- The boot intro movie flickers a little more than before on PC.
- Linux has not been run on real hardware yet.
- Android, not yet proven on real phones: the choppiness reported on the
  Retroid Pocket 6, heat, an intro blink, Turnip on real Adreno GPUs.
- MyCareer on Linux/Android: the 3D apartment is Windows-only for now (it drew
  black in the Linux test VM). There the home is the classic Crib room with
  the same spots (door, mirror, computer, phone, Career Menu), without the
  Furniture Shop. Stat bar and Supersim backdrop work everywhere.
- MyCareer: only the apartment is built (house and mansion change the title
  only); the Crib's Team items use the Crib's default colours, not your team's.

## Third-party software

SOFTDRINK 2K28 and 2K5 Mod Studio / 2k-football-mod-tools by cruuz and
contributors; Kenney, Poly Haven and ambientCG (CC0) for the MyCareer home;
cgltf (MIT); SDL3 (zlib); libadrenotools (BSD-2-Clause, Android); extract-xiso
2.7.1 by in <in@fishtank.com>; Kenney Input Prompts (CC0). Licences and
notices are in `docs\third-party`; the MyCareer assets are listed file by file
in `mods\mycareer-home\MANIFEST.md`.
