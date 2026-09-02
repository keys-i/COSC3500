"""Expose the visualiser command line

Render delegates snapshots to run_render while play starts a live M1 stream
Both commands share parsing, drawing, export, and audio helpers in this package
"""

from pathlib import Path
from typing import Annotated

import typer

from .audio import *
from .core import *
from .render import *

app = typer.Typer(add_completion=False, pretty_exceptions_enable=False)


@app.command("render")
def render_command(
    state: Path,
    fps: Annotated[float, typer.Option()] = 60.0,
    duration: Annotated[float | None, typer.Option()] = None,
    poster_seconds: Annotated[float, typer.Option("--poster-seconds")] = 0.0,
    scene_meta: Annotated[Path | None, typer.Option("--scene-meta")] = None,
    start: Annotated[float | None, typer.Option()] = None,
    check: Annotated[bool, typer.Option("--check", "--headless")] = False,
    export: Annotated[str | None, typer.Option()] = None,
    reduced_motion: Annotated[bool, typer.Option("--reduced-motion")] = False,
):
    """Render, preview, or validate a snapshot CSV"""
    run_render(
        state,
        fps,
        duration,
        poster_seconds,
        scene_meta,
        start,
        check,
        export,
        reduced_motion,
    )


@app.command()
def play(
    scenario: str,
    binary: Annotated[Path, typer.Option()],
    fps: Annotated[float, typer.Option()] = 60.0,
    reduced_motion: Annotated[bool, typer.Option("--reduced-motion")] = False,
):
    """Play a live m1 scenario stream"""
    valid_fps(fps)
    play_stream(scenario, binary, fps, reduced_motion)
