# Scenario format

M1 reads a strict version-4 `.sim` file and converts it to typed state before
the first simulation step. Raw names, strings and configuration never belong
in a hot loop.

A scenario chooses one generic kernel. Its local Lua module supplies policy and
is compiled ahead of time by CLX, so Lua is not interpreted while M1 runs.

## Bundle layout

Keep a scenario beside the rules and artwork it depends on:

| Need | File | Selector |
| --- | --- | --- |
| Reusable bundle | `proj/scenarios/<name>/scenario.sim` | `templates/<name>` |
| Variant sharing one bundle | `proj/scenarios/<bundle>/<name>.sim` | `templates/<bundle>/<name>` |
| Repository fixture | `tests/scenarios/fixtures/<name>.sim` | `test/<name>` |
| Invalid fixture | `tests/scenarios/fixtures/invalid/<name>.sim` | `test/invalid/<name>` |

A reusable bundle may also hold `scene.meta` and local assets. Production demos
must resolve a safe `[rules] file=*.lua` entry through the generated CLX
registry; there is no second native controller for scene-specific behaviour.

Use this metadata to make a bundle renderable:

```ini
[scene]
kind=demo
art=ready

[poster]
subtitle=One complete sentence with one scenario joke
meme=UNIQUE MEME TEXT
```

`kind` is `demo` or `benchmark`. Film belongs to the presentation format, not
the scene kind. The batch visualiser skips benchmark bundles and any demo that
does not declare `art=ready`.

## Run and replay

```bash
make m
build/dev/bin/m1 templates/conway
build/dev/bin/m1 templates/conway --snapshots --seed 17
build/dev/bin/m1 templates/chronus --snapshots --seed auto
```

`--snapshots` writes state under `results/snapshots/`; `--stream` sends the same
rows to standard output for live playback. A numeric seed fixes simulation
choices and tie breaks. `auto` chooses a fresh seed and prints the resolved
`run_seed`, which can be supplied later for replay.

The renderer derives a separate `render_seed`. Camera and presentation
variation can therefore change without changing simulation state or its
checksum. Benchmarks accept numeric seeds only.

## Text rules

- Begin with `[scenario]` and `[world]`
- Give each section a unique heading and each field a unique key
- Use ASCII letters, digits, `_` and `-` in names
- Write comments after `#`
- End a physical line with `\` to continue it
- Leave no empty item in a comma-separated list
- Use finite numbers; `NaN` and infinity are rejected
- Keep asset and Lua paths relative and free of `.`, `..`, `\` and `:` components

Unknown simulation sections and fields fail immediately. Presentation and cue
fields are checked when the visualiser loads them. Errors include the source
line, so start with the first one.

## Scenario and world

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

`kernel` is `continuous`, `cellular`, `turn`, `timeline` or `pde`. Continuous
worlds require `time_step`, `seed` and either `boundary=wrap` or
`boundary=bounded`. Bounded movement reflects at the wall; wrapped movement
uses toroidal distance.

Cellular worlds also accept bounded or wrapped edges. Turn, timeline and PDE
worlds are bounded, have no time step and may carry a seed when their policy
needs a replayable choice.

The optional output section controls state export:

```ini
[output]
snapshot_stride=1
view=plane
```

`snapshot_stride` is a positive integer. `view` is `plane` or `grid`, with grid
required by cellular and turn scenarios.

## Continuous characters

Declare the number of character types, then define each one:

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

`count` sets the starting population and `capacity` reserves inactive slots.
Fixed `x` and `y` coordinates are valid only for a singleton. Presentation
fields are `visible`, `shape`, `colour`, `glyph`, `layer`, `size`, `label`,
`sprite` and `motion`; a sprite names a bundle image and requires
`shape=sprite`.

Every continuous character needs `speed`. `max_steering` caps the combined
steering vector before speed is applied.

A behaviour definition accepts `code`, `weight`, optional `target` and optional
`parameter`. Supported codes are:

- `idle`, `seek`, `flee`, `pursue`, `evade` and `consume`
- `separate`, `align`, `cohere`, `avoid` and `wander`

Sensing needs `target` and `sensing_radius`. `consume` also needs a
`capture_radius` no larger than the sensing radius. For `pursue` and `evade`,
`parameter` is the look-ahead multiplier; for `separate` and `avoid`, it is the
local distance. A character may list a definition name or a built-in code in
`behaviours`.

## Cellular kernel

Width and height define the integer grid. `[cellular]` gives the number of
states, and optional inline `cells` contains exactly `width*height` digits.

```ini
[cellular]
states=13

[rules]
file=rules.lua
```

The module may implement `on_setup()` to fill the initial buffer and must
implement `next_cell(current, generation, cell)` for transitions.
`engine.neighbour_count(state)` is available only inside `next_cell`;
`engine.board_set` may change cellular state only during setup.

Conway uses this path directly. Chronus uses its cellular state for coupled
country values while timeline agents carry the visible transport layer. The
dataset origins and modelling boundaries are recorded once in
[the reference ledger](refs.md).

## PDE kernel

The PDE path solves one to eight uncoupled two-dimensional linear parabolic
fields. It does not claim arbitrary nonlinear or higher-dimensional equations.

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

The extents, final time and sample point are required. A non-zero `x_scale` or
`y_scale` applies asinh clustering around its matching focus; zero keeps that
axis uniform. `theta` is optional and ranges from `1/3` to `1`.

Lua supplies the initial value, coefficients and four boundaries:

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

The solver evaluates
`u_tau = xx*u_xx + xy*u_xy + yy*u_yy + x*u_x + y*u_y + value*u + source`.
`field` is the zero-based index in `fields`; boundary sides are left `0`, right
`1`, bottom `2` and top `3`. Boundary kind `0` is Dirichlet, `1` is outward
Neumann and `2` is natural. A natural boundary requires zero normal diffusion.

`pde_reference` is optional. Coefficients are tabulated before stepping, which
keeps Lua calls out of the native solve loop. The complete working example is
`proj/scenarios/heston/`.

## Turn and timeline kernels

A turn scenario uses the grid topology:

```ini
[turn]
topology=grid

[rules]
file=rules.lua
```

CLX runs `on_setup()` before simulation. Turn modules then receive
`on_turn(step)` while timeline modules receive `on_timeline(step)`. Each
callback emits validated engine commands at the kernel boundary; native
`[action.*]` and `[event.*]` sections do not exist.

Timeline commands can move an entity, swap a singleton sprite or replace its
short text:

```lua
engine.move(entity, x, y, z)
engine.state(entity, image_asset_id)
engine.text(entity, text)
```

Text is limited to 96 bytes with no newline. Reusable IDs should be resolved in
`on_setup()` rather than searched every step.

Chess starts from the standard position and uses its compiled Lua legal-move
core from move one. It handles castling, en passant, promotion, checkmate,
stalemate, repetition, the fifty-move rule and insufficient material. Search
time is reported for observation and never used to choose a move.

Snapshot entity rows append `run_seed`, `render_seed`, `result` and
`turn_duration_us`, leaving a field blank where it does not apply.

## Presentation

Without a presentation section, exports use the neutral theme for 20 seconds:

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
`perspective`. Demos last 8–120 seconds and films last 180–1200 seconds. Use
perspective only where a horizon and depth order remain legible; boards and
cellular grids normally stay flat.

Assets and timed cues are bundle-local:

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

Asset kinds are `image` and `audio`. Every cue needs `frame` and one of these
kinds:

- `backdrop`, `camera`, `caption`, `sprite` and `effect`
- `audio`, `music`, `narration`, `dialogue` and `scene`

Backdrop, sprite, audio and music cues name an asset. Textual cues carry
`text`; dialogue also takes `speaker`, `voice` and a rate from 80–300. Optional
placement fields are `x`, `y`, `width`, `height`, `rotation`, `scale`,
`opacity`, `duration`, `volume` and `layer`.

`parallax` applies only to backdrops, sprites and effects. It ranges from `0`
to `1.25` and defaults to `1`. The flat-scene layer convention is far
background `-100`, environment `-60`, gameplay `0`, effects `20` and
foreground `80`.

`scene.meta` records the visual intent, palette, layer order, materials,
relative sizes, safe margin and review frames. Depth-aware placement uses
`plane`, `depth`, `foot_y`, `pose`, `material` and `occluder`. The renderer
sorts those fields with a stable entity ID, so the same state draws in the same
order.

Prefix an asset path with `shared/` to load it from `proj/assets/`. Missing art
must remain visibly missing; do not hide it behind emoji or an unrelated
placeholder.

## Render a bundle

```bash
build/dev/bin/m1 templates/chess --snapshots

uv run --locked --offline python -m typer proj.visualiser run render \
  results/snapshots/chess.csv \
  --scene-meta proj/scenarios/chess/scene.meta --check

uv run --locked --offline python -m typer proj.visualiser run render \
  results/snapshots/chess.csv \
  --scene-meta proj/scenarios/chess/scene.meta --fps 60 \
  --export chess.mp4
```

`--reduced-motion` disables authored camera pans. To export every finished demo
at 2560×1440 and 60 fps, run `tools/scripts/test.sh viz`.

## Before adding a bundle

- Keep variants beside the Lua and artwork they share
- Add one deterministic fixture when parser or simulation behaviour changes
- Run the formatter, M1 build and deterministic test suite
- Read [the performance contract](performance.md) before changing a measured case
