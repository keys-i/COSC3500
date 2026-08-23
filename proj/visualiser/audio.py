"""Build and mux the audio track for an exported visualisation

Cue timing begins in simulation time and is scaled to the selected film
interval
Narration is generated in a temporary directory while ffmpeg performs the
final mix
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from .core import presentation_duration, presentation_seconds


def run_ffmpeg(command):
    """Run one ffmpeg command and preserve its diagnostic text on failure"""
    result = subprocess.run(
        command, check=False, capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"ffmpeg failed: {result.stderr.strip() or result.returncode}"
        )


def narration_file(cue, directory, index, tts):
    """Generate one narration source with the platform speech program"""
    if tts == "say":
        path = directory / f"narration-{index}.aiff"
        command = ["say"]
        command.extend(("-v", cue.voice or "Karen"))
        if cue.rate:
            command.extend(("-r", str(cue.rate)))
        command.extend(("-o", str(path), cue.text))
    else:
        path = directory / f"narration-{index}.wav"
        command = [
            tts,
            "-v",
            cue.voice or "en-au",
            "-s",
            str(cue.rate or 190),
            "-w",
            str(path),
            cue.text,
        ]
    result = subprocess.run(
        command, check=False, capture_output=True, text=True
    )
    if result.returncode != 0 or not path.is_file():
        raise RuntimeError(
            f"narration failed: {result.stderr.strip() or result.returncode}"
        )
    return path


def mux_audio(
    ffmpeg,
    video,
    output,
    cues,
    timeline,
    first,
    last,
    run_first,
    run_last,
    duration,
    poster_seconds=0.0,
    window_start=0.0,
    window_span=None,
):
    """Select audible cues, align them to the film window, and write the MP4

    No audio cues still produce a silent stereo track for every export
    Remove temporary narration and intermediate video after ffmpeg succeeds
    """
    # Select cues in simulation time before scaling positions to film time
    selected = [cue for cue in cues if first <= cue.frame < last]
    audio = [
        (cue, cue.asset) for cue in selected if cue.kind in {"audio", "music"}
    ]
    with tempfile.TemporaryDirectory(
        prefix="m1-audio-", dir=output.parent
    ) as temp:
        # Identical narration requests share one generated source file
        narration = {}
        tts = (
            "say"
            if sys.platform == "darwin" and shutil.which("say")
            else (shutil.which("espeak-ng") or shutil.which("espeak"))
        )
        # Generate narration once for each distinct text, voice, and rate
        for cue in selected:
            if cue.kind not in {"narration", "dialogue"}:
                continue
            if not tts:
                raise RuntimeError(
                    "narration needs macOS say or Linux espeak-ng"
                )
            key = cue.text, cue.voice, cue.rate
            if key not in narration:
                narration[key] = narration_file(
                    cue, Path(temp), len(narration), tts
                )
            audio.append((cue, narration[key]))
        # Preserve an audio stream when the scenario has no audible cues
        if not audio:
            run_ffmpeg(
                [
                    ffmpeg,
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-i",
                    str(video),
                    "-f",
                    "lavfi",
                    "-i",
                    "anullsrc=channel_layout=stereo:sample_rate=48000",
                    "-map",
                    "0:v:0",
                    "-map",
                    "1:a:0",
                    "-c:v",
                    "copy",
                    "-c:a",
                    "aac",
                    "-t",
                    f"{duration:.3f}",
                    "-movflags",
                    "+faststart",
                    str(output),
                ]
            )
            video.unlink()
            return
        command = [
            ffmpeg,
            "-hide_banner",
            "-loglevel",
            "error",
            "-i",
            str(video),
        ]
        for _, path in audio:
            command.extend(("-i", str(path)))
        # Build one delayed input label per cue before mixing them into stereo
        filters, labels = [], []
        active_duration = duration - poster_seconds
        window_span = window_span or presentation_duration(timeline)
        scale = active_duration / window_span
        for index, (cue, _) in enumerate(audio, 1):
            cue_seconds = presentation_seconds(
                timeline, cue.frame, run_first, run_last
            )
            # adelay uses milliseconds while presentation positions use seconds
            delay = round(
                poster_seconds * 1000.0
                + (cue_seconds - window_start) * scale * 1000.0
            )
            length = 0.0
            if cue.duration > 0.0:
                end_seconds = presentation_seconds(
                    timeline,
                    min(cue.frame + cue.duration, last),
                    run_first,
                    run_last,
                )
                length = (end_seconds - cue_seconds) * scale
            trim = f"atrim=duration={length:.3f}," if length > 0.0 else ""
            filters.append(
                f"[{index}:a]{trim}adelay={delay}|{delay},volume={cue.volume}[a{index}]"
            )
            labels.append(f"[a{index}]")
        filters.append(
            f"{''.join(labels)}amix=inputs={len(labels)}:normalize=0,"
            "aresample=48000,aformat=channel_layouts=stereo,"
            f"apad,atrim=duration={duration:.3f}[a]"
        )
        run_ffmpeg(
            command
            + [
                "-filter_complex",
                ";".join(filters),
                "-map",
                "0:v:0",
                "-map",
                "[a]",
                "-c:v",
                "copy",
                "-c:a",
                "aac",
                "-movflags",
                "+faststart",
                str(output),
            ]
        )
    video.unlink()
