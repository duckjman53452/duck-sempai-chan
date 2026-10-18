# Input Buffer: Geode mod for Geometry Dash

Target: **Geode v5.10.1, GD 2.2081** (Windows / Android / macOS / iOS).
All sources pass a full compile check (`-fsyntax-only`) against the Geode v5.10.1 headers and the 2.2081 bindings. Every hooked function has a real address on every platform.

```
input-buffer/
├── mod.json                 settings, keybind, metadata
├── .github/workflows/       GitHub build -> one .geode for every platform
└── src/
    ├── TimingMap.hpp/.cpp   shadow scan at level open: route + safe window per input
    ├── Predict.hpp/.cpp     hidden clone players running GD's own physics
    ├── SimHooks.cpp         keeps clones from touching the real game
    ├── Buffer.hpp/.cpp      input engine: map -> look-ahead -> release hold
    ├── Visuals.hpp/.cpp     shadow route, frame-shift markers, look-ahead paths
    ├── Overlay.hpp/.cpp     corner panel: stage, map status, diagnostics, log
    └── main.cpp             hooks + level lifecycle + pause menu + F6
```

## Building

Push the folder to a GitHub repo. The included workflow builds it on GitHub's servers (Actions tab → *Build Output* artifact → `.geode`). Change `id` / `developer` in `mod.json` first if you like.

## What happens when a level opens

1. **Shadow scan** (`TimingMap::scan`), queued for the frame after the level finishes loading:
   - A hidden clone icon starts at your start position and plays the level with GD's own `PlayerObject::update` + `checkCollisions`. It searches inputs depth-first, deciding every 2 ticks and trying "keep the current input" before "switch". Duplicate states are pruned, and it backtracks from every crash until it reaches the end or hits the time limit (default 5 s).
   - The route found is replayed tick by tick and recorded: x, y, mode, gravity, and button state per tick.
   - Each click/release on the route is re-tested 1…16 ticks earlier and later (the rest of the route unchanged, `Look-ahead` ticks of survival required). This gives every input a safe window.
   - The level is then reset, so portals, orbs and pads the shadow used are back to normal. The attempt counter isn't bumped.
2. **Shadow route drawn into the level**: thin line (blue = holding, white = released), blue dots = clicks, pink dots = releases, green bars = safe windows.
3. A toast and the overlay's MAP line show the result, e.g. `MAP 100%  412 inputs  3.1s` or `MAP 63%  (time limit)`.

## What happens on every input

In order, first match wins:

| | When | What it does | Marker color |
|---|---|---|---|
| 1 | You're **on the shadow route**: same mode and gravity, within 6 units of its y, same button state | Inside the input's safe window → untouched. Up to N ticks *early* → moved into the window, aiming between the earliest safe tick and the shadow's own tick. | blue |
| 2 | Ship / wave / swing, **off the route** | Live look-ahead: if sending it now crashes within the reaction margin, delay it to the nearest tick that survives. | yellow |
| 3 | Other modes, **off the route** | Early release is held until the click gets used (orb, landing). | green |

N is the stage window (Stage 1: 1-2, Stage 2: 3-6, Stage 3: 8-16 ticks). **Off** = vanilla.

## Seeing it change frames

- **Frame-shift marker** (in the level): a **ring** where you actually pressed, a **dot** where the game received it, a line between, and a floating **+Nt**. Colored by source (table above).
- **Look-ahead paths**: when #2 steps in, the path that would have crashed is drawn in **red with an X**, and the one it picked in **green**.
- **Overlay log**: `+3t wave click  map  #12044`. Console: `[Buffer] Stage 2 map moved wave click +3 ticks on tick #12044 (P1) into the shadow's safe window`.

## Diagnostics line

`in 38 | 240 tps | route 12 | map 3 look 1 hold 0 | sims 30 (4 crash, 2 ?)`

| Reading | Means |
|---|---|
| `in` stays 0 while you click | the input hook isn't firing |
| `tps` far from 240 | tick counting is off, so windows are wrong length |
| `route` stays 0 | you're never matching the shadow route (or MAP failed) |
| `sims` rises, `crash` always 0 | clones never detect deaths: look-ahead can't trigger |
| `crash` ≈ `sims` even on easy parts | clones die instantly: state copy is wrong |
| stage chip says `OFF` | press F6 or use the pause-menu button; nothing is changed while Off |

## Limits

- **Frozen-level scan.** GD only runs triggers and moves objects while the level actually plays, so the scan sees the level as it is at the start. Anywhere move/toggle/spawn triggers change the layout, the map's route can be wrong. The "on the route" check stops it from being used where you aren't actually following it, and the live look-ahead covers flight modes there.
- **Delays only.** A late input can't be pulled earlier without snapping the icon backwards.
- **One shadow.** Dual portals stop the scan (`MAP nn% (dual portal)`). Platformer and 2-player levels skip the map.
- **Big levels** may need a longer *Scan Time Limit*. The level-open pause lasts up to that long.
