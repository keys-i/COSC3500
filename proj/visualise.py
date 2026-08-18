#!/usr/bin/env python3
"""Preview, validate, play and export deterministic simulation snapshots."""

import argparse
import csv
from dataclasses import dataclass
import io
import math
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import threading

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")

ROOT = Path(__file__).resolve().parent.parent
WINDOW = (960, 720)
STATE_FIELDS = {
    "frame", "record", "entity_id", "type_id", "type_name", "x", "y",
    "world_width", "world_height", "view", "shape", "colour", "glyph",
    "layer",
}
CUE_HEADER = [
    "frame", "kind", "asset", "text", "x", "y", "width", "height",
    "rotation", "scale", "opacity", "duration", "volume", "layer",
]
PALETTE = [
    (231, 76, 60), (52, 152, 219), (46, 204, 113), (241, 196, 15),
    (155, 89, 182), (230, 126, 34),
]


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


@dataclass
class Frame:
    number: int
    width: float
    height: float
    view: str
    entities: list[Entity]


@dataclass(frozen=True)
class Cue:
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


def optional_number(row, field, line, default):
    text = row.get(field, "")
    return default if not text else finite(text, field, line)


def dimensions(row, line):
    width = finite(row["world_width"], "world_width", line)
    height = finite(row["world_height"], "world_height", line)
    view = row["view"]
    if width <= 0.0 or height <= 0.0:
        raise ValueError(f"line {line}: world dimensions must be positive")
    if view not in ("plane", "grid"):
        raise ValueError(f"line {line}: view must be plane or grid")
    if view == "grid" and (not width.is_integer() or
                             not height.is_integer()):
        raise ValueError(f"line {line}: grid dimensions must be integers")
    return width, height, view


def parse_colour(text, type_id, line):
    if not text:
        return PALETTE[type_id % len(PALETTE)]
    if len(text) != 6 or any(char not in "0123456789abcdefABCDEF"
                             for char in text):
        raise ValueError(f"line {line}: colour must be six hex digits")
    return tuple(int(text[index:index + 2], 16) for index in (0, 2, 4))


def safe_file(root, value, field, line):
    candidate = Path(value)
    if not value or candidate.is_absolute() or ".." in candidate.parts:
        raise ValueError(f"line {line}: {field} must be a local asset")
    root = root.resolve()
    path = (root / candidate).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f"line {line}: {field} is not a readable local asset")
    return path


def repository_file(value, field):
    candidate = Path(value)
    path = candidate.resolve() if candidate.is_absolute() else \
        (Path.cwd() / candidate).resolve()
    root = ROOT.resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f"{field} must be a readable file in this repository")
    return path


def row_entity(row, line):
    entity_id = integer(row["entity_id"], "entity_id", line)
    type_id = integer(row["type_id"], "type_id", line)
    layer = integer(row["layer"], "layer", line)
    x = finite(row["x"], "x", line)
    y = finite(row["y"], "y", line)
    name = row["type_name"].strip()
    shape = row["shape"]
    glyph = row["glyph"]
    rotation = optional_number(row, "rotation", line, 0.0)
    scale = optional_number(row, "scale", line, 1.0)
    opacity = optional_number(row, "opacity", line, 1.0)
    if entity_id < 0 or type_id < 0 or not name:
        raise ValueError(f"line {line}: invalid entity or type")
    if shape not in ("circle", "cell", "text", "sprite"):
        raise ValueError(f"line {line}: invalid shape")
    if shape == "text" and not glyph:
        raise ValueError(f"line {line}: text shape needs a glyph")
    if scale <= 0.0 or not 0.0 <= opacity <= 1.0:
        raise ValueError(f"line {line}: invalid scale or opacity")
    return Entity(
        entity_id, type_id, name, x, y, shape,
        parse_colour(row["colour"], type_id, line), glyph, layer,
        row.get("sprite", ""), rotation, scale, opacity,
    )


def parse_frames(source):
    frames = []
    current = None
    reader = csv.DictReader(source)
    if (reader.fieldnames is None or
            not STATE_FIELDS <= set(reader.fieldnames)):
        raise ValueError("snapshot header does not match the state contract")
    for line, row in enumerate(reader, 2):
        if None in row or any(value is None for value in row.values()):
            raise ValueError(f"line {line}: snapshot row width is invalid")
        frame = integer(row["frame"], "frame", line)
        if frame < 0:
            raise ValueError(f"line {line}: frame must be non-negative")
        record = row["record"]
        if record == "frame":
            if current is not None and frame <= current.number:
                raise ValueError(f"line {line}: frames must be ordered")
            width, height, view = dimensions(row, line)
            current = Frame(frame, width, height, view, [])
            frames.append(current)
            continue
        if record == "end":
            if current is None or frame != current.number:
                raise ValueError(f"line {line}: end needs the current frame")
            continue
        if record != "entity" or current is None or frame != current.number:
            raise ValueError(f"line {line}: entity needs the current frame")
        width, height, view = dimensions(row, line)
        if (width, height, view) != (
                current.width, current.height, current.view):
            raise ValueError(f"line {line}: frame metadata changed")
        entity = row_entity(row, line)
        if entity.x < 0.0 or entity.x >= width or entity.y < 0.0 or \
                entity.y >= height:
            raise ValueError(f"line {line}: position is outside the world")
        if view == "grid" and (not entity.x.is_integer() or
                                not entity.y.is_integer()):
            raise ValueError(f"line {line}: grid positions must be integers")
        current.entities.append(entity)
    if not frames:
        raise ValueError("snapshot contains no frames")
    return frames


def load_frames(path):
    with path.open(newline="", encoding="utf-8") as source:
        return parse_frames(source)


def load_cues(path):
    cues = []
    root = ROOT
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != CUE_HEADER:
            raise ValueError("cue header does not match the state contract")
        for line, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"line {line}: cue row width is invalid")
            frame = integer(row["frame"], "frame", line)
            kind = row["kind"]
            if frame < 0 or kind not in {
                    "camera", "caption", "sprite", "video", "audio"}:
                raise ValueError(f"line {line}: invalid cue")
            asset = None
            if kind in {"sprite", "video", "audio"}:
                asset = safe_file(root, row["asset"], "asset", line)
            if kind == "caption" and not row["text"]:
                raise ValueError(f"line {line}: caption needs text")
            scale = finite(row["scale"], "scale", line)
            opacity = finite(row["opacity"], "opacity", line)
            width = finite(row["width"], "width", line)
            height = finite(row["height"], "height", line)
            duration = finite(row["duration"], "duration", line)
            volume = finite(row["volume"], "volume", line)
            if (scale <= 0.0 or not 0.0 <= opacity <= 1.0 or
                    min(width, height, duration, volume) < 0.0):
                raise ValueError(f"line {line}: invalid cue numeric range")
            cues.append(Cue(
                frame, kind, asset, row["text"], finite(row["x"], "x", line),
                finite(row["y"], "y", line), width, height,
                finite(row["rotation"], "rotation", line), scale, opacity,
                duration, volume,
                integer(row["layer"], "layer", line),
            ))
    return cues


def cue_map(cues):
    result = {}
    for cue in cues:
        count = max(1, math.ceil(cue.duration))
        for frame in range(cue.frame, cue.frame + count):
            result.setdefault(frame, []).append(cue)
    return result


def draw_grid(pygame, screen, frame, scale, offset_x, offset_y):
    for row in range(int(frame.height)):
        for column in range(int(frame.width)):
            colour = (235, 236, 208) if (row + column) % 2 == 0 else (
                119, 149, 86)
            rectangle = pygame.Rect(
                round(offset_x + column * scale),
                round(offset_y + row * scale), math.ceil(scale),
                math.ceil(scale),
            )
            pygame.draw.rect(screen, colour, rectangle)


def camera_for(frame, cues):
    camera = next((cue for cue in cues if cue.kind == "camera"), None)
    if camera is None:
        return frame.width / 2.0, frame.height / 2.0, 1.0
    return camera.x, camera.y, camera.scale


def transform(frame, camera, x, y):
    centre_x, centre_y, zoom = camera
    scale = min(WINDOW[0] / frame.width, WINDOW[1] / frame.height) * zoom
    offset_x = WINDOW[0] / 2.0 - centre_x * scale
    offset_y = WINDOW[1] / 2.0 - centre_y * scale
    return scale, offset_x + x * scale, offset_y + y * scale


def apply_surface(pygame, screen, surface, centre, rotation, scale, opacity):
    if scale != 1.0 or rotation != 0.0:
        surface = pygame.transform.rotozoom(surface, rotation, scale)
    if opacity != 1.0:
        surface = surface.copy()
        surface.set_alpha(round(opacity * 255.0))
    screen.blit(surface, surface.get_rect(center=centre))


def size_surface(pygame, surface, width, height, world_scale):
    if width <= 0.0 and height <= 0.0:
        return surface
    source_width, source_height = surface.get_size()
    target_width = width * world_scale if width > 0.0 else (
        height * world_scale * source_width / source_height)
    target_height = height * world_scale if height > 0.0 else (
        width * world_scale * source_height / source_width)
    return pygame.transform.smoothscale(
        surface, (max(1, round(target_width)), max(1, round(target_height))))


def image_surface(pygame, path, cache):
    if path not in cache:
        cache[path] = pygame.image.load(path)
    return cache[path]


def video_surface(pygame, path, cache, index):
    key = path, index
    if key not in cache:
        try:
            import av
        except ImportError as error:
            raise RuntimeError("PyAV is missing; run uv sync") from error
        with av.open(str(path), options={"threads": "1"}) as container:
            stream = container.streams.video[0]
            stream.thread_type = "NONE"
            stream.codec_context.thread_count = 1
            decoded = None
            for current, decoded_frame in enumerate(container.decode(stream)):
                if current == index:
                    decoded = decoded_frame
                    break
            if decoded is None:
                raise ValueError("video cue is shorter than the rendered scene")
            image = decoded.reformat(format="rgb24")
            pixels = bytes(image.planes[0])
            row_size = image.width * 3
            stride = image.planes[0].line_size
            if stride != row_size:
                pixels = b"".join(
                    pixels[row * stride:row * stride + row_size]
                    for row in range(image.height))
        cache[key] = pygame.image.frombuffer(
            pixels, (image.width, image.height), "RGB")
    return cache[key]


def render(pygame, screen, font, frame, cues, cache):
    screen.fill((25, 28, 34))
    camera = camera_for(frame, cues)
    scale, offset_x, offset_y = transform(frame, camera, 0.0, 0.0)
    if frame.view == "grid":
        draw_grid(pygame, screen, frame, scale, offset_x, offset_y)
    layers = [(entity.layer, entity) for entity in frame.entities]
    layers.extend((cue.layer, cue) for cue in cues if cue.kind != "camera")
    for _, item in sorted(layers, key=lambda pair: pair[0]):
        if isinstance(item, Entity):
            _, x, y = transform(frame, camera, item.x, item.y)
            if frame.view == "grid":
                x += scale / 2.0
                y += scale / 2.0
            centre = round(x), round(y)
            if item.shape == "sprite":
                path = safe_file(ROOT, item.sprite, "sprite", item.entity_id)
                apply_surface(
                    pygame, screen, image_surface(pygame, path, cache), centre,
                    item.rotation, item.scale, item.opacity)
            elif item.shape == "cell":
                inset = max(1, round(scale * 0.08))
                rectangle = pygame.Rect(
                    round(x - scale / 2.0) + inset,
                    round(y - scale / 2.0) + inset,
                    max(1, round(scale) - 2 * inset),
                    max(1, round(scale) - 2 * inset),
                )
                pygame.draw.rect(screen, item.colour, rectangle)
            elif item.shape == "text":
                apply_surface(
                    pygame, screen, font.render(item.glyph, True, item.colour),
                    centre, item.rotation, item.scale, item.opacity)
            else:
                radius = max(4, min(24, round(scale * 0.3 * item.scale)))
                pygame.draw.circle(screen, item.colour, centre, radius)
                if frame.view == "plane":
                    label = font.render(
                        f"{item.name} {item.entity_id}", True, (235, 235, 235))
                    screen.blit(label, (centre[0] + radius, centre[1] - 8))
            continue
        _, x, y = transform(frame, camera, item.x, item.y)
        centre = round(x), round(y)
        if item.kind == "caption":
            apply_surface(pygame, screen,
                          font.render(item.text, True, (255, 255, 255)), centre,
                          item.rotation, item.scale, item.opacity)
        elif item.kind == "sprite":
            surface = size_surface(
                pygame, image_surface(pygame, item.asset, cache), item.width,
                item.height, scale)
            apply_surface(pygame, screen,
                          surface, centre,
                          item.rotation, item.scale, item.opacity)
        elif item.kind == "video":
            surface = size_surface(
                pygame,
                video_surface(
                    pygame, item.asset, cache,
                    max(0, frame.number - item.frame)),
                item.width, item.height, scale)
            apply_surface(pygame, screen,
                          surface, centre,
                          item.rotation, item.scale, item.opacity)


def video_path(name):
    candidate = Path(name)
    allowed = " .-_"
    if (candidate.name != name or candidate.suffix.lower() != ".mp4" or
            not candidate.stem or any(not char.isalnum() and char not in allowed
                                      for char in name)):
        raise ValueError("--export needs a safe .mp4 filename, not a path")
    directory = ROOT / "results" / "videos"
    if directory.is_symlink():
        raise ValueError(f"refusing symlinked output: {directory}")
    directory.mkdir(parents=True, exist_ok=True)
    output = directory / name
    if output.exists() or output.is_symlink():
        raise ValueError(f"refusing to overwrite {output}")
    return output


def run_ffmpeg(command):
    result = subprocess.run(
        command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"ffmpeg failed: {result.stderr.strip() or result.returncode}")


def mux_audio(ffmpeg, video, output, cues, fps):
    audio = [cue for cue in cues if cue.kind == "audio"]
    if not audio:
        video.replace(output)
        return
    command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-threads", "1"]
    command.extend(("-i", str(video)))
    for cue in audio:
        command.extend(("-i", str(cue.asset)))
    filters = []
    labels = []
    for index, cue in enumerate(audio, 1):
        delay = round(cue.frame * 1000.0 / fps)
        label = f"a{index}"
        parts = [f"[{index}:a]adelay={delay}|{delay}",
                 f"volume={cue.volume}"]
        if cue.duration > 0.0:
            parts.append(f"atrim=duration={cue.duration / fps}")
        filters.append(f"{','.join(parts)}[{label}]")
        labels.append(f"[{label}]")
    filters.append(f"{''.join(labels)}amix=inputs={len(labels)}:normalize=0[a]")
    command.extend(("-filter_complex", ";".join(filters), "-map", "0:v:0",
                    "-map", "[a]", "-c:v", "copy", "-c:a", "aac",
                    "-movflags", "+faststart", str(output)))
    run_ffmpeg(command)
    video.unlink()


def export_video(pygame, font, frames, cues_by_frame, cues, fps, name):
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise RuntimeError("ffmpeg is missing; install it and retry")
    output = video_path(name)
    intermediate = output.with_suffix(".video.tmp.mp4")
    command = [
        ffmpeg, "-hide_banner", "-loglevel", "error", "-threads", "1",
        "-f", "rawvideo", "-pixel_format", "rgb24",
        "-video_size", f"{WINDOW[0]}x{WINDOW[1]}", "-framerate", str(fps),
        "-i", "pipe:0", "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p",
        "-movflags", "+faststart", str(intermediate),
    ]
    surface = pygame.Surface(WINDOW)
    cache = {}
    try:
        with subprocess.Popen(
                command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE) as encoder:
            if encoder.stdin is None:
                raise RuntimeError("cannot open ffmpeg input")
            for frame in frames:
                render(pygame, surface, font, frame,
                       cues_by_frame.get(frame.number, []), cache)
                encoder.stdin.write(pygame.image.tobytes(surface, "RGB"))
            encoder.stdin.close()
            message = encoder.stderr.read().decode("utf-8", "replace").strip()
            if encoder.wait() != 0:
                raise RuntimeError(
                    f"ffmpeg failed: {message or 'encoding error'}")
        mux_audio(ffmpeg, intermediate, output, cues, fps)
    except Exception:
        intermediate.unlink(missing_ok=True)
        output.unlink(missing_ok=True)
        raise
    print(f"video={output.relative_to(ROOT)}")


def preview(pygame, frames, cues_by_frame, fps):
    pygame.init()
    screen = pygame.display.set_mode(WINDOW)
    font = pygame.font.Font(None, 20)
    clock = pygame.time.Clock()
    cache = {}
    index = 0
    paused = False
    running = True
    while running:
        pygame.display.set_caption(
            f"simulation — frame {frames[index].number}")
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_SPACE:
                    paused = not paused
                elif event.key == pygame.K_LEFT:
                    index, paused = max(0, index - 1), True
                elif event.key == pygame.K_RIGHT:
                    index, paused = min(len(frames) - 1, index + 1), True
                elif event.key == pygame.K_HOME:
                    index, paused = 0, True
                elif event.key == pygame.K_END:
                    index, paused = len(frames) - 1, True
        render(pygame, screen, font, frames[index],
               cues_by_frame.get(frames[index].number, []), cache)
        pygame.display.flip()
        clock.tick(fps)
        if not paused and index < len(frames) - 1:
            index += 1
    pygame.quit()


def resolve_scenario(value):
    root = ROOT / "proj" / "m1" / "scenarios"
    candidate = Path(value)
    if (candidate.is_absolute() or len(candidate.parts) != 2 or
            candidate.parts[0] not in {"templates", "test"}):
        raise ValueError(
            "scenario must be templates/<name> or test/<name>")
    flat = candidate.with_name(f"{candidate.name}.sim")
    source = flat if (root / flat).is_file() else candidate / "scenario.sim"
    safe_file(root, str(source), "scenario", 0)
    return candidate.as_posix()


def stream_frames(source, outbox, failures):
    try:
        reader = csv.DictReader(source)
        if (reader.fieldnames is None or
                not STATE_FIELDS <= set(reader.fieldnames)):
            raise ValueError("engine stream has the wrong state header")
        rows = io.StringIO()
        writer = csv.DictWriter(rows, fieldnames=reader.fieldnames)
        writer.writeheader()
        frame = None
        for row in reader:
            writer.writerow(row)
            if row["record"] == "frame":
                frame = row["frame"]
            elif row["record"] == "end" and row["frame"] == frame:
                outbox.put(rows.getvalue())
                rows = io.StringIO()
                writer = csv.DictWriter(rows, fieldnames=reader.fieldnames)
                writer.writeheader()
    except (OSError, ValueError, csv.Error) as error:
        failures.put(error)
    finally:
        outbox.put(None)


def play(args):
    scenario = resolve_scenario(args.scenario)
    binary = repository_file(args.binary, "--binary")
    if not os.access(binary, os.X_OK):
        raise ValueError("--binary must name an executable m1 stream binary")
    process = subprocess.Popen(
        [str(binary), str(scenario), "--stream"], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1,
    )
    if process.stdin is None or process.stdout is None:
        raise RuntimeError("cannot open private engine stream")
    frames = queue.Queue()
    failures = queue.Queue()
    reader = threading.Thread(
        target=stream_frames, args=(process.stdout, frames, failures),
        daemon=True)
    reader.start()
    try:
        import pygame
    except ImportError as error:
        process.kill()
        raise RuntimeError("Pygame is missing; run uv sync") from error
    pygame.init()
    screen = pygame.display.set_mode(WINDOW)
    font = pygame.font.Font(None, 20)
    clock = pygame.time.Clock()
    current = None
    pending_tick = False
    paused = False
    try:
        running = True
        while running:
            try:
                payload = frames.get_nowait()
            except queue.Empty:
                payload = ""
            if payload is None:
                running = False
            elif payload:
                current = parse_frames(io.StringIO(payload))[-1]
                pending_tick = True
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN and \
                        event.key == pygame.K_ESCAPE:
                    running = False
                elif event.type == pygame.KEYDOWN and \
                        event.key == pygame.K_SPACE:
                    paused = not paused
            if current is not None:
                render(pygame, screen, font, current, [], {})
                pygame.display.flip()
            if pending_tick and not paused:
                try:
                    process.stdin.write("tick\n")
                    process.stdin.flush()
                except BrokenPipeError:
                    running = False
                pending_tick = False
            clock.tick(args.fps)
        if not failures.empty():
            raise failures.get()
    finally:
        try:
            process.stdin.close()
        except BrokenPipeError:
            pass
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=2)
        pygame.quit()


def parse_args():
    if sys.argv[1:] == ["--headless-check"]:
        return argparse.Namespace(command="self-check")
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command")
    render_parser = commands.add_parser("render")
    render_parser.add_argument("state", type=Path)
    render_parser.add_argument("--cues", type=Path)
    render_parser.add_argument("--fps", type=float, default=12.0)
    render_parser.add_argument("--check", "--headless", action="store_true")
    render_parser.add_argument("--export", metavar="NAME.mp4")
    play_parser = commands.add_parser("play")
    play_parser.add_argument("scenario")
    play_parser.add_argument("--binary", required=True)
    play_parser.add_argument("--fps", type=float, default=12.0)
    arguments = sys.argv[1:]
    if arguments and arguments[0] not in {"render", "play", "-h", "--help"}:
        arguments.insert(0, "render")
    args = parser.parse_args(arguments)
    if args.command is None:
        parser.error("choose render or play")
    if not math.isfinite(args.fps) or args.fps <= 0.0:
        parser.error("--fps must be positive and finite")
    return args


def main():
    args = parse_args()
    if args.command == "self-check":
        try:
            import av
            import pygame
        except ImportError as error:
            raise RuntimeError("visualiser dependencies are missing") from error
        if shutil.which("ffmpeg") is None:
            raise RuntimeError("ffmpeg is missing")
        resolve_scenario("templates/conway")
        resolve_scenario("templates/connect-four")
        _ = av.__version__, pygame.version.ver
        print("PASS: visualiser dependencies")
        return 0
    if args.command == "play":
        play(args)
        return 0
    frames = load_frames(repository_file(args.state, "state"))
    cues = load_cues(repository_file(args.cues, "cues")) if args.cues else []
    if args.check:
        print(f"PASS: {len(frames)} snapshot frames; {len(cues)} cues")
        return 0
    try:
        import pygame
    except ImportError as error:
        raise RuntimeError("Pygame is missing; run uv sync") from error
    pygame.font.init()
    try:
        mapped = cue_map(cues)
        if args.export:
            export_video(pygame, pygame.font.Font(None, 20), frames, mapped,
                         cues, args.fps, args.export)
        else:
            preview(pygame, frames, mapped, args.fps)
    finally:
        pygame.font.quit()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, csv.Error,
            subprocess.SubprocessError) as error:
        print(f"visualise: {error}", file=sys.stderr)
        raise SystemExit(1)
