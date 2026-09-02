"""Parse snapshot data and draw visual frames

The engine writes CSV snapshots while scenario files provide presentation data
This module checks both inputs, interpolates frames, and draws shared
primitives
Scenario renderers receive drawing helpers through bind and add theme hooks
"""

import bisect
import configparser
import csv
import importlib.util
import itertools
import math
import os
import random
import re
import sys
from dataclasses import dataclass, field, replace
from pathlib import Path

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")

ROOT = Path(__file__).resolve().parents[2]
WINDOW = (1920, 1080)


def load_asset_table(directory):
    """Read a name-to-file table rooted at one trusted asset directory"""
    source = directory / "assets.tsv"
    with source.open(encoding="utf-8", newline="") as handle:
        rows = csv.DictReader(handle, delimiter="\t")
        if tuple(rows.fieldnames or ()) != ("name", "path"):
            raise ValueError(f"{source}: invalid asset table header")
        return {row["name"]: directory / row["path"] for row in rows}


def load_static_table(name):
    source = ROOT / "proj/assets" / name
    with source.open(encoding="utf-8", newline="") as handle:
        return tuple(csv.DictReader(handle, delimiter="\t"))


def load_pygame():
    try:
        import pygame
    except ImportError as error:
        raise RuntimeError("Pygame is missing; run uv sync") from error
    return pygame


SHARED_ASSETS = load_asset_table(ROOT / "proj/assets")
PAPER_TEXTURE = SHARED_ASSETS["paper_texture"]
DARK_WOOD = SHARED_ASSETS["dark_wood"]
ANGULAR_ACTIVE = SHARED_ASSETS["angular_active"]
HAND_FONT = SHARED_ASSETS["hand_font"]
PALETTE = [
    (int(row["red"]), int(row["green"]), int(row["blue"]))
    for row in load_static_table("palette.tsv")
]
PERSON_TOKENS = frozenset(
    row["token"] for row in load_static_table("person-tokens.tsv")
)
PLANE_ROWS = load_static_table("planes.tsv")
PLANE_ORDER = {row["plane"]: int(row["order"]) for row in PLANE_ROWS}
PLANE_SCALES = {row["plane"]: float(row["scale"]) for row in PLANE_ROWS}
# Every frame starts with metadata, followed by entity rows and an end row
STATE_FIELDS = {
    "frame",
    "record",
    "entity_id",
    "type_id",
    "type_name",
    "x",
    "y",
    "world_width",
    "world_height",
    "view",
    "shape",
    "colour",
    "glyph",
    "layer",
    "theme",
    "run_seed",
    "render_seed",
}


@dataclass(frozen=True)
class Theme:
    sky: tuple[int, int, int]
    ground: tuple[int, int, int]
    ink: tuple[int, int, int]
    accent: tuple[int, int, int]
    scene: str


VISUAL_FIELDS = ("theme", "sky", "ground", "ink", "accent", "scene")


def visual_colour(text, source, line):
    try:
        colour = tuple(int(value) for value in text.split(","))
    except ValueError as error:
        raise ValueError(f"{source}:{line}: invalid RGB colour") from error
    if len(colour) != 3 or any(value < 0 or value > 255 for value in colour):
        raise ValueError(f"{source}:{line}: invalid RGB colour")
    return colour


def load_scene_visuals():
    """Load shared and scenario theme tables before snapshots are accepted"""
    # Shared themes load first so scenario tables can add their own themes
    sources = [ROOT / "proj/assets/visual.tsv"]
    sources.extend(
        sorted((ROOT / "proj/scenarios").glob("*/assets/visual.tsv"))
    )
    themes, assets = {}, {}
    for source in sources:
        with source.open(encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            if tuple(reader.fieldnames or ()) != VISUAL_FIELDS:
                raise ValueError(f"{source}: invalid visual table header")
            for line, row in enumerate(reader, 2):
                name = row["theme"].strip()
                if not name or name in themes:
                    raise ValueError(f"{source}:{line}: duplicate visual theme")
                themes[name] = Theme(
                    visual_colour(row["sky"], source, line),
                    visual_colour(row["ground"], source, line),
                    visual_colour(row["ink"], source, line),
                    visual_colour(row["accent"], source, line),
                    row["scene"].strip(),
                )
                if source.parent.parent.parent.name == "scenarios":
                    assets[name] = source.parent
    return themes, assets


THEMES, SCENE_ASSETS = load_scene_visuals()
THEME_NAMES = {theme: name for name, theme in THEMES.items()}


@dataclass(frozen=True)
class Entity:
    entity_id: int
    type_id: int
    name: str
    x: float
    y: float
    shape: str
    colour: tuple[int, int, int]
    glyph: str
    layer: int
    sprite: str = ""
    rotation: float = 0.0
    scale: float = 1.0
    opacity: float = 1.0
    size: float = 1.0
    label: str = ""
    velocity_x: float = 0.0
    velocity_y: float = 0.0
    z: float = 0.0
    state_id: int = 0
    motion: str = "static"
    facing_left: bool = False
    plane: str = "auto"
    material: str = ""
    pose: str = ""
    foot_y: float = -1.0
    occluder: bool = False


@dataclass
class Frame:
    # Entities retain source order while entity_index is built only when needed
    number: int
    width: float
    height: float
    view: str
    entities: list[Entity]
    presentation: dict[str, str] = field(default_factory=dict)
    entity_index: dict[int, Entity] = field(default_factory=dict)
    source_dimensions: tuple[str, str, str] = field(
        default=("", "", ""), repr=False
    )


@dataclass(frozen=True)
class Cue:
    # Cue positions use world coordinates except caption screen fractions
    frame: int
    kind: str
    asset: Path | None
    text: str
    x: float
    y: float
    width: float
    height: float
    rotation: float
    scale: float
    opacity: float
    duration: float
    volume: float
    layer: int
    parallax: float
    speaker: str = ""
    voice: str = ""
    rate: int = 0
    material: str = ""
    ambient: str = ""
    when_result: str = ""


@dataclass
class RenderOptions:
    labels: bool = False
    trails: bool = False
    trail_length: float = 0.0
    vectors: bool = False
    hud: bool = False
    pan_x: float = 0.0
    pan_y: float = 0.0
    zoom: float = 1.0
    reduced_motion: bool = False
    focus_entity: int | None = None
    focus_radius: float = 0.0
    suppress_title_card: bool = False


def finite(text, field, line):
    try:
        value = float(text)
    except ValueError as error:
        raise ValueError(f"line {line}: {field} must be a number") from error
    if not math.isfinite(value):
        raise ValueError(f"line {line}: {field} must be finite")
    return value


def integer(text, field, line):
    try:
        return int(text)
    except ValueError as error:
        raise ValueError(f"line {line}: {field} must be an integer") from error


def seed_value(text, field, line):
    value = integer(text, field, line)
    if not 0 <= value < 1 << 64:
        raise ValueError(
            f"line {line}: {field} must be an unsigned 64-bit integer"
        )
    return value


def derived_render_seed(run_seed):
    """Derive the render seed with the SplitMix64 finalizer"""
    value = (run_seed + 0x9E3779B97F4A7C15) & ((1 << 64) - 1)
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & ((1 << 64) - 1)
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & ((1 << 64) - 1)
    return value ^ (value >> 31)


def optional_number(row, field, line, default):
    value = row.get(field, "")
    return default if not value else finite(value, field, line)


def optional_integer(row, field, line, default):
    value = row.get(field, "")
    return default if not value else integer(value, field, line)


def dimensions(row, line):
    width = finite(row["world_width"], "world_width", line)
    height = finite(row["world_height"], "world_height", line)
    view = row["view"]
    if width <= 0.0 or height <= 0.0:
        raise ValueError(f"line {line}: world dimensions must be positive")
    if view not in ("plane", "grid"):
        raise ValueError(f"line {line}: view must be plane or grid")
    if view == "grid" and (not width.is_integer() or not height.is_integer()):
        raise ValueError(f"line {line}: grid dimensions must be integers")
    return width, height, view


def parse_colour(text, type_id, line):
    if not text:
        return PALETTE[type_id % len(PALETTE)]
    if len(text) != 6 or any(
        char not in "0123456789abcdefABCDEF" for char in text
    ):
        raise ValueError(f"line {line}: colour must be six hex digits")
    return (
        int(text[0:2], 16),
        int(text[2:4], 16),
        int(text[4:6], 16),
    )


def safe_file(root, value, field, line):
    """Resolve one relative asset and reject escapes through paths or links"""
    candidate = Path(value)
    if not value or candidate.is_absolute() or ".." in candidate.parts:
        raise ValueError(f"line {line}: {field} must be a local asset")
    # Resolve symlinks before checking containment
    root = root.resolve()
    path = (root / candidate).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f"line {line}: {field} is not a readable local asset")
    return path


def repository_file(value, field):
    """Resolve one user file and require it to remain in the repository"""
    candidate = Path(value)
    path = (
        candidate.resolve()
        if candidate.is_absolute()
        else (Path.cwd() / candidate).resolve()
    )
    if not path.is_relative_to(ROOT.resolve()) or not path.is_file():
        raise ValueError(f"{field} must be a readable file in this repository")
    return path


def load_scene_meta(path):
    """Validate metadata used only for poster and scene presentation details"""
    parser = configparser.ConfigParser(interpolation=None)
    try:
        with path.open(encoding="utf-8") as source:
            parser.read_file(source)
    except configparser.Error as error:
        raise ValueError(f"invalid scene metadata: {error}") from error
    if set(parser.sections()) != {"scene", "poster", "visual"}:
        raise ValueError(
            "scene metadata needs exactly [scene], [poster], and [visual]"
        )
    # Scene fields decide whether this scenario may export a poster
    scene = {key: value.strip() for key, value in parser["scene"].items()}
    if (
        set(scene) != {"kind", "art"}
        or scene["kind"] not in {"demo", "benchmark"}
        or scene["art"] != "ready"
    ):
        raise ValueError("[scene] needs kind=demo|benchmark and art=ready")
    # Poster text has strict length limits because it is drawn at a fixed size
    poster = {key: value.strip() for key, value in parser["poster"].items()}
    if set(poster) != {"subtitle", "meme"} or any(
        not value for value in poster.values()
    ):
        raise ValueError("[poster] needs exactly subtitle and meme")
    if len(poster["subtitle"]) > 120 or len(poster["meme"]) > 40:
        raise ValueError("poster copy is too long")
    # Visual fields are presentation hints rather than simulation inputs
    visual = {key: value.strip() for key, value in parser["visual"].items()}
    required = {
        "reference",
        "palette",
        "layers",
        "scale",
        "camera",
        "safe_margin",
    }
    optional = {
        "projection",
        "materials",
        "surface_stack",
        "layer_order",
        "horizon",
        "passive_agents",
        "actors",
        "narration_focus",
        "hud",
        "disclaimer",
        "reduced_motion",
        "terminal_hold_seconds",
        "policy",
        "violence",
    }
    unknown = set(visual) - required - optional
    if unknown:
        raise ValueError(f"[visual] has unknown field: {unknown.pop()}")
    if not required <= set(visual) or any(not visual[key] for key in required):
        raise ValueError(
            "[visual] needs reference, palette, layers, scale, camera, and safe_margin"
        )
    try:
        margin = float(visual["safe_margin"])
    except ValueError as error:
        raise ValueError("[visual] safe_margin must be numeric") from error
    if not math.isfinite(margin) or not 0.02 <= margin <= 0.15:
        raise ValueError("[visual] safe_margin must be between 0.02 and 0.15")
    if "projection" in visual and visual["projection"] not in {
        "flat",
        "isometric",
        "perspective",
    }:
        raise ValueError(
            "[visual] projection must be flat, isometric, or perspective"
        )
    for key in (
        "materials",
        "surface_stack",
        "layer_order",
        "passive_agents",
        "actors",
        "narration_focus",
        "hud",
        "disclaimer",
        "policy",
        "violence",
    ):
        if key in visual and (len(visual[key]) > 240 or not visual[key]):
            raise ValueError(
                f"[visual] {key} must be non-empty and at most 240 characters"
            )
    for key in ("reduced_motion",):
        if key in visual:
            visual_bool(visual[key], f"[visual] {key}")
    hold = None
    if "terminal_hold_seconds" in visual:
        hold = finite(
            visual["terminal_hold_seconds"], "[visual] terminal_hold_seconds", 0
        )
        if not 0.0 <= hold <= 10.0:
            raise ValueError("[visual] terminal_hold_seconds must be in 0..10")
    result: dict[str, str | float] = dict(visual)
    result["safe_margin"] = margin
    if hold is not None:
        result["terminal_hold_seconds"] = hold
    return poster, result


def row_entity(row, line, facing=None):
    """Validate one entity row and return its immutable renderer record"""
    # Decode optional render fields before the checks that combine them
    entity_id = integer(row["entity_id"], "entity_id", line)
    type_id = integer(row["type_id"], "type_id", line)
    name = row["type_name"].strip()
    shape = row["shape"]
    rotation = optional_number(row, "rotation", line, 0.0)
    scale = optional_number(row, "scale", line, 1.0)
    opacity = optional_number(row, "opacity", line, 1.0)
    size = optional_number(row, "size", line, 1.0)
    z = optional_number(row, "z", line, 0.0)
    velocity_x = optional_number(row, "velocity_x", line, 0.0)
    velocity_y = optional_number(row, "velocity_y", line, 0.0)
    state_id = optional_integer(row, "state_id", line, 0)
    motion = row.get("motion") or "static"
    plane = row.get("plane", "auto").lower() or "auto"
    material = row.get("material", "").strip()
    pose = row.get("pose", "").strip().lower()
    foot_y = optional_number(row, "foot_y", line, -1.0)
    occluder_text = row.get("occluder", "false").lower()
    if occluder_text not in {
        "0",
        "1",
        "true",
        "false",
        "yes",
        "no",
        "on",
        "off",
    }:
        raise ValueError(f"line {line}: invalid occluder")
    occluder = occluder_text in {"1", "true", "yes", "on"}
    if entity_id < 0 or type_id < 0 or not name:
        raise ValueError(f"line {line}: invalid entity or type")
    if shape not in {"circle", "cell", "icon", "text", "sprite"}:
        raise ValueError(f"line {line}: invalid shape")
    if shape == "text" and not row["glyph"]:
        raise ValueError(f"line {line}: text shape needs a glyph")
    if shape == "icon" and (
        not row["glyph"]
        or len(row["glyph"]) > 32
        or any(not char.isalnum() and char not in "-_" for char in row["glyph"])
    ):
        raise ValueError(f"line {line}: invalid icon")
    if motion not in {"static", "grounded", "ballistic", "flight", "water"}:
        raise ValueError(f"line {line}: invalid motion")
    if plane not in {"auto", "far", "mid", "ground", "fore", "overlay"}:
        raise ValueError(f"line {line}: invalid plane")
    if pose and any(not char.isalnum() and char not in "_-" for char in pose):
        raise ValueError(f"line {line}: invalid pose")
    if foot_y < -1.0:
        raise ValueError(
            f"line {line}: foot_y must be non-negative when present"
        )
    if (
        scale <= 0.0
        or size <= 0.0
        or z < 0.0
        or state_id < 0
        or not 0.0 <= opacity <= 1.0
    ):
        raise ValueError(f"line {line}: invalid scale, size or opacity")
    # Flight rotation and facing derive from velocity to keep rows compact
    facing_left = False
    if facing is not None and motion == "flight":
        if abs(velocity_x) > 0.05:
            facing[entity_id] = velocity_x < 0.0
        rotation = max(
            -15.0,
            min(
                15.0,
                -math.degrees(math.atan2(velocity_y, abs(velocity_x) + 0.1))
                * 0.25,
            ),
        )
        facing_left = facing.get(entity_id, False)
    return Entity(
        entity_id,
        type_id,
        name,
        finite(row["x"], "x", line),
        finite(row["y"], "y", line),
        shape,
        parse_colour(row["colour"], type_id, line),
        row["glyph"],
        integer(row["layer"], "layer", line),
        row.get("sprite", ""),
        rotation,
        scale,
        opacity,
        size,
        row.get("label", ""),
        velocity_x,
        velocity_y,
        z,
        state_id,
        motion,
        facing_left,
        plane,
        material,
        pose,
        foot_y,
        occluder,
    )


def state_reader(source, message):
    """Create a CSV reader after checking the required engine state columns"""
    # The engine may add fields, but every visualiser field must remain present
    reader = csv.DictReader(source)
    if reader.fieldnames is None or not STATE_FIELDS <= set(reader.fieldnames):
        raise ValueError(message)
    return reader


def parse_state_row(row, line, current, last, facing=None):
    """Advance the frame-row-entity state machine by one snapshot row

    Frame rows open a frame, entity rows populate it, and end rows return it
    Invalid ordering, duplicate ids, and changing frame metadata fail here
    """
    # Keep this state machine strict so partial streams cannot form frames
    if None in row or None in row.values():
        raise ValueError(f"line {line}: snapshot row width is invalid")
    number = integer(row["frame"], "frame", line)
    if number < 0:
        raise ValueError(f"line {line}: frame must be non-negative")
    record = row["record"]
    run_seed = seed_value(row["run_seed"], "run_seed", line)
    render_seed = seed_value(row["render_seed"], "render_seed", line)
    if render_seed != derived_render_seed(run_seed):
        raise ValueError(f"line {line}: render_seed does not match run_seed")
    # Frame rows establish metadata inherited by the following entity rows
    if record == "frame":
        if number <= last:
            raise ValueError(f"line {line}: frames must be ordered")
        width, height, view = dimensions(row, line)
        projection = row.get("projection") or "flat"
        if projection not in {"flat", "isometric", "perspective"}:
            raise ValueError(f"line {line}: invalid projection")
        theme = row["theme"]
        if theme not in THEMES:
            raise ValueError(f"line {line}: invalid presentation theme")
        if view == "grid" and projection != "flat":
            raise ValueError(f"line {line}: grid views require flat projection")
        return Frame(
            number,
            width,
            height,
            view,
            [],
            {
                key: row.get(key, "")
                for key in (
                    "title",
                    "subtitle",
                    "theme",
                    "duration_seconds",
                    "hud",
                    "labels",
                    "trails",
                    "vectors",
                    "focus_entity",
                    "focus_radius",
                    "kernel",
                    "projection",
                    "format",
                    "run_seed",
                    "render_seed",
                    "result",
                    "turn_duration_us",
                )
            },
            source_dimensions=(row["world_width"], row["world_height"], view),
        ), None
    # End rows close the current frame before it reaches the caller
    if record == "end":
        if current is None or number != current.number:
            raise ValueError(f"line {line}: end needs the current frame")
        return None, current
    if record != "entity" or current is None or number != current.number:
        raise ValueError(f"line {line}: entity needs the current frame")
    width, height, view = current.width, current.height, current.view
    if (
        row["world_width"],
        row["world_height"],
        row["view"],
    ) != current.source_dimensions and dimensions(row, line) != (
        width,
        height,
        view,
    ):
        raise ValueError(f"line {line}: frame metadata changed")
    entity = row_entity(row, line, facing)
    if not (0.0 <= entity.x < width and 0.0 <= entity.y < height):
        raise ValueError(f"line {line}: position is outside the world")
    if view == "grid" and (
        not entity.x.is_integer() or not entity.y.is_integer()
    ):
        raise ValueError(f"line {line}: grid positions must be integers")
    if entity.entity_id in current.entity_index:
        raise ValueError(f"line {line}: duplicate entity_id")
    current.entities.append(entity)
    current.entity_index[entity.entity_id] = entity
    return current, None


def parse_frames(source):
    """Parse every completed frame from a snapshot stream in source order"""
    # Snapshot files omit frame objects, so frame rows delimit each group
    reader = state_reader(
        source, "snapshot header does not match the state contract"
    )
    frames = []
    current, facing = None, {}
    for line, row in enumerate(reader, 2):
        current, _ = parse_state_row(
            row, line, current, frames[-1].number if frames else -1, facing
        )
        if row["record"] == "frame":
            frames.append(current)
    if not frames:
        raise ValueError("snapshot contains no frames")
    return frames


def load_frames(path):
    """Open one checked UTF-8 snapshot and return its parsed frame sequence"""
    with path.open(newline="", encoding="utf-8") as source:
        return parse_frames(source)


def cue_error(kind, text):
    """Return a cue error or an empty string for a valid kind and text"""
    if kind not in {
        "camera",
        "caption",
        "sprite",
        "audio",
        "music",
        "narration",
        "effect",
        "dialogue",
        "scene",
        "backdrop",
        "poster",
    }:
        return "invalid cue"
    if (
        kind in {"caption", "narration", "effect", "dialogue", "scene"}
        and not text
    ):
        return f"{kind} needs text"
    if kind == "scene" and text not in THEMES:
        return "invalid scene theme"
    return ""


NARRATION_BANNED = frozenset(
    {"lua", "aot", "kernel", "seed", "deterministic", "backend"}
)


def narration_error(text):
    words = {word.strip(".,;:!?()[]{}\"'").lower() for word in text.split()}
    banned = sorted(words & NARRATION_BANNED)
    return "" if not banned else f"narration cannot mention {banned[0]}"


def visual_bool(value, field):
    if value not in {"true", "false"}:
        raise ValueError(f"{field} must be true or false")
    return value


def visual_asset(source, value, line):
    if not value or Path(value).is_absolute() or ".." in Path(value).parts:
        raise ValueError(f"line {line}: asset must be a local file")
    root = (
        ROOT / "proj" / "assets"
        if value.startswith("shared/")
        else source.parent
    )
    path = (root / value.removeprefix("shared/")).resolve()
    if not path.is_relative_to(ROOT.resolve()) or not path.is_file():
        raise ValueError(f"line {line}: asset is not a readable local file")
    return path


def load_visual_plan(source):
    """Read and validate presentation, assets, and cues from one scenario file

    Presentation values are copied to every snapshot frame after parsing
    Asset paths and cue ranges are checked here so drawing code can trust them
    """
    # Scenario files describe presentation while snapshots remain engine output
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    try:
        with source.open(encoding="utf-8") as handle:
            parser.read_file(handle)
    except configparser.Error as error:
        raise ValueError(f"invalid scenario presentation: {error}") from error
    # Defaults let simulation-only scenarios render without presentation data
    scenario = (
        dict(parser.items("scenario")) if parser.has_section("scenario") else {}
    )
    kernel = scenario.get("kernel", "")
    section = (
        dict(parser.items("presentation"))
        if parser.has_section("presentation")
        else {}
    )
    fields = {
        "title",
        "subtitle",
        "theme",
        "duration_seconds",
        "hud",
        "labels",
        "trails",
        "vectors",
        "format",
        "projection",
        "focus_entity",
        "focus_radius",
        "pacing",
    }
    unknown = set(section) - fields
    if unknown:
        raise ValueError(f"[presentation]: unknown field: {unknown.pop()}")
    presentation = {
        "title": "",
        "subtitle": "",
        "theme": "neutral",
        "duration_seconds": "20",
        "hud": "true",
        "labels": "auto",
        "trails": "0",
        "vectors": "false",
        "format": "demo",
        "projection": "flat",
        "focus_entity": "",
        "focus_radius": "",
        "pacing": "",
        "kernel": kernel,
    }
    presentation.update({key: value.strip() for key, value in section.items()})
    if section and not {"title", "theme", "duration_seconds"} <= set(section):
        raise ValueError(
            "[presentation] needs title, theme and duration_seconds"
        )
    if (
        not presentation["title"]
        and section
        or len(presentation["title"]) > 80
        or len(presentation["subtitle"]) > 120
        or presentation["theme"] not in THEMES
    ):
        raise ValueError("invalid presentation title, subtitle or theme")
    duration = finite(presentation["duration_seconds"], "duration_seconds", 0)
    steps = integer(parser.get("world", "steps", fallback="-1"), "steps", 0)
    film = presentation["format"] == "film"
    if presentation["format"] not in {"demo", "film"} or not (
        180.0 <= duration <= 1200.0 if film else 8.0 <= duration <= 300.0
    ):
        raise ValueError("invalid presentation format or duration_seconds")
    if film and kernel != "timeline":
        raise ValueError("film format needs timeline kernel")
    if presentation["projection"] not in {"flat", "isometric", "perspective"}:
        raise ValueError("invalid presentation projection")
    if presentation["labels"] not in {"auto", "none", "name"}:
        raise ValueError("invalid presentation labels")
    if not 0 <= integer(presentation["trails"], "trails", 0) <= 240:
        raise ValueError("invalid presentation trails")
    visual_bool(presentation["hud"], "presentation hud")
    visual_bool(presentation["vectors"], "presentation vectors")
    focus = presentation["focus_entity"], presentation["focus_radius"]
    if bool(focus[0]) != bool(focus[1]):
        raise ValueError("focus_entity and focus_radius must be used together")
    if focus[0] and (
        integer(focus[0], "focus_entity", 0) < 0
        or finite(focus[1], "focus_radius", 0) <= 0.0
    ):
        raise ValueError("invalid presentation focus")
    if presentation["pacing"]:
        # Knots map simulation frames onto screen time for non-linear playback
        knots = []
        for item in presentation["pacing"].split(","):
            parts = item.split(":")
            if len(parts) != 2:
                raise ValueError("pacing needs frame:seconds knots")
            knots.append(
                (
                    integer(parts[0], "pacing frame", 0),
                    finite(parts[1], "pacing seconds", 0),
                )
            )
        if (
            len(knots) < 2
            or knots[0] != (0, 0.0)
            or knots[-1] != (steps, duration)
            or any(
                right[0] <= left[0] or right[1] <= left[1]
                for left, right in itertools.pairwise(knots)
            )
        ):
            raise ValueError("pacing knots must span the ordered run")
    if (
        focus[0]
        and "characters" in parser
        and integer(
            parser["characters"].get("count", "-1"), "characters count", 0
        )
        <= integer(focus[0], "focus_entity", 0)
    ):
        raise ValueError("focus_entity is outside the scenario population")
    # Resolve named files once so cues reference paths rather than raw text
    assets = {}
    for name in parser.sections():
        if not name.startswith("asset."):
            continue
        asset = parser[name]
        if set(asset) != {"file", "kind"} or asset["kind"] not in {
            "image",
            "audio",
        }:
            raise ValueError(f"[{name}] needs file and image or audio kind")
        assets[name[6:]] = asset["kind"], visual_asset(source, asset["file"], 0)
    # Validate every cue before sorting it into presentation order
    cues = []
    for name in parser.sections():
        if not name.startswith("cue."):
            continue
        cue = parser[name]
        allowed = {
            "frame",
            "kind",
            "asset",
            "text",
            "x",
            "y",
            "width",
            "height",
            "rotation",
            "scale",
            "opacity",
            "duration",
            "volume",
            "layer",
            "parallax",
            "speaker",
            "voice",
            "rate",
            "material",
            "ambient",
            "when_result",
        }
        if set(cue) - allowed or "frame" not in cue or "kind" not in cue:
            raise ValueError(f"[{name}] has invalid cue fields")
        kind, text = cue["kind"], cue.get("text", "")
        if len(text) > 512 or "\n" in text or "\r" in text:
            raise ValueError(f"[{name}]: invalid cue text")
        if error := cue_error(kind, text):
            raise ValueError(f"[{name}]: {error}")
        if kind in {"narration", "dialogue"} and (
            error := narration_error(text)
        ):
            raise ValueError(f"[{name}]: {error}")
        asset_name = cue.get("asset", "")
        needs_asset = kind in {"sprite", "audio", "music", "backdrop"}
        if needs_asset and asset_name not in assets:
            raise ValueError(f"[{name}]: cue kind needs an asset")
        if asset_name and asset_name not in assets:
            raise ValueError(f"[{name}]: unknown cue asset")
        if asset_name and not (needs_asset or kind == "poster"):
            raise ValueError(f"[{name}]: cue kind does not take an asset")
        asset = assets.get(asset_name)
        if asset and asset[0] != (
            "audio" if kind in {"audio", "music"} else "image"
        ):
            raise ValueError(f"[{name}]: cue kind does not match its asset")
        values = [
            finite(cue.get(field, default), field, 0)
            for field, default in (
                ("x", "0"),
                ("y", "0"),
                ("width", "0"),
                ("height", "0"),
                ("rotation", "0"),
                ("scale", "1"),
                ("opacity", "1"),
                ("duration", "0"),
                ("volume", "1"),
            )
        ]
        if (
            values[5] <= 0.0
            or not 0.0 <= values[6] <= 1.0
            or min(values[2:4] + values[7:]) < 0.0
        ):
            raise ValueError(f"[{name}]: invalid cue numeric range")
        frame, layer = (
            integer(cue["frame"], "frame", 0),
            integer(cue.get("layer", "0"), "layer", 0),
        )
        parallax = finite(cue.get("parallax", "1"), "parallax", 0)
        if frame < 0 or not 0.0 <= parallax <= 1.25:
            raise ValueError(f"[{name}]: invalid cue frame or parallax")
        if kind not in {"backdrop", "sprite", "effect"} and parallax != 1.0:
            raise ValueError(
                f"[{name}]: parallax only applies to world visuals"
            )
        if kind == "backdrop" and (values[2] == 0.0 or values[3] == 0.0):
            raise ValueError(
                f"[{name}]: backdrop needs positive width and height"
            )
        if kind == "poster" and frame != 0:
            raise ValueError(f"[{name}]: poster cue must use frame=0")
        rate = integer(cue.get("rate", "0"), "rate", 0)
        if rate and not 80 <= rate <= 300:
            raise ValueError(f"[{name}]: rate must be in 80..300")
        if any(
            len(cue.get(field, "")) > limit
            or "\n" in cue.get(field, "")
            or "\r" in cue.get(field, "")
            for field, limit in (("speaker", 48), ("voice", 80))
        ):
            raise ValueError(f"[{name}]: invalid cue speaker or voice")
        if kind == "dialogue" and (
            not cue.get("speaker")
            or not cue.get("voice")
            or not 80 <= rate <= 300
        ):
            raise ValueError(
                f"[{name}]: dialogue needs speaker, voice and rate"
            )
        ambient = cue.get("ambient", "").lower()
        if ambient and ambient not in {"bubbles", "steam", "embers"}:
            raise ValueError(f"[{name}]: invalid ambient")
        when_result = cue.get("when_result", "")
        if when_result:
            when_result = str(integer(when_result, "when_result", 0))
        x, y, width, height, rotation, scale, opacity, duration, volume = values
        cues.append(
            Cue(
                frame,
                kind,
                asset[1] if asset else None,
                text,
                x,
                y,
                width,
                height,
                rotation,
                scale,
                opacity,
                duration,
                volume,
                layer,
                parallax,
                cue.get("speaker", ""),
                cue.get("voice", ""),
                rate,
                cue.get("material", ""),
                ambient,
                when_result,
            )
        )
    return presentation, sorted(cues, key=lambda cue: cue.frame)


def apply_visual_plan(frames, presentation):
    """Attach validated scenario presentation values to every parsed frame"""
    for frame in frames:
        frame.presentation.update(presentation)
        if frame.view == "grid" and presentation["projection"] != "flat":
            raise ValueError("grid views require flat projection")


def smoothstep(value):
    value = min(1.0, max(0.0, value))
    return value * value * (3.0 - 2.0 * value)


def drop_progress(value):
    value = min(1.0, max(0.0, value))
    return value * value


def lerp(start, end, amount):
    return start + (end - start) * amount


def lerp_angle(start, end, amount):
    delta = (end - start + 180.0) % 360.0 - 180.0
    return start + delta * amount


def wrapped_lerp(start, end, amount, extent):
    delta = end - start
    if abs(delta) > extent * 0.75:
        delta -= math.copysign(extent, delta)
    return (start + delta * amount) % extent


def interpolate_entity(start, end, amount, frame):
    """Interpolate one entity between surrounding snapshots for playback"""
    # Missing endpoints fade while a ballistic grid arrival drops from above
    dropping = (
        start is None
        and end is not None
        and frame.view == "grid"
        and end.motion == "ballistic"
    )
    if start is None:
        assert end is not None
        if dropping:
            start = replace(end, y=-1.0, opacity=1.0)
        else:
            return replace(end, opacity=end.opacity * amount)
    if end is None:
        return replace(start, opacity=start.opacity * (1.0 - amount))
    kernel = frame.presentation.get("kernel", "")
    if dropping:
        amount = drop_progress(amount)
        x, y, z = end.x, lerp(start.y, end.y, amount), 0.0
    elif kernel == "continuous" and frame.view == "plane":
        # Continuous worlds wrap at edges instead of crossing the whole map
        x = wrapped_lerp(start.x, end.x, amount, frame.width)
        y = wrapped_lerp(start.y, end.y, amount, frame.height)
        z = lerp(start.z, end.z, amount)
    elif kernel == "timeline" and end.motion == "static":
        return start
    else:
        amount = smoothstep(amount)
        x = lerp(start.x, end.x, amount)
        y = lerp(start.y, end.y, amount)
        z = lerp(start.z, end.z, amount)
        distance = math.hypot(end.x - start.x, end.y - start.y)
        if end.motion == "ballistic" and kernel != "timeline":
            z += max(1.0, distance * 0.22) * 4.0 * amount * (1.0 - amount)
        elif end.motion == "flight":
            z += max(0.15, end.size * 0.12) * math.sin(math.pi * amount)
        elif end.motion == "water":
            z += max(0.03, end.size * 0.025) * math.sin(2.0 * math.pi * amount)
    return replace(
        end,
        x=x,
        y=y,
        z=max(0.0, z),
        state_id=start.state_id,
        rotation=lerp_angle(start.rotation, end.rotation, amount),
        scale=lerp(start.scale, end.scale, amount),
        opacity=lerp(start.opacity, end.opacity, amount),
        size=lerp(start.size, end.size, amount),
        velocity_x=lerp(start.velocity_x, end.velocity_x, amount),
        velocity_y=lerp(start.velocity_y, end.velocity_y, amount),
    )


def stabilise_flight(frames):
    """Preserve flight direction and derive a bounded bank angle"""
    # Heading persists through slow frames so sprites do not flip repeatedly
    facing = {}
    for frame in frames:
        stable = []
        for entity in frame.entities:
            if entity.motion != "flight":
                stable.append(entity)
                continue
            if abs(entity.velocity_x) > 0.05:
                facing[entity.entity_id] = entity.velocity_x < 0.0
            bank = -math.degrees(
                math.atan2(entity.velocity_y, abs(entity.velocity_x) + 0.1)
            )
            stable.append(
                replace(
                    entity,
                    rotation=max(-15.0, min(15.0, bank * 0.25)),
                    facing_left=facing.get(entity.entity_id, False),
                )
            )
        frame.entities = stable
        frame.entity_index.clear()


def frame_index(frame):
    if not frame.entity_index:
        frame.entity_index = {
            entity.entity_id: entity for entity in frame.entities
        }
    return frame.entity_index


def entity_appearance(entity):
    return entity.type_id, entity.shape, entity.colour, entity.glyph


def interpolate_turn(start, end, amount, result):
    """Interpolate one turn while keeping discrete changes aligned"""
    before, after = frame_index(start), frame_index(end)
    unchanged = {
        cell
        for cell in before.keys() & after.keys()
        if entity_appearance(before[cell]) == entity_appearance(after[cell])
    }
    result.entities.extend(after[cell] for cell in sorted(unchanged))
    removed = [before[cell] for cell in before.keys() - unchanged]
    added = [after[cell] for cell in after.keys() - unchanged]
    for new in sorted(added, key=lambda entity: entity.entity_id):
        choices = [
            old
            for old in removed
            if entity_appearance(old) == entity_appearance(new)
        ]
        if choices:
            old = min(
                choices,
                key=lambda entity: (
                    abs(entity.x - new.x) + abs(entity.y - new.y)
                ),
            )
            removed.remove(old)
        else:
            old = None
        result.entities.append(interpolate_entity(old, new, amount, result))
    result.entities.extend(
        interpolate_entity(old, None, amount, result) for old in removed
    )


def sample_frame(frames, position, numbers=None):
    """Return the frame visible at a fractional simulation position"""
    # Locate enclosing snapshots then interpolate their matching entity ids
    numbers = numbers or tuple(frame.number for frame in frames)
    index = min(len(frames) - 1, bisect.bisect_right(numbers, position))
    if index == 0:
        return frames[0]
    start, end = frames[index - 1], frames[index]
    if position <= start.number:
        return start
    if position >= end.number:
        return end
    amount = (position - start.number) / max(1.0, end.number - start.number)
    before = frame_index(start)
    after = frame_index(end)
    result = Frame(
        start.number,
        start.width,
        start.height,
        start.view,
        [],
        start.presentation,
    )
    kernel = result.presentation.get("kernel", "")
    if kernel == "turn" and result.view == "grid":
        interpolate_turn(start, end, amount, result)
        return result
    # Stable entity order
    for entity_id in sorted(before.keys() | after.keys()):
        old, new = before.get(entity_id), after.get(entity_id)
        if kernel == "cellular":
            if old is not None:
                result.entities.append(old)
            continue
        result.entities.append(interpolate_entity(old, new, amount, result))
    return result


def trail_point(start, end, amount, entity_id):
    """Return a trail point when one entity exists at both ends"""
    old, new = (
        frame_index(start).get(entity_id),
        frame_index(end).get(entity_id),
    )
    if old is None:
        return None if new is None else (new.x, new.y, new.z)
    if new is None:
        return old.x, old.y, old.z
    kernel = start.presentation.get("kernel", "")
    if kernel == "continuous" and start.view == "plane":
        return (
            wrapped_lerp(old.x, new.x, amount, start.width),
            wrapped_lerp(old.y, new.y, amount, start.height),
            lerp(old.z, new.z, amount),
        )
    if kernel == "timeline" and new.motion == "static":
        return old.x, old.y, old.z
    amount = smoothstep(amount)
    x, y, z = (
        lerp(old.x, new.x, amount),
        lerp(old.y, new.y, amount),
        lerp(old.z, new.z, amount),
    )
    if new.motion == "ballistic" and kernel != "timeline":
        z += (
            max(1.0, math.hypot(new.x - old.x, new.y - old.y) * 0.22)
            * 4.0
            * amount
            * (1.0 - amount)
        )
    elif new.motion == "flight":
        z += max(0.15, new.size * 0.12) * math.sin(math.pi * amount)
    elif new.motion == "water":
        z += max(0.03, new.size * 0.025) * math.sin(2.0 * math.pi * amount)
    return x, y, max(0.0, z)


def active_cues(cues, position):
    return [
        cue
        for cue in cues
        if cue.frame <= position
        and (
            (cue.kind == "backdrop" and cue.duration <= 0.0)
            or position < cue.frame + max(1.0, cue.duration)
        )
    ]


def outcome_cues(cues, result, terminal_frame):
    return [
        replace(cue, frame=terminal_frame) if cue.when_result else cue
        for cue in cues
        if not cue.when_result or cue.when_result == result
    ]


def theme_for(frame):
    name = frame.presentation.get("theme", "")
    if name not in THEMES:
        raise ValueError(f"unknown presentation theme: {name}")
    return THEMES[name]


def draw_gradient_background(pygame, screen, theme, sky_only=False):
    width, height = screen.get_size()
    horizon = height if sky_only else round(height * 0.4)
    sky_glow = tuple(min(255, value + 34) for value in theme.sky)
    ground_shadow = tuple(max(0, value - 26) for value in theme.ground)
    for y in range(height):
        if y < horizon:
            amount = y / max(1, horizon)
            start, end = theme.sky, sky_glow
        else:
            amount = (y - horizon) / max(1, height - horizon)
            start, end = theme.ground, ground_shadow
        colour = tuple(
            round(lerp(first, last, amount)) for first, last in zip(start, end)
        )
        pygame.draw.line(screen, colour, (0, y), (width, y))


def draw_wood_background(pygame, screen, cache):
    width, height = screen.get_size()
    wood = image_surface(pygame, DARK_WOOD, cache)
    scale = max(width / wood.get_width(), height / wood.get_height())
    scaled = pygame.transform.smoothscale(
        wood,
        (
            round(wood.get_width() * scale),
            round(wood.get_height() * scale),
        ),
    )
    screen.blit(
        scaled,
        (
            (width - scaled.get_width()) // 2,
            (height - scaled.get_height()) // 2,
        ),
    )


def draw_background(pygame, screen, theme, cache):
    if draw := getattr(scene_renderer(theme), "draw_background", None):
        draw(pygame, screen, theme, cache)
        return
    draw_gradient_background(pygame, screen, theme)


def background_surface(pygame, cache, theme, size):
    key = "background", theme.scene, size
    if key not in cache:
        cache[key] = pygame.Surface(size)
        draw_background(pygame, cache[key], theme, cache)
    return cache[key]


def draw_backdrop(pygame, screen, background, frame, camera, cache):
    if frame.presentation.get("projection") != "perspective":
        screen.blit(background, (0, 0))
        return
    width, height = screen.get_size()
    padding_x, padding_y = max(1, width // 15), max(1, height // 15)
    key = "backdrop", id(background), width, height
    if key not in cache:
        cache[key] = pygame.transform.smoothscale(
            background,
            (width + 2 * padding_x, height + 2 * padding_y),
        )
    pan_x = (camera[0] / frame.width - 0.5) * padding_x
    pan_y = (camera[1] / frame.height - 0.5) * padding_y
    screen.blit(
        cache[key],
        (-padding_x - round(pan_x), -padding_y - round(pan_y)),
    )


def presentation_flag(frame, key, fallback):
    value = frame.presentation.get(key, "").lower()
    if not value:
        return fallback
    if key == "labels":
        if value in {"name", "on", "true", "yes", "1"}:
            return True
        if value in {"none", "off", "false", "no", "0"}:
            return False
        if value == "auto":
            return len(frame.entities) <= 32
    if value in {"true", "on", "yes", "1"}:
        return True
    if value in {"false", "off", "no", "0"}:
        return False
    return fallback


def presentation_options(frame):
    """Build per-run draw options from validated presentation fields"""
    options = RenderOptions()
    for key in ("hud", "labels", "vectors"):
        setattr(
            options, key, presentation_flag(frame, key, getattr(options, key))
        )
    trail_value = frame.presentation.get("trails", "")
    if trail_value.isdigit():
        options.trail_length = float(trail_value)
        options.trails = options.trail_length > 0.0
    else:
        options.trails = presentation_flag(frame, "trails", options.trails)
    entity = frame.presentation.get("focus_entity", "")
    radius = frame.presentation.get("focus_radius", "")
    if bool(entity) != bool(radius):
        raise ValueError("focus_entity and focus_radius must be set together")
    if entity:
        options.focus_entity = integer(entity, "focus_entity", 0)
        options.focus_radius = finite(radius, "focus_radius", 0)
        if options.focus_entity < 0 or options.focus_radius <= 0.0:
            raise ValueError("invalid presentation focus")
    return options


def presentation_duration(frame):
    return finite(
        frame.presentation.get("duration_seconds") or "20",
        "duration_seconds",
        0,
    )


def presentation_position(frame, seconds, first, last):
    """Map presentation seconds to a fractional frame using pacing knots"""
    # Pacing knots are frame:seconds pairs; linear playback is the default
    duration = presentation_duration(frame)
    seconds = min(duration, max(0.0, seconds))
    hold = finite(
        frame.presentation.get("terminal_hold_seconds", "3"),
        "terminal_hold_seconds",
        0,
    )
    if 0.0 < hold < duration:
        if seconds >= duration - hold:
            return float(last)
        seconds = seconds * duration / (duration - hold)
    text = frame.presentation.get("pacing", "")
    if not text:
        return first + (last - first) * seconds / duration
    knots = tuple(
        (int(item.split(":")[0]), float(item.split(":")[1]))
        for item in text.split(",")
    )
    times = tuple(knot[1] for knot in knots)
    index = min(len(knots) - 2, max(0, bisect.bisect_right(times, seconds) - 1))
    (left_frame, left_time), (right_frame, right_time) = knots[
        index : index + 2
    ]
    amount = (seconds - left_time) / (right_time - left_time)
    return left_frame + (right_frame - left_frame) * amount


def presentation_seconds(frame, position, first, last):
    """Map a fractional simulation frame back to presentation seconds"""
    duration = presentation_duration(frame)
    position = min(float(last), max(float(first), float(position)))
    text = frame.presentation.get("pacing", "")
    if text:
        knots = tuple(
            (int(item.split(":")[0]), float(item.split(":")[1]))
            for item in text.split(",")
        )
        numbers = tuple(knot[0] for knot in knots)
        index = min(
            len(knots) - 2, max(0, bisect.bisect_right(numbers, position) - 1)
        )
        (left_frame, left_time), (right_frame, right_time) = knots[
            index : index + 2
        ]
        amount = (position - left_frame) / (right_frame - left_frame)
        seconds = left_time + (right_time - left_time) * amount
    else:
        seconds = (position - first) * duration / max(1.0, last - first)
    hold = finite(
        frame.presentation.get("terminal_hold_seconds", "3"),
        "terminal_hold_seconds",
        0,
    )
    if 0.0 < hold < duration:
        seconds = min(duration - hold, seconds * (duration - hold) / duration)
    return seconds


def auto_camera(frame):
    if frame.view != "plane" or not 0 < len(frame.entities) <= 32:
        return frame.width / 2.0, frame.height / 2.0, 1.0
    xs = [entity.x for entity in frame.entities]
    ys = [entity.y for entity in frame.entities]
    width = max(1.0, max(xs) - min(xs))
    height = max(1.0, max(ys) - min(ys))
    margin = max(width, height) * 0.2 + min(frame.width, frame.height) * 0.04
    zoom = min(
        frame.width / (width + 2.0 * margin),
        frame.height / (height + 2.0 * margin),
    )
    return (
        (min(xs) + max(xs)) / 2.0,
        (min(ys) + max(ys)) / 2.0,
        max(1.2, min(4.0, zoom)),
    )


def camera_for(frame, cameras, position, options, window, camera_frames=None):
    """Choose the active camera or derive one from the current frame"""
    if options.reduced_motion:
        return frame.width / 2.0, frame.height / 2.0, options.zoom
    default = auto_camera(frame)

    def zoom(cue):
        if cue.width > 0.0 and cue.height > 0.0:
            projection = frame.presentation.get("projection") or "flat"
            if projection == "isometric":
                world = frame.width + frame.height
                view = cue.width + cue.height
                return cue.scale * world / view
            world_scale = min(
                window[0] / frame.width,
                window[1] / frame.height,
            )
            view_scale = min(
                window[0] / cue.width,
                window[1] / cue.height,
            )
            return cue.scale * view_scale / world_scale
        return cue.scale

    camera_frames = camera_frames or tuple(cue.frame for cue in cameras)
    index = bisect.bisect_right(camera_frames, position)
    if index == 0:
        camera = default
    else:
        target = cameras[index - 1]
        destination = target.x, target.y, zoom(target)
        if target.duration <= 0.0:
            camera = destination
        else:
            source = (
                destination
                if index == 1 and target.frame == 0
                else (
                    camera_for(
                        frame,
                        cameras[: index - 1],
                        target.frame,
                        RenderOptions(),
                        window,
                    )
                    if index > 1
                    else default
                )
            )
            amount = smoothstep((position - target.frame) / target.duration)
            camera = tuple(
                lerp(start, end, amount)
                for start, end in zip(source, destination)
            )
    return (
        camera[0] + options.pan_x,
        camera[1] + options.pan_y,
        camera[2] * options.zoom,
    )


def transform(frame, camera, x, y, window, z=0.0):
    """Project one world position into pixels and return its drawing scale"""
    # Return pixels per world unit and the projected upper-left world position
    centre_x, centre_y, zoom = camera
    projection = frame.presentation.get("projection") or "flat"
    if projection == "isometric":
        scale = (
            min(
                window[0] / (frame.width + frame.height),
                window[1] / ((frame.width + frame.height) * 0.58),
            )
            * zoom
        )
        delta_x, delta_y = x - centre_x, y - centre_y
        return (
            scale,
            window[0] / 2.0 + (delta_x - delta_y) * scale,
            window[1] / 2.0 + (delta_x + delta_y) * scale * 0.5 - z * scale,
        )
    if projection == "perspective":
        base = min(window[0] / frame.width, window[1] / frame.height) * zoom
        relative_y = (y - centre_y) / max(1.0, frame.height)
        depth_scale = 1.0 + max(-0.22, min(0.22, relative_y * 0.55))
        scale = base * depth_scale
        return (
            scale,
            window[0] / 2.0 + (x - centre_x) * scale,
            window[1] * 0.56 + (y - centre_y) * base * 0.68 - z * scale,
        )
    scale = min(window[0] / frame.width, window[1] / frame.height) * zoom
    return (
        scale,
        window[0] / 2.0 + (x - centre_x) * scale,
        window[1] / 2.0 + (y - centre_y) * scale - z * scale,
    )


def cue_camera(frame, camera, cue):
    """Apply parallax to camera position only"""
    centre_x = frame.width / 2.0
    centre_y = frame.height / 2.0
    return (
        centre_x + (camera[0] - centre_x) * cue.parallax,
        centre_y + (camera[1] - centre_y) * cue.parallax,
        camera[2],
    )


def draw_world_ground(pygame, screen, frame, camera, theme):
    """Draw flat, isometric, or perspective ground beneath world entities"""
    projection = frame.presentation.get("projection") or "flat"
    if (
        frame.view != "plane"
        or projection != "isometric"
        or frame.presentation.get("format") == "film"
    ):
        return
    corners = [
        transform(frame, camera, x, y, screen.get_size())[1:]
        for x, y in (
            (0.0, 0.0),
            (frame.width, 0.0),
            (frame.width, frame.height),
            (0.0, frame.height),
        )
    ]
    line_colour = tuple(
        (3 * ground + ink) // 4 for ground, ink in zip(theme.ground, theme.ink)
    )
    pygame.draw.lines(screen, line_colour, True, corners, 2)
    divisions = 8 if projection == "isometric" else 5
    for index in range(1, divisions):
        y = frame.height * index / divisions
        left = transform(frame, camera, 0.0, y, screen.get_size())[1:]
        right = transform(frame, camera, frame.width, y, screen.get_size())[1:]
        pygame.draw.line(screen, line_colour, left, right, 1)
        x = frame.width * index / divisions
        top = transform(frame, camera, x, 0.0, screen.get_size())[1:]
        bottom = transform(frame, camera, x, frame.height, screen.get_size())[
            1:
        ]
        pygame.draw.line(screen, line_colour, top, bottom, 1)


def draw_grid(
    pygame, screen, fonts, frame, scale, offset_x, offset_y, theme, cache
):
    """Draw the generic grid backdrop and optional coordinate labels"""
    if draw := getattr(scene_renderer(theme), "draw_grid", None):
        draw(
            pygame,
            screen,
            fonts,
            frame,
            scale,
            offset_x,
            offset_y,
            theme,
            cache,
        )
        return
    board = (
        round(offset_x),
        round(offset_y),
        round(frame.width * scale),
        round(frame.height * scale),
    )
    shadow = tuple(max(0, value - 34) for value in theme.ground)
    pygame.draw.rect(
        screen,
        shadow,
        (board[0] + 12, board[1] + 16, board[2], board[3]),
        border_radius=8,
    )
    for row in range(int(frame.height)):
        for column in range(int(frame.width)):
            pygame.draw.rect(
                screen,
                theme.ground,
                (
                    round(offset_x + column * scale),
                    round(offset_y + row * scale),
                    math.ceil(scale),
                    math.ceil(scale),
                ),
            )
    pygame.draw.rect(screen, theme.ink, board, 3)


def font_for(pygame, cache, size, bold=False, path=None):
    key = "font", path, size, bold
    if key not in cache:
        font = (
            pygame.font.Font(path, size)
            if path and path.is_file()
            else pygame.font.Font(None, size)
        )
        font.bold = bold
        cache[key] = font
    return cache[key]


def draw_text(
    pygame,
    screen,
    fonts,
    text,
    position,
    size,
    colour,
    alpha=1.0,
    bold=False,
    anchor="center",
):
    surface = font_for(pygame, fonts, size, bold).render(text, True, colour)
    surface.set_alpha(round(alpha * 255.0))
    rectangle = surface.get_rect()
    setattr(rectangle, anchor, position)
    screen.blit(surface, rectangle)


def image_surface(pygame, path, cache):
    if path not in cache:
        cache[path] = pygame.image.load(path)
    return cache[path]


def entity_sprite(pygame, path, side, cache, trim=True):
    key = "entity-sprite", path, side, trim
    if key not in cache:
        source = image_surface(pygame, path, cache)
        if trim:
            bounds = source.get_bounding_rect()
            if bounds.width and bounds.height:
                source = source.subsurface(bounds)
        width, height = source.get_size()
        ratio = min(side / max(1, width), side / max(1, height))
        cache[key] = pygame.transform.smoothscale(
            source,
            (max(1, round(width * ratio)), max(1, round(height * ratio))),
        )
    return cache[key]


def circular_entity_sprite(pygame, path, side, cache, ratio=0.49):
    key = "circular-entity-sprite", path, side
    if key not in cache:
        source = entity_sprite(pygame, path, side, cache, False)
        sprite = pygame.Surface(source.get_size(), pygame.SRCALPHA)
        sprite.blit(source, (0, 0))
        mask = pygame.Surface(sprite.get_size(), pygame.SRCALPHA)
        radius = round(min(mask.get_size()) * ratio)
        pygame.draw.circle(
            mask,
            (255, 255, 255, 255),
            mask.get_rect().center,
            radius,
        )
        sprite.blit(mask, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
        cache[key] = sprite
    return cache[key]


def size_surface(pygame, surface, width, height, world_scale, cache):
    """Scale an image to world dimensions while caching the result"""
    if width <= 0.0 and height <= 0.0:
        return surface
    source_width, source_height = surface.get_size()
    target_width = (
        width * world_scale
        if width > 0.0
        else (height * world_scale * source_width / source_height)
    )
    target_height = (
        height * world_scale
        if height > 0.0
        else (width * world_scale * source_height / source_width)
    )
    size = max(1, round(target_width)), max(1, round(target_height))
    if surface.get_size() == size:
        return surface
    key = "sized-surface", id(surface)
    previous = cache.get(key)
    if previous is not None and previous[0] == size:
        if previous[1] is not None:
            return previous[1]
        scaled = pygame.transform.smoothscale(surface, size)
        cache[key] = size, scaled
        return scaled
    scaled = pygame.transform.smoothscale(surface, size)
    cache[key] = size, None
    return scaled


def apply_surface(pygame, screen, surface, centre, rotation, scale, opacity):
    if scale != 1.0 or rotation != 0.0:
        surface = pygame.transform.rotozoom(surface, rotation, scale)
    if opacity != 1.0:
        surface = surface.copy()
        surface.set_alpha(round(opacity * 255.0))
    screen.blit(surface, surface.get_rect(center=centre))


def visible_at(frame, camera, x, y, screen, margin=0.0):
    _, x, y = transform(frame, camera, x, y, screen.get_size())
    width, height = screen.get_size()
    return -margin <= x <= width + margin and -margin <= y <= height + margin


def preload_render_assets(pygame, frames, cues, cache):
    """Load all referenced assets before preview or export starts drawing"""
    # Fail before opening a window or encoder when a referenced asset is absent
    paths = {
        cue.asset
        for cue in cues
        if cue.asset is not None and cue.kind not in {"audio", "music"}
    }
    themes = {theme_for(frame) for frame in frames}
    themes.update(
        THEMES[cue.text]
        for cue in cues
        if cue.kind == "scene" and cue.text in THEMES
    )
    for theme in themes:
        if render_assets := getattr(
            scene_renderer(theme), "render_assets", None
        ):
            paths.update(render_assets())
    for frame in frames:
        for entity in frame.entities:
            if entity.shape == "sprite":
                paths.add(
                    safe_file(ROOT, entity.sprite, "sprite", entity.entity_id)
                )
            elif entity.shape == "icon" and not icon_supported(entity.glyph):
                path = icon_asset(entity.glyph)
                if path is None:
                    raise ValueError(f"missing icon asset: {entity.glyph}")
                paths.add(path)
    for path in paths:
        image_surface(pygame, path, cache)


def canvas_size(frame):
    """Return a scenario renderer's native canvas or the shared default."""
    renderer = scene_renderer(frame)
    return getattr(renderer, "CANVAS_SIZE", WINDOW) if renderer else WINDOW


def render_context(pygame, frames, cues, reduced_motion=False):
    """Create the shared surface, font cache, asset cache, and draw options"""
    cache = {}
    preload_render_assets(pygame, frames, cues, cache)
    options = presentation_options(frames[0])
    options.reduced_motion = reduced_motion
    return pygame.Surface(canvas_size(frames[0])), {}, cache, options


def present_canvas(pygame, screen, canvas):
    width, height = screen.get_size()
    canvas_width, canvas_height = canvas.get_size()
    factor = min(width / canvas_width, height / canvas_height)
    size = (
        max(1, round(canvas_width * factor)),
        max(1, round(canvas_height * factor)),
    )
    screen.fill((0, 0, 0))
    surface = (
        canvas
        if size == canvas.get_size()
        else pygame.transform.smoothscale(canvas, size)
    )
    screen.blit(surface, ((width - size[0]) // 2, (height - size[1]) // 2))


def face_left(pygame, surface, cache):
    key = "face-left", id(surface)
    if key not in cache:
        cache[key] = pygame.transform.flip(surface, True, False)
    return cache[key]


def icon_asset(glyph):
    return next(
        (
            path
            for renderer in SCENE_RENDERERS.values()
            if (icon_asset := getattr(renderer, "icon_asset", None))
            and (path := icon_asset(glyph)) is not None
        ),
        None,
    )


def icon_supported(glyph):
    return any(
        (has_icon := getattr(renderer, "has_icon", None)) and has_icon(glyph)
        for renderer in SCENE_RENDERERS.values()
    )


def validate_render_assets(frames):
    """Check renderer-specific asset sets without creating a visible window"""
    for frame in frames:
        for entity in frame.entities:
            if entity.shape == "icon" and not icon_supported(entity.glyph):
                raise ValueError(f"missing icon asset: {entity.glyph}")
            if entity.shape == "sprite":
                safe_file(ROOT, entity.sprite, "sprite", entity.entity_id)


def asset_icon(pygame, glyph, colour, side, cache, trim=True):
    path = icon_asset(glyph)
    if path is None or not path.is_file():
        raise ValueError(f"missing icon asset: {glyph}")
    key = "asset-icon", path, colour, side, trim
    if key not in cache:
        try:
            surface = entity_sprite(pygame, path, side, cache, trim)
        except pygame.error as error:
            raise ValueError(f"unreadable icon asset: {glyph}") from error
        cache[key] = surface
    return cache[key]


def icon_surface(pygame, glyph, colour, radius, cache):
    key = "icon", glyph, colour, radius
    if key not in cache:
        rendered = next(
            (
                surface
                for renderer in SCENE_RENDERERS.values()
                if (render_icon := getattr(renderer, "icon_surface", None))
                and (
                    surface := render_icon(pygame, glyph, colour, radius, cache)
                )
                is not None
            ),
            None,
        )
        cache[key] = rendered or asset_icon(
            pygame, glyph, colour, radius * 2 + 12, cache, trim=False
        )
    return cache[key]


def cellular_visual(theme, state_id, colour, opacity):
    if visual := getattr(scene_renderer(theme), "cellular_visual", None):
        return visual(state_id, colour, opacity)
    return colour, opacity


def person_entity(entity):
    name = entity.name.replace("-", "_").lower()
    return entity.shape == "circle" and (
        any(token in name.split("_") for token in PERSON_TOKENS)
        or name.startswith(("active_", "next_"))
    )


def entity_plane(entity):
    if entity.plane != "auto":
        return PLANE_ORDER[entity.plane]
    if entity.occluder:
        return PLANE_ORDER["fore"]
    return PLANE_ORDER["ground"]


def figure_pose(entity):
    if entity.pose:
        return entity.pose
    if entity.motion == "ballistic":
        return "throw"
    if abs(entity.velocity_x) > 0.02 or abs(entity.velocity_y) > 0.02:
        return "run"
    return "stand"


def draw_angular_person(pygame, screen, entity, centre, scale):
    """Draw the generic low-detail person shape with pose and material cues"""
    side = max(14, min(120, round(scale * entity.size * entity.scale * 1.5)))
    x, y = centre
    ink = tuple(max(0, value - 62) for value in entity.colour)
    fill = entity.colour
    alpha = round(entity.opacity * 255)
    layer = pygame.Surface((side * 2, side * 3), pygame.SRCALPHA)
    cx, base = side, side * 2
    head = [
        (cx, side // 5),
        (cx + side // 3, side // 2),
        (cx + side // 5, side * 4 // 5),
        (cx - side // 5, side * 4 // 5),
        (cx - side // 3, side // 2),
    ]
    body = [
        (cx - side // 4, side),
        (cx + side // 4, side),
        (cx + side // 3, side * 3 // 2),
        (cx - side // 3, side * 3 // 2),
    ]
    pygame.draw.polygon(layer, (*fill, alpha), head)
    pygame.draw.polygon(layer, (*ink, alpha), head, max(1, side // 12))
    pygame.draw.polygon(layer, (*fill, alpha), body)
    pygame.draw.polygon(layer, (*ink, alpha), body, max(1, side // 12))
    pose = figure_pose(entity)
    stride = side // 3 if pose in {"run", "throw"} else side // 7
    if pose == "fallen":
        stride = side // 2
    pygame.draw.line(
        layer,
        (*ink, alpha),
        (cx - side // 5, side * 3 // 2),
        (cx - stride, base),
        max(2, side // 9),
    )
    pygame.draw.line(
        layer,
        (*ink, alpha),
        (cx + side // 5, side * 3 // 2),
        (cx + stride, base),
        max(2, side // 9),
    )
    arm = -1 if entity.facing_left or entity.velocity_x < -0.02 else 1
    arm_y = side * 4 // 5 if pose == "throw" else side * 7 // 5
    pygame.draw.line(
        layer,
        (*ink, alpha),
        (cx, side * 6 // 5),
        (cx + arm * side * 3 // 5, arm_y),
        max(2, side // 10),
    )
    if entity.material in {"hi-vis", "safety", "vest"}:
        pygame.draw.line(
            layer,
            (246, 196, 69, alpha),
            (cx - side // 4, side * 5 // 4),
            (cx + side // 4, side * 5 // 4),
            max(2, side // 10),
        )
    if pose == "fallen":
        layer = pygame.transform.rotate(layer, 70 * arm)
    screen.blit(layer, layer.get_rect(midbottom=(x, y + side // 4)))


def draw_material_object(pygame, screen, entity, centre, scale):
    """Draw a shaded object when no sprite or icon is supplied"""
    side = max(10, min(120, round(scale * entity.size * entity.scale * 0.7)))
    x, y = centre
    alpha = round(entity.opacity * 255)
    fill = entity.colour
    if entity.material in {"wood", "timber"}:
        fill = (126, 82, 47)
    elif entity.material in {"metal", "steel"}:
        fill = (119, 143, 157)
    elif entity.material in {"stone", "rock"}:
        fill = (103, 105, 99)
    ink = tuple(max(0, value - 58) for value in fill)
    layer = pygame.Surface((side * 2 + 8, side * 2 + 8), pygame.SRCALPHA)
    local = side + 4, side + 4
    points = [
        (local[0], 3),
        (side * 2 + 5, side // 2),
        (side * 2 + 2, side * 3 // 2),
        (local[0], side * 2 + 5),
        (3, side * 3 // 2),
        (3, side // 2),
    ]
    pygame.draw.polygon(layer, (*fill, alpha), points)
    pygame.draw.polygon(layer, (*ink, alpha), points, max(1, side // 10))
    pygame.draw.line(
        layer,
        (*tuple(min(255, value + 45) for value in fill), alpha),
        points[0],
        points[2],
        max(1, side // 12),
    )
    screen.blit(layer, layer.get_rect(center=(x, y)))


def draw_entity(
    pygame, screen, fonts, frame, camera, entity, cache, options, position
):
    """Draw one entity through scenario hooks or the generic shape renderers

    Shared code handles projection, depth cues, labels, and velocity vectors
    Scenario hooks may replace shapes without changing layer ordering
    """
    # Scenario renderers may replace one primitive without changing layers
    renderer = scene_renderer(frame)
    if (hide := getattr(renderer, "hide_entity", None)) and hide(entity):
        return
    scale, x, y = transform(
        frame, camera, entity.x, entity.y, screen.get_size(), entity.z
    )
    if frame.view == "plane" and entity.plane != "auto":
        scale *= PLANE_SCALES[entity.plane]
    if (draw := getattr(renderer, "draw_entity", None)) and draw(
        pygame,
        screen,
        fonts,
        frame,
        camera,
        entity,
        cache,
        options,
        position,
        scale,
        x,
        y,
    ):
        return
    if frame.view == "grid":
        x += scale / 2.0
        y += scale / 2.0
    centre = round(x), round(y)
    # Plane entities receive a ground shadow before their visible shape
    if frame.view == "plane" and entity.motion != "water":
        foot_y = entity.y if entity.foot_y < 0.0 else entity.foot_y
        ground_scale, ground_x, ground_y = transform(
            frame, camera, entity.x, foot_y, screen.get_size()
        )
        radius = max(5, round(ground_scale * entity.size * 0.55))
        alpha = round(70 / (1.0 + entity.z * 0.3))
        shadow_key = "shadow", radius, alpha
        if shadow_key not in cache:
            shadow = pygame.Surface(
                (radius * 2, max(4, radius // 2)), pygame.SRCALPHA
            )
            pygame.draw.ellipse(shadow, (15, 18, 17, alpha), shadow.get_rect())
            cache[shadow_key] = shadow
        shadow = cache[shadow_key]
        screen.blit(
            shadow,
            shadow.get_rect(center=(round(ground_x), round(ground_y))),
        )
    # Generic primitives cover scenarios without a renderer-specific hook
    if person_entity(entity):
        draw_angular_person(pygame, screen, entity, centre, scale)
    elif entity.shape == "sprite":
        path_key = "sprite-path", entity.sprite
        path = cache.get(path_key)
        if path is None:
            path = safe_file(ROOT, entity.sprite, "sprite", entity.entity_id)
            cache[path_key] = path
        side = max(1, round(scale * entity.size * 2.0))
        circular = False
        if sprite_path := getattr(renderer, "sprite_path", None):
            path, circular = sprite_path(frame, entity, path, position)
        if circular:
            ratio = getattr(renderer, "circular_clip_ratio", lambda path: 0.49)(
                path
            )
            sprite = circular_entity_sprite(pygame, path, side, cache, ratio)
        else:
            sprite = entity_sprite(pygame, path, side, cache)
        if entity.facing_left or (
            entity.motion != "flight" and entity.velocity_x < -0.05
        ):
            sprite = face_left(pygame, sprite, cache)
        apply_surface(
            pygame,
            screen,
            sprite,
            centre,
            entity.rotation,
            entity.scale,
            entity.opacity,
        )
        if after_sprite := getattr(renderer, "after_sprite", None):
            after_sprite(pygame, screen, fonts, frame, entity, centre, scale)
    elif entity.shape == "cell":
        if draw_cell := getattr(renderer, "draw_cell", None):
            draw_cell(pygame, screen, fonts, frame, entity, x, y, centre, scale)
        else:
            colour, opacity = entity.colour, entity.opacity
            inset = max(1, round(scale * 0.08))
            rectangle = pygame.Rect(
                round(x - scale / 2.0) + inset,
                round(y - scale / 2.0) + inset,
                max(1, round(scale) - 2 * inset),
                max(1, round(scale) - 2 * inset),
            )
            if opacity == 1.0:
                pygame.draw.rect(
                    screen, colour, rectangle, border_radius=max(1, inset)
                )
            else:
                layer = pygame.Surface(rectangle.size, pygame.SRCALPHA)
                layer.fill((*colour, round(opacity * 255.0)))
                screen.blit(layer, rectangle.topleft)
    elif entity.shape == "icon":
        minimum = 14 if len(frame.entities) <= 32 else 7
        radius = max(
            minimum,
            min(70, round(scale * entity.scale * entity.size)),
        )
        if icon_radius := getattr(renderer, "icon_radius", None):
            radius = icon_radius(entity, frame, scale, minimum, radius)
        if underlay := getattr(renderer, "draw_icon_underlay", None):
            underlay(pygame, screen, entity, centre, radius)
        icon = icon_surface(
            pygame,
            entity.glyph,
            entity.colour,
            radius,
            cache,
        )
        rotation = entity.rotation
        fixed_icon = getattr(renderer, "fixed_icon", None)
        if (
            entity.facing_left
            or (entity.motion != "flight" and entity.velocity_x < -0.05)
        ) and not (fixed_icon and fixed_icon(entity.glyph)):
            icon = face_left(pygame, icon, cache)
        apply_surface(
            pygame, screen, icon, centre, rotation, 1.0, entity.opacity
        )
    elif entity.shape == "text":
        text_colour = entity.colour
        text_size = max(24, min(160, round(scale * 0.65 * entity.size)))
        draw_text(
            pygame,
            screen,
            fonts,
            entity.glyph,
            centre,
            text_size,
            text_colour,
            entity.opacity,
            True,
        )
    elif (
        entity.shape == "circle"
        and (draw_circle := getattr(renderer, "draw_circle", None))
        and draw_circle(pygame, screen, entity, centre, scale)
    ):
        pass
    else:
        draw_material_object(pygame, screen, entity, centre, scale)
    # Optional diagnostics are drawn last so they stay readable over primitives
    if options.labels and (entity.label or entity.name):
        draw_text(
            pygame,
            screen,
            fonts,
            entity.label or entity.name,
            (centre[0], centre[1] - max(16, round(scale * 0.38))),
            max(18, min(34, round(scale * 0.2))),
            (248, 250, 252),
            entity.opacity,
            True,
        )
    if (
        options.vectors
        and frame.view == "plane"
        and (entity.velocity_x or entity.velocity_y)
    ):
        pygame.draw.line(
            screen,
            (255, 255, 255),
            centre,
            (
                round(x + entity.velocity_x * scale),
                round(y + entity.velocity_y * scale),
            ),
            2,
        )


def draw_trails(
    pygame, screen, frames, numbers, position, frame, camera, options
):
    """Draw bounded interpolated motion trails behind visible plane entities"""
    # Sample a bounded number of interpolated positions to cap preview cost
    if draw := getattr(scene_renderer(frame), "draw_trails", None):
        draw(pygame, screen, frames, numbers, position, frame, camera, options)
        return
    if (
        not options.trails
        or frame.view != "plane"
        or position <= frames[0].number
    ):
        return
    start = max(frames[0].number, position - options.trail_length)
    sample_count = min(
        32,
        max(8, math.ceil(options.trail_length / 4.0) + 1),
    )
    samples = []
    for index in range(sample_count):
        sample = lerp(start, position, index / (sample_count - 1))
        end_index = min(len(frames) - 1, bisect.bisect_right(numbers, sample))
        if end_index == 0:
            samples.append((frames[0], frames[0], 0.0))
            continue
        before, after = frames[end_index - 1], frames[end_index]
        samples.append(
            (
                before,
                after,
                (sample - before.number)
                / max(1.0, after.number - before.number),
            )
        )
    layer = pygame.Surface(screen.get_size(), pygame.SRCALPHA)
    focus = frame_index(frame).get(options.focus_entity)
    entities = [focus] if focus is not None else frame.entities[:300]
    for entity in entities:
        track = [
            trail_point(sample[0], sample[1], sample[2], entity.entity_id)
            for sample in samples
        ]
        for index, (previous, current) in enumerate(
            itertools.pairwise(track), start=1
        ):
            if previous is None or current is None:
                continue
            x0, y0, z0 = previous
            x1, y1, z1 = current
            if (
                abs(x1 - x0) > frame.width / 2.0
                or abs(y1 - y0) > frame.height / 2.0
            ):
                continue
            _, x0, y0 = transform(frame, camera, x0, y0, screen.get_size(), z0)
            _, x1, y1 = transform(frame, camera, x1, y1, screen.get_size(), z1)
            progress = index / max(1, sample_count - 1)
            pygame.draw.line(
                layer,
                (*entity.colour, round(18 + 150 * progress)),
                (round(x0), round(y0)),
                (round(x1), round(y1)),
                3,
            )
    screen.blit(layer, (0, 0))


def draw_focus_ring(pygame, screen, frame, camera, options):
    if options.focus_entity is None or options.focus_radius <= 0.0:
        return
    entity = frame_index(frame).get(options.focus_entity)
    if entity is None:
        return
    scale, x, y = transform(
        frame, camera, entity.x, entity.y, screen.get_size(), entity.z
    )
    radius = max(2, round(options.focus_radius * scale))
    layer = pygame.Surface(screen.get_size(), pygame.SRCALPHA)
    pygame.draw.circle(layer, (241, 187, 76, 20), (round(x), round(y)), radius)
    pygame.draw.circle(
        layer, (241, 187, 76, 180), (round(x), round(y)), radius, 2
    )
    screen.blit(layer, (0, 0))


def caption_lines(font, content, max_width):
    lines = []
    line = []
    for word in content.split():
        candidate = " ".join((*line, word))
        if line and font.size(candidate)[0] > max_width:
            lines.append(" ".join(line))
            line = [word]
        else:
            line.append(word)
    if line:
        lines.append(" ".join(line))
    return lines


def draw_caption(pygame, screen, fonts, cue, frame, camera, position):
    """Lay out one active caption or dialogue cue in screen or world space"""
    # Fade the cue at both ends before measuring and laying out its text box
    duration = max(1.0, cue.duration)
    fade = (
        min(position - cue.frame, cue.frame + duration - position, 0.25) / 0.25
    )
    alpha = cue.opacity * min(1.0, max(0.0, fade))
    dialogue = cue.kind == "dialogue"
    content = cue.text
    if cue.speaker and cue.speaker.lower() not in {"commentary", "narrator"}:
        name = cue.speaker.replace("_", " ").title()
        content = f"{name} — {content}"
    size = max(28, min(76, round(44 * cue.scale)))
    font = font_for(pygame, fonts, size, True)
    scale, x, projected_y = transform(
        frame, camera, cue.x, cue.y, screen.get_size()
    )
    max_width = min(900, screen.get_width() - 128)
    renderer = scene_renderer(frame)
    if caption_width := getattr(renderer, "caption_width", None):
        max_width = caption_width(max_width)
    if dialogue and cue.width > 0.0:
        max_width = round(cue.width * scale)
    lines = caption_lines(font, content, max_width)
    text_colour = (38, 36, 31) if dialogue else (250, 247, 237)
    rendered = [font.render(line, True, text_colour) for line in lines]
    content_width = max(line.get_width() for line in rendered)
    content_height = sum(line.get_height() for line in rendered)
    box_width = content_width + 42
    box_height = content_height + 24
    if dialogue:
        box_width = max(box_width, round(cue.width * scale))
        box_height = max(box_height, round(cue.height * scale))
    lower = pygame.Surface((box_width, box_height), pygame.SRCALPHA)
    if dialogue:
        pygame.draw.rect(
            lower,
            (250, 246, 232, round(242 * alpha)),
            lower.get_rect(),
            border_radius=18,
        )
        pygame.draw.rect(
            lower,
            (52, 47, 39, round(220 * alpha)),
            lower.get_rect(),
            2,
            border_radius=18,
        )
    else:
        pygame.draw.rect(
            lower,
            (12, 18, 28, round(218 * alpha)),
            lower.get_rect(),
            border_radius=10,
        )
        pygame.draw.rect(
            lower,
            (248, 192, 84, round(255 * alpha)),
            (0, 0, 5, lower.get_height()),
            border_radius=3,
        )
    line_y = (box_height - content_height) // 2
    for line in rendered:
        line.set_alpha(round(255 * alpha))
        lower.blit(line, (22, line_y))
        line_y += line.get_height()
    if cue.rotation:
        lower = pygame.transform.rotozoom(lower, -cue.rotation, 1.0)
    # Dialogue follows a named actor when possible or uses a safe fallback
    actor = next(
        (entity for entity in frame.entities if entity.name == cue.speaker),
        None,
    )
    if dialogue and actor is not None:
        actor_scale, actor_x, actor_y = transform(
            frame,
            camera,
            actor.x,
            actor.y,
            screen.get_size(),
            actor.z,
        )
        radius = max(18, round(actor_scale * actor.size * 0.8))
        destination = lower.get_rect(
            midbottom=(round(actor_x), round(actor_y - radius - 18))
        )
        if destination.top < 24:
            destination = lower.get_rect(
                midtop=(round(actor_x), round(actor_y + radius + 18))
            )
        inset = round(
            min(screen.get_size())
            * float(frame.presentation.get("safe_margin", "0.04"))
        )
        destination.clamp_ip(screen.get_rect().inflate(-2 * inset, -2 * inset))
        tail_y = (
            destination.bottom
            if destination.centery < actor_y
            else destination.top
        )
        pygame.draw.polygon(
            screen,
            (250, 246, 232, round(242 * alpha)),
            [
                (destination.centerx - 14, tail_y),
                (destination.centerx + 14, tail_y),
                (round(actor_x), round(actor_y)),
            ],
        )
    elif dialogue and cue.x == 0.0 and cue.y == 0.0:
        inset = round(
            min(screen.get_size())
            * float(frame.presentation.get("safe_margin", "0.04"))
        )
        destination = lower.get_rect(
            midleft=(inset, screen.get_height() - 2 * inset)
        )
    elif not dialogue:
        inset = round(
            min(screen.get_size())
            * float(frame.presentation.get("safe_margin", "0.04"))
        )
        if caption_destination := getattr(
            renderer, "caption_destination", None
        ):
            destination = caption_destination(lower, screen, inset)
        elif cue.y >= frame.height / 2.0:
            destination = lower.get_rect(
                midbottom=(screen.get_width() // 2, screen.get_height() - inset)
            )
        else:
            destination = lower.get_rect(
                midtop=(screen.get_width() // 2, inset)
            )
    else:
        destination = lower.get_rect(center=(round(x), round(projected_y)))
    screen.blit(lower, destination)


def draw_title_card(
    pygame, screen, fonts, frame, position, first, last, fallback
):
    """Draw the opening title treatment during its configured interval"""
    title = frame.presentation.get("title") or fallback
    subtitle = frame.presentation.get("subtitle", "")
    if not title:
        return
    duration = presentation_duration(frame)
    elapsed = (position - first) * duration / max(1.0, last - first)
    hold = 3.0 if frame.presentation.get("format") == "film" else 1.8
    if elapsed >= hold + 0.7:
        return
    alpha = 1.0 - smoothstep((elapsed - hold) / 0.7)
    width = min(round(screen.get_width() * 0.62), 1080)
    height = 150 if subtitle else 106
    panel = pygame.Surface((width, height), pygame.SRCALPHA)
    panel.fill((15, 18, 21, round(205 * alpha)))
    pygame.draw.rect(
        panel,
        (235, 180, 73, round(255 * alpha)),
        (0, 0, 7, height),
    )
    draw_text(
        pygame,
        panel,
        fonts,
        title,
        (30, 28),
        48,
        (250, 247, 237),
        alpha,
        True,
        "topleft",
    )
    if subtitle:
        draw_text(
            pygame,
            panel,
            fonts,
            subtitle,
            (31, 88),
            28,
            (214, 218, 220),
            alpha,
            False,
            "topleft",
        )
    screen.blit(panel, (64, screen.get_height() - height - 58))


def poster_cue(cues):
    return next((cue for cue in cues if cue.kind == "poster"), None)


def draw_poster(pygame, screen, fonts, frame, cue, title, cache, metadata):
    """Draw the still poster used before an exported presentation starts"""
    # Build the paper base and optional scene image before placing poster copy
    width, height = screen.get_size()
    ink, red, gold = (41, 38, 31), (156, 53, 43), (211, 164, 77)
    if PAPER_TEXTURE.is_file():
        paper = image_surface(pygame, PAPER_TEXTURE, cache)
        paper = pygame.transform.smoothscale(paper, (width, height))
        paper = paper.copy()
        paper.set_alpha(112)
        screen.blit(paper, (0, 0))
    panel = pygame.Rect(54, 48, width - 108, height - 96)
    wash = pygame.Surface(panel.size, pygame.SRCALPHA)
    wash.fill((250, 245, 226, 58))
    screen.blit(wash, panel.topleft)
    if (
        cue is not None
        and cue.asset is not None
        and cue.asset.resolve() != PAPER_TEXTURE.resolve()
    ):
        source = image_surface(pygame, cue.asset, cache)
        ratio = max(
            panel.width / source.get_width(), panel.height / source.get_height()
        )
        size = (
            round(source.get_width() * ratio),
            round(source.get_height() * ratio),
        )
        image = pygame.transform.smoothscale(source, size)
        crop = pygame.Rect(0, 0, panel.width, panel.height)
        crop.center = image.get_rect().center
        screen.blit(image, panel.topleft, crop)
        plate_wash = pygame.Surface(panel.size, pygame.SRCALPHA)
        plate_wash.fill((250, 245, 226, 32))
        screen.blit(plate_wash, panel.topleft)
    pygame.draw.line(
        screen,
        ink,
        (panel.left, panel.top),
        (panel.right - 26, panel.top + 9),
        8,
    )
    pygame.draw.line(
        screen,
        ink,
        (panel.left + 18, panel.bottom - 7),
        (panel.right, panel.bottom),
        6,
    )
    # Poster metadata is checked by load_scene_meta before this draw path
    text = (
        cue.text
        if cue is not None and cue.text
        else (frame.presentation.get("title") or title)
    )
    joke, meme = metadata["subtitle"], metadata["meme"]
    heading_size = 74 if len(text) > 30 else 88
    marker = font_for(pygame, fonts, heading_size, False, HAND_FONT)
    heading = marker.render(text, True, ink)
    heading = pygame.transform.rotate(heading, 1.2)
    screen.blit(heading, heading.get_rect(topleft=(130, 126)))
    handwritten = font_for(pygame, fonts, 38, False, HAND_FONT)
    note = handwritten.render(joke, True, red)
    note = pygame.transform.rotate(note, -1.1)
    screen.blit(note, note.get_rect(topleft=(154, 268)))
    pygame.draw.lines(
        screen,
        gold,
        False,
        ((132, 350), (510, 346), (926, 353), (1340, 344), (width - 178, 349)),
        4,
    )
    sticker_font = font_for(pygame, fonts, 28, True, HAND_FONT)
    sticker = sticker_font.render(meme, True, ink)
    sticker_pad = 14
    sticker_box = pygame.Surface(
        (
            sticker.get_width() + sticker_pad * 2,
            sticker.get_height() + sticker_pad,
        ),
        pygame.SRCALPHA,
    )
    sticker_box.fill((250, 245, 226, 205))
    sticker_box.blit(sticker, (sticker_pad, sticker_pad // 2))
    angle = (-2.5, 1.8, -1.2)[sum(map(ord, text)) % 3]
    sticker_box = pygame.transform.rotate(sticker_box, angle)
    corners = (
        sticker_box.get_rect(topright=(panel.right - 48, panel.top + 48)),
        sticker_box.get_rect(bottomleft=(panel.left + 48, panel.bottom - 42)),
        sticker_box.get_rect(bottomright=(panel.right - 48, panel.bottom - 42)),
    )
    screen.blit(sticker_box, corners[sum(map(ord, text)) % len(corners)])


def frosted_panel(pygame, screen, rectangle):
    source = screen.subsurface(rectangle).copy()
    small = pygame.transform.smoothscale(
        source,
        (max(1, rectangle.width // 12), max(1, rectangle.height // 12)),
    )
    panel = pygame.transform.smoothscale(small, rectangle.size)
    glaze = pygame.Surface(rectangle.size, pygame.SRCALPHA)
    glaze.fill((12, 18, 25, 154))
    panel.blit(glaze, (0, 0))
    pygame.draw.line(
        panel, (255, 255, 255, 38), (10, 7), (rectangle.width - 10, 7), 2
    )
    mask = pygame.Surface(rectangle.size, pygame.SRCALPHA)
    pygame.draw.rect(
        mask, (255, 255, 255, 255), mask.get_rect(), border_radius=10
    )
    panel.blit(mask, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
    return panel


def draw_turn_status(pygame, screen, fonts, frames, position, numbers, cache):
    frame = sample_frame(frames, position, numbers)
    if draw := getattr(scene_renderer(frame), "draw_turn_status", None):
        draw(pygame, screen, fonts, frames, position, numbers, cache)


def draw_asset_cue(pygame, screen, frame, camera, cue, cache):
    """Load and draw one image cue at its projected position and size"""
    renderer = scene_renderer(frame)
    if (skip := getattr(renderer, "skip_asset_cue", None)) and skip(cue):
        return
    visual_camera = cue_camera(frame, camera, cue)
    scale, x, y = transform(
        frame, visual_camera, cue.x, cue.y, screen.get_size()
    )
    surface = size_surface(
        pygame,
        image_surface(pygame, cue.asset, cache),
        cue.width,
        cue.height,
        scale,
        cache,
    )
    centre = round(x), round(y)
    if before := getattr(renderer, "before_asset_cue", None):
        before(pygame, screen, cue, surface, centre)
    apply_surface(
        pygame,
        screen,
        surface,
        centre,
        cue.rotation,
        cue.scale,
        cue.opacity,
    )
    if after := getattr(renderer, "after_asset_cue", None):
        after(pygame, screen, cue, surface, centre)


def draw_scene_chrome(pygame, screen, fonts, frame, camera, position, theme):
    if draw := getattr(scene_renderer(frame), "draw_scene_chrome", None):
        draw(pygame, screen, fonts, frame, camera, position, theme)


def draw_ambient(pygame, screen, frame, camera, cue, position, theme):
    """Draw one ambient effect and report whether it handled a cue"""
    # Return False for ordinary effects so the caller can draw its default ring
    if not cue.ambient:
        return False
    scale, x, y = transform(
        frame, cue_camera(frame, camera, cue), cue.x, cue.y, screen.get_size()
    )
    scale *= cue.scale
    progress = min(
        1.0, max(0.0, (position - cue.frame) / max(1.0, cue.duration))
    )
    alpha = round(255 * cue.opacity * (1.0 - 0.45 * progress))
    layer = pygame.Surface(screen.get_size(), pygame.SRCALPHA)
    seed_text = frame.presentation.get("render_seed", "")
    ambient_seed = int(seed_text) if seed_text.isdigit() else 0
    ambient_seed ^= sum(
        (index + 1) * ord(character) for index, character in enumerate(cue.text)
    )
    # Each branch produces a deterministic layer from cue and frame data
    if cue.ambient == "steam":
        softness = 3
        mist = pygame.Surface(
            (
                math.ceil(screen.get_width() / softness),
                math.ceil(screen.get_height() / softness),
            ),
            pygame.SRCALPHA,
        )
        rng = random.Random(ambient_seed ^ 0x5EA7)
        phase = (position - cue.frame) * 0.12
        profiles = tuple(
            (
                rng.uniform(-0.34, 0.34),
                rng.uniform(0.62, 1.42),
                rng.random(),
                rng.uniform(0.54, 1.46),
                rng.uniform(5.4, 13.8),
                rng.uniform(-math.pi, math.pi),
            )
            for _ in range(9)
        )
        for index, (offset, speed, start, frequency, curl, angle) in enumerate(
            profiles
        ):
            life = (start + phase * speed * 0.115) % 1.0
            envelope = math.sin(math.pi * life) ** 1.65
            points = []
            for segment in range(17):
                rise = segment / 16.0
                turbulence = (
                    math.sin(phase * frequency + rise * curl + angle)
                    + 0.53
                    * math.sin(
                        phase * (frequency * 1.71)
                        - rise * (curl * 1.37)
                        + angle * 0.43
                    )
                    + 0.24
                    * math.sin(
                        phase * (frequency * 2.63)
                        + rise * (curl * 2.11)
                        - index
                    )
                )
                sideways = (
                    offset * (1.0 - rise * 0.28)
                    + turbulence * (0.018 + rise**1.45 * 0.105)
                    + (life - 0.5) * rise * 0.07
                )
                lift = (
                    0.03
                    + rise * (0.38 + life * 0.74)
                    + 0.025 * math.sin(phase * speed + rise * 8.0 + angle)
                )
                points.append(
                    (
                        round((x + scale * sideways) / softness),
                        round((y - scale * lift) / softness),
                    )
                )
            for segment, (begin, end) in enumerate(itertools.pairwise(points)):
                rise = segment / 16.0
                wisp_alpha = round(
                    cue.opacity * 255 * envelope * (1.0 - rise**1.4 * 0.80)
                )
                width = max(
                    1,
                    round(
                        scale
                        * (0.040 - rise * 0.023)
                        * (0.74 + life * 0.42)
                        / softness
                    ),
                )
                pygame.draw.line(
                    mist,
                    (247, 249, 247, wisp_alpha // 3),
                    begin,
                    end,
                    width + 2,
                )
                if segment % 2 == index % 2:
                    puff = max(1, round(width * (1.7 + rise)))
                    pygame.draw.circle(
                        mist,
                        (248, 250, 249, wisp_alpha // 5),
                        end,
                        puff,
                    )
        layer.blit(
            pygame.transform.smoothscale(mist, screen.get_size()), (0, 0)
        )
    elif cue.ambient == "bubbles":
        from pygame import gfxdraw

        rng = random.Random(ambient_seed ^ 0xBABB1E)
        phase = (position - cue.frame) * 0.15
        currents = tuple(
            (
                rng.uniform(0.07, 0.18),
                rng.uniform(0.11, 0.30),
                rng.uniform(-0.22, 0.22),
                rng.uniform(0.72, 1.58),
                rng.uniform(0.7, 2.4),
                rng.uniform(-math.pi, math.pi),
            )
            for _ in range(6)
        )
        for index, (radius, span, speed, arc, power, offset) in enumerate(
            currents
        ):
            points = []
            for segment in range(16):
                amount = segment / 15.0
                angle = (
                    offset
                    + phase * speed
                    + amount * arc
                    + 0.42
                    * math.sin(
                        phase * (0.31 + index * 0.07) + amount * (4.7 + index)
                    )
                )
                orbit = (
                    radius
                    + span * amount**power
                    + 0.026
                    * math.sin(phase * (0.77 + index * 0.09) + segment * 1.37)
                )
                points.append(
                    (
                        round(x + math.cos(angle) * scale * orbit),
                        round(
                            y
                            + math.sin(angle) * scale * orbit * 0.58
                            + scale
                            * 0.018
                            * math.sin(phase * 0.63 + segment + index)
                        ),
                    )
                )
            pygame.draw.aalines(
                layer,
                (245, 205, 135, round(cue.opacity * (92 - index * 8))),
                False,
                points,
            )

        bubbles = tuple(
            (
                rng.uniform(-0.34, 0.34),
                rng.uniform(-0.23, 0.23),
                rng.uniform(0.42, 1.12),
                rng.uniform(0.042, 0.13),
                rng.random(),
                rng.uniform(-1.8, 1.8),
                rng.uniform(-math.pi, math.pi),
            )
            for _ in range(18)
        )
        for index, (
            offset_x,
            offset_y,
            size,
            speed,
            start,
            spin,
            angle,
        ) in enumerate(bubbles):
            life = (start + phase * speed) % 1.0
            orbit = 0.025 + 0.055 * math.sin(math.pi * life) ** 2
            theta = (
                angle
                + spin * life
                + 0.48 * math.sin(phase * (0.23 + index * 0.017) + index)
            )
            centre = (
                round(
                    x
                    + scale
                    * (
                        offset_x
                        + math.cos(theta) * orbit
                        + 0.018 * math.sin(phase * 0.71 + index * 2.1)
                    )
                ),
                round(
                    y
                    + scale
                    * (
                        offset_y
                        + math.sin(theta) * orbit * 0.62
                        + 0.014 * math.cos(phase * 0.53 + index * 1.7)
                    )
                ),
            )
            emerge = min(1.0, life / 0.12)
            fade = min(1.0, (1.0 - life) / 0.16)
            bubble = max(
                1,
                round(scale * size * (0.012 + 0.035 * life**0.72) * emerge),
            )
            bubble_alpha = round(238 * cue.opacity * emerge * fade)
            gfxdraw.filled_circle(
                layer,
                *centre,
                bubble,
                (255, 226, 164, bubble_alpha // 6),
            )
            gfxdraw.aacircle(
                layer,
                *centre,
                bubble,
                (255, 235, 187, bubble_alpha),
            )
            if index % 5 == 0 and 0.34 < life < 0.70:
                satellite = max(1, round(bubble * (0.52 + life * 0.22)))
                satellite_centre = (
                    centre[0] + round(bubble * math.cos(theta) * 0.72),
                    centre[1] + round(bubble * math.sin(theta) * 0.44),
                )
                gfxdraw.aacircle(
                    layer,
                    *satellite_centre,
                    satellite,
                    (255, 232, 180, bubble_alpha * 3 // 4),
                )
            if life > 0.84:
                pop = (life - 0.84) / 0.16
                pop_alpha = round(190 * cue.opacity * (1.0 - pop))
                ring = bubble + max(1, round(scale * 0.035 * pop))
                gfxdraw.aacircle(
                    layer,
                    *centre,
                    ring,
                    (255, 238, 192, pop_alpha),
                )
                for splash in range(3):
                    splash_angle = angle + splash * 2.1 + phase * 0.17
                    splash_centre = (
                        centre[0] + round(math.cos(splash_angle) * ring * 1.3),
                        centre[1] + round(math.sin(splash_angle) * ring * 0.75),
                    )
                    gfxdraw.filled_circle(
                        layer,
                        *splash_centre,
                        max(1, bubble // 5),
                        (255, 229, 177, pop_alpha),
                    )
    else:  # embers
        for offset in (-0.35, 0, 0.35):
            pygame.draw.line(
                layer,
                (*theme.accent, alpha),
                (round(x + offset * scale), round(y)),
                (
                    round(x + offset * scale + scale * 0.08),
                    round(y - scale * 0.25),
                ),
                2,
            )
    screen.blit(layer, (0, 0))
    return True


def render(
    pygame, screen, fonts, frames, position, cues, cache, options, title
):
    """Draw one presentation position through the complete generic pipeline

    The pipeline selects a theme and camera, then draws world layers and HUD
    A scenario renderer may take over after the generic frame is selected
    """
    # Rendering layers background, negative cues, world, overlays, then HUD
    if (numbers := cache.get("frame_numbers")) is None:
        cache["frame_numbers"] = numbers = tuple(
            frame.number for frame in frames
        )
    frame = sample_frame(frames, position, numbers)
    theme = theme_for(frame)
    if (scenes := cache.get("scene_cues")) is None:
        cache["scene_cues"] = scenes = tuple(
            sorted(
                (cue for cue in cues if cue.kind == "scene"),
                key=lambda cue: cue.frame,
            )
        )
    if (scene_frames := cache.get("scene_frames")) is None:
        cache["scene_frames"] = scene_frames = tuple(
            cue.frame for cue in scenes
        )
    scene_index = bisect.bisect_right(scene_frames, position)
    if scene_index:
        name = scenes[scene_index - 1].text
        if name not in THEMES:
            raise ValueError(f"unknown scene theme: {name}")
        theme = THEMES[name]
    if draw_scene := getattr(scene_renderer(frame), "draw_scene", None):
        next_index = min(
            len(frames) - 1,
            bisect.bisect_left(numbers, math.floor(position) + 1),
        )
        draw_scene(
            pygame,
            screen,
            fonts,
            frame,
            position,
            cache,
            frames[next_index],
        )
        return
    # Cache sorted cues because render runs many times per preview or export
    if (explicit_cameras := cache.get("explicit_cameras")) is None:
        cache["explicit_cameras"] = explicit_cameras = tuple(
            sorted(
                (cue for cue in cues if cue.kind == "camera"),
                key=lambda cue: cue.frame,
            )
        )
    if (camera_frames := cache.get("camera_frames")) is None:
        cache["camera_frames"] = camera_frames = tuple(
            cue.frame for cue in explicit_cameras
        )
    camera = camera_for(
        frame,
        explicit_cameras,
        position,
        options,
        screen.get_size(),
        camera_frames,
    )
    background = background_surface(pygame, cache, theme, screen.get_size())
    draw_backdrop(pygame, screen, background, frame, camera, cache)
    current_cues = active_cues(cues, position)
    underlays = tuple(
        cue for cue in current_cues if cue.kind == "backdrop" and cue.layer < 0
    )
    for cue in underlays:
        draw_asset_cue(pygame, screen, frame, camera, cue, cache)
    draw_world_ground(pygame, screen, frame, camera, theme)
    draw_focus_ring(pygame, screen, frame, camera, options)
    scale, offset_x, offset_y = transform(
        frame, camera, 0.0, 0.0, screen.get_size()
    )
    if frame.view == "grid":
        draw_grid(
            pygame,
            screen,
            fonts,
            frame,
            scale,
            offset_x,
            offset_y,
            theme,
            cache,
        )
    # Merge entities and cues before the depth sort so they share one order
    layers = [(entity.layer, entity) for entity in frame.entities]
    layers.extend(
        (cue.layer, cue)
        for cue in current_cues
        if cue.kind in {"sprite", "effect"}
        or (cue.kind == "backdrop" and cue not in underlays)
    )
    projection = frame.presentation.get("projection") or "flat"

    def order(pair):
        layer, item = pair
        if not isinstance(item, Entity) or frame.view == "grid":
            return layer, 0.0, 0
        depth = item.x + item.y if projection == "isometric" else item.y
        foot_y = item.y if item.foot_y < 0.0 else item.foot_y
        # Plane and ground contact keep foreground figures above distant ones
        return layer, entity_plane(item), depth, foot_y, item.entity_id

    trail_drawn = False
    for layer, item in sorted(layers, key=order):
        if layer >= 0 and not trail_drawn:
            draw_trails(
                pygame,
                screen,
                frames,
                numbers,
                position,
                frame,
                camera,
                options,
            )
            trail_drawn = True
        if isinstance(item, Entity):
            if frame.view != "grid" and not visible_at(
                frame, camera, item.x, item.y, screen, max(screen.get_size())
            ):
                continue
            draw_entity(
                pygame,
                screen,
                fonts,
                frame,
                camera,
                item,
                cache,
                options,
                position,
            )
            continue
        cue = item
        visual_camera = cue_camera(frame, camera, cue)
        if cue.kind in {"backdrop", "sprite"}:
            margin = (
                max(screen.get_size())
                + math.hypot(cue.width, cue.height)
                * min(screen.get_size())
                / min(frame.width, frame.height)
                * cue.scale
            )
            if cue.kind == "sprite" and not visible_at(
                frame, visual_camera, cue.x, cue.y, screen, margin
            ):
                continue
            draw_asset_cue(pygame, screen, frame, camera, cue, cache)
        elif cue.kind == "effect":
            if draw_ambient(
                pygame, screen, frame, camera, cue, position, theme
            ):
                continue
            scale, x, y = transform(
                frame, visual_camera, cue.x, cue.y, screen.get_size()
            )
            progress = min(
                1.0,
                max(0.0, (position - cue.frame) / max(1.0, cue.duration)),
            )
            radius = max(
                8,
                round(
                    max(cue.width, cue.height, 2.0)
                    * scale
                    * (0.8 + progress * 0.6)
                ),
            )
            effect = pygame.Surface(screen.get_size(), pygame.SRCALPHA)
            pygame.draw.circle(
                effect,
                (*theme.accent, round(255 * cue.opacity * (1.0 - progress))),
                (round(x), round(y)),
                radius,
                3,
            )
            screen.blit(effect, (0, 0))
    if not trail_drawn:
        draw_trails(
            pygame, screen, frames, numbers, position, frame, camera, options
        )
    for cue in current_cues:
        if cue.kind in {"caption", "dialogue"}:
            draw_caption(pygame, screen, fonts, cue, frame, camera, position)
    draw_scene_chrome(pygame, screen, fonts, frame, camera, position, theme)
    if not options.suppress_title_card:
        draw_title_card(
            pygame,
            screen,
            fonts,
            frame,
            position,
            frames[0].number,
            frames[-1].number,
            title,
        )
    if options.hud:
        draw_turn_status(
            pygame, screen, fonts, frames, position, numbers, cache
        )


def load_scene_renderers():
    """Load optional scenario modules and bind their declared generic API"""
    # Scenario modules mark required drawing helpers with None before injection
    renderers = {}
    api = {
        name: value
        for name, value in globals().items()
        if not name.startswith("__")
    }
    for theme, assets in SCENE_ASSETS.items():
        source = assets / "renderer.py"
        if not source.is_file():
            continue
        module_name = "_scenario_" + re.sub(r"\W+", "_", theme)
        spec = importlib.util.spec_from_file_location(module_name, source)
        if spec is None or spec.loader is None:
            raise ValueError(f"cannot load scenario renderer: {source}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
        if bind := getattr(module, "bind", None):
            required = {
                name
                for name, value in vars(module).items()
                if value is None and name in api
            }
            bind(
                {name: api[name] for name in required},
                assets,
            )
        for name in getattr(module, "EXPORTS", ()):
            globals()[name] = getattr(module, name)
        renderers[theme] = module
    return renderers


SCENE_RENDERERS = load_scene_renderers()


def scene_renderer(frame):
    if isinstance(frame, str):
        theme = frame
    elif isinstance(frame, Theme):
        theme = THEME_NAMES.get(frame, "")
    else:
        theme = frame.presentation.get("theme", "")
    return SCENE_RENDERERS.get(theme)
