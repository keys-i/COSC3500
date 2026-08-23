"""Render Conway state, territory, and faction chatter

The visualiser imports this module for the Conway theme and binds its generic
drawing API
TSV files hold faction labels, colours, ranks, and messages so this code maps
state to visuals
"""

import csv
import math
from typing import Any

EXPORTS = ("conway_territory", "conway_label_size", "conway_chatter")

draw_text: Any = None


def _rows(path):
    with path.open(encoding="utf-8", newline="") as handle:
        return tuple(csv.DictReader(handle, delimiter="\t"))


def bind(api, assets):
    """Bind generic helpers and load the ordered Conway asset tables"""
    # The visualiser injects its API before this scenario loads its tables
    globals().update(api)
    global CONWAY_STATES, CONWAY_RANKS, CONWAY_TAUNTS, CONWAY_SEQUENCE
    global CONWAY_FACTIONS
    CONWAY_STATES = {
        int(row["state"]): (
            tuple(int(row[channel]) for channel in ("red", "green", "blue")),
            float(row["opacity"]),
        )
        for row in _rows(assets / "states.tsv")
    }
    ranks = {}
    for row in _rows(assets / "ranks.tsv"):
        ranks.setdefault(int(row["state"]), []).append(
            (int(row["sequence"]), row["rank"])
        )
    CONWAY_RANKS = {
        state: tuple(rank for _, rank in sorted(rows))
        for state, rows in ranks.items()
    }
    taunts = {}
    for row in _rows(assets / "taunts.tsv"):
        taunts.setdefault(int(row["state"]), []).append(
            (int(row["sequence"]), row["target"], row["text"])
        )
    CONWAY_TAUNTS = {
        state: tuple((target, text) for _, target, text in sorted(rows))
        for state, rows in taunts.items()
    }
    CONWAY_SEQUENCE = tuple(
        int(row["state"])
        for row in sorted(
            _rows(assets / "chatter-sequence.tsv"),
            key=lambda row: int(row["sequence"]),
        )
    )
    CONWAY_FACTIONS = tuple(
        row["name"]
        for row in sorted(
            _rows(assets / "factions.tsv"), key=lambda row: int(row["state"])
        )
    )


def cellular_visual(state_id, colour, opacity):
    if state_id in CONWAY_STATES:
        state_colour, factor = CONWAY_STATES[state_id]
        return state_colour, opacity * factor
    return colour, opacity


def draw_grid(
    pygame, screen, fonts, frame, scale, offset_x, offset_y, theme, cache
):
    """Draw and cache the checkerboard backdrop for the current Conway grid"""
    # Cache the board because dimensions and theme alone affect this background
    board = (
        round(offset_x),
        round(offset_y),
        round(frame.width * scale),
        round(frame.height * scale),
    )
    key = "cellular-grid", theme, board[2:], round(scale, 6)
    if key not in cache:
        base = pygame.Surface((board[2] + 24, board[3] + 28), pygame.SRCALPHA)
        for row in range(int(frame.height)):
            for column in range(int(frame.width)):
                multiplier = 0.85 if (row + column) % 2 else 1.0
                colour = tuple(
                    round(value * multiplier) for value in theme.ground
                )
                pygame.draw.rect(
                    base,
                    (*colour, 132),
                    (
                        round(column * scale),
                        round(row * scale),
                        math.ceil(scale),
                        math.ceil(scale),
                    ),
                )
        pygame.draw.rect(base, theme.ink, (0, 0, *board[2:]), 3)
        cache[key] = base
    screen.blit(cache[key], board[:2])


def draw_cell(pygame, screen, fonts, frame, entity, x, y, centre, scale):
    """Draw one Conway state cell and label the central Nano faction cell"""
    colour, opacity = cellular_visual(
        entity.state_id, entity.colour, entity.opacity
    )
    inset = max(1, round(scale * 0.08))
    rectangle = pygame.Rect(
        round(x - scale / 2.0) + inset,
        round(y - scale / 2.0) + inset,
        max(1, round(scale) - 2 * inset),
        max(1, round(scale) - 2 * inset),
    )
    if opacity == 1.0:
        pygame.draw.rect(screen, colour, rectangle, border_radius=max(1, inset))
    else:
        layer = pygame.Surface(rectangle.size, pygame.SRCALPHA)
        layer.fill((*colour, round(opacity * 255.0)))
        screen.blit(layer, rectangle.topleft)
    if (
        entity.state_id == 3
        and int(entity.x) == int(frame.width) // 2
        and int(entity.y) == int(frame.height) // 2
    ):
        draw_text(
            pygame,
            screen,
            fonts,
            "NANO",
            (centre[0], rectangle.top - 5),
            18,
            (255, 232, 92),
            1.0,
            True,
            "midbottom",
        )


def draw_scene_chrome(pygame, screen, fonts, frame, camera, position, theme):
    """Draw territory shares and rolling faction chatter over the board"""
    # Build the territory bar before drawing the time-based message window
    width, height = screen.get_size()
    vim, emacs, nano, vim_percent, emacs_percent, nano_percent = (
        conway_territory(frame)
    )
    counts = vim, emacs, nano
    percents = vim_percent, emacs_percent, nano_percent
    colours = tuple(CONWAY_STATES[state][0] for state in (1, 2, 3))
    panel = pygame.Surface((820, 260), pygame.SRCALPHA)
    panel.fill((11, 16, 18, 224))
    pygame.draw.rect(
        panel, (210, 218, 211), panel.get_rect(), 2, border_radius=10
    )
    bar = pygame.Rect(24, 48, 772, 28)
    total = sum(counts)
    widths = (
        [round(bar.width * count / total) for count in counts]
        if total
        else [0, 0, bar.width]
    )
    if total:
        widths[max(range(3), key=counts.__getitem__)] += bar.width - sum(widths)
    fill = pygame.Surface(bar.size, pygame.SRCALPHA)
    pygame.draw.rect(
        fill,
        colours[2] if total else (55, 62, 64),
        fill.get_rect(),
        border_radius=10,
    )
    left = 0
    for colour, segment_width in zip(colours[:2], widths[:2]):
        fill.set_clip(pygame.Rect(left, 0, segment_width, bar.height))
        pygame.draw.rect(fill, colour, fill.get_rect(), border_radius=10)
        left += segment_width
    fill.set_clip(None)
    panel.blit(fill, bar)
    pygame.draw.rect(panel, (235, 248, 241), bar, 2, border_radius=10)
    left = bar.left
    for name, count, percent, colour, segment_width in zip(
        CONWAY_FACTIONS, counts, percents, colours, widths
    ):
        if count:
            draw_text(
                pygame,
                panel,
                fonts,
                f"{name} {percent}%",
                (left + segment_width // 2, bar.top - 5),
                conway_label_size(percent),
                colour,
                1.0,
                True,
                "midbottom",
            )
        left += segment_width
    # Clip chatter so older rows can scroll upward without spilling
    chat = pygame.Rect(16, 90, 788, 154)
    pygame.draw.rect(panel, (11, 16, 18, 218), chat, border_radius=7)
    pygame.draw.rect(panel, (93, 108, 109), chat, 1, border_radius=7)
    draw_text(
        pygame,
        panel,
        fonts,
        "FRONTLINE COMMS",
        (28, 100),
        16,
        (210, 218, 211),
        1.0,
        True,
        "topleft",
    )
    viewport = pygame.Rect(28, 126, 764, 108)
    panel.set_clip(viewport)
    scroll = min((frame.number % 36) / 6.0, 1.0) * 22
    for row, (state, line) in enumerate(conway_chatter(frame)):
        draw_text(
            pygame,
            panel,
            fonts,
            line,
            (viewport.left, viewport.top + row * 22 - scroll),
            14,
            colours[state - 1],
            1.0,
            True,
            "topleft",
        )
    panel.set_clip(None)
    screen.blit(
        panel,
        (
            width - panel.get_width() - 36,
            height - panel.get_height() - 36,
        ),
    )


def conway_territory(frame):
    """Count live faction cells and return shares totalling one hundred"""
    # Largest-remainder rounding preserves exactly one hundred percent
    counts = [
        sum(
            entity.shape == "cell" and entity.state_id == state
            for entity in frame.entities
        )
        for state in (1, 2, 3)
    ]
    total = sum(counts)
    if not total:
        return 0, 0, 0, 0, 0, 0
    exact = [100 * count / total for count in counts]
    shares = [
        math.floor(percent) if count else 0
        for count, percent in zip(counts, exact)
    ]
    order = sorted(
        range(3),
        key=lambda index: (exact[index] - shares[index], counts[index], -index),
        reverse=True,
    )
    for index in order[: 100 - sum(shares)]:
        shares[index] += 1
    for index, count in enumerate(counts):
        if count and not shares[index]:
            shares[index] = 1
            shares[max(range(3), key=shares.__getitem__)] -= 1
    return *counts, *shares


def conway_label_size(percent):
    return max(1, round(34 * percent / 50))


def conway_chatter(frame):
    """Build rolling faction messages for the current simulation frame"""
    live = {
        entity.state_id
        for entity in frame.entities
        if entity.shape == "cell" and entity.state_id in (1, 2, 3)
    }
    lines = []
    message = frame.number // 36
    for turn in range(message - 5, message + 1):
        cycle, slot = divmod(turn, len(CONWAY_SEQUENCE))
        state = CONWAY_SEQUENCE[slot]
        if state in live:
            occurrence = cycle if state == 3 else cycle * 2 + slot // 2
            target, taunt = CONWAY_TAUNTS[state][
                occurrence % len(CONWAY_TAUNTS[state])
            ]
            if target == "NANO" and 3 not in live:
                target, taunt = CONWAY_TAUNTS[state][0]
            rank = CONWAY_RANKS[state][occurrence % len(CONWAY_RANKS[state])]
            lines.append((state, f"{rank} > {target}: {taunt}"))
    return lines
