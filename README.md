# Input Buffer: Geode mod for Geometry Dash

Target: **Geode v5.10.1, GD 2.2081** (Windows / Android / macOS / iOS).

```
input-buffer/
├── mod.json          settings, keybind, metadata
├── about.md          in-game mod description
├── CMakeLists.txt
└── src/
    ├── Buffer.hpp/.cpp   buffer engine: discrete release-hold + flight-mode delay search
    ├── Predict.hpp/.cpp  look-ahead simulator (hidden clone players, GD's own physics)
    ├── SimHooks.cpp      stops clones from affecting the real game during simulation
    ├── Overlay.hpp/.cpp  corner status panel + stage-change toast
    └── main.cpp          hooks: input, physics step, level lifecycle, pause menu, hotkey
```

## Build

```
# once: install the Geode CLI + SDK  (https://docs.geode-sdk.org/getting-started)
geode sdk install
geode sdk install-binaries

cd input-buffer
geode build          # produces build/yourname.input-buffer.geode
```

Change `id` / `developer` in `mod.json` before building. Drop the `.geode` file into GD via *Geode → Install from file*.

## How it works

### Where it hooks

| Hook | Role |
|---|---|
| `GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick)` | Runs once per physics step (240/s). `Engine::onTick` runs **before** the original, so held releases are resolved before the step's inputs and physics. |
| `GJBaseGameLayer::handleButton(down, button, isPlayer1)` | Where each press/release reaches the game. `Engine::onButton` may swallow a release. |
| `PlayLayer::init / resetLevel / onQuit` | Adds the overlay, resets the tick counter and per-attempt stats. |
| `PauseLayer::customSetup` | Adds the stage button and a settings shortcut. |

### The mechanism: hold, don't replay

Rather than inventing a synthetic click on a guessed "valid frame", the engine **extends the hold**:

1. **Press**: passed through untouched. The engine snapshots the player (last jump time, last flip time, y-velocity, gravity, dash state).
2. **Release**: if the press hasn't produced an action yet, and the player is in a discrete mode, the release is swallowed. GD still sees the button as held.
3. **Each tick**: the engine checks whether an action happened since the press:
   - `m_lastJumpTime` / `m_lastFlipTime` changed (jumps, ring jumps, ball/spider flips)
   - `m_isUpsideDown` flipped (blue/green orbs)
   - `m_isDashing` went true (dash orbs)
   - fallback: a y-velocity jump larger than gravity can produce in one tick
4. **Resolve**:
   - Action happened → send the release on the next tick and log `saved = consumedTick − releaseTick`.
   - N ticks passed with no action → send the release. Optionally log it as unused.
   - Player died, stage switched to Off, or a new press arrived → release immediately.

Because the game's own hold logic does the jump (cube landing with the button down, GD 2.2's buffered orbs), the motion is exactly what vanilla produces for a slightly longer hold. There's no teleporting or position correction to look odd in slow motion. The only difference from vanilla is that the button stays down for at most N extra ticks.

The synthetic release goes back through `layer->handleButton(...)`, so other mods and GD's own input recording see an ordinary release. A re-entry flag keeps the engine from catching its own dispatch.

### Ship / wave / swing: simulate, then delay

In these modes there's no "valid frame" to wait for, because holding *is* the movement. A fixed wave angle doesn't capture it either: ship and swing paths curve, and speed portals, mini size and gravity change the slope. So instead of computing angles, the buffer runs the game's own physics ahead of time on two hidden clone players (`Predict.cpp`).

On every click or release in a flight mode:

1. **Quick check (the common case)**: clone the icon, apply the input now, and step it `margin` ticks (default 6, about 25 ms) with `PlayerObject::update` + `checkCollisions`. If it survives, the input goes through untouched. This costs about 6 simulated steps per input.
2. **Imminent crash**: the input would kill you within the margin, faster than anyone could steer out. Try delaying it by k = 1…N ticks (N = stage window). For each k: hold the old state for k ticks, switch, and count how long the icon survives afterwards, up to `lookahead` ticks (default 48).
3. **Pick**: the smallest k that survives the whole look-ahead, or failing that the k that survives longest, but only if it beats the margin. If holding the old state crashes before tick k, larger delays can't help, so the search stops. If nothing beats sending it now, the input goes through as-is.
4. **Apply**: the input is swallowed and re-sent through `handleButton` on tick `now + k`. The overlay logs it as `+3t wave click   #12044   P1`. The console logs `... delayed wave press 3 ticks ... would have crashed in 2`.

The margin is what keeps this from over-steering. In a tight wave corridor, almost any input leads to a crash 20-30 ticks later if you never touched the button again. Those crashes are yours to steer around, so they're ignored. Only near-instant crashes count, like clipping a spike corner because you clicked 2 ticks early.

Dual mode simulates both icons, and a timing must keep both alive. Mixed dual (cube + wave) goes through the flight path as well.

**Limits, by design:**
- **Delays only.** A click that comes too *late* can't be fixed without rewinding the icon. That would be a visible snap, so late inputs are left alone. In practice this covers about half of mistimed inputs, the early ones.
- **Static world.** The look-ahead checks collisions against the level as it is on the tick of your input. Moving saws and move triggers are frozen for those few ticks, so at Stage 3 with fast-moving hazards a pick can occasionally be off.
- **Unknown outcomes stay hands-off.** If a simulated path touches an orb, pad, portal, teleport or touch trigger, that run is marked unknown and never used to justify a delay. Gravity portals are the exception and are simulated on the clone.
- **Cost.** An actual rescue at Stage 3 can simulate up to ~16 × 64 steps once. Stages 1-2 stay small. Turn *Flight Buffer* off in settings to drop back to vanilla flight input.

### Keeping clones from touching the game

While a simulation runs, `SimHooks.cpp` intercepts everything a clone could do to the real game:

| Hook | During simulation |
|---|---|
| `PlayLayer::destroyPlayer` | clone crash recorded, real death path never runs |
| `playerTouchedRing`, `playerTouchedTrigger`, `bumpPlayer`, `playerWillSwitchMode`, `teleportPlayer`, `toggleDualMode` | run aborted (unknown) |
| `flipGravity` | applied to the clone only, no level effects |
| `pickupItem`, `destroyObject`, `gameEventTriggered` | skipped (no coins, keys, broken blocks or event triggers) |
| `PlayerObject::tryPlaceCheckpoint` | skipped for clones (no practice auto-checkpoints) |
| `PlayerObject::update` | records the real step delta so clones advance the same amount |

Platformer mode is skipped entirely.

## Things to verify in-game

These are written against the public 2.2081 bindings but haven't been run:

- **Clone sync**: at Stage 3, turn on *Log Unused Buffers* and fly a wave through an easy section. You should see no flight saves at all. If saves appear on clean inputs, `copyAttributes` isn't carrying some state across. Extend `Predictor::sync` (mini size, speed, gravity are the usual suspects).
- **Step order**: clones run `update` then `checkCollisions` per step. If the real loop also calls `PlayerObject::postCollision` / `updateRotation` in between and predictions drift, add those to `Predictor::step`.
- **Clone creation**: clones are `PlayerObject::create(1, 1, layer, layer->m_objectLayer, false)`, invisible, with effects off. Check that no stray trail or particles appear at the level start position.

- **Tick rate**: confirm `processCommands` with `isHalfTick == false` fires about 240×/s (log `Engine::tick()` once a second). If half-ticks are counted wrong, windows will be 2× too long or short.
- **Orbs while held**: buffering an orb relies on GD activating it while the button is still down. If a particular orb type demands a fresh click edge, early taps on it won't be saved. Check yellow, blue, dash and spider orbs at Stage 3, where it's easiest to see.
- **Detection false positives**: pads and gravity portals also trip the "action happened" check. That just ends the window early, which is harmless but means a few saves won't register.
- **Pause menu placement**: the button row sits bottom-right. Adjust `menu->setPosition` in `main.cpp` if it overlaps anything on your layout.

## Overlay & telemetry

- Corner panel: stage chip (`S2 / 4t`), saves this attempt, best save, then the newest saves (`+2t   tick #12044   P1`) fading with age. It pulses on each save.
- Stage change: centered toast (`Stage 2 - 4 tick window`).
- Console: with *Console Telemetry* on, every save and stage change is mirrored to the Geode console and log file, e.g. `[Buffer] Stage 1 saved 2 ticks on tick #12044 (P1)`.
- Settings for position, scale, log length, hide-when-off and unused-buffer logging are in the mod's settings, or the gear button in the pause menu. Overlay settings apply on the next stage change, save, or level load.

The overlay can't be hidden while a stage is active. That's deliberate; see the note in `about.md`.
