# Hexonquest — Nintendo 3DS Homebrew Strategy Game

A Polytopia-inspired 4X strategy game for Nintendo 3DS, built in C++17 with
devkitARM, libctru, citro2d, and citro3d. This document is a handover
summary for continuing development in a new conversation/repository.

## How to build

Requires [devkitPro](https://devkitpro.org/) with the 3DS toolchain
(`3ds-dev` package group), which provides `devkitARM`, `libctru`,
`citro2d`, and `citro3d`.

```bash
export DEVKITPRO=/opt/devkitpro       # wherever devkitPro is installed
export DEVKITARM=$DEVKITPRO/devkitARM
mkdir build && cd build
cmake ..
make
```

Output is `build/polytopia3ds.3dsx` (+ `.smdh`), installable via
Homebrew Launcher or Citra. **Note:** `assets/icon.png` (48x24 or similar,
see `smdhtool` docs) does not exist yet — `smdhtool` will fail without it.
Add a real icon file before building, or stub the CMake step.

Nothing in this repo has been compiled against a real devkitARM toolchain
(no such environment was available while building it) — everything was
written and manually verified for API-correctness, brace balance, and
declaration/definition consistency, but **treat the first real build as a
verification pass**, not a formality. See "Known risks" below.

## Directory layout

```
CMakeLists.txt
include/            headers, mirrors source/ structure
  core/              Engine, Types, HexCoord, Random, Settings, Logger
  world/             Tile, HexGrid
  render/            Renderer (citro2d wrapper), Camera
  input/             InputManager (hid/touch polling)
  game/              Unit, City, Player, TechTree, GameState, Pathfinder,
                     TurnManager, FogOfWar
  ai/                AIController
  audio/             AudioManager (ndsp wrapper)
  fx/                ParticleSystem, AnimationSystem (header-only)
  save/              SaveManager
  ui/                UIWidgets (UIButton)
source/              .cpp implementations, same structure
assets/, data/, shaders/, fonts/, audio/, save/, tools/, romfs/gfx/
                     placeholder asset directories (mostly empty —
                     no actual art/audio/font assets exist yet)
```

## Architecture overview

- **`Engine`** (`core/Engine.h/.cpp`) is the single top-level owner of every
  subsystem and runs the main loop. It's a state machine
  (`EngineState`): `MainMenu → NewGameSetup → Playing ⇄ {CityPanel,
  TechPanel, PauseMenu, SettingsPanel, SaveSlotPanel}`. Each panel state
  has a matched `handleXInput()` / `renderX()` pair, and most build their
  button layout via a shared `buildXButtons()` helper consumed by *both*
  the input handler and the renderer, so hitboxes can never drift out of
  sync with what's drawn.
- **`GameState`** (`game/GameState.h/.cpp`) is the single source of truth
  for a match: the `HexGrid`, all `Player`/`Unit`/`City` records, and
  `FogOfWar`. All mutation goes through its methods (`moveUnit`,
  `attack`, `foundCity`, `embarkUnit`/`disembarkUnit`, `buildRoad`,
  `healUnitAtCity`, `disbandUnit`, `researchTech`, `queueProduction`,
  etc.) — each has a paired `canX()` validator, and callers are expected
  to honor the boolean return value.
- **`TurnManager`** drives player order. AI turns resolve **synchronously
  within a single `update()` call** — there is no multi-frame "AI is
  thinking" state; this is why a turn-change banner was added purely for
  player feedback (see session log).
- **`AIController`** is a deliberately simple greedy AI: research
  cheapest-affordable tech, produce the highest-stat unlocked unit (or a
  Boat at coastal cities once Sailing is researched and no idle boat
  exists), and per-unit chase-nearest-enemy / expand-to-nearest-resource
  / hitch-a-ride-on-a-boat-if-stranded logic.
- **`Pathfinder`** provides `computeReachable` (Dijkstra flood fill,
  movement-budget-bounded) and `findPath` (A*), both taking a
  caller-supplied `EnterPredicate` so land/water/occupancy rules are
  decided by the caller, not baked in.
- **`FogOfWar`** stores one `VisibilityState` per tile per player,
  recomputed from scratch (cheap at these map sizes) on every unit
  move, city founding, embark/disembark, and turn start.
- **Rendering** is all vector shapes via citro2d (hexagons drawn as
  6-triangle fans) — there are **no sprite/texture assets**. `Renderer`
  centralizes terrain/resource color palettes and a `drawButton()`
  helper shared by every UI panel.
- **Save system**: binary, versioned (`SaveManager::kSaveVersion`,
  currently 3), 3 slots, with a lightweight `peekSlot()` for
  slot-picker previews without a full load. Bump the version any time a
  serialized struct's field layout changes.
- **Settings**: separate small binary file, persists audio volumes, the
  debug-grid toggle, whether the tutorial has been seen, and the last
  New Game setup choices (map size index / opponent count).

## Full session changelog

**Session 1 — Foundation.** CMake build (devkitARM toolchain, links
citro2d/citro3d/ctru), core math (`Vec2`, `HexCoord` axial/cube hex
math), `Random` (xorshift128+), `HexGrid` with procedural island
terrain generation (synthetic multi-octave noise + radial falloff +
coastline smoothing), `Renderer` (citro2d wrapper, hex fill/outline
drawing), `Camera2D` (pan/zoom/inertia), `InputManager` (circle-pad +
touch tap/drag gesture derivation), `Engine` main loop rendering a
static explorable hex map.

**Session 2 — Game systems.** `Unit`/`City`/`Player`/`TechTree`
data model, `GameState` (spawn/move/combat/production/research/
fog/victory), `Pathfinder` (Dijkstra + A*), `TurnManager`,
`AIController` (greedy: research/produce/move-attack), `SaveManager`
(binary save/load to SD card). Wired into `Engine`: unit
select/move/attack, city founding, turn end, fog-of-war rendering.

**Session 3 — UI/menus.** `UIButton` widget + `Renderer::drawButton`.
Full menu state machine: Main Menu, Pause Menu (Resume/Save/Load/
Settings/Quit), City Panel (production + link to Tech Panel), Tech
Panel, Settings Panel (volume sliders, grid toggle). Shared
`buildXButtons()` pattern established here and used for every
subsequent panel.

**Session 4 — Boats & roads.** Embarkation (land units board/ride/
disembark from Boats — passenger shares the boat's tile and is
excluded from tile-occupant lookups so `unitAt()` always resolves to
the visible boat), Roads tech now has a real effect (halves tile move
cost), first-launch tutorial tooltip sequence.

**Session 5 — Veterancy, scoring, AI boats.** Kill-count → veteran
promotion (+50% max HP, no offense buff). Turn-limit (30) score-based
victory (`computeScore` = stars + population/level + tech count +
army size) as a fallback when no domination winner emerges. AI
extended to build/use boats for cross-water expansion.

**Session 6 — Heal/disband, bottom-screen redesign, multi-slot
saves.** `healUnitAtCity`, `disbandUnit` (50% refund). Bottom screen
reorganized (fixed-height minimap strip, unit info block, quick-action
button row) to fix a real overlap bug. `SaveManager` extended to 3
slots with a picker panel.

**Session 7 — New Game setup, turn banner, city production
indicator.** Map size (Small/Medium/Large) and opponent count (1–5)
picker screen replacing hardcoded values. Brief "Your turn!" / "Rival
N's turn" banner (since AI turns resolve in a single frame with no
other feedback). Small dot indicator on producing cities.

**Session 8 — Verification pass.** Confirmed prior in-flight work
(tech-tier grouping via `UIButton::header`, disband two-tap confirm,
spaced multi-player start positions via `findSpacedStartTile`) was
complete and correct. Added: remembered New Game setup choices
(persisted in `Settings`), FPS/frame-time debug readout, two-tap
confirmation before "Quit to Main Menu"/"Exit Application".

**Session 9 — Unit cycling, Tech Panel reachability.**
`cycleToNextReadyUnit()` (SELECT+A/B, or a "Next Unit" touch button)
tabs between units with moves left. Tech Panel is now also reachable
from the Pause Menu directly (previously only reachable through a
City Panel, which meant a player with zero cities could never research
again).

**Session 10 — Fixed real UI overflow bugs.** Tech Panel (two tiers +
8 techs + Back) and City Panel (7 unit buttons + Research + Close) both
literally overflowed the 240px bottom screen at their original
spacing — buttons were unreachable/off-screen. Retightened both.
Added a compact legend explaining Build Road/Heal/Disband
requirements.

**Session 11 — Systematic overflow audit.** Checked every remaining
button panel (Main Menu, Pause Menu, Settings, Save Slot Picker, New
Game Setup) against the 240px bottom-screen height. Found and fixed
New Game Setup's margin (was only 4px); everything else confirmed
safe with healthy margins.

**Session 12 — Full turn-cycle trace, 4 real bugs found & fixed.**
(a) **Fog of war went stale mid-turn** — vision only recomputed once
at turn start, so a moving unit's newly-revealed terrain wouldn't
render until the *next* turn. Fixed by recomputing fog inside
`moveUnit`/`foundCity`/`embarkUnit`/`disembarkUnit`. (b) Captured
cities left stale territory borders on worked tiles and a dangling
`capitalCityId` on the former owner. (c) Silent star loss when
production finished but the city tile was occupied (stars were spent
with no unit spawned). (d) Disbanding a boat with a passenger aboard
silently destroyed the passenger with no refund/warning — now
blocked, requiring disembark first.

**Session 13 — AI-turn stale-pointer audit.** Confirmed `units_`
never reallocates during `AIController::takeTurn()` (safe). Found and
fixed a real *logic* bug: after killing an in-range enemy, the AI
would still set its movement target to that now-dead unit's stale
coordinate instead of picking a sensible new target.

**Session 14 — Economy double-counting bug.** City work-radius scans
(on founding and on level-up) never checked whether a tile was already
claimed by another city, letting overlapping work radii double-count
the same resource tile's yield across two cities' economies. Fixed
both scan sites, plus blocked founding a city directly on a
tile another city already works. `AudioManager`'s channel allocator
verified safe (SFX round-robin is hard-bounded to channels 1–23,
can't collide with the hardcoded music channel 0).

**Session 15 — Elimination soft-lock.** If the human was eliminated
in a 3+ player game while multiple AI tribes survived, `checkVictory`
never ended the match — the human was stuck manually ending empty
turns forever. Fixed: match now ends immediately once the human has
no assets left, crediting the highest-scoring surviving AI. Also
wired up `Player::alive`, which was declared and serialized but never
actually set anywhere.

**Session 16 — Capture notification.** AI capturing a human city
previously produced zero feedback (no message, sound, or camera
movement) — a player who'd panned away could lose a city and never
know. Fixed via before/after snapshot-diffing of the human's city
list each frame; now shows "Your capital has fallen!" / "A city was
captured!" and snaps the camera to the location. Verified the
`TechDef::unlocksUnit` sentinel for Roads has no off-by-one risk
anywhere it's used.

## Verification method used every session

Every session ended with this sweep (worth re-running after any new
changes, especially manual edits made outside this process):

```bash
# Brace balance across every file
for f in $(find . -name "*.cpp" -o -name "*.h"); do
  o=$(grep -o "{" "$f" | wc -l); c=$(grep -o "}" "$f" | wc -l)
  [ "$o" != "$c" ] && echo "MISMATCH: $f open=$o close=$c"
done

# No leftover placeholders
grep -rn "TODO\|FIXME\|omitted\|implement later" source include

# No duplicate/orphaned function definitions in a given file
grep -oP "(?<=ClassName::)\w+(?=\()" source/path/File.cpp | sort | uniq -c | awk '$1!=1'
```

This caught several real defects mid-session (an `str_replace` that
clipped a function signature leaving an orphaned body, a duplicated
leftover line from a layout edit) before they could compound.

## Known risks / not yet done

- **Never compiled against a real devkitARM toolchain.** All API usage
  (citro2d/citro3d/libctru/ndsp signatures) was written from careful
  recollection and cross-checked for internal consistency, but a first
  real build may surface genuine compiler errors (wrong header, wrong
  argument order, etc.) that static review can't catch. **Prioritize
  getting an actual build running early in the next session.**
- **No real assets.** All terrain/units/UI render as flat vector
  shapes; `AudioManager::loadSound()` calls point at `romfs:/audio/*.wav`
  paths that don't exist (fails gracefully, game runs silently).
  `assets/icon.png` needed for the CMake `smdhtool` step to succeed.
- **Font**: `Renderer` uses citro2d's default system font throughout
  (no custom `C2D_Font` loaded) — fine functionally, but not
  visually distinctive.
- **AI is intentionally simple** (greedy, no lookahead, no
  threat-assessment) — a reasonable opponent to build against, not a
  strong one.
- **Multiplayer** does not exist — single human + N local AI only.
- Two items were queued for the *next* session when this handout was
  requested (not yet investigated): (1) whether the new capture
  notification could false-fire during a human-initiated recapture on
  the same turn (need to confirm the `wasHumanTurn` guard suppresses
  it correctly), and (2) whether the AI's boat-ferry target selection
  could oscillate indefinitely between two equidistant coastal tiles.

## Suggested next steps

1. **Get a real build working.** This is the single highest-value next
   step — everything above is unverified against an actual compiler.
2. Resolve the two queued verification items above.
3. Add real assets (at minimum: an app icon, a system font, basic SFX)
   — the game is fully playable without them but they're the biggest
   gap between "functional" and "shippable."
4. Consider whether the AI needs any strengthening once a human can
   actually playtest it on hardware/Citra.
