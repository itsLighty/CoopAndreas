# CoopAndreas
[![Made in Ukraine](https://img.shields.io/badge/made_in-ukraine-ffd700.svg?labelColor=0057b7)](https://stand-with-ukraine.pp.ua)

Videos, pictures, news, suggestions, and communication can be found here:

[![Discord](https://img.shields.io/badge/Discord%20Server%20(25,000+%20members)-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/Z3ugSgFJMU)
[![YouTube](https://img.shields.io/badge/YouTube-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://www.youtube.com/@CoopAndreas)

## Disclaimer
This mod is an unofficial modification for **Grand Theft Auto: San Andreas** and requires a legitimate copy of the game to function. No original game files or assets from Rockstar Games are included in this repository, and all content provided is independently developed. The project is not affiliated with Rockstar Games or Take-Two Interactive. All rights to the original game, its assets, and intellectual property belong to Rockstar Games and Take-Two Interactive. This mod is created solely for educational and non-commercial purposes. Users must comply with the terms of service and license agreements of Rockstar Games.


## Building (Windows)

### Client & Server

1. Make sure you have the **C++ package** installed in **Visual Studio 2022** and **[xmake](https://xmake.io/)** (needed to build the project).

2. Open a terminal in the repository root and build the projects:

```bash
# Build client DLL
xmake --build client

# Build server binary
xmake --build server
```
If `GTA_SA_DIR` is set, the client DLL (`CoopAndreasSA.dll`) will automatically be placed in your game directory.

---

### Proxy (DLL Loader)

The proxy is required to load (inject) the main DLL (`CoopAndreasSA.dll`) into the game executable.

1. Go to your game directory and rename `eax.dll` to `eax_orig.dll`.

2. Build the **Proxy** project:

```bash
xmake --build proxy
```

3. Rename the output DLL to `eax.dll` and copy it to your game folder.

---

### Launcher

The launcher is required to start the game's executable and pass the needed cmd args.

1. Build the **Launcher** project:

```bash
xmake --build launcher
```

2. Copy both `LaunchCoopAndreas.exe` and `LaunchCoopAndreas.exe.manifest` to your game folder.

---

### `main.scm`

1. Download and install **[Sanny Builder 4](https://github.com/sannybuilder/dev/releases)**.

2. Copy all files from the `sdk/Sanny Builder 4/` directory to the **Sanny Builder 4 installation folder**.  
   This will add **CoopAndreas opcodes** to the compiler.

3. Open `scm/main.txt` in **Sanny Builder 4**, compile it, and copy all generated files to  
   `${GTA_SA_DIR}/CoopAndreas/`.

---

### Running CoopAndreas

1. Run `server.exe` it will start accepting client connections.
2. Run `LaunchCoopAndreas.exe`, and follow the serial key instructions, then click "Launch CoopAndreas".
3. (in-game) press **Start Game**, enter your nickname, then provide the server IP and port.
   - If the server is running on the same machine, use `127.0.0.1`
   - Default port: `6767`


## Building Server on GNU/Linux

TODO

## Donate

PayPal: https://www.paypal.com/donate/?hosted_button_id=PSL39QT3X22LA

USDT TRC20: `TNdTwiy9JM2zUe8qgBoMJoAExKf4gs5vGA`

BTC: `bc1qwsl8jv2gyvry75j727qkktr5vgcmqm5e69qt2t`

ETH: `0xE7aE0448A147844208C9D51b0Ac673Bafbe2a35c`

monobank: https://send.monobank.ua/jar/8wPrs73MBa


## TODO list:
### Already Done ✓
- [X] setup C/C++ project
- [X] client - server connection
- [X] On foot sync
  - [x] Walk/Run
  - [x] Jump
  - [x] Climb
  - [x] Crouch - (Fix Crouch Desync)
  - [x] Hit/Fight
- [X] Weapon sync
  - [X] Hold weapon
  - [X] Aim sync
  - [X] Lag shot sync - using keys, bad accuracy
  - [X] Shot sync - every bullet will be synced
- [X] Players info
  - [X] Health bar
  - [X] Armour bar
  - [X] Nickname
  - [X] weapon icon
- [X] explosion sync
- [X] vehicle sync
  - [X] spawn/delete
  - [X] enter/exit
  - [X] color(1/2) sync
  - [X] paintjob sync
  - [X] tuning sync
  - [X] hydra thrusters
  - [X] plane wheels
  - [X] turret aim/shot
  - [X] horn
  - [X] keys (provides steer angles, brake, etc.)
  - [X] pos, speed, quat
  - [X] health
  - [X] damage status
  - [X] doors 
	- [X] locked state
  - [X] engine state
- [X] chat
- [X] time
- [X] weather
- [X] proper key sync
- [X] rendering
  - [X] text rendering (dx)
  - [X] sprite/txd rendering
- [X] style
  - [X] tatoo
  - [X] clothes
  - [X] haircut
- [X] separate ped sync
- [X] EnEx markers sync
- [X] mission markers sync
  
### Current Tasks
- [ ] pickups
  - [ ] graffities, horseshoes, snapshots, oysters
  - [ ] static weapons, armours
  - [ ] static bribes
  - [ ] drop
    - [ ] money
    - [ ] weapons
- [ ] jetpack sync
  - [X] flight 
  - [ ] pickup (related to task above) 
- [X] cutscenes
  - [X] cutscenes
  - [X] vote to skip
- [ ] Players map sync
  - [ ] Areas aka GangZones
  - [X] Mission icons
  - [X] Player map pin
    - [X] fix proportion
  - [X] Player mark (waypoint)
- [X] wanted level
- [X] stats sync
  - [X] fat
  - [X] muscle
  - [X] weapon skills
  - [X] fight styles
  - [X] sync money
  - [X] breath level bar
  - [X] stamina sync
  - [X] max hp sync
- [ ] fire sync
- [ ] cheat code sync
- [ ] anim sync
  - [X] sprunk drinking
  - [X] fast food eating
  - [ ] idle anims
  - [ ] funny TAB+NUM4 (or NUM6) anim sync (did you know about this?)
- [ ] gang groups sync
- [ ] stream in/out players, peds, vehicles, etc.
- [ ] vehicle sync
  - [ ] force hydraulics sync
  - [ ] trailer sync
- [ ] passenger sync
  - [X] gamepad support
  - [X] proper seat sync
  - [ ] radio sync
  - [X] drive by shooting
- [X] fixes
  - [X] mouse
  - [X] widescreen
  - [X] fast load
- [ ] Fix models loading (green polygon)  --- related to stream it/out
- [ ] smooth interpolation
  - [ ] move
  - [ ] rotation
  - [X] weapon aim interpolation
- [ ] npc sync
  - [X] pos, rot, speed
  - [X] weapons
  - [ ] in vehicle sync
    - [ ] driver
      - [X] position velocity rotation  
      - [X] gas/break lights
      - [X] wheel movement
      - [ ] horn
      - [ ] siren
      - [X] current path, target entity, mission
    - [X] passenger
  - [ ] aim
  - [X] shots
  - [ ] task sync (good luck, warrior!)
  - [X] radar icon?
  - [X] speech sync
- [ ] player voice commands
- [ ] chat reactions (see LD_CHAT.txd)
- [ ] gang wars sync
- [ ] parachute jump sync
- [ ] stunt
  - [ ] collecting
  - [ ] for-player slow motion
- [ ] chat gamepad support with on-screen keyboard
### Minor tasks and ideas
- [ ] Sync laser sniper rifle red dot with all players
- [X] Sync moon sniper rifle shot changing size easter egg with all players

## TODO Missions
### Already Done ✓
### Current Tasks
- [X] Big Smoke
- [X] Ryder
- [X] Tagging Up Turf
- [X] Cleaning The Hood
- [X] Drive-Thru
- [X] Nines And AK's
- [X] Drive-By
- [X] Sweet's Girl
- [X] Cesar Vialpando
- [X] OG Loc
- [X] Running Dog
- [X] Wrong Side Of The Tracks
- [X] Just Business
- [X] Home Invasion
- [X] Catalyst
- [X] Robbing Uncle Sam
- [X] Life's A Beach
- [X] Madd Dogg's Rhymes
- [X] Management Issues
- [X] House Party
- [X] High Stakes, Low Rider
- [X] Burning Desire
- [X] Gray Imports
- [X] Doberman
- [X] Los Sepulcros
- [X] Reuniting The Families
- [X] The Green Sabre
- [X] Badlands
- [X] Tanker Commander
- [X] Body Harvest
- [X] Local Liquor Store
- [X] Against All Odds
- [X] Small Town Bank
- [X] Wu Zi Mu
- [X] Farewell, My Love...
- [X] Are You Going To San Fierro?
- [X] Wear Flowers In Your Hair
- [X] 555 WE TIP
- [X] Deconstruction
- [X] Air Raid
- [X] Supply Lines...
- [X] New Model Army
- [X] Photo Opportunity
- [X] Jizzy
- [X] T-Bone Mendez
- [X] Mike Toreno
- [X] Outrider
- [X] Snail Trail
- [X] Ice Cold Killa
- [X] Pier 69
- [X] Toreno's Last Flight
- [X] Mountain Cloud Boys
- [X] Ran Fa Li
- [X] Lure
- [X] Amphibious Assault
- [X] The Da Nang Thang
- [X] Yay Ka-Boom-Boom
- [X] Zeroing In
- [X] Test Drive
- [X] Customs Fast Track
- [X] Puncture Wounds
- [X] Monster
- [X] Highjack
- [X] Interdiction
- [X] Verdant Meadows
- [X] N.O.E.
- [X] Stowaway
- [X] Black Project
- [X] Green Goo
- [X] Fender Ketchup
- [X] Explosive Situation
- [X] You've Had Your Chips
- [X] Don Peyote
- [X] Architectural Espionage
- [X] Key To Her Heart
- [X] Dam And Blast
- [X] Cop Wheels
- [X] Up, Up And Away!
- [X] Intensive Care
- [X] The Meat Business
- [X] Fish In A Barrel
- [X] Misappropriation
- [X] Madd Dogg
- [X] Freefall
- [X] High Noon
- [X] Saint Mark's Bistro
- [X] A Home In The Hills
- [X] Vertical Bird
- [X] Home Coming
- [X] Beat Down On B Dup
- [X] Grove 4 Life
- [X] Cut Throat Business
- [X] Riot
- [X] Los Desperados
- [X] End Of The Line

## TODO Other Scripts
### Already done ✓
### Current Tasks
- [X] Property purchase sync
- [X] Submissions
  - [X] Taxi driver
  - [X] Firefighter
  - [X] Vigilante
  - [X] Paramedic
  - [X] Pimp
  - [X] Freight Train Missions
- [X] Hiden races
  - [X] BMX
  - [X] NRG-500
  - [X] The Chiliad Challenge 
- [X] Stadions
  - [X] 8-Track
  - [X] Blood Bowl
  - [X] Dirt Track
  - [X] Kick Start
- [X] Ammu-nation challenge
- [X] Schools
  - [X] Driving school
  - [X] Flight school
  - [X] Bike school
  - [X] Boat school
- [ ] Asset Missions
  - [ ] Trucker (8 missions)
  - [ ] Valet (5 missions)
  - [ ] Career (7 missions)
- [X] Courier
  - [X] Los Santos - Roboi's Food Mart
  - [X] San Fierro - Hippy Shopper
  - [X] Las Venturas - Burger Shot
- [ ] Street Racing (22)
  - [ ] Little Loop
  - [ ] Backroad Wanderer
  - [ ] City Circuit
  - [ ] Vinewood
  - [ ] Freeway
  - [ ] Into The Country
  - [ ] Dirtbike Danger
  - [ ] Bandito Country
  - [ ] Go-Go Karting
  - [ ] San Fierro Fastlane
  - [ ] San Fierro Hills
  - [ ] Country Endurance
  - [ ] SF To LV
  - [ ] Dam Rider
  - [ ] Desert Tricks
  - [ ] LV Ringroad
  - [ ] World War Ace
  - [ ] Barnstorming
  - [ ] Win Military Service
  - [ ] Chopper Checkpoint
  - [ ] Whirly Bird Waypoint
  - [ ] Heli Hell
- [ ] Import / Export
