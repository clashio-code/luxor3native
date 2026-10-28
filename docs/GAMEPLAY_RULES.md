# Luxor 3 Gameplay Rule Provenance Ledger

This ledger prevents prototype behavior from being mistaken for verified Luxor 3 behavior. It describes evidence available in this repository and the locally supplied Luxor 3 installation. The original executable is inspected statically only; it is never launched. No rule marked UNKNOWN or INFERRED is approved for an authenticity claim.

## Evidence labels

- **VERIFIED — configuration:** Explicitly present in a game data/config file. This verifies the configured value, not necessarily every runtime interpretation.
- **VERIFIED — executable evidence:** A symbol, string, or parser diagnostic is present in `Luxor3.exe`. This proves the concept/key exists, but does not prove full semantics.
- **VERIFIED — observed video:** Directly readable from recorded original gameplay, e.g. text shown by the game itself or HUD values legible frame-by-frame. Limited to what that recording shows.
- **INFERRED:** Suggested by data layout or static executable evidence, but not behaviorally demonstrated.
- **UNKNOWN:** No adequate evidence yet. Do not implement as if known.
- **PROTOTYPE ONLY:** Currently implemented in this repository, but not established as original-game behavior.

## Ground truth found so far

| Topic | Evidence | Status / limits |
|---|---|---|
| Binary is Windows PE32/I386; original renderer imports OpenGL 1.x fixed-function; FMOD 3 is imported | `docs/FINDINGS.md` §§1 and PE probe results | VERIFIED — static executable facts only. No original source or symbols with debug source files. |
| Level files name map and level mode parameters | `data/levels/**/*.lvl`; C++ parser in `src/core.cpp::load_level` handles only a subset | VERIFIED — data structure exists. C++ support across every level family is not established. |
| Track files encode vertices, named sections, reflectors, named points | `data/maps/*/path.LX3TRK`; `src/core.cpp::load_track` | VERIFIED — geometry format. Geometry alone does not define switch choice or all runtime restrictions. |
| Per-section start/end speeds, switch gates/states, danger/occluded/unshootable/spawn-restrictive sections | e.g. `data/maps/geb/pathGamePlayData.txt` | VERIFIED — configuration content. Meaning/application requires complete parser and runtime interpretation. |
| Train speed per section: StartSpeed/EndSpeed ease along each section; a section naming no speed carries the EndSpeed of the previous configured section; DefaultTrackStartSpeed 800 is a fallback only when a map authors no speeds at all | `data/maps/*/pathGamePlayData.txt` (92 EndSpeed + 66 StartSpeed directives across 26 of 27 maps, values 0–130); measured from recorded gameplay: median 87 logical px/s on geb with the recording's scale verified at 0.609 of the logical frame from adjacent-ball spacing (`tools/analysis/measure_train_speed.py`, `tools/analysis/measure_ball_spacing.py`); difficulty scale `TrainSpeedMultiplier` 0.9/1/1.15/1.3 from `DifficultyGameVariables.txt` | VERIFIED — configuration plus quantitative video measurement for the inheritance rule (labeled INFERRED inside it). The interpolation shape between StartSpeed and EndSpeed is INFERRED: the data names the two endpoints, the exact curve between them is not proven. |
| Ball sprites are 30-frame roll animations at 32 fps, looping (Wild: 29 frames); each flavor's sequence name comes from GamePieceFlavors.txt `BallRollAnim` | `data/AnimDefs.txt` `Ball_*` blocks; `data/GamePieceFlavors.txt`; frame files `data/gfx/balls/<flavor>/ball####.png` | VERIFIED — configuration content. Now consumed by the port: build_scene resolves `Ball_<Flavor>` per ball via `animation_frame_index`. |
| Shooter animation: belowSpheresAnim/aboveSpheresAnim per shooter definition pointing at registered tilt sequences (e.g. `Shooter_Tilt`, 17 fps, non-looping, 17 frames) | `data/ShooterDefs.txt`, `data/AnimDefs.txt`, frames `data/gfx/Shooter/shooter_####.png` (143x41) | VERIFIED — configuration content. Not yet consumed by the port's renderer; the shooter still draws ball textures only. |
| Game rule text shown by the game itself: match 3 or more same-colored spheres; every 3rd match in a row grants a powerup from the god of that color; catch powerups to activate | `data/locale/strings/English.txt:92-94` (`InstShooter`, `InstMatch3`, `InstPowerup`), resolved into `data/UIScreens/Instructions.txt`; per-mode help ids `ClassicHelp` `PuzzleHelp` `SurvivalHelp` `CentipedeHelp` `CTNHelp` `EndlessHelp` `TreasureHuntHelp` at `English.txt:215-227` | VERIFIED — configuration (the game's own instructions). Same wording as the separate video OCR of the intro card. |
| Difficulty scoring bonus: Easy "No scoring bonus", Medium "1.5X", Hard "2X", Insane "3X" | `data/locale/strings/English.txt:175-178`; multipliers `EasyScoreMultiplier 1.0 / Medium 1.5 / Hard 2.0 / Insane 3.0` in `data/DifficultyGameVariables.txt` | VERIFIED — configuration. The port has not wired the scoring multiplier into its scorer yet. |
| Sixteen powerups with per-god flavor association and four cost/strength tiers each | `data/PowerupRules.txt` (16 blocks with `AssociatedFlavor` and Level tiers), names resolved in `data/locale/strings/English.txt:325-424` (`CatchWild` "Wild!", `CatchColorBomb` "Color Bomb!", `CatchSandstorm` "Sandstorm!", ...) | VERIFIED — configuration. Effect semantics at runtime are not proven by the file text. |
| Ball material sets: normal, glass, wooden, energy (`Set "normal"|"glass"|"wooden"|"energy"`) purchasable in the Store | `data/GamePieceFlavors.txt` sets; store wiring in `data/UIScreens/Store.txt` (`SelectBallSetForPurchase normal/glass/wooden/energy`) | VERIFIED — configuration. The port always uses the normal set's anims (Ball_*), matching a fresh profile. |
| Generic train values | `data/GameVariables.txt`: `TrainAcceleration "200.0"`, `TrainHighAcceleration "800.0"`, `DefaultTrackStartSpeed "800.0"`, `DefaultTrackEndSpeed "2.0"`, plus chain-reaction pullback parameters | VERIFIED — configured values. Current code transcribes some values but does not load the file, and does not implement the full behavior. |
| Shooter configuration | `data/ShooterDefs.txt`: `primaryAmmoOffset`, `secondaryAmmoOffsets`, animation/resource names | VERIFIED — shooter definitions. Does not on its own specify complete aiming/firing behavior. |
| Shooter cadence and recoil parameters | `data/GameVariables.txt`: `ShooterSecondsBeforeNextShot "0.5"`, recoil/tilt values | VERIFIED — parameter existence/value. Actual firing/input semantics are not proven by the value alone. |
| Score-related configured parameters | `data/GameVariables.txt`: `BonusPerComboCollapse "750"`, `BonusPerChainedCollapse "1500"`, `BonusPerAccuracyPercent "15"`, `PowerupPointsPerPiece "100"`, `PyrmidBasePointValue "100"` | VERIFIED — configured values. Exact formula, timing, combo definition, and award conditions remain UNKNOWN. |
| Executable has shooter/training/powerup subsystems | `strings Luxor3.exe`: `ClassicShooterAmmoSelectionStrategy`, `NileShooterAmmoSelectionStrategy`, `PuzzleShooterAmmoSelectionStrategy`, parser/error strings; RTTI names include `BallProjectile`, `Train`, `TrainPiece`, `PowerupRule...`, `TrackSwitch` | VERIFIED — names/concepts present. Static names do not reveal full algorithms or call order. |
| Executable parser recognizes level keys and emits validation errors | strings include `MapPath`, `StartingTrackSectionSpawnOrder`, `TrainConstructionData`, `NumSpheresToSpawnWith`, `ColorSpawnStreak`, errors about missing blocks and unknown keys | VERIFIED — parser vocabulary/validation exists. Unknown keys beyond observed strings remain possible. |
| `pathGamePlayData.txt` is readable generically for every map that has it | `src/core.cpp::load_path_gameplay_data`; `src/config_audit.cpp` reports 27/27 shipped map files parsed with 0 unknown directives | VERIFIED — parsing only. Runtime meaning of each directive is still UNKNOWN. |
| `GameVariables.txt` and `DifficultyGameVariables.txt` are readable generically, including `#include` | `src/core.cpp::load_name_value_config`; `src/config_audit.cpp` reports 113 keys for GameVariables.txt and 24 for DifficultyGameVariables.txt against the real tree | VERIFIED — parsing only. Values are stored as text; no gameplay consumes them yet. |
| Classic gameplay intro rule text: prevent scarabs pushing spheres into pyramid by making matches of 3+ same-colour spheres | Video `uVn4xol_H14`, 480x360 at 30fps, intro card at ~00:06; OCR reads `MATCHES OF 3 OR MORE SAME COLORED ...` | VERIFIED — observed video, Classic Mode Stage 1-1/geb only. This proves the stated objective and 3+ same-colour match rule at intro level; not collision, insertion, or chain mechanics. |
| Every 3rd match in a row grants a power from the god of that colour | Same intro card at ~00:06; OCR reads `EVERY 3RD MATCH IN A ROW WILL GRANT YOU POWERS FROM THE GOD OF THAT COLOR.` | VERIFIED — observed video instructions. Trigger details and exact power selection/application remain unverified. |
| Score HUD increases and displays combo/bonus text during play | Video ~00:10-00:60 and sampled frames: visible score increments, combo text, score popups; e.g. score HUD readable at ~24s as 5,100 and ~30s as 11,775 | VERIFIED — observed HUD values at sampled frames. This does not reveal the scoring formula. |
| Original UI has bottom HUD counters and central ammo slot; shooter is a winged scarab at bottom | Video gameplay frames ~00:10 onwards | VERIFIED — visual observation for this recording only. Exact internal meanings of all HUD counters not yet decoded. |

## Current prototype rules that are NOT established as original behavior

The current `src/scene.cpp` and `src/game.cpp` implement these as prototype choices. They must not be described as authentic Luxor 3 rules unless later evidence confirms them.

| Current code behavior | Source location | Provenance |
|---|---|---|
| Shooter starts with Red and next Blue | `src/scene.h::ShooterState` defaults | PROTOTYPE ONLY |
| Next ammo advances by cycling enum values modulo `Flavor::Count` | `src/scene.cpp::fire_shot` | PROTOTYPE ONLY; original has mode-specific ammo strategies and level flavor lists. |
| Projectile speed default is 700 pixels/second | `src/scene.h::fire_shot` default argument | PROTOTYPE ONLY; no matching original configuration value established. |
| Muzzle offset 24 pixels | `src/scene.cpp::fire_shot` | PROTOTYPE ONLY |
| Aim clamp at approximately ±(pi/2 - 0.08), dead zone 0.15 | `src/scene.cpp::set_aim` | PROTOTYPE ONLY |
| Collision uses projectile/ball radii and swept segment-circle check | `src/scene.cpp::advance_shooter` | PROTOTYPE ONLY; not validated against original collision semantics. |
| Inserted contiguous run of 3+ equal flavors is deleted | `src/scene.cpp::advance_shooter` | PROTOTYPE ONLY; parser/config/source evidence does not yet establish exact collapse/match rule. |
| Score is number of removed balls times 10 | `src/scene.cpp::advance_shooter` | PROTOTYPE ONLY; conflicts with existence of multiple configured score values; actual formula UNKNOWN. |
| Ten balls per train, colors cycling through all enum values | `src/game.cpp` train construction loop | PROTOTYPE ONLY; level data carries `NumSpheresToSpawnWith`/flavor constraints that the loop currently ignores. |
| Shooter position heuristic based on first two points and fixed screen clamps | `src/game.cpp` | PROTOTYPE ONLY; shooter definitions provide offsets, but placement semantics still need evidence. |
| Train route chooses first unvisited section whose first vertex matches current endpoint | uncommitted/current `src/core.cpp` + `src/scene.cpp::build_route` | PROTOTYPE ONLY and unsafe. It ignores `SwitchState`, `SwitchGate`, `Occluded`, `Danger`, and other map gameplay metadata. |
| Train speed deceleration/ramp applies a 200-pixel threshold | `src/scene.cpp::advance_train*` | PROTOTYPE ONLY. Data has per-section start/end speed and separate configured values. The route advance now eases per-section (see the verified table above); the single-section `advance_train` path retains the old prototype ramp but the game loop uses the route variant. |

## Rules still UNKNOWN and must be established before implementing as authentic

1. What a valid shot does at contact: insertion order, spacing, pullback, separation, bounce, blockers, reflectors, danger zones, and whether collision is swept or frame-based.
2. Exact match/collapse eligibility, wild-ball behavior, special-piece behavior, chain reactions, and how holes/gaps are resolved.
3. Ammo selection for each mode/difficulty, including streak/accuracy behavior and puzzle queue semantics.
4. Train route traversal and switch selection, including `SwitchState`, `SwitchGate`, route loops, occlusion and spawn restrictions.
5. Exact score award conditions and formulas (the config names are known; complete runtime semantics are not).
6. Shooter aim range, input response, rate limit, recoil, and screen placement for all map geometry.
7. Win/loss conditions and the distinct mode rules for classic, puzzle, crossing-the-Nile, centipede/onslaught, survival, and treasure hunter.
8. Whether the game has data-defined rules not yet found in current file searches.

## Verification policy

- Do not mark a feature “original-like” solely because a similarly named constant exists.
- For every gameplay change, record the source file/key or executable evidence, exact interpretation, and a reproducible test.
- Where configuration is insufficient, keep the rule UNKNOWN until static analysis or a lawful observation/reference of original gameplay resolves it.
- Keep speculative implementations behind an explicit prototype label/flag and do not use them as evidence of game compatibility.
- Tests should prove parser and simulation behavior for multiple real levels/maps and mode families; synthetic fixtures alone only prove the fixture format.
