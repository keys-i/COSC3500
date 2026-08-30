"""Render the carrom board, pieces, and match score panels

The hidden match-score entity carries text state while visible entities are
coins on the board
TSV tables keep player names, icon glyphs, colours, and paths outside rendering
logic
"""

import csv
import math
from collections import Counter
from typing import Any

EXPORTS = (
    "CARROM_CARPET",
    "CARROM_TABLE",
    "CARROM_STRIKER_MOON",
    "CARROM_STRIKER_SUN",
    "CARROM_STRIKERS",
    "CARROM_WHITE",
    "CARROM_BLACK",
    "CARROM_QUEEN",
    "CARROM_GLYPHS",
    "carrom_hud",
    "carrom_taken",
)

image_surface: Any = None
load_asset_table: Any = None
entity_sprite: Any = None
circular_entity_sprite: Any = None
draw_text: Any = None
font_for: Any = None
frosted_panel: Any = None
optional_integer: Any = None
sample_frame: Any = None
CARROM_CARPET: Any = None
CARROM_TABLE: Any = None
CARROM_STRIKER_MOON: Any = None
CARROM_STRIKER_SUN: Any = None
CARROM_STRIKERS: Any = ()
CARROM_WHITE: Any = None
CARROM_BLACK: Any = None
CARROM_QUEEN: Any = None
CARROM_GLYPHS: Any = frozenset()
CARROM_COLOURS: Any = {}
CARROM_PLAYERS: Any = ()


def bind(api, assets):
    """Bind helpers and load carrom asset, glyph, and player tables"""
    globals().update(api)
    paths = load_asset_table(assets)
    globals().update(
        CARROM_CARPET=paths["carpet"],
        CARROM_TABLE=paths["table"],
        CARROM_STRIKER_MOON=paths["striker_moon"],
        CARROM_STRIKER_SUN=paths["striker_sun"],
        CARROM_WHITE=paths["white_coin"],
        CARROM_BLACK=paths["black_coin"],
        CARROM_QUEEN=paths["queen"],
    )
    globals()["CARROM_STRIKERS"] = (CARROM_STRIKER_MOON, CARROM_STRIKER_SUN)
    with (assets / "glyphs.tsv").open(encoding="utf-8", newline="") as handle:
        rows = tuple(csv.DictReader(handle, delimiter="\t"))
    globals()["CARROM_GLYPHS"] = frozenset(row["glyph"] for row in rows)
    CARROM_COLOURS.update(
        (row["glyph"], tuple(map(int, row["colour"].split(","))))
        for row in rows
    )
    with (assets / "players.tsv").open(encoding="utf-8", newline="") as handle:
        globals()["CARROM_PLAYERS"] = tuple(
            row
            for row in sorted(
                csv.DictReader(handle, delimiter="\t"),
                key=lambda row: int(row["index"]),
            )
        )


def render_assets():
    return {
        CARROM_CARPET,
        CARROM_TABLE,
        CARROM_WHITE,
        CARROM_BLACK,
        CARROM_QUEEN,
        *CARROM_STRIKERS,
    }


def has_icon(glyph):
    return glyph in CARROM_GLYPHS


def icon_surface(pygame, glyph, colour, radius, cache):
    if glyph not in CARROM_GLYPHS:
        return None
    key = "icon", glyph, colour, radius
    if key not in cache:
        side = radius * 2 + 12
        surface = pygame.Surface((side, side), pygame.SRCALPHA)
        centre = side // 2, side // 2
        pygame.draw.circle(surface, (40, 32, 25), centre, radius + 2)
        pygame.draw.circle(surface, CARROM_COLOURS[glyph], centre, radius)
        pygame.draw.circle(surface, (247, 236, 203), centre, radius, 2)
        cache[key] = surface
    return cache[key]


def circular_clip_ratio(path):
    return 0.46 if path == CARROM_STRIKER_SUN else 0.49


def hide_entity(entity):
    return entity.name == "match_score"


def sprite_path(frame, entity, path, position):
    if entity.name != "striker":
        return path, False
    active = carrom_hud(frame)[1]
    return CARROM_STRIKERS[active if active in {0, 1} else 0], True


def draw_icon_underlay(pygame, screen, entity, centre, radius):
    if entity.glyph == "striker":
        pygame.draw.circle(screen, (29, 46, 109), centre, radius + 5)
        pygame.draw.circle(screen, (247, 239, 215), centre, radius + 2)


def fixed_icon(glyph):
    return glyph in CARROM_GLYPHS


def carrom_hud(frame):
    """Decode hidden match score into score, turn, sides, and queen state"""
    # One hidden text entity carries match state in each snapshot
    status = next(
        (
            entity.glyph
            for entity in frame.entities
            if entity.name == "match_score"
        ),
        "",
    )
    parts = status.split(" · ")
    if len(parts) != 6:
        return (
            (0, 0),
            -1,
            ("COLOUR OPEN", "COLOUR OPEN"),
            "BOARD",
            "QUEEN OPEN",
        )
    try:
        scores = (
            int(parts[1].removeprefix("IVY ")),
            int(parts[2].removeprefix("NOAH ")),
        )
    except ValueError:
        scores = 0, 0
    sides = (
        ("WHITE", "BLACK")
        if parts[3] == "IVY WHITE / NOAH BLACK"
        else (
            ("BLACK", "WHITE")
            if parts[3] == "IVY BLACK / NOAH WHITE"
            else ("COLOUR OPEN", "COLOUR OPEN")
        )
    )
    active = (
        0
        if parts[5].startswith("IVY")
        else 1
        if parts[5].startswith("NOAH")
        else -1
    )
    return scores, active, sides, parts[0], parts[4]


def carrom_taken(frame, sides, queen):
    """Derive taken pieces from missing board coins and queen ownership"""
    # Coin counts come from remaining board entities instead of score text
    current = Counter(entity.name for entity in frame.entities)
    missing = {
        colour: max(
            0,
            9
            - sum(
                count
                for name, count in current.items()
                if name.startswith(f"{colour.lower()}_")
            ),
        )
        for colour in ("WHITE", "BLACK")
    }
    queen_owner = (
        0
        if queen == "QUEEN COVERED IVY"
        else 1
        if queen == "QUEEN COVERED NOAH"
        else -1
    )
    return tuple(
        (
            missing["WHITE"] if side == "WHITE" else 0,
            missing["BLACK"] if side == "BLACK" else 0,
            int(index == queen_owner),
        )
        for index, side in enumerate(sides)
    )


def draw_turn_status(pygame, screen, fonts, frames, position, numbers, cache):
    """Draw player score panels from the inferred current carrom match state"""
    # Decode the hidden match record before choosing compact or full HUD layout
    frame = sample_frame(frames, position, numbers)
    scores, next_player, sides, board, queen = carrom_hud(frame)
    taken = carrom_taken(frame, sides, queen)
    result_text = frame.presentation.get("result", "")
    if result_text:
        result = optional_integer(frame.presentation, "result", frame.number, 0)
        content = (
            "Draw"
            if result == 0
            else CARROM_PLAYERS[result - 1]["win"]
            if 1 <= result <= len(CARROM_PLAYERS)
            else f"Result {result}"
        )
        next_player = -1
    else:
        content = f"{board} · {queen}"
    width, height = screen.get_size()
    side = max(0, (width - height) // 2)
    panel_width = min(340, side - 54)
    if panel_width < 320:
        draw_text(
            pygame,
            screen,
            fonts,
            content,
            (24, 24),
            28,
            (250, 247, 237),
            1.0,
            True,
            "topleft",
        )
        return
    # Full layout mirrors the same derived state into one panel for each player
    for index, (player, x) in enumerate(
        zip(CARROM_PLAYERS, (28, width - panel_width - 28))
    ):
        panel_height = 370
        panel_rect = pygame.Rect(x, 84, panel_width, panel_height)
        panel = frosted_panel(pygame, screen, panel_rect)
        border = (235, 180, 73) if index == next_player else (93, 101, 112)
        pygame.draw.rect(panel, border, panel.get_rect(), 3, border_radius=10)
        draw_text(
            pygame,
            panel,
            fonts,
            player["label"],
            (20, 18),
            40,
            (250, 247, 237),
            1.0,
            True,
            "topleft",
        )
        coin_box = pygame.Rect(20, 72, 86, 86)
        pygame.draw.circle(panel, (31, 35, 40), coin_box.center, 43)
        pygame.draw.circle(panel, border, coin_box.center, 43, 2)
        striker = circular_entity_sprite(
            pygame,
            CARROM_STRIKERS[index],
            math.ceil(84 * 1.125),
            cache,
            circular_clip_ratio(CARROM_STRIKERS[index]),
        )
        phase = position * 0.09 + index * math.pi
        striker = pygame.transform.rotozoom(
            striker,
            10.0 * math.sin(phase),
            0.875 + 0.125 * math.cos(phase),
        )
        panel.blit(striker, striker.get_rect(center=coin_box.center))
        draw_text(
            pygame,
            panel,
            fonts,
            "MATCH SCORE",
            (124, 75),
            17,
            (171, 181, 190),
            1.0,
            True,
            "topleft",
        )
        draw_text(
            pygame,
            panel,
            fonts,
            f"{scores[index]} pts",
            (124, 98),
            29,
            (235, 239, 241),
            1.0,
            True,
            "topleft",
        )
        draw_text(
            pygame,
            panel,
            fonts,
            "PIECES TAKEN",
            (20, 207),
            18,
            (171, 181, 190),
            1.0,
            True,
            "topleft",
        )
        for row, (count, path) in enumerate(
            zip(taken[index], (CARROM_WHITE, CARROM_BLACK, CARROM_QUEEN))
        ):
            row_y = 234 + row * 29
            draw_text(
                pygame,
                panel,
                fonts,
                str(count),
                (24, row_y),
                25,
                (235, 239, 241),
                1.0,
                True,
                "topleft",
            )
            icon = entity_sprite(pygame, path, 25, cache)
            panel.blit(icon, icon.get_rect(center=(65, row_y + 13)))
        turn_text = (
            "GAME OVER"
            if next_player < 0
            else "YOUR TURN"
            if index == next_player
            else "WAITING"
        )
        turn_strip = pygame.Rect(3, panel_height - 48, panel_width - 6, 45)
        active_turn = index == next_player
        pygame.draw.rect(
            panel,
            (250, 205, 70, 68) if active_turn else (185, 200, 215, 84),
            turn_strip,
            border_radius=7,
        )
        pygame.draw.rect(
            panel,
            (255, 229, 139, 122) if active_turn else (230, 236, 242, 128),
            turn_strip,
            1,
            border_radius=7,
        )
        draw_text(
            pygame,
            panel,
            fonts,
            turn_text,
            (panel_width // 2, panel_height - 25),
            25,
            (250, 247, 237),
            1.0,
            True,
        )
        screen.blit(panel, panel_rect.topleft)
        tray_paths = (
            [CARROM_WHITE] * taken[index][0]
            + [CARROM_BLACK] * taken[index][1]
            + [CARROM_QUEEN] * taken[index][2]
        )
        tray_x = side + 16 if index == 0 else width - side - 16
        for piece_index, path in enumerate(tray_paths):
            icon = entity_sprite(pygame, path, 26, cache)
            centre = tray_x, 512 + piece_index * 25
            pygame.draw.circle(screen, (10, 14, 18, 120), centre, 15)
            screen.blit(icon, icon.get_rect(center=centre))
    status = font_for(pygame, fonts, 42, True).render(
        content or "Opening position", True, (250, 247, 237)
    )
    status_panel = pygame.Surface(
        (status.get_width() + 42, status.get_height() + 22), pygame.SRCALPHA
    )
    status_panel.fill((15, 18, 21, 218))
    status_panel.blit(status, (21, 11))
    screen.blit(status_panel, status_panel.get_rect(midtop=(width // 2, 28)))


def caption_width(max_width):
    return min(max_width, 260)


def caption_destination(lower, screen, inset):
    return lower.get_rect(topleft=(inset, 2 * inset))


def skip_asset_cue(cue):
    return cue.asset is not None and cue.asset.name == "coffee.svg"


def carrom_board_cue(cue):
    return cue.asset is not None and cue.asset.name == "carrom-board.jpg"


def before_asset_cue(pygame, screen, cue, surface, centre):
    """Prepare carrom board cue placement before the shared image draw step"""
    if not carrom_board_cue(cue):
        return
    rectangle = surface.get_rect(center=centre)
    shadow = pygame.Surface(screen.get_size(), pygame.SRCALPHA)
    for spread, offset, alpha in ((38, 18, 24), (22, 11, 42), (10, 6, 72)):
        pygame.draw.rect(
            shadow,
            (24, 11, 8, alpha),
            rectangle.inflate(spread, spread).move(offset, offset),
            border_radius=10,
        )
    screen.blit(shadow, (0, 0))
    pygame.draw.rect(
        screen,
        (83, 35, 24),
        rectangle.inflate(12, 12),
        6,
        border_radius=7,
    )
    pygame.draw.line(
        screen,
        (223, 145, 91),
        rectangle.inflate(4, 4).topleft,
        rectangle.inflate(4, 4).topright,
        2,
    )


def after_asset_cue(pygame, screen, cue, surface, centre):
    """Add carrom cue framing after the shared image draw step"""
    if not carrom_board_cue(cue):
        return
    rectangle = surface.get_rect(center=centre)
    polish = pygame.Surface(rectangle.size, pygame.SRCALPHA)
    width, height = rectangle.size
    rim = round(min(width, height) * 0.17)
    pygame.draw.rect(polish, (255, 232, 190, 27), (0, 0, width, rim))
    pygame.draw.rect(polish, (255, 226, 176, 22), (0, height - rim, width, rim))
    pygame.draw.rect(
        polish, (255, 235, 199, 24), (0, rim, rim, height - 2 * rim)
    )
    pygame.draw.rect(
        polish,
        (255, 221, 169, 18),
        (width - rim, rim, rim, height - 2 * rim),
    )
    pygame.draw.polygon(
        polish,
        (255, 253, 237, 48),
        (
            (round(width * 0.08), 0),
            (round(width * 0.34), 0),
            (round(width * 0.26), rim),
            (0, rim),
        ),
    )
    pygame.draw.polygon(
        polish,
        (255, 249, 225, 32),
        (
            (0, round(height * 0.30)),
            (rim, round(height * 0.18)),
            (rim, round(height * 0.44)),
            (0, round(height * 0.58)),
        ),
    )
    inner = pygame.Rect(rim, rim, width - 2 * rim, height - 2 * rim)
    pygame.draw.rect(polish, (255, 248, 219, 62), polish.get_rect(), 4)
    pygame.draw.rect(polish, (255, 244, 210, 44), inner, 3)
    pygame.draw.line(polish, (255, 255, 240, 92), (12, 10), (width - 14, 10), 4)
    pygame.draw.line(
        polish, (255, 252, 229, 58), (10, 12), (10, height - 14), 3
    )
    pygame.draw.line(
        polish,
        (53, 22, 14, 52),
        (14, height - 9),
        (width - 10, height - 9),
        4,
    )
    screen.blit(polish, rectangle.topleft)


def draw_background(pygame, screen, theme, cache):
    """Draw the carrom surface and surrounding table background"""
    width, height = screen.get_size()
    carpet = image_surface(pygame, CARROM_CARPET, cache)
    factor = max(width / carpet.get_width(), height / carpet.get_height())
    carpet = pygame.transform.smoothscale(
        carpet,
        (
            round(carpet.get_width() * factor),
            round(carpet.get_height() * factor),
        ),
    )
    screen.blit(
        carpet,
        (
            (width - carpet.get_width()) // 2,
            (height - carpet.get_height()) // 2,
        ),
    )
    table = pygame.Rect(
        round(width * 0.09), -48, round(width * 0.82), height + 96
    )
    pygame.draw.rect(screen, (48, 21, 18), table.move(18, 20), border_radius=26)
    veneer = pygame.transform.smoothscale(
        image_surface(pygame, CARROM_TABLE, cache), table.size
    ).copy()
    mask = pygame.Surface(table.size, pygame.SRCALPHA)
    pygame.draw.rect(
        mask, (255, 255, 255, 255), mask.get_rect(), border_radius=26
    )
    veneer.blit(mask, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
    screen.blit(veneer, table.topleft)
    pygame.draw.line(screen, (207, 121, 78), table.topleft, table.topright, 3)
    pygame.draw.rect(screen, (92, 42, 29), table, 4, border_radius=26)
