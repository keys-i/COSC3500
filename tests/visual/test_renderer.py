# pyright: reportAttributeAccessIssue=false
"""Check visualiser parsing, geometry, assets, and scenario renderers

Small frames exercise parsing and drawing without opening a window
"""

import csv
import hashlib
import io
import math
import sys
import tempfile
import unittest
from dataclasses import replace
from itertools import pairwise
from pathlib import Path
from unittest.mock import patch

import pygame

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from proj import visualiser as visualise  # noqa: I001


SEA_LAND_CELL_DEGREES = 2
SEA_LAND_SAMPLE_DEGREES = 0.25


def _unwrap_ring(ring):
    """Shift a polygon ring so neighbouring longitudes stay close together"""
    # Keep adjacent points close before indexing polygons across the date line
    unwrapped = []
    for longitude, latitude in ring:
        if unwrapped:
            longitude = (
                unwrapped[-1][0]
                + (longitude - unwrapped[-1][0] + 180.0) % 360.0
                - 180.0
            )
        unwrapped.append((longitude, latitude))
    return tuple(unwrapped)


def _seir_land_index():
    """Build the coarse grid used to check rendered land ownership"""
    # A coarse grid narrows polygon candidates for repeated land checks
    polygons, cells = [], {}
    for _country, name, rings, _centre in visualise.seir_regions({}):
        for source_ring in rings:
            ring = _unwrap_ring(source_ring)
            minimum_x = min(point[0] for point in ring)
            maximum_x = max(point[0] for point in ring)
            minimum_y = min(point[1] for point in ring)
            maximum_y = max(point[1] for point in ring)
            for offset in (-360.0, 0.0, 360.0):
                left, right = minimum_x + offset, maximum_x + offset
                if right < -180.0 or left > 180.0:
                    continue
                shifted = tuple((x + offset, y) for x, y in ring)
                polygon = len(polygons)
                polygons.append(
                    (shifted, (left, minimum_y, right, maximum_y), name)
                )
                x_cells = range(
                    max(
                        0,
                        math.floor(
                            (max(-180.0, left) + 180.0) / SEA_LAND_CELL_DEGREES
                        ),
                    ),
                    min(
                        179,
                        math.floor(
                            (min(179.999, right) + 180.0)
                            / SEA_LAND_CELL_DEGREES
                        ),
                    )
                    + 1,
                )
                y_cells = range(
                    max(
                        0,
                        math.floor(
                            (max(-90.0, minimum_y) + 90.0)
                            / SEA_LAND_CELL_DEGREES
                        ),
                    ),
                    min(
                        89,
                        math.floor(
                            (min(89.999, maximum_y) + 90.0)
                            / SEA_LAND_CELL_DEGREES
                        ),
                    )
                    + 1,
                )
                for x_cell in x_cells:
                    for y_cell in y_cells:
                        cells.setdefault((x_cell, y_cell), []).append(polygon)
    return tuple(polygons), cells


def _point_in_ring(point, ring):
    """Classify a point with the standard ray-crossing polygon test"""
    # Ray casting toggles once for each boundary crossing to classify the point
    longitude, latitude = point
    inside = False
    for (left_x, left_y), (right_x, right_y) in pairwise(ring):
        if (left_y > latitude) != (
            right_y > latitude
        ) and longitude < left_x + (right_x - left_x) * (latitude - left_y) / (
            right_y - left_y
        ):
            inside = not inside
    return inside


def _seir_land_at(point, index):
    """Return the indexed region containing one longitude-latitude point"""
    polygons, cells = index
    longitude = (point[0] + 180.0) % 360.0 - 180.0
    latitude = point[1]
    cell = (
        min(
            179,
            max(
                0,
                math.floor((longitude + 180.0) / SEA_LAND_CELL_DEGREES),
            ),
        ),
        min(
            89,
            max(0, math.floor((latitude + 90.0) / SEA_LAND_CELL_DEGREES)),
        ),
    )
    for polygon in cells.get(cell, ()):
        ring, (left, bottom, right, top), name = polygons[polygon]
        if (
            left <= longitude <= right
            and bottom <= latitude <= top
            and _point_in_ring((longitude, latitude), ring)
        ):
            return name
    return None


def _seir_passage(point, passages):
    longitude, latitude = point
    return (
        (
            "suez" in passages
            and 31.8 <= longitude <= 34.0
            and 27.0 <= latitude <= 31.6
        )
        or (
            "bosporus" in passages
            and 25.5 <= longitude <= 29.5
            and 39.8 <= latitude <= 41.5
        )
        or (
            "gibraltar" in passages
            and -6.3 <= longitude <= -5.2
            and 35.5 <= latitude <= 36.3
        )
        or (
            "yenisei" in passages
            and 78.5 <= longitude <= 79.0
            and 72.6 <= latitude <= 73.0
        )
    )


class RendererContractTest(unittest.TestCase):
    def test_seir_region_layer_promotes_prepared_month_without_polygons(self):
        pygame.init()
        north = ("AAA", "North", (), (-20.0, 10.0))
        south = ("AAA", "South", (), (65.0, -12.0))
        parts = (
            (0, north, ((0, 0), (12, 0), (0, 12))),
            (1, south, ((12, 0), (24, 0), (24, 12))),
        )
        states = {
            "AAA": (0.02, 0.01, 0.0, 0.2, 0.77),
        }
        north_colour = visualise.seir_region_colour(
            visualise.seir_region_share(7, 0, north, states, 0.03),
            visualise.seir_region_vaccinated(7, 0, north, states),
        )
        south_colour = visualise.seir_region_colour(
            visualise.seir_region_share(7, 1, south, states, 0.03),
            visualise.seir_region_vaccinated(7, 1, south, states),
        )
        self.assertNotEqual(north_colour, south_colour)
        cache = {"seir-region-build-budget": len(parts)}
        rectangle = pygame.Rect(0, 0, 24, 12)
        _current, prepared, _promoted = visualise.seir_region_layer(
            pygame, parts, states, states, rectangle, 0, cache
        )
        target = prepared["surface"]
        with patch.object(
            pygame.draw, "polygon", wraps=pygame.draw.polygon
        ) as draw:
            current, _prepared, promoted = visualise.seir_region_layer(
                pygame, parts, states, None, rectangle, 1, cache
            )
        self.assertTrue(promoted)
        self.assertIs(current[2], target)
        draw.assert_not_called()

    def test_carrom_uses_the_supplied_board_unchanged(self):
        board = ROOT / "proj/scenarios/carrom/assets/carrom-board.jpg"
        self.assertEqual(
            hashlib.sha256(board.read_bytes()).hexdigest(),
            "c524e6ed97698e16e4c0258258829cee184c2ef213b34f8e39c18d21b72c947d",
        )

    def test_carrom_has_distinct_round_moon_and_sun_strikers(self):
        self.assertEqual(
            tuple(path.name for path in visualise.CARROM_STRIKERS),
            ("striker-moon-teal-navy.png", "striker-sun-red-yellow.png"),
        )
        for path in visualise.CARROM_STRIKERS:
            sprite = visualise.circular_entity_sprite(pygame, path, 70, {})
            self.assertEqual(sprite.get_at((0, 0)).a, 0)
            self.assertGreater(sprite.get_at(sprite.get_rect().center).a, 0)

    def test_chess_pack_is_flat_and_complete(self):
        assets = ROOT / "proj/scenarios/chess/assets"
        icons = [visualise.icon_asset(name) for name in visualise.CHESS_GLYPHS]
        self.assertNotIn(None, icons)
        names = (
            "chai-reference-top.png",
            "chess-board.png",
            *(icon.name for icon in icons if icon),
        )
        missing = [name for name in names if not (assets / name).is_file()]
        self.assertEqual(missing, [])
        _, cues = visualise.load_visual_plan(
            ROOT / "proj/scenarios/chess/scenario.sim"
        )
        chai = [cue for cue in cues if cue.asset and "chai" in cue.asset.name]
        effects = {cue.ambient: cue for cue in cues if cue.ambient}
        self.assertEqual(len(chai), 1)
        self.assertEqual(chai[0].x, 9.5)
        self.assertEqual(chai[0].width, 1.5)
        self.assertEqual(set(effects), {"bubbles", "steam"})
        self.assertTrue(all(cue.x == 9.5 for cue in effects.values()))
        self.assertTrue(all(cue.duration == 600 for cue in effects.values()))

    def test_chess_preserves_the_packs_piece_proportions(self):
        cache = {}
        king = visualise.icon_surface(pygame, "wk", (0, 0, 0), 24, cache)
        pawn = visualise.icon_surface(pygame, "wp", (0, 0, 0), 24, cache)
        self.assertGreater(
            king.get_bounding_rect().height,
            pawn.get_bounding_rect().height,
        )

    def test_scene_metadata_accepts_extended_visual_contract(self):
        _, conway = visualise.load_scene_meta(
            ROOT / "proj/scenarios/conway/scene.meta"
        )
        self.assertEqual(conway["terminal_hold_seconds"], 0)

    def test_scene_narration_is_visible_only(self):
        self.assertEqual(
            visualise.narration_error("Joe finds the open file and takes it."),
            "",
        )
        self.assertIn(
            "seed", visualise.narration_error("The seed chooses a move.")
        )

    def test_chess_hud_reports_structured_moves_and_each_players_last_time(
        self,
    ):
        before = visualise.Frame(
            0,
            8,
            8,
            "grid",
            [
                visualise.Entity(
                    52, 0, "white_pawn", 4, 6, "icon", (0, 0, 0), "wp", 0
                )
            ],
            {"theme": "chess", "turn_duration_us": "2750"},
        )
        after = visualise.Frame(
            1,
            8,
            8,
            "grid",
            [
                visualise.Entity(
                    36, 0, "white_pawn", 4, 4, "icon", (0, 0, 0), "wp", 0
                )
            ],
            {"theme": "chess", "turn_duration_us": "1500"},
        )
        frames, numbers = [before, after], (0, 1)
        self.assertEqual(
            visualise.chess_move_text(frames, 1, numbers),
            "White pawn e2 - e4",
        )
        self.assertEqual(
            visualise.chess_last_turn_ms(frames, 1, numbers, 0), 1.5
        )
        self.assertEqual(
            visualise.chess_last_turn_ms(frames, 1, numbers, 1), 2.75
        )
        self.assertEqual(
            visualise.chess_total_time_us(frames, 1, numbers, 0), 1500
        )
        self.assertEqual(
            visualise.chess_total_time_us(frames, 1, numbers, 1), 2750
        )

    def test_carrom_hud_reports_aicf_match_state(self):
        frame = visualise.Frame(
            24,
            16,
            16,
            "plane",
            [
                visualise.Entity(
                    1,
                    0,
                    "match_score",
                    8,
                    0.55,
                    "text",
                    (0, 0, 0),
                    "BOARD 3/8 · IVY 9 · NOAH 6 · IVY WHITE / NOAH BLACK · QUEEN COVERED IVY · IVY TO PLAY",
                    10,
                ),
                *[
                    visualise.Entity(
                        10 + index,
                        0,
                        f"white_{index}",
                        8,
                        8,
                        "sprite",
                        (255, 255, 255),
                        "",
                        4,
                    )
                    for index in range(1, 8)
                ],
                *[
                    visualise.Entity(
                        20 + index,
                        0,
                        f"black_{index}",
                        8,
                        8,
                        "sprite",
                        (0, 0, 0),
                        "",
                        4,
                    )
                    for index in range(1, 9)
                ],
            ],
            {"theme": "carrom"},
        )
        scores, active, sides, board, queen = visualise.carrom_hud(frame)
        self.assertEqual(
            (scores, active, sides, board, queen),
            ((9, 6), 0, ("WHITE", "BLACK"), "BOARD 3/8", "QUEEN COVERED IVY"),
        )
        self.assertEqual(
            visualise.carrom_taken(frame, sides, queen), ((2, 0, 1), (0, 1, 0))
        )

    def test_perspective_keeps_near_figures_larger(self):
        frame = visualise.Frame(
            0, 10.0, 10.0, "plane", [], {"projection": "perspective"}
        )
        far = visualise.transform(frame, (5.0, 5.0, 1.0), 5.0, 0.0, (320, 180))
        near = visualise.transform(
            frame, (5.0, 5.0, 1.0), 5.0, 10.0, (320, 180)
        )
        self.assertGreater(near[0], far[0])

    def test_conway_hud_uses_mixed_ancestry_and_free_land(self):
        with (ROOT / "proj/scenarios/conway/assets/factions.tsv").open(
            encoding="utf-8", newline=""
        ) as handle:
            self.assertEqual(
                tuple(
                    row["name"]
                    for row in csv.DictReader(handle, delimiter="\t")
                ),
                ("Vim Empire", "Emacs Union", "Nano"),
            )
        cells = [
            visualise.Entity(
                1, 0, "red_live", 2, 2, "cell", (0, 0, 0), "", 0, state_id=1
            ),
            visualise.Entity(
                2, 0, "blue_live", 3, 2, "cell", (0, 0, 0), "", 0, state_id=2
            ),
            visualise.Entity(
                3, 0, "blue_live", 8, 8, "cell", (0, 0, 0), "", 0, state_id=2
            ),
            visualise.Entity(
                4,
                0,
                "vim_emacs_heir",
                2,
                3,
                "cell",
                (0, 0, 0),
                "",
                0,
                state_id=4,
            ),
            visualise.Entity(
                5, 0, "nano", 5, 5, "cell", (0, 0, 0), "", 0, state_id=3
            ),
        ]
        frame = visualise.Frame(0, 10, 10, "grid", cells)
        vim, emacs, nano, vim_percent, emacs_percent, nano_percent = (
            visualise.conway_territory(frame)
        )
        self.assertEqual(
            (vim, emacs, nano, vim_percent, emacs_percent, nano_percent),
            (1.75, 2.25, 1.0, 35, 45, 20),
        )
        self.assertEqual(vim_percent + emacs_percent + nano_percent, 100)
        self.assertEqual(visualise.conway_realm_summary(frame), (5, 1, 95))
        final = replace(
            frame,
            entities=[cells[0]] * 1806 + [cells[1]] * 112 + [cells[3]] * 622,
        )
        self.assertEqual(visualise.conway_territory(final)[3:], (89, 11, 0))
        self.assertEqual(visualise.conway_label_size(50), 34)
        self.assertEqual(visualise.conway_label_size(25), 17)
        self.assertEqual(
            visualise.conway_territory(visualise.Frame(0, 1, 1, "grid", [])),
            (0, 0, 0, 0, 0, 0),
        )
        chatter = visualise.conway_chatter(replace(frame, number=144))
        self.assertEqual(len(chatter), 6)
        self.assertEqual(len({line for _, line in chatter}), 6)
        self.assertTrue(any(state == 3 for state, _ in chatter))
        self.assertTrue(any("> NANO:" in line for _, line in chatter))
        self.assertTrue(
            all("#" not in line and " · " not in line for _, line in chatter)
        )
        clustered = visualise.Frame(
            0,
            10,
            10,
            "grid",
            [
                visualise.Entity(
                    10, 0, "vim", 1, 1, "cell", (0, 0, 0), "", 0, state_id=1
                ),
                visualise.Entity(
                    4, 0, "vim", 2, 1, "cell", (0, 0, 0), "", 0, state_id=1
                ),
                visualise.Entity(
                    30, 0, "vim", 7, 7, "cell", (0, 0, 0), "", 0, state_id=1
                ),
                visualise.Entity(
                    20, 0, "vim", 8, 7, "cell", (0, 0, 0), "", 0, state_id=1
                ),
                visualise.Entity(
                    40, 0, "emacs", 4, 4, "cell", (0, 0, 0), "", 0, state_id=2
                ),
                visualise.Entity(
                    35, 0, "emacs", 5, 4, "cell", (0, 0, 0), "", 0, state_id=2
                ),
                visualise.Entity(
                    1, 0, "mixed", 1, 2, "cell", (0, 0, 0), "", 0, state_id=4
                ),
                visualise.Entity(
                    50, 0, "nano", 5, 5, "cell", (0, 0, 0), "", 0, state_id=3
                ),
            ],
        )
        first = visualise.conway_chatter(clustered)
        second = visualise.conway_chatter(replace(clustered, number=36))
        self.assertTrue(
            any(
                state == 1 and line.startswith("MARSHAL MODAL >")
                for state, line in first
            )
        )
        self.assertTrue(
            any(
                state == 2 and line.startswith("MAJOR MACRO >")
                for state, line in first
            )
        )
        self.assertTrue(
            any(
                state == 3 and line.startswith("CORPORAL CTRL-O >")
                for state, line in first
            )
        )
        self.assertEqual(first[1:], second[:5])
        without_nano = replace(
            clustered,
            number=144,
            entities=[
                entity for entity in clustered.entities if entity.state_id != 3
            ],
        )
        self.assertFalse(
            any(
                "> NANO:" in line
                for _, line in visualise.conway_chatter(without_nano)
            )
        )
        self.assertTrue(
            all(
                state != 3
                for state, _ in visualise.conway_chatter(without_nano)
            )
        )

    def test_seir_feed_has_global_coverage_without_duplicates(self):
        recent_feed = visualise.seir_feed_events(62)
        self.assertEqual(len(recent_feed), 8)
        self.assertTrue(any(event[1] == "DISASTER" for event in recent_feed))
        self.assertEqual(
            len({event[2] or event[3] for event in recent_feed}), 8
        )
        codes = {event[2] for event in visualise.SEIR_FEED}
        for region in (
            {"CI", "EG", "KE", "NG", "SN", "ZA"},
            {"RU", "UA"},
            {"AR", "BR", "CL", "CO", "MX", "PE"},
            {"ID", "MY", "PH", "SG", "TH"},
            {"AQ", "ARC"},
            {"BD", "BT", "IN", "LK", "MV", "NP", "PK"},
            {"EU", "ES"},
            {"AE", "IL", "IR", "IQ", "JO", "LB", "OM", "QA", "SA"},
        ):
            self.assertTrue(codes & region)
        labels = {event[3] for event in visualise.SEIR_FEED}
        self.assertTrue(
            {"HORMUZ SHIPPING", "RUSSIAN AIRSPACE", "UKRAINE AIRSPACE"}
            <= labels
        )
        selected = {
            event[1]
            for position in (19, 32, 46, 53, 56, 61, 62)
            for event in visualise.seir_feed_events(position)
        }
        self.assertTrue(
            {"BUSINESS", "DISASTER", "FUNNY", "SPORTS", "TECH"} <= selected
        )
        self.assertEqual(
            visualise.seir_feed_events(0),
            ((0.0, "NEWS", "JP", "JAPAN", "G20 leaders meet in Osaka."),),
        )
        self.assertEqual(len(visualise.SEIR_DISASTERS), 10)
        self.assertEqual(
            {disaster[3] for disaster in visualise.SEIR_DISASTERS},
            {"EARTHQUAKE", "ERUPTION", "FLOOD", "WILDFIRE"},
        )
        self.assertEqual(
            {storm[4] for storm in visualise.SEIR_STORMS},
            {"DORIAN", "FREDDY", "HAGIBIS", "MOCHA", "OTIS"},
        )

    def test_seir_transport_has_source_ids_and_water_routed_corridors(self):
        assets = ROOT / "proj/scenarios/chronus/assets"

        def records(name):
            with (assets / name).open(encoding="utf-8", newline="") as handle:
                return tuple(csv.DictReader(handle, delimiter="\t"))

        airports = records("airports.tsv")
        flights = records("flight-routes.tsv")
        ports = records("ports.tsv")
        corridors = records("ship-routes.tsv")
        airport_by_code = {row["code"]: row for row in airports}
        route_countries = {
            airport_by_code[code]["country"]
            for row in flights
            for code in (row["source"], row["destination"])
        }
        for region in (
            {"EGY", "KEN", "NGA", "ZAF"},
            {"RUS"},
            {"ARG", "BRA", "CHL", "COL", "PER"},
            {"IDN", "MYS", "PHL", "SGP", "THA"},
        ):
            self.assertTrue(route_countries & region)
        self.assertEqual(len({row["ourairports_id"] for row in airports}), 300)
        self.assertTrue(
            all(row["ourairports_id"].isdigit() for row in airports)
        )
        self.assertTrue(all(row["ourairports_ident"] for row in airports))
        self.assertEqual(
            {row["source_dataset"] for row in flights},
            {"OpenSky-2020-01", "OpenFlights-2014"},
        )
        self.assertEqual(
            visualise.SEIR_ROUTE_OPERATORS,
            tuple(row["openflights_airline"] for row in flights),
        )
        observed = [
            row for row in flights if row["source_dataset"] == "OpenSky-2020-01"
        ]
        self.assertGreaterEqual(len(observed), 400)
        self.assertTrue(
            all(int(row["observed_flights"]) > 0 for row in observed)
        )
        self.assertTrue(all(row["firstseen_utc"] for row in observed))
        self.assertTrue(all(row["operator_icao"] for row in observed))
        self.assertTrue(
            all(0.0 < float(row["frequency"]) <= 1.0 for row in flights)
        )
        self.assertTrue(
            all(
                row["source_ident"]
                == airport_by_code[row["source"]]["ourairports_ident"]
                and row["destination_ident"]
                == airport_by_code[row["destination"]]["ourairports_ident"]
                for row in flights
            )
        )
        self.assertEqual(
            set(airport_by_code),
            {
                code
                for row in flights
                for code in (row["source"], row["destination"])
            },
        )
        self.assertEqual({row["source"] for row in ports}, {"WPI", "UNLOCODE"})
        self.assertEqual(
            len({(row["source"], row["source_id"]) for row in ports}), 250
        )
        self.assertTrue(
            all(
                row["path_model"] == "SeaRoute 1.6.0 Marnet"
                for row in corridors
            )
        )
        self.assertTrue(all(float(row["length_km"]) > 0 for row in corridors))
        self.assertEqual(
            {row["ais_band"] for row in corridors},
            {"sparse", "normal", "busy", "modeled"},
        )
        observed = [row for row in corridors if row["ais_band"] != "modeled"]
        modeled = [row for row in corridors if row["ais_band"] == "modeled"]
        self.assertEqual((len(observed), len(modeled)), (150, 150))
        self.assertTrue(all(int(row["ais_samples"]) > 0 for row in observed))
        self.assertTrue(
            all(
                int(row["ais_samples"]) == 0
                and float(row["ais_log_mean"]) == 0.0
                and float(row["ais_density"]) == 0.15
                for row in modeled
            )
        )
        self.assertTrue(
            all(0.0 <= float(row["ais_density"]) <= 1.0 for row in corridors)
        )
        ship_pairs = {(row["source"], row["destination"]) for row in corridors}
        self.assertTrue(
            {("REK", "TOS"), ("HBA", "MCM"), ("CPT", "MCM"), ("USH", "MCM")}
            <= ship_pairs
        )

        passages = {
            (row["source"], row["destination"]): frozenset(
                filter(None, row["passages"].split(","))
            )
            for row in corridors
        }
        land_index = _seir_land_index()

        def land_hits(path, route_passages=frozenset()):
            hits = []
            # Endpoints connect docks to the sea graph
            for start, end in pairwise(path[1:-1]):
                longitude_a, latitude_a = map(math.radians, start)
                longitude_b, latitude_b = map(math.radians, end)
                angle = math.acos(
                    max(
                        -1.0,
                        min(
                            1.0,
                            math.sin(latitude_a) * math.sin(latitude_b)
                            + math.cos(latitude_a)
                            * math.cos(latitude_b)
                            * math.cos(longitude_b - longitude_a),
                        ),
                    )
                )
                steps = max(
                    1,
                    math.ceil(angle / math.radians(SEA_LAND_SAMPLE_DEGREES)),
                )
                for sample in range(1, steps):
                    point = visualise.seir_route_point(
                        start, end, sample / steps
                    )
                    if any(
                        math.hypot(
                            ((point[0] - endpoint[0] + 180.0) % 360.0 - 180.0)
                            * math.cos(math.radians(point[1])),
                            point[1] - endpoint[1],
                        )
                        <= 0.35
                        for endpoint in (path[0], path[-1])
                    ):
                        continue
                    if (
                        land := _seir_land_at(point, land_index)
                    ) and not _seir_passage(point, route_passages):
                        hits.append(
                            (round(point[0], 2), round(point[1], 2), land)
                        )
                        break
            return hits

        failures = {}
        for route, path in visualise.SEIR_SHIP_PATHS.items():
            if hits := land_hits(path, passages[route]):
                failures[f"{route[0]}->{route[1]}"] = hits
        for route, path in visualise.SEIR_SHIP_DETOURS.items():
            if hits := land_hits(path):
                failures[f"detour {route[0]}->{route[1]}"] = hits
        self.assertEqual(failures, {})

    def test_seir_timeline_uses_population_states_without_a_grid_map(self):
        presentation, _ = visualise.load_visual_plan(
            ROOT / "proj/scenarios/chronus/scenario.sim"
        )
        self.assertEqual(presentation["duration_seconds"], "264")
        self.assertEqual(int(presentation["duration_seconds"]) / 88, 3)
        self.assertEqual(visualise.seir_date(0).isoformat(), "2019-06-01")
        self.assertEqual(visualise.seir_date(86).isoformat(), "2026-08-01")
        self.assertEqual(visualise.seir_date(87).isoformat(), "2026-09-01")
        self.assertGreaterEqual(len(visualise.seir_features({})), 170)
        regions = visualise.seir_regions({})
        self.assertGreaterEqual(len(regions), 4500)
        self.assertTrue(any(region[1] == "Hubei" for region in regions))
        self.assertEqual(
            visualise.seir_region_owner("PAK", "Azad Kashmir"), "IND"
        )
        self.assertEqual(visualise.seir_region_owner("KAS", "Kashmir"), "IND")
        self.assertEqual(
            visualise.seir_region_owner("PAK", "Northern Areas"), "IND"
        )
        self.assertEqual(
            {claim[0] for claim in visualise.seir_india_claims({})},
            {
                "Aksai Chin",
                "Shaksam Valley",
                "Gilgit-Baltistan",
                "Azad Kashmir",
                "Siachen Glacier",
            },
        )
        self.assertEqual(
            {route[3] for route in visualise.SEIR_ROUTES},
            {"international", "domestic", "cargo"},
        )
        self.assertEqual(len(visualise.SEIR_ROUTES), 1200)
        self.assertEqual(
            len(
                {
                    tuple(sorted((source, destination)))
                    for source, destination, _, _, _ in visualise.SEIR_ROUTES
                }
            ),
            len(visualise.SEIR_ROUTES),
        )
        self.assertEqual(
            {route[3] for route in visualise.SEIR_SHIP_ROUTES},
            {
                "cargo",
                "fuel",
                "bulk",
                "ferry",
                "cruise",
                "passenger",
                "ro-ro",
                "fishing",
                "research",
            },
        )
        self.assertEqual(len(visualise.SEIR_SHIP_ROUTES), 300)
        self.assertEqual(len(visualise.SEIR_HORMUZ_ROUTES), 8)
        self.assertTrue(
            visualise.SEIR_HORMUZ_ROUTES <= visualise.SEIR_SHIP_PATHS.keys()
        )
        self.assertEqual(
            len(
                {
                    tuple(sorted((source, destination)))
                    for source, destination, _, _, _ in visualise.SEIR_SHIP_ROUTES
                }
            ),
            len(visualise.SEIR_SHIP_ROUTES),
        )
        self.assertEqual(len(visualise.SEIR_AIRPORTS), 300)
        self.assertEqual(len(visualise.SEIR_PORTS), 250)
        self.assertEqual(
            {operation[-1] for operation in visualise.SEIR_OPERATIONS},
            {"BANNED", "OPEN", "REROUTED", "RESTRICTED"},
        )
        operations = {
            operation[4]: operation for operation in visualise.SEIR_OPERATIONS
        }
        self.assertEqual(
            operations["UKRAINE CIVIL AIRSPACE CLOSED"][:2], (32.0, 88.0)
        )
        self.assertEqual(
            operations["HORMUZ SAFE PASSAGE HALTED"][:2], (81.0, 88.0)
        )
        self.assertFalse(
            {
                endpoint
                for source, destination, _, _, _ in visualise.SEIR_ROUTES
                for endpoint in (source, destination)
            }
            - visualise.SEIR_AIRPORTS.keys()
        )
        self.assertEqual(
            {
                endpoint
                for source, destination, _, _, _ in visualise.SEIR_SHIP_ROUTES
                for endpoint in (source, destination)
            },
            set(visualise.SEIR_PORTS),
        )
        for service in ("passenger", "cargo"):
            airlines = visualise.SEIR_AIRLINES[service]
            self.assertEqual(
                {airline[3] for airline in airlines if airline[3]},
                {1, 2, 3, 4, 5},
            )
        self.assertEqual(visualise.SEIR_AIRLINES_BY_CODE["ET"][2], "ETH")
        rectangle = pygame.Rect(0, 0, 1200, 600)
        self.assertEqual(
            visualise.seir_project(-180, 90, rectangle), rectangle.topleft
        )
        self.assertEqual(
            visualise.seir_project(180, -90, rectangle), rectangle.bottomright
        )
        route_cache = {}
        route_surface = pygame.Surface(rectangle.size, pygame.SRCALPHA)
        visualise.draw_seir_transport_routes(
            pygame, route_surface, rectangle, 20, route_cache
        )
        route_layer = route_cache["seir-transport-route-layer", rectangle.size]
        detour_layer = route_cache[
            "seir-transport-detour-layer", rectangle.size
        ]
        self.assertGreater(
            pygame.mask.from_surface(
                route_cache["seir-transport-war-layer", rectangle.size]
            ).count(),
            0,
        )
        self.assertGreater(
            pygame.mask.from_surface(
                route_cache["seir-transport-gulf-layer", rectangle.size]
            ).count(),
            0,
        )
        visualise.draw_seir_transport_routes(
            pygame, route_surface, rectangle, 54, route_cache
        )
        self.assertIs(
            route_cache["seir-transport-route-layer", rectangle.size],
            route_layer,
        )
        self.assertIs(
            route_cache["seir-transport-detour-layer", rectangle.size],
            detour_layer,
        )
        vaccinated = visualise.seir_region_colour(0.0, 0.8)
        self.assertGreater(vaccinated[1], vaccinated[0])
        self.assertGreater(vaccinated[1], vaccinated[2])
        storm = visualise.seir_project_float(-77.4, 26.7, rectangle)
        self.assertEqual(
            visualise.seir_weather_offset(
                storm,
                visualise.seir_weather(2.9, rectangle),
                rectangle.width,
                2.9,
                7,
                5.0,
            ),
            storm,
        )
        self.assertNotEqual(
            visualise.seir_weather_offset(
                storm,
                visualise.seir_weather(3.25, rectangle),
                rectangle.width,
                3.25,
                7,
                5.0,
            ),
            storm,
        )
        self.assertEqual(
            set(visualise.SEIR_SHIP_PATHS),
            {(route[0], route[1]) for route in visualise.SEIR_SHIP_ROUTES},
        )
        self.assertTrue(
            all(
                path[0] == visualise.SEIR_PORTS[source]
                and path[-1] == visualise.SEIR_PORTS[destination]
                for (
                    source,
                    destination,
                ), path in visualise.SEIR_SHIP_PATHS.items()
            )
        )
        ship_path = visualise.SEIR_SHIP_PATHS["CMB", "CPT"]
        detour = visualise.seir_ship_path("YTN", "RTM", 54)
        self.assertNotEqual(detour, visualise.seir_ship_path("YTN", "RTM", 53))
        self.assertIn(visualise.SEIR_PORTS["CPT"], detour)
        self.assertEqual(
            (detour[0], detour[-1]),
            (visualise.SEIR_PORTS["YTN"], visualise.SEIR_PORTS["RTM"]),
        )
        calm, _ = visualise.seir_path_pose(ship_path, 0.5, rectangle)
        rough, _ = visualise.seir_path_pose(ship_path, 0.5, rectangle, 1.0)
        self.assertGreater(math.dist(calm, rough), 1.0)
        for progress in (0.0, 1.0):
            calm, _ = visualise.seir_path_pose(ship_path, progress, rectangle)
            rough, _ = visualise.seir_path_pose(
                ship_path, progress, rectangle, 1.0
            )
            self.assertEqual(calm, rough)

        self.assertIn("INFLUENCER", {event[1] for event in visualise.SEIR_FEED})
        self.assertGreaterEqual(len(visualise.SEIR_FEED), 120)
        chicken = pygame.image.load(visualise.CHRONUS_CHICKEN)
        self.assertEqual(chicken.get_at((0, 0)).a, 0)
        shirt = chicken.get_at((627, 1000))
        self.assertGreater(shirt.r, 3 * shirt.b)
        angles = []
        for progress in range(5, 96):
            _, direction = visualise.seir_route_pose(
                visualise.SEIR_AIRPORTS["NRT"][:2],
                visualise.SEIR_AIRPORTS["LAX"][:2],
                progress / 100,
                rectangle,
            )
            angles.append(math.atan2(direction[1], direction[0]))
        turns = (
            abs((right - left + math.pi) % (2 * math.pi) - math.pi)
            for left, right in pairwise(angles)
        )
        self.assertLess(max(turns), 0.08)
        december = visualise.seir_calendar_values(6.9)
        january = visualise.seir_calendar_values(7.9)
        self.assertEqual((december[3], december[4] > 0), (2020, True))
        self.assertEqual((january[3], january[4]), (2020, 0.0))
        self.assertEqual(visualise.seir_calendar_values(6.83)[2], 0.0)
        entity = lambda entity_id, name, x, y=0, z=0.0: visualise.Entity(
            entity_id, 0, name, x, y, "circle", (0, 0, 0), "", 0, z=z
        )
        flight_count = len(visualise.SEIR_ROUTES)
        frame = visualise.Frame(
            0,
            101,
            101,
            "plane",
            [
                entity(0, "country_state_a", 1, 2, 3),
                entity(1, "country_state_a", 4, 5, 6),
                entity(2, "country_state_b", 7, 8),
                entity(3, "country_state_b", 9, 10),
                *(
                    entity(4 + index, "flight_state", 50, index, 0.9)
                    for index in range(flight_count)
                ),
                *(
                    entity(
                        4 + flight_count + index,
                        "ship_state",
                        25,
                        12,
                        0.5,
                    )
                    for index in range(len(visualise.SEIR_SHIP_ROUTES))
                ),
            ],
        )
        states, flights, ships = visualise.seir_snapshot(
            frame, (("AAA", 10, ()), ("BBB", 20, ()))
        )
        self.assertEqual(states["AAA"], (0.01, 0.02, 0.03, 0.07, 0.08))
        self.assertEqual(states["BBB"], (0.04, 0.05, 0.06, 0.09, 0.10))
        self.assertEqual(flights[0], (0.5, 0, 0.9))
        self.assertEqual(flights[-1], (0.5, flight_count - 1, 0.9))
        self.assertEqual(ships[0], (0.25, 12, -0.5))


ANGULAR_ACTIVE = visualise.ANGULAR_ACTIVE
STATE_FIELDS = visualise.STATE_FIELDS
WINDOW = visualise.WINDOW
EXPORT_SIZE = visualise.EXPORT_SIZE
Cue = visualise.Cue
Entity = visualise.Entity
Frame = visualise.Frame
RenderOptions = visualise.RenderOptions
active_cues = visualise.active_cues
auto_camera = visualise.auto_camera
camera_for = visualise.camera_for
canvas_size = visualise.canvas_size
caption_lines = visualise.caption_lines
cellular_visual = visualise.cellular_visual
chess_material = visualise.chess_material
chess_move_text = visualise.chess_move_text
conway_territory = visualise.conway_territory
conway_realm_summary = visualise.conway_realm_summary
conway_label_size = visualise.conway_label_size
cue_camera = visualise.cue_camera
cue_error = visualise.cue_error
derived_render_seed = visualise.derived_render_seed
draw_caption = visualise.draw_caption
draw_poster = visualise.draw_poster
drop_progress = visualise.drop_progress
figure_pose = visualise.figure_pose
font_for = visualise.font_for
icon_surface = visualise.icon_surface
interpolate_entity = visualise.interpolate_entity
load_visual_plan = visualise.load_visual_plan
narration_error = visualise.narration_error
outcome_cues = visualise.outcome_cues
output_directory = visualise.output_directory
parse_frames = visualise.parse_frames
preload_render_assets = visualise.preload_render_assets
present_canvas = visualise.present_canvas
presentation_duration = visualise.presentation_duration
presentation_flag = visualise.presentation_flag
presentation_options = visualise.presentation_options
presentation_position = visualise.presentation_position
presentation_seconds = visualise.presentation_seconds
render = visualise.render
reserve_output = visualise.reserve_output
sample_frame = visualise.sample_frame
smoothstep = visualise.smoothstep
stabilise_flight = visualise.stabilise_flight
theme_for = visualise.theme_for
transform = visualise.transform
validate_render_assets = visualise.validate_render_assets
wrapped_lerp = visualise.wrapped_lerp


def core_self_check():
    """Exercise generic parsing, interpolation, validation, and presentation"""
    assert smoothstep(0.0) == 0.0 and smoothstep(1.0) == 1.0
    assert visualise.export_progress(0, 4) == (
        "render [----------------------------]   0%"
    )
    assert visualise.export_progress(2, 4) == (
        "render [##############--------------]  50%"
    )
    assert visualise.export_progress(4, 4) == (
        "render [############################] 100%"
    )
    neutral = Frame(0, 1.0, 1.0, "plane", [], {"theme": "neutral"})
    chronus = replace(neutral, presentation={"theme": "chronus"})
    assert canvas_size(neutral) == WINDOW
    assert canvas_size(chronus) == EXPORT_SIZE
    assert cue_error("effect", "") == "effect needs text"
    assert cue_error("scene", "missing") == "invalid scene theme"
    _, chess_cues = load_visual_plan(
        ROOT / "proj" / "scenarios" / "chess" / "scenario.sim"
    )
    outcome_fixture = chess_cues + [
        replace(chess_cues[0], when_result=result) for result in ("0", "1", "2")
    ]
    for result in ("0", "1", "2"):
        selected = outcome_cues(outcome_fixture, result, 385)
        assert [cue.when_result for cue in selected if cue.when_result] == [
            result
        ]
        assert next(cue for cue in selected if cue.when_result).frame == 385
    for name in (
        "presentation",
        "poster-frame",
        "poster-asset",
        "focus-pair",
        "focus-radius",
        "focus-entity",
        "theme",
        "cue-scale",
        "backdrop",
        "scene-theme",
    ):
        try:
            load_visual_plan(
                ROOT
                / "tests"
                / "scenarios"
                / "fixtures"
                / "invalid"
                / f"{name}.sim"
            )
        except ValueError:
            continue
        raise AssertionError(f"{name} was accepted")
    assert abs(wrapped_lerp(99.0, 1.0, 0.5, 100.0)) < 0.001
    first = Frame(
        0,
        10.0,
        10.0,
        "plane",
        [Entity(1, 0, "one", 1.0, 1.0, "circle", (1, 2, 3), "", 0)],
    )
    try:
        theme_for(first)
    except ValueError:
        pass
    else:
        raise AssertionError("missing presentation theme was accepted")
    fields = sorted(STATE_FIELDS)
    base = {field: "" for field in fields}
    base.update(
        frame="0",
        record="frame",
        world_width="1",
        world_height="1",
        view="plane",
        theme="neutral",
        run_seed="0",
        render_seed=str(derived_render_seed(0)),
    )
    assert derived_render_seed(0) == 0xE220A8397B1DCDAF
    source = io.StringIO()
    writer = csv.DictWriter(source, fieldnames=fields)
    writer.writeheader()
    writer.writerows(
        (base, {**base, "record": "end"}, {**base, "record": "entity"})
    )
    source.seek(0)
    try:
        parse_frames(source)
    except ValueError:
        pass
    else:
        raise AssertionError("entity after frame end was accepted")
    entity = {
        **base,
        "record": "entity",
        "entity_id": "1",
        "type_id": "0",
        "type_name": "one",
        "x": "0",
        "y": "0",
        "shape": "circle",
        "layer": "0",
    }
    source = io.StringIO()
    writer = csv.DictWriter(source, fieldnames=fields)
    writer.writeheader()
    writer.writerows((base, entity, entity, {**base, "record": "end"}))
    source.seek(0)
    try:
        parse_frames(source)
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate entity_id was accepted")
    source = io.StringIO()
    writer = csv.DictWriter(source, fieldnames=fields)
    writer.writeheader()
    writer.writerows(
        (
            {**base, "render_seed": "0"},
            {**base, "record": "end", "render_seed": "0"},
        )
    )
    source.seek(0)
    try:
        parse_frames(source)
    except ValueError as error:
        assert "render_seed" in str(error)
    else:
        raise AssertionError("mismatched render_seed was accepted")
    last = Frame(
        10,
        10.0,
        10.0,
        "plane",
        [
            Entity(1, 0, "one", 3.0, 5.0, "circle", (1, 2, 3), "", 0),
            Entity(2, 1, "two", 2.0, 2.0, "circle", (1, 2, 3), "", 0),
        ],
    )
    middle = sample_frame([first, last], 5.0)
    assert len(middle.entities) == 2 and 1.9 < middle.entities[0].x < 2.1
    assert 0.45 < middle.entities[1].opacity < 0.55
    assert sample_frame([first, last], first.number) is first
    chess_first = Frame(
        0,
        8.0,
        8.0,
        "grid",
        [
            Entity(
                62,
                0,
                "white_knight",
                6.0,
                7.0,
                "icon",
                (1, 2, 3),
                "wn",
                0,
            )
        ],
        {"theme": "chess"},
    )
    chess_last = replace(
        chess_first,
        number=1,
        entities=[replace(chess_first.entities[0], entity_id=45, x=5.0, y=5.0)],
        entity_index={},
    )
    assert chess_move_text([chess_first, chess_last], 1.0) == (
        "White knight g1 - f3"
    )
    material = chess_material([chess_first, chess_last], chess_last)
    assert material == ((3, "—"), (0, "—"))
    assert cellular_visual("conway", 1, (1, 2, 3), 1.0) == (
        (225, 72, 78),
        1.0,
    )
    assert cellular_visual("conway", 3, (1, 2, 3), 1.0) == (
        (250, 204, 21),
        1.0,
    )
    assert cellular_visual("conway", 4, (1, 2, 3), 1.0) == ((185, 90, 117), 1.0)
    assert cellular_visual("conway", 6, (1, 2, 3), 1.0)[1] == 1.0
    assert cellular_visual("conway", 12, (1, 2, 3), 1.0)[1] == 1.0
    assert conway_label_size(50) == 34
    assert conway_label_size(25) == 17
    territory = Frame(
        0,
        10.0,
        10.0,
        "grid",
        [
            Entity(
                1, 0, "red_live", 2, 2, "cell", (0, 0, 0), "", 0, state_id=1
            ),
            Entity(
                2, 0, "blue_live", 3, 2, "cell", (0, 0, 0), "", 0, state_id=2
            ),
            Entity(
                3, 0, "red_live", 8, 8, "cell", (0, 0, 0), "", 0, state_id=1
            ),
            Entity(
                4,
                0,
                "vim_emacs_heir",
                2,
                3,
                "cell",
                (0, 0, 0),
                "",
                0,
                state_id=4,
            ),
        ],
    )
    assert conway_territory(territory) == (2.75, 1.25, 0.0, 69, 31, 0)
    assert conway_realm_summary(territory) == (4, 1, 96)
    assert conway_territory(replace(territory, entities=[])) == (
        0,
        0,
        0,
        0,
        0,
        0,
    )
    thrown = replace(last.entities[0], motion="ballistic")
    airborne = interpolate_entity(first.entities[0], thrown, 0.5, first)
    assert airborne.z > first.entities[0].z
    timeline = replace(first, presentation={"kernel": "timeline"})
    direct = interpolate_entity(first.entities[0], thrown, 0.5, timeline)
    assert direct.z == 0.0 and direct.x == 2.0 and direct.y == 3.0
    recorded = replace(thrown, z=2.0)
    midpoint = interpolate_entity(first.entities[0], recorded, 0.5, timeline)
    assert midpoint.z == 1.0
    flying = replace(
        first.entities[0], motion="flight", velocity_x=-0.01, velocity_y=100.0
    )
    flight_frame = replace(first, entities=[flying])
    stabilise_flight([flight_frame])
    assert abs(flight_frame.entities[0].rotation) <= 15.0
    drop_frame = replace(first, view="grid", presentation={"kernel": "turn"})
    dropped = interpolate_entity(None, thrown, 0.5, drop_frame)
    assert dropped.x == thrown.x and -1.0 < dropped.y < thrown.y
    assert dropped.z == 0.0
    assert drop_progress(0.82) < 1.0 and drop_progress(1.0) == 1.0
    labels = replace(first, presentation={"labels": "name"})
    hidden = replace(first, presentation={"labels": "none"})
    assert presentation_flag(labels, "labels", False)
    assert not presentation_flag(hidden, "labels", True)
    trail = replace(first, presentation={"trails": "12"})
    assert presentation_options(trail).trail_length == 12.0
    focused = replace(
        first, presentation={"focus_entity": "1", "focus_radius": "3"}
    )
    assert presentation_options(focused).focus_entity == 1
    try:
        presentation_options(replace(first, presentation={"focus_entity": "1"}))
    except ValueError:
        pass
    else:
        raise AssertionError("partial focus was accepted")
    assert presentation_duration(first) == 20.0
    held = replace(first, presentation={"duration_seconds": "20"})
    assert presentation_position(held, 17.0, 0.0, 10.0) == 10.0
    assert presentation_position(held, 8.0, 0.0, 10.0) < 10.0
    unheld = replace(
        first,
        presentation={"duration_seconds": "20", "terminal_hold_seconds": 0},
    )
    assert presentation_position(unheld, 19.0, 0.0, 10.0) < 10.0
    paced = replace(
        first,
        presentation={
            "duration_seconds": "6",
            "terminal_hold_seconds": 0,
            "pacing": "0:0,1:3,2:6",
        },
    )
    assert presentation_position(paced, 3.0, 0.0, 2.0) == 1.0
    assert presentation_seconds(paced, 1.0, 0.0, 2.0) == 3.0
    assert narration_error("The birds turn above the shoreline.") == ""
    assert "seed" in narration_error("The seed decides the next turn.")
    assert max(0.0, 1800.0) / 30.0 == 60.0
    close = replace(
        first,
        entities=[Entity(1, 0, "one", 4.0, 4.0, "circle", (1, 2, 3), "", 0)],
    )
    assert auto_camera(close)[2] > 1.0
    cell_first = Frame(
        0,
        2.0,
        2.0,
        "grid",
        [Entity(1, 0, "cell", 0.0, 0.0, "cell", (1, 2, 3), "", 0)],
        {"kernel": "cellular"},
    )
    cell_last = replace(
        cell_first,
        number=1,
        entities=[Entity(1, 0, "cell", 0.0, 0.0, "cell", (4, 5, 6), "", 0)],
    )
    assert sample_frame([cell_first, cell_last], 0.5).entities[0].colour == (
        1,
        2,
        3,
    )
    assert sample_frame([cell_first, cell_last], 1.0).entities[0].colour == (
        4,
        5,
        6,
    )
    first_camera = Cue(
        0,
        "camera",
        None,
        "",
        2.0,
        2.0,
        0.0,
        0.0,
        0.0,
        1.0,
        1.0,
        0.0,
        1.0,
        0,
        1.0,
    )
    next_camera = replace(
        first_camera, frame=6, x=6.0, y=4.0, scale=2.0, duration=3.0
    )
    held = camera_for(
        first,
        (first_camera, next_camera),
        4.0,
        RenderOptions(),
        (320, 180),
    )
    moving = camera_for(
        first,
        (first_camera, next_camera),
        7.5,
        RenderOptions(),
        (320, 180),
    )
    assert held == (2.0, 2.0, 1.0) and 2.0 < moving[0] < 6.0
    assert camera_for(
        first,
        (first_camera, next_camera),
        7.5,
        RenderOptions(reduced_motion=True),
        (320, 180),
    ) == (5.0, 5.0, 1.0)
    overlapping = replace(next_camera, frame=8, x=10.0, duration=4.0)
    before_overlap = camera_for(
        first,
        (first_camera, next_camera),
        8.0,
        RenderOptions(),
        (320, 180),
    )
    at_overlap = camera_for(
        first,
        (first_camera, next_camera, overlapping),
        8.0,
        RenderOptions(),
        (320, 180),
    )
    assert at_overlap == before_overlap
    backdrop = replace(first_camera, kind="backdrop", duration=0.0)
    assert active_cues((backdrop,), 100.0) == [backdrop]
    expiring = replace(backdrop, duration=5.0)
    assert active_cues((expiring,), 4.9) == [expiring]
    assert active_cues((expiring,), 5.0) == []
    pan = 8.0, 2.0, 1.3
    assert cue_camera(first, pan, backdrop) == pan
    fractional = replace(backdrop, parallax=0.5)
    assert cue_camera(first, pan, fractional) == (6.5, 3.5, 1.3)
    framed = replace(first_camera, width=5.0, height=5.0)
    assert (
        camera_for(first, (framed,), 0.0, RenderOptions(), (320, 180))[2] == 2.0
    )
    opening = replace(framed, duration=10.0)
    assert camera_for(first, (opening,), 0.0, RenderOptions(), (320, 180)) == (
        2.0,
        2.0,
        2.0,
    )
    perspective = replace(first, presentation={"projection": "perspective"})
    far = transform(perspective, (5.0, 5.0, 1.0), 5.0, 0.0, (320, 180))
    near = transform(perspective, (5.0, 5.0, 1.0), 5.0, 10.0, (320, 180))
    assert far[2] < near[2] and far[0] < near[0]
    assert (
        transform(perspective, (5.0, 5.0, 2.0), 5.0, 10.0, (320, 180))[0]
        > near[0]
    )
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary) / "root"
        outside = Path(temporary) / "outside"
        root.mkdir()
        outside.mkdir()
        reserved = root / "reserved.mp4"
        inode = reserve_output(reserved)
        try:
            reserve_output(reserved)
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate export reservation was accepted")
        assert reserved.stat().st_ino == inode
        reserved.unlink()
        (root / "results").symlink_to(outside, target_is_directory=True)
        try:
            output_directory(root)
        except ValueError:
            pass
        else:
            raise AssertionError("symlinked output root was accepted")
    missing_icon = replace(
        first,
        entities=[replace(first.entities[0], shape="icon", glyph="missing")],
        presentation={"theme": "neutral"},
    )
    try:
        validate_render_assets([missing_icon])
    except ValueError:
        pass
    else:
        raise AssertionError("missing render asset was accepted")


def renderer_self_check(pygame):
    """Exercise scenario renderers with small in-memory frame fixtures"""
    assert EXPORT_SIZE == (2560, 1440)
    pygame.init()
    pygame.font.init()
    assert ANGULAR_ACTIVE.is_file()
    runner = Entity(
        1, 0, "runner", 1.0, 1.0, "circle", (20, 40, 80), "", 0, velocity_x=1.0
    )
    assert figure_pose(runner) == "run"
    assert figure_pose(replace(runner, pose="freeze")) == "freeze"
    screen, fonts = pygame.Surface((320, 180)), {}
    cases = (
        ("cellular", "conway", "grid", "cell", ""),
        ("turn", "chess", "grid", "icon", "wk"),
    )
    first = None
    for kernel, theme, view, shape, glyph in cases:
        first = Frame(
            0,
            8.0,
            6.0,
            view,
            [Entity(1, 0, "one", 2.0, 2.0, shape, (205, 92, 63), glyph, 0)],
            {"kernel": kernel, "theme": theme},
        )
        last = replace(
            first,
            number=1,
            entities=[replace(first.entities[0], x=5.0, y=3.0, velocity_x=0.5)],
        )
        cache = {}
        render(
            pygame,
            screen,
            fonts,
            [first, last],
            0.5,
            [],
            cache,
            RenderOptions(trails=True, trail_length=1.0),
            theme,
        )
        if theme == "conway":
            assert any(
                key[0] == "cellular-grid"
                for key in cache
                if isinstance(key, tuple)
            )
    assert first is not None
    icons = {}
    chess = icon_surface(pygame, "wk", (205, 92, 63), 24, icons)
    assert chess is icon_surface(pygame, "wk", (205, 92, 63), 24, icons)
    coin = icon_surface(pygame, "queen", (205, 92, 63), 24, icons)
    assert coin.get_bounding_rect().width >= coin.get_width() * 0.7
    try:
        icon_surface(pygame, "missing-glyph", (205, 92, 63), 12, icons)
    except ValueError as error:
        assert str(error) == "missing icon asset: missing-glyph"
    else:
        raise AssertionError("missing icon asset was accepted")
    content = "Every authored caption word survives the renderer"
    lines = caption_lines(font_for(pygame, fonts, 24), content, 100)
    assert " ".join(lines) == content and len(lines) > 1
    with tempfile.TemporaryDirectory() as temporary:
        asset = Path(temporary) / "foreground.bmp"
        foreground = pygame.Surface((4, 4))
        foreground.fill((251, 2, 3))
        pygame.image.save(foreground, asset)
        layered = Frame(
            0,
            10.0,
            10.0,
            "plane",
            [Entity(1, 0, "one", 5.0, 5.0, "circle", (2, 3, 251), "", 0)],
            {"kernel": "timeline", "theme": "neutral"},
        )
        foreground_cue = Cue(
            0,
            "backdrop",
            asset,
            "",
            5.0,
            5.0,
            2.0,
            2.0,
            0.0,
            1.0,
            1.0,
            0.0,
            1.0,
            1,
            1.0,
        )
        assets = {}
        preload_render_assets(pygame, [layered], [foreground_cue], assets)
        assert asset in assets
        render(
            pygame,
            screen,
            fonts,
            [layered],
            0.0,
            [foreground_cue],
            {},
            RenderOptions(),
            "",
        )
        assert screen.get_at((160, 90))[:3] == (251, 2, 3)
    poster = Cue(
        0,
        "poster",
        None,
        "Opening",
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
        1.0,
        0.0,
        1.0,
        0,
        1.0,
    )
    draw_poster(
        pygame,
        screen,
        fonts,
        first,
        poster,
        "Fallback",
        {},
        dict.fromkeys(("subtitle", "meme"), "x"),
    )
    assert screen.get_at((0, 0))[:3] != (0, 0, 0)
    canvas, window = pygame.Surface(WINDOW), pygame.Surface((320, 320))
    canvas.fill((7, 8, 9))
    present_canvas(pygame, window, canvas)
    assert window.get_at((160, 0))[:3] == (0, 0, 0)
    assert window.get_at((160, 160))[:3] != (0, 0, 0)
    dialogue = Cue(
        0,
        "dialogue",
        None,
        "Stay close",
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
        1.0,
        2.0,
        1.0,
        9,
        1.0,
        "one",
    )
    draw_caption(pygame, screen, fonts, dialogue, first, (5.0, 5.0, 1.0), 0.5)
    pygame.font.quit()
    pygame.quit()


class VisualiserChecks(unittest.TestCase):
    def test_core_validation(self):
        core_self_check()

    def test_renderer_paths(self):
        renderer_self_check(pygame)

    def test_renderer_dependencies(self):
        self.assertIsNotNone(visualise.shutil.which("ffmpeg"))
        for scenario in (
            "templates/chess",
            "templates/chronus",
            "templates/carrom",
            "templates/heston",
            "templates/conway",
            "test/cellular-host",
        ):
            visualise.resolve_scenario(scenario)
        self.assertEqual(
            visualise.scenario_source("test/cellular-host"),
            ROOT
            / "tests"
            / "scenarios"
            / "fixtures"
            / "cellular-host"
            / "scenario.sim",
        )
        pygame.init()
        try:
            with visualise.tempfile.TemporaryDirectory(
                prefix="m1-image-"
            ) as temp:
                image = Path(temp) / "one.bmp"
                pygame.image.save(pygame.Surface((1, 1)), image)
                self.assertEqual(
                    visualise.image_surface(pygame, image, {}).get_size(),
                    (1, 1),
                )
        finally:
            pygame.font.quit()
            pygame.quit()


if __name__ == "__main__":
    unittest.main()
