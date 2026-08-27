# M1 scenario guide

M1 reads a strict version-4 `.sim` file, validates it once, then compiles it
into typed state before the first step. A production scenario selects one
existing generic kernel and supplies scenario policy through its local Lua
module, compiled ahead of time by CLX. It is not a runtime scripting API.

> [!IMPORTANT]
> Keep raw text, names and configuration out of the hot loop. The parser turns
> them into compact IDs and numeric values before simulation starts.

## Find or make a scenario

Reusable scenarios are bundles. Put variants directly in that bundle when
they share its Lua and assets:

| Need | Path under `proj/scenarios/` | Run selector |
| --- | --- | --- |
| Reusable scenario | `<name>/scenario.sim` | `templates/<name>` |
| Shared variant | `<bundle>/<name>.sim` | `templates/<bundle>/<name>` |
| Repository test | `tests/scenarios/fixtures/<name>.sim` or `<name>/scenario.sim` when it has Lua/assets | `test/<name>` |
| Invalid repository test | `tests/scenarios/fixtures/invalid/<name>.sim` or `<name>/scenario.sim` when it has Lua | `test/invalid/<name>` |

Each reusable scenario has a local `[rules] file=*.lua` module. Bundles may
also contain `scene.meta` and assets. CMake rejects a production demo without
a safe local Lua file or a matching generated CLX registry entry. Do not
introduce a native scene-specific controller as a second rules path.

```ini
[scene]
kind=demo
art=ready

[poster]
subtitle=One complete sentence, with one scenario joke.
meme=UNIQUE MEME TEXT
```

`[scene]` accepts `kind=demo|benchmark` and `art=ready`; film is a
`[presentation] format`, not a scene kind. Benchmark bundles keep the shared
`[poster]` fields but are skipped by production rendering.

`proj/scenarios/` deliberately sits outside `proj/m1/`: M1 contains the current
serial compiler and kernels, while M2 can consume the same validated scenario
inputs instead of copying presentation data or rules into a parallel tree.

## Export every demo

```bash
tools/scripts/test.sh viz
```

This builds M1 once, snapshots each template whose `scene.meta` declares
`kind=demo` and `art=ready`, and writes a 2560×1440, 60 fps video to
`results/videos/`.
It deliberately skips capacity and benchmark templates.

```bash
make m
build/dev/bin/m1 templates/conway
build/dev/bin/m1 templates/conway --snapshots
build/dev/bin/m1 templates/conway --snapshots --seed 17
```

`--snapshots` writes state CSV files under `results/snapshots/`; the visualiser
reads presentation and cue data from the scenario bundle.
`--stream` writes snapshots to standard output for the visualiser's `play`
command. Snapshot and stream exports select a fresh seed unless `--seed UINT64`
is supplied. M1
prints and stores that resolved `run_seed`, so `--seed <run_seed>` reproduces
the simulation decisions. This is pseudo-deterministic: the numeric seed fixes
all seeded choices and tie breaks for that run, while `auto` explicitly asks
for a fresh recorded seed. `render_seed` is derived separately from `run_seed`,
so presentation variation cannot change simulation state or its checksum.
Benchmark runs require a numeric seed.

## File rules

- Start with `[scenario]` and `[world]`; both are required
- Use one `[section]` heading and unique `key=value` fields per section
- `#` starts a comment; blank lines are ignored
- End a physical line with `\` to continue it
- Names use ASCII letters, digits, `_` and `-`
- Comma-separated lists cannot contain empty items
- Simulation sections and fields fail when unknown; `[presentation]` and
  `[cue.*]` are validated by the visualiser
- Numeric values must be finite; the parser rejects `NaN` and infinity
- Local asset and Lua paths must be relative, with no `.` or `..` component,
  `\\` or `:`

The error prints the source line, so fix the first error rather than guessing
at the rest.

## Shared sections

Every scenario begins with these two sections:

```ini
[scenario]
version=4
kernel=continuous

[world]
width=120
height=68
time_step=0.05
steps=900
seed=7
boundary=wrap
```

`kernel` is one of `continuous`, `cellular`, `turn`, `timeline` or `pde`.
Continuous worlds require `time_step`, `seed` and either `boundary=wrap` or
`boundary=bounded`. Wrapped worlds use the toroidal benchmark path. Bounded
worlds use direct distance and reflect movement at the wall, which keeps a
flat presentation arena inside the camera. Cellular worlds also take `bounded`
or `wrap`. Turn, timeline and PDE worlds require `boundary=bounded`; they
have no time step and may use an optional seed when a policy needs replayable tie
breaking or a selected reel.

`[output]` is optional. `snapshot_stride` is a positive integer; `view` is
`plane` or `grid`. Cellular and turn scenarios require `view=grid`.

```ini
[output]
snapshot_stride=1
view=plane
```

`[presentation]` is optional. Without it, exports use the neutral theme and a
20-second duration. The visualiser validates it when rendering; templates use it to control `title`, optional
`subtitle`, `theme`, `duration_seconds`, `format`, `projection`, `hud`,
`labels`, `trails` and `vectors`.

```ini
[presentation]
title=Conway
subtitle=Cellular automaton
theme=conway
duration_seconds=60
format=demo
projection=flat
hud=false
labels=none
trails=0
vectors=false
```

`format` is `demo` or `film`; `projection` is `flat`, `isometric` or
`perspective`. Demo duration is 8–120 seconds and film duration is 180–1200
seconds. Presentation output is diagnostic or for a talk: it never belongs in
a benchmark run. Use perspective only when the scene has a legible horizon and
depth ordering; boards and cellular grids normally remain flat.

## Characters and behaviours

Declare the number of character sections, then one section per name:

```ini
[characters]
count=1

[character.actor]
count=90
speed=6
behaviours=wander
shape=icon
glyph=actor
colour=315f72
size=1.05
motion=grounded
```

`count` is the initial population and `capacity` reserves inactive slots.
`x` and `y` place a singleton; generated populations cannot take fixed
coordinates. `visible`, `shape`, `colour`, `glyph`, `layer`, `size`, `label`,
`sprite` and `motion` describe presentation. A `sprite` must name an image
asset and is used only with `shape=sprite`.

`max_steering` caps the combined steering vector before speed is applied.

Continuous characters need `speed`. Any sensing behaviour also needs a
`target` and `sensing_radius`; `consume` additionally needs `capture_radius`.
Capture radius cannot exceed sensing radius.

Behaviour definitions use `code`, optional `target`, `weight`, and optional
`parameter`. Supported codes are `idle`, `seek`, `flee`, `pursue`, `evade`,
`consume`, `separate`, `align`, `cohere`, `avoid` and `wander`. `pursue` and
`evade` use `parameter` as a look-ahead multiplier. `separate` and `avoid` use
it as their local distance. A character can use a definition name or a built-in
code in its comma-separated `behaviours` list.

## Kernel sections

### Continuous

Use characters and behaviours as above. The kernel handles movement, local
sensing, capture and steering. Use `boundary=wrap` for periodic worlds and
`boundary=bounded` for contained scenes.

### Cellular

`[cellular]` needs `states` and a bundle-local Lua rules file. Width and height
are integer grid dimensions; optional inline `cells` contains exactly
`width*height` digits. The Lua module owns every state transition.

```ini
[cellular]
states=13

[rules]
file=rules.lua
```

Conway's Lua module seeds the board and implements its local transitions.
`chronus` advances susceptible, exposed, infectious, recovered,
vaccinated and deceased shares for 177 coupled countries. Its 1,200 aircraft and
300 ships are timeline agents: each keeps an observed/fallback airport pair or
water-routed maritime corridor while dated closures, reopenings, wars and
diversions alter its synthetic schedule. Ship agents reverse at route ends and
carry deterministic wave deviation without leaving their corridor. The
renderer adds deterministic aircraft turbulence and ship motion only inside
dated WMO storm footprints; the displacement is an animation, not an observed
track. Closures, reciprocal airspace bans, controlled reopenings, port
restrictions and war diversions remain visible as operational map alerts.
The renderer projects the synthetic shares onto Natural Earth boundaries and shows
all 300 airport and 250 port nodes in the bundled network. Of the 1,200 air
corridors, 447 are airport pairs observed in the January 2020 OpenSky snapshot;
753 use the historical OpenFlights network, selected from real airport pairs
with additional coverage across Africa, Latin America, India, Russia, South-East
Asia and Oceania.
Carrier colours and labels appear only when that corridor's OpenFlights airline
code matches the bundled carrier table; other aircraft stay neutral rather than
being assigned a made-up operator.
The renderer keeps one aircraft marker per small screen cell and draws faint
corridors beneath land, so dense European and Indian hubs stay readable without
deleting any route agent.
Airport rows retain OurAirports and OpenFlights IDs, while every port carries a
WPI or UN/LOCODE source ID. The original 150 sea corridors follow the SeaRoute
1.6.0 Marnet lane graph, including named canals and straits, and retain
route-wise World Bank 2015–2021 AIS-density weighting. The additional 150 are
WPI-plus-SeaRoute modeled connectivity: `ais_samples=0` and a conservative
`0.15` cadence, not observed voyages or schedules. The positions remain
simulated rather than live traffic or published timetables. It is not a
forecast, historical case data, or medical claim.

The eight-card world feed rotates geography with sport, technology, business,
light stories and disasters while de-duplicating countries.

Cellular Lua may define `on_setup()` to fill the initial cell buffer and
`next_cell(current, generation, cell)` to return each cell's next state.
Both functions are compiled by CLX. `engine.neighbour_count(state)` is
available only inside `next_cell`; `engine.board_set` is accepted for cellular
state only during setup.

### PDE

`pde` solves uncoupled two-dimensional linear parabolic fields; it is not a
claim to execute arbitrary nonlinear or higher-dimensional PDEs. `[world]`
provides an integer `width` and `height` grid, `steps`, and
`boundary=bounded`. It must also name a local `[rules]` Lua module.

```ini
[scenario]
version=4
kernel=pde

[world]
width=2049
height=321
steps=256
boundary=bounded

[rules]
file=rules.lua

[pde]
fields=call,put
x_min=0
x_max=42000
y_min=0
y_max=0.5
final_time=0.24931506849315068
sample_x=5200
sample_y=0.04
x_focus=5250
x_scale=800
y_focus=0
y_scale=0.03
theta=0.33333333333333333
```

`fields` names one to eight independent fields. `x_min`, `x_max`, `y_min`,
`y_max`, `final_time`, `sample_x` and `sample_y` are required. `x_focus`,
`x_scale`, `y_focus`, `y_scale` select optional asinh grid clustering; zero
scales keep an axis uniform. `theta` is optional and ranges from `1/3` to `1`.
The sample point must lie in the stated extent.

Lua supplies the initial data, coefficients and four boundaries:

```lua
function pde_initial(field, x, y)
  return 0
end

function pde_coefficients(field, x, y)
  return xx, xy, yy, x_drift, y_drift, value, source
end

function pde_boundary(field, side, coordinate, tau)
  return kind, value
end

function pde_reference(field)
  return reference
end
```

The equation is
`u_tau = xx*u_xx + xy*u_xy + yy*u_yy + x*u_x + y*u_y + value*u + source`.
`pde_initial`, `pde_coefficients` and `pde_boundary` are required;
`pde_reference` is optional and reports a comparison value. `field` is the
zero-based position in the `[pde] fields` list. Boundary `side` is `0` left,
`1` right, `2` bottom, or `3` top. Return `kind=0` for
Dirichlet, `1` for outward Neumann, or `2` for natural. Natural boundaries
require zero normal diffusion (`xx` on left/right, `yy` on bottom/top).
Coefficients are tabulated once before stepping so the hot C++ solve loop does
not call Lua per cell. See `proj/scenarios/heston/` for the Heston experiment.

### Turn

`[turn]` selects a fixed grid whose dimensions come from `[world]`.

```ini
[turn]
topology=grid

[rules]
file=rules.lua
```

`[rules]` names the static Lua module entry compiled by CLX v0.3.0 during the
build. It is required for every production demo. `on_setup` runs before simulation; turn scenarios define
`on_turn(step)` and timeline scenarios define `on_timeline(step)`. Compiled
callbacks run at their kernel boundary and emit validated engine commands;
there are no native `[action.*]` or `[event.*]` sections. Continuous callbacks
never run inside an entity update; cellular `next_cell` is the per-cell
transition function. PDE callbacks define its data before the native solve;
they are described in the PDE section below. Board demonstrations use
`controller=script` and a compiled Lua policy where appropriate. Chess uses
only its CLX-compiled Lua
legal-move core from the standard initial position: castling, en passant,
promotion, checkmate, stalemate, repetition, fifty-move and
insufficient-material results are evaluated before and after each move. Both
players search from move one without a scripted opening. Its bounded legal-move
evaluator is deterministic; displayed turn and cumulative search times are
measurements, not inputs to move choice.

### Timeline

Timeline animation is driven by its bundle-local compiled Lua module. Resolve
reusable IDs in `on_setup`, then emit current-state commands from
`on_timeline(step)`; generic C++ kernels remain responsible only for shared
physics and rendering mechanics.
`engine.move(entity, x, y, z)`
changes position and optional height; `engine.state(entity, image_asset_id)`
swaps a singleton sprite pose; and `engine.text(entity, text)` updates a
singleton text actor. Text is limited to 96 newline-free bytes. The callback
runs once per timeline step, not inside a physics or search inner loop.

### Snapshot run columns

Entity rows append `run_seed`, `render_seed`, `result` and `turn_duration_us`.
They are blank where a field does not apply. `run_seed` records a replayable
run; `turn_duration_us` records callback work, not a game-clock input.

## Assets and cues

```ini
[asset.bell]
file=bell.wav
kind=audio

[cue.ring]
frame=42
kind=audio
asset=bell
volume=0.7
```

Asset kinds are `image` and `audio`. A cue is one of
`backdrop`, `camera`, `caption`, `sprite`, `audio`, `music`, `narration`,
`effect`, `dialogue` or `scene`. Every cue needs `frame` and `kind`.

- `backdrop`, `sprite`, `audio` and `music` need a matching asset
- `backdrop` also needs positive world-space `width` and `height`
- `caption`, `narration`, `effect`, `dialogue` and `scene` need `text`
- `dialogue` also needs `speaker`, `voice` and `rate` from 80–300
- Optional placement fields are `x`, `y`, `width`, `height`, `rotation`,
  `scale`, `opacity`, `duration`, `volume` and `layer`
- `parallax` is optional on `backdrop`, `sprite` and `effect` cues only; it is
  finite from `0` to `1.25` and defaults to `1`

Flat scenes use a small fixed layer convention: far background `-100` at
`parallax=0.2`, environment `-60` near `0.55`, gameplay near `0`, effects near
`20`, and foreground near `80` at `1.1`. Parallax changes camera translation,
not zoom, so layers stay registered during a camera move.

Use camera, caption and effect cues to present a simulation. Do not use them
to implement simulation rules.

`scene.meta` defines the production visuals. Its `[visual]` section records
reference intent, palette, layer order, materials, relative-size constraints,
camera/parallax, safe margin and required review frames. Entity/cue placement
uses `plane`, `depth`, `foot_y`, `pose`, `material` and `occluder`
when the scene needs depth. The renderer sorts deterministically by plane,
ground contact, depth and stable ID; only compiled Lua changes scene state.

Asset paths are bundle-relative. Prefix a path with `shared/` to read it from
`proj/assets/`, which keeps presentation art reusable by M1 and M2.
Use the named asset at its intended scale and crop; it is part of the scene
art. Do not replace a missing asset with emoji, an unrelated icon pack or a
synthetic placeholder and then call the scene complete.

Tabletop scenarios use a top-view table surface and readable card/piece stacks.
Carrom's board marks follow the supplied reference layout:
pockets, baselines, circles and centre motif. Chess uses board-edge files and
ranks, captured-piece rails, named players and recorded work timing.

## Render a scenario

```bash
make m
build/dev/bin/m1 templates/chess --snapshots

uv run --locked --offline python -m typer proj.visualiser run render \
  results/snapshots/chess.csv \
  --scene-meta proj/scenarios/chess/scene.meta --check

uv run --locked --offline python -m typer proj.visualiser run render \
  results/snapshots/chess.csv \
  --scene-meta proj/scenarios/chess/scene.meta --fps 60 \
  --export 'chess.mp4'
```

Use `--reduced-motion` on `render` or `play` to disable authored camera pans.
`tools/scripts/test.sh viz` exports the renderable templates.

## Before adding a template

```bash
make fmt
make m
build/dev/bin/m1 templates/<name>
tools/scripts/test.sh test
```

Add the smallest focused deterministic test under `tests/` whenever a template
adds a parser or simulation rule. Read [Performance](performance.md) before
changing the benchmark scenario.
