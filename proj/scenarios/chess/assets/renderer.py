"""Render the chess board, pieces, move text, and score panels

The bound generic API handles interpolation and primitives while tables define
pieces and players
Move text is inferred from neighbouring snapshots because the stream contains
board state rather than moves
"""

import bisect
import csv
from collections import Counter
from typing import Any

EXPORTS = (
    "CHESS_ROOT",
    "OAK_WOOD",
    "CHESS_GLYPHS",
    "CHESS_SIDES",
    "CHESS_PIECES",
    "chess_move_text",
    "chess_material",
    "chess_captures",
    "chess_last_turn_ms",
    "chess_total_time_us",
)

DARK_WOOD: Any = None
PAPER_TEXTURE: Any = None
image_surface: Any = None
load_asset_table: Any = None
draw_text: Any = None
entity_appearance: Any = None
frame_index: Any = None
optional_integer: Any = None
optional_number: Any = None
asset_icon: Any = None
font_for: Any = None
frosted_panel: Any = None
sample_frame: Any = None
CHESS_ROOT: Any = None
OAK_WOOD: Any = None
CHESS_GLYPHS: Any = frozenset()
CHESS_SIDES: Any = {}
CHESS_PIECES: Any = {}
CHESS_FILES: Any = {}
CHESS_MATERIAL: Any = ()
CHESS_PLAYERS: Any = ()


def bind(api, assets):
    """Bind helpers and load chess piece, material, and player tables"""
    globals().update(api)
    paths = load_asset_table(assets)
    globals()["CHESS_ROOT"] = paths["piece_root"]
    globals()["OAK_WOOD"] = paths["oak_wood"]
    with (assets / "pieces.tsv").open(encoding="utf-8", newline="") as handle:
        rows = tuple(csv.DictReader(handle, delimiter="\t"))
    globals()["CHESS_GLYPHS"] = frozenset(row["glyph"] for row in rows)
    CHESS_SIDES.update({row["glyph"][0]: row["side"] for row in rows})
    CHESS_PIECES.update({row["glyph"][1]: row["piece"] for row in rows})
    CHESS_FILES.update({row["glyph"]: row["file"] for row in rows})
    with (assets / "material.tsv").open(encoding="utf-8", newline="") as handle:
        globals()["CHESS_MATERIAL"] = tuple(
            (row["piece"], int(row["value"]), row["glyph"])
            for row in csv.DictReader(handle, delimiter="\t")
        )
    with (assets / "players.tsv").open(encoding="utf-8", newline="") as handle:
        globals()["CHESS_PLAYERS"] = tuple(
            row
            for row in sorted(
                csv.DictReader(handle, delimiter="\t"),
                key=lambda row: int(row["index"]),
            )
        )


def render_assets():
    return {OAK_WOOD, PAPER_TEXTURE, DARK_WOOD, *map(icon_asset, CHESS_GLYPHS)}


def icon_asset(glyph):
    file = CHESS_FILES.get(glyph)
    return CHESS_ROOT / file if file else None


def has_icon(glyph):
    return glyph in CHESS_GLYPHS


def icon_radius(entity, frame, scale, minimum, radius):
    return min(radius, max(minimum, round(scale * 0.46)))


def chess_move_text(frames, position, numbers=None):
    """Infer one readable move from board states on either side of position"""
    # Compare adjacent states because snapshots do not contain move records
    numbers = numbers or tuple(frame.number for frame in frames)
    index = min(len(frames) - 1, bisect.bisect_left(numbers, position))
    if index == 0 or frames[index].presentation.get("theme") != "chess":
        return ""
    before, after = frame_index(frames[index - 1]), frame_index(frames[index])
    changed = {
        cell
        for cell in before.keys() & after.keys()
        if entity_appearance(before[cell]) != entity_appearance(after[cell])
    }
    removed = [
        before[cell]
        for cell in sorted((before.keys() - after.keys()) | changed)
    ]
    added = [
        after[cell] for cell in sorted((after.keys() - before.keys()) | changed)
    ]
    if not added:
        return ""
    kings = [piece for piece in added if piece.name.endswith("_king")]
    moved = kings[0] if kings else added[0]
    side, piece = moved.name.split("_", 1)
    choices = [old for old in removed if old.name == moved.name]
    if not choices:
        choices = [old for old in removed if old.name == f"{side}_pawn"]
    if not choices:
        return ""
    old = min(
        # Promotions replace a pawn, so nearest removal identifies its start
        choices,
        key=lambda entity: abs(entity.x - moved.x) + abs(entity.y - moved.y),
    )

    def square_name(entity):
        return f"{chr(ord('a') + round(entity.x))}{8 - round(entity.y)}"

    if piece == "king" and abs(old.x - moved.x) == 2:
        flank = "kingside" if moved.x > old.x else "queenside"
        return (
            f"{side.title()} king {square_name(old)} - {square_name(moved)}"
            f" · castles {flank}"
        )
    captured = before.get(moved.entity_id)
    capture = " · capture" if captured and captured.name != moved.name else ""
    promotion = (
        " · promotes" if old.name.endswith("_pawn") and piece != "pawn" else ""
    )
    return (
        f"{side.title()} {piece.replace('_', ' ')} "
        f"{square_name(old)} - {square_name(moved)}{capture}{promotion}"
    )


def chess_material(frames, frame):
    """Return remaining material and captured pieces for both sides"""
    opening = Counter(entity.name for entity in frames[0].entities)
    current = Counter(entity.name for entity in frame.entities)
    result = []
    for side in ("white", "black"):
        remaining = 0
        captured = []
        for piece, value, glyph in CHESS_MATERIAL:
            name = f"{side}_{piece}"
            remaining += current[name] * value
            missing = max(0, opening[name] - current[name])
            if missing:
                captured.append(f"{missing}{glyph.upper()}")
        result.append((remaining, " ".join(captured) or "—"))
    return tuple(result)


def chess_captures(frames, frame):
    opening = Counter(entity.name for entity in frames[0].entities)
    current = Counter(entity.name for entity in frame.entities)
    return tuple(
        tuple(
            ("w" if side == "white" else "b") + glyph
            for piece, value, glyph in CHESS_MATERIAL
            for _ in range(
                max(
                    0,
                    opening[f"{side}_{piece}"] - current[f"{side}_{piece}"],
                )
            )
        )
        for side in ("white", "black")
    )


def chess_last_turn_ms(frames, position, numbers, player):
    """Return the selected player's last turn duration in milliseconds"""
    index = min(len(frames) - 1, bisect.bisect_left(numbers, position))
    for position_index in range(index, -1, -1):
        current = frames[position_index]
        if (current.number - 1) % 2 == player:
            return (
                optional_number(
                    current.presentation,
                    "turn_duration_us",
                    current.number,
                    0.0,
                )
                / 1000.0
            )
    return None


def chess_total_time_us(frames, position, numbers, player):
    """Accumulate the selected player's completed search time to this frame."""
    index = min(len(frames) - 1, bisect.bisect_left(numbers, position))
    return sum(
        optional_number(
            frame.presentation, "turn_duration_us", frame.number, 0.0
        )
        for frame in frames[: index + 1]
        if (frame.number - 1) % 2 == player
    )


def draw_turn_status(pygame, screen, fonts, frames, position, numbers, cache):
    """Draw clocks, material, captures, and the inferred latest chess move"""
    # Derive move and terminal state before selecting a compact or full layout
    frame = sample_frame(frames, position, numbers)
    content = chess_move_text(frames, position, numbers)
    move_side = ""
    result_text = frame.presentation.get("result", "")
    if result_text:
        result = optional_integer(frame.presentation, "result", frame.number, 0)
        content = (
            "Draw · terminal rule reached"
            if result == 0
            else CHESS_PLAYERS[result - 1]["win"]
            if 1 <= result <= len(CHESS_PLAYERS)
            else f"Result {result}"
        )
    elif content:
        move_side, _, content = content.partition(" ")
        move_side = move_side.lower()
    next_player = -1 if result_text else frame.number % 2
    captures = chess_captures(frames, frame)
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
    # Each panel combines clock, material, captures, and current-turn state
    for index, (player, x) in enumerate(
        zip(CHESS_PLAYERS, (28, width - panel_width - 28))
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
        draw_text(
            pygame,
            panel,
            fonts,
            player["side"],
            (22, 60),
            21,
            (186, 194, 201),
            1.0,
            True,
            "topleft",
        )
        pawn_box = pygame.Rect(20, 94, 86, 86)
        pygame.draw.rect(panel, (31, 35, 40), pawn_box, border_radius=8)
        pygame.draw.rect(panel, border, pawn_box, 2, border_radius=8)
        pawn = asset_icon(pygame, player["pawn"], (255, 255, 255), 68, cache)
        panel.blit(pawn, pawn.get_rect(center=pawn_box.center))
        last_ms = chess_last_turn_ms(frames, position, numbers, index)
        total_us = chess_total_time_us(frames, position, numbers, index)
        draw_text(
            pygame,
            panel,
            fonts,
            "LAST TURN",
            (124, 96),
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
            f"{last_ms:.1f} ms" if last_ms is not None else "—",
            (124, 117),
            27,
            (235, 239, 241),
            1.0,
            True,
            "topleft",
        )
        draw_text(
            pygame,
            panel,
            fonts,
            "TOTAL TIME",
            (124, 151),
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
            f"{total_us / 1000:.1f} ms",
            (124, 172),
            27,
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
            (20, 209),
            18,
            (171, 181, 190),
            1.0,
            True,
            "topleft",
        )
        taken = captures[1 - index]
        if not taken:
            draw_text(
                pygame,
                panel,
                fonts,
                "—",
                (22, 242),
                25,
                (171, 181, 190),
                1.0,
                False,
                "topleft",
            )
        for piece_index, glyph in enumerate(taken):
            column, row = piece_index % 8, piece_index // 8
            box = pygame.Rect(20 + column * 38, 238 + row * 38, 34, 34)
            pygame.draw.rect(panel, (31, 35, 40), box, border_radius=5)
            pygame.draw.rect(panel, (78, 86, 96), box, 1, border_radius=5)
            icon = asset_icon(pygame, glyph, (255, 255, 255), 27, cache)
            panel.blit(icon, icon.get_rect(center=box.center))
        turn_text = (
            "GAME OVER"
            if next_player < 0
            else "YOUR TURN"
            if index == next_player
            else "WAITING"
        )
        turn_strip = pygame.Rect(3, panel_height - 48, panel_width - 6, 45)
        active_turn = index == next_player
        glass_tint = (250, 205, 70, 68) if active_turn else (185, 200, 215, 84)
        glass_edge = (
            (255, 229, 139, 122) if active_turn else (230, 236, 242, 128)
        )
        pygame.draw.rect(panel, (0, 0, 0, 0), turn_strip, border_radius=7)
        pygame.draw.rect(panel, glass_tint, turn_strip, border_radius=7)
        pygame.draw.rect(panel, glass_edge, turn_strip, 1, border_radius=7)
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
        screen.blit(panel, (x, 84))
    status = font_for(pygame, fonts, 36, True).render(
        content or "Opening position", True, (250, 247, 237)
    )
    status_panel = pygame.Surface(
        (status.get_width() + 94, status.get_height() + 22), pygame.SRCALPHA
    )
    marker_index = (
        0
        if move_side == "white"
        else 1
        if move_side == "black"
        else max(0, next_player)
    )
    marker = ((246, 239, 218), (70, 55, 45))[marker_index]
    pygame.draw.circle(
        status_panel, marker, (29, status_panel.get_height() // 2), 13
    )
    pygame.draw.circle(
        status_panel,
        (235, 180, 73),
        (29, status_panel.get_height() // 2),
        13,
        2,
    )
    status_panel.blit(status, (73, 11))
    screen.blit(status_panel, status_panel.get_rect(midtop=(width // 2, 28)))


def draw_background(pygame, screen, theme, cache):
    """Draw the wood and paper background behind the chess board"""
    width, height = screen.get_size()
    carpet = pygame.transform.smoothscale(
        image_surface(pygame, PAPER_TEXTURE, cache), (width, height)
    ).copy()
    carpet.fill((177, 116, 132), special_flags=pygame.BLEND_RGB_MULT)
    for index in range(width * height // 480):
        x = (index * 73 + index * index * 17) % width
        y = (index * 151 + index * index * 29) % height
        shade = (144, 91, 106) if index % 4 else (196, 142, 154)
        pygame.draw.line(
            carpet,
            shade,
            (x, y),
            (x + index % 3 - 1, y + 2 + index % 2),
        )
    screen.blit(carpet, (0, 0))
    table = pygame.Rect(
        round(width * 0.09), -48, round(width * 0.82), height + 96
    )
    pygame.draw.rect(screen, (70, 28, 27), table.move(18, 20), border_radius=26)
    veneer = pygame.transform.smoothscale(
        image_surface(pygame, DARK_WOOD, cache), table.size
    ).copy()
    glaze = pygame.Surface(table.size, pygame.SRCALPHA)
    glaze.fill((188, 137, 78, 44))
    veneer.blit(glaze, (0, 0))
    mask = pygame.Surface(table.size, pygame.SRCALPHA)
    pygame.draw.rect(
        mask, (255, 255, 255, 255), mask.get_rect(), border_radius=26
    )
    veneer.blit(mask, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
    screen.blit(veneer, table.topleft)
    pygame.draw.line(screen, (230, 176, 111), table.topleft, table.topright, 3)
    pygame.draw.rect(screen, (119, 70, 39), table, 4, border_radius=26)


def draw_grid(
    pygame, screen, fonts, frame, scale, offset_x, offset_y, theme, cache
):
    """Draw the chess board, coordinate labels, and square styling"""
    # Paint board squares first, then add labels and decorations above the grid
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
    border = max(28, min(42, round(scale * 0.38)))
    rail = pygame.Rect(board).inflate(border * 2, border * 2)
    depth = max(14, min(24, round(scale * 0.22)))
    pygame.draw.rect(
        screen, (28, 12, 10), rail.move(8, depth + 8), border_radius=10
    )
    pygame.draw.rect(
        screen, (44, 20, 17), rail.move(0, depth), border_radius=10
    )
    rail_key = "chess-oak-rail", rail.size
    if rail_key not in cache:
        source = pygame.transform.rotate(
            image_surface(pygame, OAK_WOOD, cache), 90
        )
        oak = pygame.transform.smoothscale(source, rail.size).copy()
        glaze = pygame.Surface(rail.size, pygame.SRCALPHA)
        glaze.fill((238, 177, 86, 104))
        oak.blit(glaze, (0, 0))
        mask = pygame.Surface(rail.size, pygame.SRCALPHA)
        pygame.draw.rect(
            mask, (255, 255, 255, 255), mask.get_rect(), border_radius=10
        )
        oak.blit(mask, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
        cache[rail_key] = oak
    screen.blit(cache[rail_key], rail.topleft)
    pygame.draw.rect(screen, (209, 157, 91), rail, 3, border_radius=10)
    key = "chess-board", board[2], board[3]
    if key not in cache:
        paper = pygame.transform.smoothscale(
            image_surface(pygame, PAPER_TEXTURE, cache), board[2:]
        ).copy()
        paper.fill((242, 226, 188), special_flags=pygame.BLEND_RGB_MULT)
        oak = pygame.transform.smoothscale(
            image_surface(pygame, OAK_WOOD, cache), board[2:]
        ).copy()
        glaze = pygame.Surface(board[2:], pygame.SRCALPHA)
        glaze.fill((229, 166, 77, 78))
        oak.blit(glaze, (0, 0))
        squares = pygame.Surface(board[2:])
        for row in range(8):
            for column in range(8):
                left = round(column * board[2] / 8)
                top = round(row * board[3] / 8)
                right = round((column + 1) * board[2] / 8)
                bottom = round((row + 1) * board[3] / 8)
                rectangle = pygame.Rect(left, top, right - left, bottom - top)
                texture = oak if (row + column) % 2 else paper
                squares.blit(texture, rectangle, rectangle)
        for index in range(9):
            line_x = round(index * board[2] / 8)
            line_y = round(index * board[3] / 8)
            pygame.draw.line(
                squares, (77, 47, 28), (line_x, 0), (line_x, board[3]), 2
            )
            pygame.draw.line(
                squares, (77, 47, 28), (0, line_y), (board[2], line_y), 2
            )
        cache[key] = squares
    screen.blit(cache[key], board[:2])
    label_size = max(20, min(32, round(scale * 0.28)))
    for index, file_name in enumerate("abcdefgh"):
        x = round(board[0] + (index + 0.5) * scale)
        for y in (board[1] - border // 2, board[1] + board[3] + border // 2):
            draw_text(
                pygame,
                screen,
                fonts,
                file_name,
                (x, y),
                label_size,
                (255, 241, 205),
                1.0,
                True,
                "center",
            )
    for index, rank in enumerate("87654321"):
        y = round(board[1] + (index + 0.5) * scale)
        for x in (board[0] - border // 2, board[0] + board[2] + border // 2):
            draw_text(
                pygame,
                screen,
                fonts,
                rank,
                (x, y),
                label_size,
                (255, 241, 205),
                1.0,
                True,
                "center",
            )
    pygame.draw.rect(screen, theme.ink, board, 3)
