"""Run the visualiser in preview, export, and live-stream modes

All modes use core parsing and rendering so headless checks match visible
output
Exports reserve their destination before encoding and live playback reads
engine frames on a worker
"""

import csv
import math
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from .audio import mux_audio
from .core import (
    ROOT,
    WINDOW,
    RenderOptions,
    apply_visual_plan,
    draw_poster,
    load_frames,
    load_pygame,
    load_scene_meta,
    load_visual_plan,
    outcome_cues,
    parse_state_row,
    poster_cue,
    present_canvas,
    presentation_duration,
    presentation_options,
    presentation_position,
    render,
    render_context,
    repository_file,
    safe_file,
    state_reader,
    validate_render_assets,
)

EXPORT_SIZE = (2560, 1440)


def output_directory(root):
    """Return the video directory after rejecting unsafe repository paths"""
    results = root / "results"
    videos = results / "videos"
    # Repository-relative output only
    for path in (results, videos):
        if path.is_symlink() or (path.exists() and not path.is_dir()):
            raise ValueError(f"refusing unsafe output directory: {path}")
    videos.mkdir(parents=True, exist_ok=True)
    if not videos.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"output directory leaves repository: {videos}")
    return videos


def reserve_output(path):
    """Create one empty export file exclusively and return its inode token"""
    try:
        # Atomic output reservation
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError as error:
        raise ValueError(f"refusing to overwrite {path}") from error
    try:
        return os.fstat(descriptor).st_ino
    finally:
        os.close(descriptor)


def video_path(name):
    """Validate an export filename and reserve it inside results/videos"""
    candidate = Path(name)
    if (
        candidate.name != name
        or candidate.suffix.lower() != ".mp4"
        or not candidate.stem
        or any(not char.isalnum() and char not in " .-_" for char in name)
    ):
        raise ValueError("--export needs a safe .mp4 filename, not a path")
    output = output_directory(ROOT) / name
    return output, reserve_output(output)


def export_progress(done, total):
    """Format the video export progress bar"""
    width = 28
    filled = width * done // total
    return (
        f"render [{'#' * filled}{'-' * (width - filled)}] "
        f"{done * 100 // total:3d}%"
    )


def export_video(
    pygame,
    frames,
    cues,
    fps,
    duration,
    name,
    title,
    start=None,
    reduced_motion=False,
    poster_seconds=0.0,
    poster_metadata=None,
):
    """Render a frame sequence to a reserved MP4 and mux its derived audio

    The temporary encode is moved into place only if its reservation remains
    Failure removes only the reserved placeholder and leaves files safe
    """
    # Frames reach ffmpeg as raw RGB for encoding
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise RuntimeError("ffmpeg is missing; install it and retry")
    surface, fonts, cache, options = render_context(
        pygame, frames, cues, reduced_motion
    )
    if not 0.0 <= poster_seconds < duration:
        raise ValueError(
            "poster duration must be non-negative and shorter than video"
        )
    if poster_seconds > 0.0 and poster_metadata is None:
        raise ValueError("poster export needs scene metadata")
    options.suppress_title_card = poster_seconds > 0.0
    poster_options = presentation_options(frames[0])
    poster_options.reduced_motion = True
    poster_options.suppress_title_card = True
    poster_options.hud = False
    poster = poster_cue(cues)
    active_duration = duration - poster_seconds
    total = max(1, round(duration * fps))
    active_span = max(1.0 / fps, (total - 1) / fps - poster_seconds)
    run_first, run_last = frames[0].number, frames[-1].number
    complete = presentation_duration(frames[0])
    window_start = start or 0.0
    window_span = complete if start is None else active_duration
    if window_start + window_span > complete:
        raise ValueError("--start and --duration exceed the simulation")
    first = presentation_position(frames[0], window_start, run_first, run_last)
    last = presentation_position(
        frames[0], window_start + window_span, run_first, run_last
    )
    if start is not None and start < 0.0:
        raise ValueError("--start must be non-negative")
    # Reserve the final name before creating temporary encoder output beside it
    output, reservation = video_path(name)
    started = time.perf_counter()
    interactive = sys.stderr.isatty()
    progress_open = False
    temporary = None
    try:
        temporary = tempfile.TemporaryDirectory(
            prefix="m1-video-", dir=output.parent
        )
        intermediate = Path(temporary.name) / "video.mp4"
        completed = Path(temporary.name) / "completed.mp4"
        command = [
            ffmpeg,
            "-hide_banner",
            "-loglevel",
            "error",
            "-f",
            "rawvideo",
            "-pixel_format",
            "rgb24",
            "-video_size",
            f"{WINDOW[0]}x{WINDOW[1]}",
            "-framerate",
            str(fps),
            "-i",
            "pipe:0",
            "-an",
            "-c:v",
            "libx264",
            "-preset",
            "medium",
            "-crf",
            "17",
            "-vf",
            f"scale={EXPORT_SIZE[0]}:{EXPORT_SIZE[1]}:flags=lanczos",
            "-pix_fmt",
            "yuv420p",
            "-movflags",
            "+faststart",
            str(intermediate),
        ]
        with subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        ) as encoder:
            if encoder.stdin is None or encoder.stderr is None:
                raise RuntimeError("cannot open ffmpeg pipes")
            shown = 0
            if interactive:
                print(
                    export_progress(0, total),
                    end="\r",
                    file=sys.stderr,
                    flush=True,
                )
                progress_open = True
            # Render poster frames first, then map film time to simulation
            for index in range(total):
                elapsed = index / fps
                if elapsed < poster_seconds:
                    render(
                        pygame,
                        surface,
                        fonts,
                        frames,
                        frames[0].number,
                        cues,
                        cache,
                        poster_options,
                        title,
                    )
                    draw_poster(
                        pygame,
                        surface,
                        fonts,
                        frames[0],
                        poster,
                        title,
                        cache,
                        poster_metadata,
                    )
                else:
                    progress = min(
                        1.0, (elapsed - poster_seconds) / active_span
                    )
                    position = presentation_position(
                        frames[0],
                        window_start + window_span * progress,
                        run_first,
                        run_last,
                    )
                    render(
                        pygame,
                        surface,
                        fonts,
                        frames,
                        position,
                        cues,
                        cache,
                        options,
                        title,
                    )
                encoder.stdin.write(pygame.image.tobytes(surface, "RGB"))
                if interactive:
                    done = index + 1
                    percent = done * 100 // total
                    if percent > shown:
                        final = done == total
                        print(
                            export_progress(done, total),
                            end="\n" if final else "\r",
                            file=sys.stderr,
                            flush=True,
                        )
                        shown = percent
                        progress_open = not final
            encoder.stdin.close()
            message = encoder.stderr.read().decode("utf-8", "replace").strip()
            if encoder.wait() != 0:
                raise RuntimeError(
                    f"ffmpeg failed: {message or 'encoding error'}"
                )
        # Audio uses the same simulation interval as the rendered frames
        mux_audio(
            ffmpeg,
            intermediate,
            completed,
            cues,
            frames[0],
            first,
            last,
            run_first,
            run_last,
            duration,
            poster_seconds,
            window_start,
            window_span,
        )
        if output.stat().st_ino != reservation:
            raise RuntimeError("export reservation changed during rendering")
        completed.replace(output)
    # Remove only the inode reserved by this call when setup or encoding fails
    except Exception:
        if progress_open:
            print(file=sys.stderr)
        try:
            if output.stat().st_ino == reservation:
                output.unlink()
        except FileNotFoundError:
            pass
        raise
    finally:
        if temporary is not None:
            temporary.cleanup()
    elapsed = time.perf_counter() - started
    print(f"video={output.relative_to(ROOT)}")
    print(f"render_frames={total}")
    print(f"render_seconds={elapsed:.6f}")
    print(f"render_ms_per_frame={elapsed * 1000.0 / total:.6f}")


def preview(pygame, frames, cues, fps, duration, title, reduced_motion=False):
    """Display an interactive preview with local playback and camera control"""
    # Preview keeps controls local while export uses the same render path above
    pygame.init()
    screen = pygame.display.set_mode(WINDOW, pygame.RESIZABLE)
    canvas, fonts, cache, options = render_context(
        pygame, frames, cues, reduced_motion
    )
    clock = pygame.time.Clock()
    first, last, position, paused, running = (
        frames[0].number,
        frames[-1].number,
        frames[0].number,
        False,
        True,
    )
    rate = max(0.0, last - first) / duration
    while running:
        pygame.display.set_caption(f"{title} — frame {position:.1f}")
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_SPACE:
                    paused = not paused
                elif event.key == pygame.K_LEFT:
                    position, paused = max(first, position - 1.0), True
                elif event.key == pygame.K_RIGHT:
                    position, paused = min(last, position + 1.0), True
                elif event.key == pygame.K_HOME:
                    position, paused = first, True
                elif event.key == pygame.K_END:
                    position, paused = last, True
                elif event.key == pygame.K_h:
                    options.hud = not options.hud
                elif event.key == pygame.K_l:
                    options.labels = not options.labels
                elif event.key == pygame.K_t:
                    options.trails = not options.trails
                elif event.key == pygame.K_v:
                    options.vectors = not options.vectors
                elif event.key in (pygame.K_PLUS, pygame.K_EQUALS):
                    options.zoom *= 1.15
                elif event.key == pygame.K_MINUS:
                    options.zoom /= 1.15
                elif event.key == pygame.K_r:
                    options, position = presentation_options(frames[0]), first
                    options.reduced_motion = reduced_motion
            elif event.type == pygame.MOUSEWHEEL:
                options.zoom *= 1.15 if event.y > 0 else 1.0 / 1.15
            elif event.type == pygame.MOUSEMOTION and event.buttons[2]:
                scale = min(
                    WINDOW[0] / frames[0].width,
                    WINDOW[1] / frames[0].height,
                )
                options.pan_x -= event.rel[0] / scale / options.zoom
                options.pan_y -= event.rel[1] / scale / options.zoom
        render(
            pygame, canvas, fonts, frames, position, cues, cache, options, title
        )
        present_canvas(pygame, screen, canvas)
        pygame.display.flip()
        elapsed = clock.tick(fps) / 1000.0
        if not paused:
            position = min(last, position + rate * elapsed)
    pygame.quit()


def resolve_scenario(value):
    """Validate an identifier from allowed template or test scenario roots"""
    # Live playback accepts scenario selectors, not filesystem paths
    candidate = Path(value)
    template = candidate.parts and candidate.parts[0] == "templates"
    if (
        candidate.is_absolute()
        or len(candidate.parts) not in {2, 3}
        or candidate.parts[0] not in {"templates", "test"}
    ):
        raise ValueError(
            "scenario must be templates/<name> or test/<name>[/<member>]"
        )
    root = (
        ROOT / "proj" / "scenarios"
        if template
        else ROOT / "tests" / "scenarios" / "fixtures"
    )
    scenario_file(root, Path(*candidate.parts[1:]))
    return candidate.as_posix()


def scenario_file(root, relative):
    for source in (relative.with_suffix(".sim"), relative / "scenario.sim"):
        try:
            return safe_file(root, str(source), "scenario", 0)
        except ValueError:
            pass
    raise ValueError("line 0: scenario is not a readable local asset")


def scenario_source(value):
    candidate = Path(resolve_scenario(value))
    root = (
        ROOT / "proj" / "scenarios"
        if candidate.parts[0] == "templates"
        else ROOT / "tests" / "scenarios" / "fixtures"
    )
    return scenario_file(root, Path(*candidate.parts[1:]))


def stream_frames(source, outbox, failures):
    """Parse engine output on a worker and send complete frames to pygame"""
    # The reader thread blocks on engine output while pygame stays responsive
    try:
        reader = state_reader(
            source, "engine stream has the wrong state header"
        )
        current, last = None, -1
        for line, row in enumerate(reader, 2):
            current, ended = parse_state_row(row, line, current, last)
            if row["record"] == "frame":
                assert current is not None
                last = current.number
            elif ended is not None:
                outbox.put(ended)
    except (OSError, ValueError, csv.Error) as error:
        failures.put(error)
    finally:
        outbox.put(None)


def play_stream(scenario, binary, fps, reduced_motion):
    """Run one approved binary and display frames through a tick handshake

    The child advances after each rendered frame so the UI does not outrun it
    Parser errors cross the worker queue and are raised after playback stops
    """
    scenario, binary = (
        resolve_scenario(scenario),
        repository_file(binary, "--binary"),
    )
    if not os.access(binary, os.X_OK):
        raise ValueError("--binary must name an executable m1 stream binary")
    presentation, cues = load_visual_plan(scenario_source(scenario))
    pygame = load_pygame()
    # The renderer only receives a repository-approved executable and scenario
    process = subprocess.Popen(
        [str(binary), str(scenario), "--stream"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        bufsize=1,
    )
    if process.stdin is None or process.stdout is None:
        raise RuntimeError("cannot open private engine stream")
    frames, failures = queue.Queue(), queue.Queue()
    threading.Thread(
        target=stream_frames,
        args=(process.stdout, frames, failures),
        daemon=True,
    ).start()
    pygame.init()
    screen = pygame.display.set_mode(WINDOW, pygame.RESIZABLE)
    canvas, clock, fonts, cache, options = (
        pygame.Surface(WINDOW),
        pygame.time.Clock(),
        {},
        {},
        RenderOptions(),
    )
    options.reduced_motion = reduced_motion
    current, pending_tick, paused, running = None, False, False, True
    # Keep window events, incoming frames, and engine ticks in one UI loop
    try:
        while running:
            try:
                payload = frames.get_nowait()
            except queue.Empty:
                payload = ""
            if payload is None:
                running = False
            elif payload:
                apply_visual_plan([payload], presentation)
                current, pending_tick = payload, True
            for event in pygame.event.get():
                if event.type == pygame.QUIT or (
                    event.type == pygame.KEYDOWN
                    and event.key == pygame.K_ESCAPE
                ):
                    running = False
                elif (
                    event.type == pygame.KEYDOWN and event.key == pygame.K_SPACE
                ):
                    paused = not paused
            if current is not None:
                render(
                    pygame,
                    canvas,
                    fonts,
                    [current],
                    current.number,
                    outcome_cues(
                        cues,
                        current.presentation.get("result", ""),
                        current.number,
                    ),
                    cache,
                    options,
                    Path(scenario).name.replace("-", " ").title(),
                )
                present_canvas(pygame, screen, canvas)
                pygame.display.flip()
            if pending_tick and not paused:
                # Advance only after this frame has reached the window
                try:
                    process.stdin.write("tick\n")
                    process.stdin.flush()
                except BrokenPipeError:
                    running = False
                pending_tick = False
            clock.tick(fps)
        if not failures.empty():
            raise failures.get()
    # Always close the child pipes and allow a bounded graceful shutdown
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


def valid_fps(fps):
    if not math.isfinite(fps) or not 1.0 <= fps <= 120.0:
        raise ValueError("--fps must be finite and between 1 and 120")


def render_options(fps, duration, poster_seconds, scene_meta, start):
    """Reject command options outside a valid presentation interval"""
    valid_fps(fps)
    if duration is not None and (
        not math.isfinite(duration) or not 1.0 <= duration <= 1205.0
    ):
        raise ValueError(
            "--duration must be finite and between 1 and 1205 seconds"
        )
    if not math.isfinite(poster_seconds) or not 0.0 <= poster_seconds <= 5.0:
        raise ValueError("--poster-seconds must be finite and between 0 and 5")
    if poster_seconds > 0.0 and scene_meta is None:
        raise ValueError("--poster-seconds requires --scene-meta")
    if start is not None and (not math.isfinite(start) or start < 0.0):
        raise ValueError("--start must be a finite non-negative frame")


def run_render(
    state,
    fps,
    duration,
    poster_seconds,
    scene_meta,
    start,
    check,
    export,
    reduced_motion,
):
    """Validate inputs, then check, preview, or export the presentation"""
    # Headless validation uses parsing and asset checks used by visible output
    render_options(fps, duration, poster_seconds, scene_meta, start)
    state = repository_file(state, "state")
    frames = load_frames(state)
    metadata = (
        repository_file(scene_meta, "scene metadata") if scene_meta else None
    )
    presentation, cues = (
        load_visual_plan(
            repository_file(metadata.parent / "scenario.sim", "scenario")
        )
        if metadata
        else ({}, [])
    )
    poster_metadata, visual = (
        load_scene_meta(metadata) if metadata else (None, {})
    )
    reduced_motion = reduced_motion or visual.get("reduced_motion") == "true"
    if presentation:
        presentation = {
            **presentation,
            "safe_margin": str(visual.get("safe_margin", 0.04)),
        }
        if "terminal_hold_seconds" in visual:
            presentation["terminal_hold_seconds"] = str(
                visual["terminal_hold_seconds"]
            )
        apply_visual_plan(frames, presentation)
    cues = outcome_cues(
        cues, frames[-1].presentation.get("result", ""), frames[-1].number
    )
    if check:
        validate_render_assets(frames)
        print(f"PASS: {len(frames)} snapshot frames; {len(cues)} cues")
        return
    pygame = load_pygame()
    pygame.font.init()
    try:
        title = state.stem.replace("-", " ").title()
        duration = duration or presentation_duration(frames[0])
        film = frames[0].presentation.get("format") == "film"
        minimum, maximum = (1.0, 1200.0) if film else (8.0, 300.0)
        if not minimum <= duration - poster_seconds <= maximum:
            raise ValueError("duration is outside the presentation range")
        if start is not None and not film:
            raise ValueError("--start is only available for film snapshots")
        if export:
            export_video(
                pygame,
                frames,
                cues,
                fps,
                duration,
                export,
                title,
                start,
                reduced_motion,
                poster_seconds,
                poster_metadata,
            )
        else:
            preview(pygame, frames, cues, fps, duration, title, reduced_motion)
    finally:
        pygame.font.quit()
