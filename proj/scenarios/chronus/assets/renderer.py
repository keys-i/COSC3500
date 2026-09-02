"""Render Chronus maps, transport movement, health data, and news panels

This theme reads static GeoJSON and TSV assets, then combines them with
ordered snapshot entities
Geographic helpers keep routes continuous across the date line and cache
immutable map geometry per render
"""

import csv
import itertools
import json
import math
from bisect import bisect_right
from datetime import date
from functools import cache
from typing import Any

caption_lines: Any = None
draw_text: Any = None
entity_sprite: Any = None
font_for: Any = None
image_surface: Any = None
SEIR_LAST_MONTH = 87
SEIR_DISASTERS = (
    (2.0, -62.0, -6.0, "WILDFIRE"),
    (6.0, 149.0, -36.0, "WILDFIRE"),
    (7.0, 121.0, 14.0, "ERUPTION"),
    (25.0, 7.0, 51.0, "FLOOD"),
    (27.0, -17.86, 28.57, "ERUPTION"),
    (38.0, 69.0, 29.0, "FLOOD"),
    (44.0, 37.0, 37.0, "EARTHQUAKE"),
    (48.0, -120.0, 55.0, "WILDFIRE"),
    (55.0, 137.3, 37.5, "EARTHQUAKE"),
    (59.0, -51.2, -30.0, "FLOOD"),
)
SEIR_STORMS = (
    (3.0, 4.0, -77.4, 26.7, "DORIAN", 18.0),
    (4.0, 5.0, 139.7, 35.7, "HAGIBIS", 15.0),
    (44.0, 46.0, 47.0, -20.0, "FREDDY", 24.0),
    (47.0, 48.0, 91.0, 18.0, "MOCHA", 18.0),
    (52.0, 53.0, -100.0, 17.0, "OTIS", 14.0),
)
SEIR_OPERATIONS = (
    (9.0, 15.0, None, None, "GLOBAL AIRSPACE RESTRICTIONS", "RESTRICTED"),
    (14.0, 15.0, 35.5, 33.9, "BEIRUT PORT DISRUPTED", "RESTRICTED"),
    (15.0, 17.0, None, None, "CONTROLLED ROUTES REOPEN", "OPEN"),
    (21.0, 22.0, 32.5, 30.5, "SUEZ CANAL CLOSED", "BANNED"),
    (32.0, 88.0, 31.0, 48.0, "UKRAINE CIVIL AIRSPACE CLOSED", "BANNED"),
    (32.0, 88.0, 38.0, 58.0, "EU/RUSSIA AIRSPACE BAN", "BANNED"),
    (34.0, 38.0, None, None, "INTERNATIONAL AIRSPACE REOPENING", "OPEN"),
    (37.0, 49.0, 31.0, 48.0, "BLACK SEA GRAIN CORRIDOR", "OPEN"),
    (46.0, 88.0, 32.5, 15.5, "SUDAN AIRSPACE RESTRICTED", "RESTRICTED"),
    (49.0, 88.0, 31.0, 48.0, "BLACK SEA PORT RESTRICTIONS", "RESTRICTED"),
    (52.0, 81.0, 34.8, 31.5, "ISRAEL AIRSPACE DISRUPTED", "RESTRICTED"),
    (54.0, 88.0, 42.5, 15.0, "RED SEA REROUTING", "REROUTED"),
    (81.0, 88.0, 47.0, 30.0, "GULF AIRSPACE RESTRICTED", "RESTRICTED"),
    (81.0, 88.0, 56.5, 26.5, "HORMUZ SAFE PASSAGE HALTED", "BANNED"),
)

EXPORTS = [
    "CHRONUS_BORDERS",
    "CHRONUS_REGIONS",
    "CHRONUS_INDIA_CLAIMS",
    "CHRONUS_FLAGS",
    "CHRONUS_CHICKEN",
    "SEIR_COLOURS",
    "SEIR_LABELS",
    "SEIR_FLIGHT_OUTLINES",
    "SEIR_AIRLINES",
    "SEIR_AIRLINES_BY_CODE",
    "SEIR_INDIA_CLAIM_REGIONS",
    "SEIR_AIRPORTS",
    "SEIR_ROUTES",
    "SEIR_ROUTE_OPERATORS",
    "SEIR_PORTS",
    "SEIR_SHIP_ROUTES",
    "SEIR_SHIP_PATHS",
    "SEIR_SHIP_DETOURS",
    "SEIR_HORMUZ_ROUTES",
    "SEIR_SHIP_STYLES",
    "SEIR_FEED",
    "SEIR_DISASTERS",
    "SEIR_STORMS",
    "SEIR_OPERATIONS",
    "seir_feed_events",
    "seir_date",
    "seir_features",
    "seir_regions",
    "seir_india_claims",
    "seir_region_owner",
    "seir_region_share",
    "seir_region_vaccinated",
    "seir_snapshot",
    "seir_mix",
    "seir_weather",
    "seir_weather_offset",
    "seir_region_colour",
    "seir_prepare_region_layer",
    "seir_region_layer",
    "seir_project_float",
    "seir_project",
    "seir_route_point",
    "seir_ship_path",
    "seir_path_point",
    "seir_path_pose",
    "seir_route_pose",
    "seir_path_segments",
    "seir_route_segments",
    "draw_seir_plane",
    "draw_seir_flights",
    "draw_seir_ship",
    "draw_seir_ships",
    "draw_seir_ocean",
    "draw_seir_transport_nodes",
    "draw_seir_transport_routes",
    "draw_seir_map",
    "draw_seir_flag",
    "draw_seir_chicken",
    "draw_seir_event_icon",
    "draw_seir_disasters",
    "draw_seir_feed",
    "draw_seir_flip_card",
    "seir_calendar_values",
    "draw_seir_calendar",
    "seir_count_text",
    "draw_seir_health",
    "draw_seir_scene",
]


def _rows(path):
    with path.open(encoding="utf-8", newline="") as handle:
        return tuple(csv.DictReader(handle, delimiter="\t"))


def bind(api, assets):
    """Bind helpers and load Chronus map, route, and display paths"""
    # The visualiser supplies helpers while this module holds map data
    globals().update(api)
    global CHRONUS_BORDERS, CHRONUS_REGIONS, CHRONUS_INDIA_CLAIMS
    global CHRONUS_FLAGS, CHRONUS_CHICKEN
    global SEIR_COLOURS, SEIR_LABELS, SEIR_FLIGHT_OUTLINES, SEIR_AIRLINES
    global SEIR_AIRLINES_BY_CODE, SEIR_ROUTE_OPERATORS
    global SEIR_INDIA_CLAIM_REGIONS, SEIR_AIRPORTS, SEIR_ROUTES
    global SEIR_PORTS, SEIR_SHIP_ROUTES, SEIR_SHIP_PATHS, SEIR_SHIP_DETOURS
    global SEIR_HORMUZ_ROUTES
    global SEIR_SHIP_STYLES
    global SEIR_FEED

    CHRONUS_BORDERS = assets / "world-borders.geojson"
    CHRONUS_REGIONS = assets / "world-regions.geojson"
    CHRONUS_INDIA_CLAIMS = assets / "india-claim-regions.geojson"
    CHRONUS_FLAGS = assets / "flags"
    CHRONUS_CHICKEN = assets / "news-chicken.png"

    # Load compact lookup tables before building route geometry
    states = _rows(assets / "states.tsv")
    SEIR_LABELS = tuple(row["label"] for row in states)
    SEIR_COLOURS = tuple(
        tuple(int(row[channel]) for channel in ("red", "green", "blue"))
        for row in states
    )
    SEIR_FLIGHT_OUTLINES = {
        row["kind"]: tuple(
            int(row[channel]) for channel in ("red", "green", "blue")
        )
        for row in _rows(assets / "flight-outlines.tsv")
    }
    airlines = {}
    for row in _rows(assets / "airlines.tsv"):
        airlines.setdefault(row["service"], []).append(
            (
                row["code"],
                row["name"],
                row["country"],
                int(row["global_rank"] or 0),
                tuple(
                    int(row[channel]) for channel in ("red", "green", "blue")
                ),
            )
        )
    SEIR_AIRLINES = {key: tuple(rows) for key, rows in airlines.items()}
    SEIR_AIRLINES_BY_CODE = {
        airline[0]: airline
        for rows in SEIR_AIRLINES.values()
        for airline in rows
    }
    SEIR_INDIA_CLAIM_REGIONS = frozenset(
        row["region"] for row in _rows(assets / "india-claims.tsv")
    )
    SEIR_AIRPORTS = {
        row["code"]: (
            float(row["longitude"]),
            float(row["latitude"]),
            row["country"],
        )
        for row in _rows(assets / "airports.tsv")
    }
    flight_rows = _rows(assets / "flight-routes.tsv")
    SEIR_ROUTES = tuple(
        (
            row["source"],
            row["destination"],
            int(row["begins"]),
            row["kind"],
            float(row["frequency"]),
        )
        for row in flight_rows
    )
    SEIR_ROUTE_OPERATORS = tuple(
        row["openflights_airline"] for row in flight_rows
    )
    SEIR_PORTS = {
        row["code"]: (float(row["longitude"]), float(row["latitude"]))
        for row in _rows(assets / "ports.tsv")
    }
    ship_rows = _rows(assets / "ship-routes.tsv")
    SEIR_SHIP_ROUTES = tuple(
        (
            row["source"],
            row["destination"],
            int(row["begins"]),
            row["kind"],
            float(row["ais_density"]),
        )
        for row in ship_rows
    )
    SEIR_HORMUZ_ROUTES = frozenset(
        (row["source"], row["destination"])
        for row in ship_rows
        if "ormuz" in row["passages"].split(",")
    )
    SEIR_SHIP_STYLES = {
        row["kind"]: (
            tuple(
                int(row[f"outline_{channel}"])
                for channel in ("red", "green", "blue")
            ),
            tuple(
                int(row[f"route_{channel}"])
                for channel in ("red", "green", "blue")
            ),
            row["profile"],
        )
        for row in _rows(assets / "ship-styles.tsv")
    }
    # Ordered waypoints provide base routes and later disruption detours
    paths = {}
    for row in _rows(assets / "ship-paths.tsv"):
        key = row["source"], row["destination"]
        paths.setdefault(key, []).append(
            (
                int(row["sequence"]),
                (float(row["longitude"]), float(row["latitude"])),
            )
        )
    SEIR_SHIP_PATHS = {
        key: tuple(point for _, point in sorted(points))
        for key, points in paths.items()
    }

    def join_paths(*parts):
        joined = []
        for part in parts:
            joined.extend(part[1:] if joined else part)
        return tuple(joined)

    def prefix_to(path, destination):
        nearest = min(
            range(len(path)),
            key=lambda index: (
                ((path[index][0] - destination[0] + 180.0) % 360.0 - 180.0) ** 2
                + (path[index][1] - destination[1]) ** 2
            ),
        )
        return (*path[: nearest + 1], destination)

    # Detours reuse route segments so route data stays in tables
    SEIR_SHIP_DETOURS = {
        ("YTN", "RTM"): join_paths(
            prefix_to(SEIR_SHIP_PATHS["YTN", "RTM"], SEIR_PORTS["SGP"]),
            SEIR_SHIP_PATHS["SGP", "CPT"],
            SEIR_SHIP_PATHS["CPT", "RTM"],
        ),
        ("KAR", "RTM"): join_paths(
            SEIR_SHIP_PATHS["KAR", "CMB"],
            SEIR_SHIP_PATHS["CMB", "CPT"],
            SEIR_SHIP_PATHS["CPT", "RTM"],
        ),
        ("CMB", "RTM"): join_paths(
            SEIR_SHIP_PATHS["CMB", "CPT"],
            SEIR_SHIP_PATHS["CPT", "RTM"],
        ),
    }
    SEIR_FEED = tuple(
        sorted(
            (
                float(row["month"]),
                row["kind"],
                row["code"],
                row["label"],
                row["summary"],
            )
            for row in _rows(assets / "feed.tsv")
        )
    )


def seir_date(month):
    month = max(0, min(SEIR_LAST_MONTH, int(month)))
    ordinal = 2019 * 12 + 5 + month
    return date(ordinal // 12, ordinal % 12 + 1, 1)


def seir_features(cache):
    """Load populated country polygons and cache immutable source geometry"""
    # Parse GeoJSON once per cache because borders never change during a run
    key = "seir-country-features"
    if key not in cache:
        document = json.loads(CHRONUS_BORDERS.read_text(encoding="utf-8"))
        features = []
        for feature in document["features"]:
            properties = feature["properties"]
            geometry = feature.get("geometry") or {}
            polygons = geometry.get("coordinates", ())
            if geometry.get("type") == "Polygon":
                polygons = (polygons,)
            rings = tuple(
                tuple((float(lon), float(lat)) for lon, lat in polygon[0])
                for polygon in polygons
                if polygon and len(polygon[0]) >= 3
            )
            population = max(0, int(properties.get("POP_EST") or 0))
            if rings and population:
                features.append(
                    (
                        properties.get("ADM0_A3") or "UNK",
                        population,
                        rings,
                    )
                )
        cache[key] = tuple(features)
    return cache[key]


def seir_regions(cache):
    """Load named region polygons with anchors for labels and state mapping"""
    # Region centres are simple label anchors rather than geographic centroids
    key = "seir-region-features"
    if key not in cache:
        document = json.loads(CHRONUS_REGIONS.read_text(encoding="utf-8"))
        regions = []
        for feature in document["features"]:
            properties = feature["properties"]
            geometry = feature.get("geometry") or {}
            polygons = geometry.get("coordinates", ())
            if geometry.get("type") == "Polygon":
                polygons = (polygons,)
            rings = tuple(
                tuple((float(lon), float(lat)) for lon, lat in polygon[0])
                for polygon in polygons
                if polygon and len(polygon[0]) >= 3
            )
            if not rings:
                continue
            points = tuple(point for ring in rings for point in ring)
            regions.append(
                (
                    properties.get("adm0_a3") or "UNK",
                    properties.get("name") or "",
                    rings,
                    (
                        sum(point[0] for point in points) / len(points),
                        sum(point[1] for point in points) / len(points),
                    ),
                )
            )
        cache[key] = tuple(regions)
    return cache[key]


def seir_india_claims(cache):
    key = "seir-india-claims"
    if key not in cache:
        document = json.loads(CHRONUS_INDIA_CLAIMS.read_text(encoding="utf-8"))
        claims = []
        for feature in document["features"]:
            geometry = feature.get("geometry") or {}
            polygons = geometry.get("coordinates", ())
            if geometry.get("type") == "Polygon":
                polygons = (polygons,)
            rings = tuple(
                tuple((float(lon), float(lat)) for lon, lat in polygon[0])
                for polygon in polygons
                if polygon and len(polygon[0]) >= 3
            )
            if rings:
                claims.append((feature["properties"]["name"], rings))
        cache[key] = tuple(claims)
    return cache[key]


def seir_region_owner(parent, name):
    return "IND" if name in SEIR_INDIA_CLAIM_REGIONS else parent


def seir_region_share(position, index, region, states, global_active):
    parent, name, _, centre = region
    parent = seir_region_owner(parent, name)
    month = min(SEIR_LAST_MONTH, max(0, int(position)))
    if parent == "ATA":
        return 0.0
    base = sum(states.get(parent, (0.0,) * 5)[:2]) or global_active * 0.65
    if name == "Hubei" and month < 7:
        return max(base, 0.00002 * math.exp(month * 0.75))
    seed = (index + 1) * 37
    if month < 7 + seed % 6:
        return 0.0
    longitude, latitude = centre
    phase = math.radians(longitude * 2.1 + latitude * 3.3) + month * 0.57
    local_wave = 0.5 + 0.5 * math.sin(phase + (seed % 19) * 0.11)
    if local_wave < 0.32:
        return 0.0
    return min(0.09, base * (0.35 + 1.65 * (local_wave - 0.32) / 0.68))


def seir_region_vaccinated(position, index, region, states):
    parent = seir_region_owner(region[0], region[1])
    if parent == "ATA":
        return 0.0
    base = states.get(parent, (0.0,) * 5)[3]
    if base <= 0.0:
        return 0.0
    wave = 0.5 + 0.5 * math.sin(index * 0.37 + int(position) * 0.07)
    return min(0.98, base * (0.78 + 0.35 * wave))


def seir_snapshot(frame, features):
    """Decode state, flight, and ship entities into renderer input tuples"""
    # State entities are ordered by id to match static GeoJSON feature order
    first = sorted(
        (
            entity
            for entity in frame.entities
            if entity.name == "country_state_a"
        ),
        key=lambda entity: entity.entity_id,
    )
    second = sorted(
        (
            entity
            for entity in frame.entities
            if entity.name == "country_state_b"
        ),
        key=lambda entity: entity.entity_id,
    )
    flights = sorted(
        (entity for entity in frame.entities if entity.name == "flight_state"),
        key=lambda entity: entity.entity_id,
    )
    ships = sorted(
        (entity for entity in frame.entities if entity.name == "ship_state"),
        key=lambda entity: entity.entity_id,
    )
    if len(first) != len(features) or len(second) != len(features):
        raise ValueError(
            "SEIR snapshot does not match the Natural Earth country set"
        )
    if len(flights) != len(SEIR_ROUTES):
        raise ValueError("SEIR snapshot does not match the flight route set")
    if len(ships) != len(SEIR_SHIP_ROUTES):
        raise ValueError("SEIR snapshot does not match the ship route set")
    states = {}
    for feature, state_a, state_b in zip(features, first, second):
        state = tuple(
            value / 100.0
            for value in (state_a.x, state_a.y, state_a.z, state_b.x, state_b.y)
        )
        if any(value < 0.0 for value in state) or sum(state) > 1.000001:
            raise ValueError("invalid SEIR population shares in snapshot")
        states[feature[0]] = state
    flight_states = tuple(
        (
            max(0.0, min(1.0, entity.x / 100.0)),
            max(0.0, entity.y),
            max(0.0, min(1.0, entity.z)),
        )
        for entity in flights
    )
    ship_states = tuple(
        (
            max(0.0, min(1.0, entity.x / 100.0)),
            max(0.0, entity.y),
            entity.z - 1.0,
        )
        for entity in ships
    )
    return states, flight_states, ship_states


def seir_mix(low, high, amount):
    amount = max(0.0, min(1.0, amount))
    return tuple(round(a + (b - a) * amount) for a, b in zip(low, high))


def seir_weather(position, rectangle):
    return tuple(
        (
            seir_project_float(longitude, latitude, rectangle),
            radius * rectangle.height / 180.0,
        )
        for begins, ends, longitude, latitude, _label, radius in SEIR_STORMS
        if begins <= position < ends
    )


def seir_weather_offset(centre, weather, wrap, position, index, amplitude):
    """Jostle transport markers only inside a dated storm footprint."""
    for storm, reach in weather:
        dx = (centre[0] - storm[0] + wrap / 2) % wrap - wrap / 2
        dy = centre[1] - storm[1]
        distance = math.hypot(dx, dy)
        if distance >= reach:
            continue
        strength = 1.0 - distance / reach
        phase = position * 72.0 + index * 2.399
        return (
            centre[0] + math.sin(phase) * amplitude * strength,
            centre[1] + math.cos(phase * 1.31) * amplitude * strength,
        )
    return centre


def seir_region_colour(active, vaccinated):
    # Square roots keep small shares visible without saturating colours
    colour = seir_mix(
        (18, 69, 88),
        SEIR_COLOURS[3],
        0.92 * math.sqrt(min(1.0, vaccinated / 0.8)),
    )
    return seir_mix(
        colour,
        SEIR_COLOURS[1],
        math.sqrt(min(1.0, active / 0.065)),
    )


def seir_project_float(longitude, latitude, rectangle):
    latitude = max(-90.0, min(90.0, latitude))
    return (
        rectangle.left + (longitude + 180.0) * rectangle.width / 360.0,
        rectangle.top + (90.0 - latitude) * rectangle.height / 180.0,
    )


def seir_project(longitude, latitude, rectangle):
    return tuple(
        round(value)
        for value in seir_project_float(longitude, latitude, rectangle)
    )


def seir_route_point(start, end, progress):
    """Return a point along the short great-circle route between coordinates"""
    # Spherical interpolation follows the short arc between airports
    vectors = []
    for longitude, latitude in (start, end):
        lon, lat = math.radians(longitude), math.radians(latitude)
        vectors.append(
            (
                math.cos(lat) * math.cos(lon),
                math.cos(lat) * math.sin(lon),
                math.sin(lat),
            )
        )
    first, second = vectors
    angle = math.acos(
        max(-1.0, min(1.0, sum(a * b for a, b in zip(first, second))))
    )
    if angle < 0.000001:
        vector = first
    else:
        scale = math.sin(angle)
        left = math.sin((1.0 - progress) * angle) / scale
        right = math.sin(progress * angle) / scale
        vector = tuple(left * a + right * b for a, b in zip(first, second))
    longitude = math.degrees(math.atan2(vector[1], vector[0]))
    latitude = math.degrees(
        math.atan2(vector[2], math.hypot(vector[0], vector[1]))
    )
    return longitude, latitude


def seir_ship_path(source, destination, position):
    key = source, destination
    if position >= 54 and key in SEIR_SHIP_DETOURS:
        return SEIR_SHIP_DETOURS[key]
    return SEIR_SHIP_PATHS[key]


@cache
def _seir_path_metrics(path):
    """Cache immutable great-circle leg lengths for ship paths."""
    lengths, cumulative, total = [], [], 0.0
    for start, end in itertools.pairwise(path):
        lon1, lat1 = map(math.radians, start)
        lon2, lat2 = map(math.radians, end)
        length = math.acos(
            max(
                -1.0,
                min(
                    1.0,
                    math.sin(lat1) * math.sin(lat2)
                    + math.cos(lat1) * math.cos(lat2) * math.cos(lon2 - lon1),
                ),
            )
        )
        lengths.append(length)
        total += length
        cumulative.append(total)
    return tuple(lengths), tuple(cumulative), total


def seir_path_point(path, progress):
    """Return a distance-weighted point along a multi-leg ship route"""
    # Allocate progress by route length so ships move at a steady map speed
    progress = max(0.0, min(1.0, progress))
    lengths, cumulative, total = _seir_path_metrics(path)
    if total <= 0.0:
        return path[0]
    target = progress * total
    index = min(len(lengths) - 1, bisect_right(cumulative, target))
    travelled = 0.0 if index == 0 else cumulative[index - 1]
    length = lengths[index]
    local = 0.0 if length == 0.0 else (target - travelled) / length
    return seir_route_point(path[index], path[index + 1], local)


def seir_path_pose(path, progress, rectangle, deviation=0.0):
    """Project a route position and tangent with an optional side offset"""
    # Unwrap nearby longitudes so headings survive date-line crossings
    progress = max(0.0, min(1.0, progress))
    centre_point = seir_path_point(path, progress)
    before_point = seir_path_point(path, max(0.0, progress - 0.008))
    after_point = seir_path_point(path, min(1.0, progress + 0.008))
    before_point = (
        centre_point[0]
        + (before_point[0] - centre_point[0] + 180.0) % 360.0
        - 180.0,
        before_point[1],
    )
    after_point = (
        centre_point[0]
        + (after_point[0] - centre_point[0] + 180.0) % 360.0
        - 180.0,
        after_point[1],
    )
    centre = seir_project_float(centre_point[0], centre_point[1], rectangle)
    before = seir_project_float(before_point[0], before_point[1], rectangle)
    after = seir_project_float(after_point[0], after_point[1], rectangle)
    direction = (after[0] - before[0], after[1] - before[1])
    length = math.hypot(*direction)
    if length:
        distance = (
            deviation
            * max(2.0, rectangle.height / 180.0)
            * math.sin(math.pi * progress)
        )
        centre = (
            centre[0] - direction[1] * distance / length,
            centre[1] + direction[0] * distance / length,
        )
    return centre, direction


def seir_route_pose(start, end, progress, rectangle):
    """Project a flight with one great-circle sample, not three ship samples."""
    centre_point = seir_route_point(start, end, max(0.0, min(1.0, progress)))
    centre = seir_project_float(*centre_point, rectangle)
    first = seir_project_float(start[0], start[1], rectangle)
    second = seir_project_float(end[0], end[1], rectangle)
    direction = (
        (second[0] - first[0] + rectangle.width / 2) % rectangle.width
        - rectangle.width / 2,
        second[1] - first[1],
    )
    return centre, direction


def seir_path_segments(path, rectangle, cache):
    """Project a route into drawable segments split at date-line crossings"""
    # Split date-line crossings so pygame never draws across the whole map
    key = "seir-path", path, tuple(rectangle)
    if key not in cache:
        points = (
            tuple(seir_project(*point, rectangle=rectangle) for point in path)
            if len(path) > 2
            else tuple(
                seir_project(
                    *seir_path_point(path, step / 32), rectangle=rectangle
                )
                for step in range(33)
            )
        )
        segments, current = [], []
        for point in points:
            if current and abs(point[0] - current[-1][0]) > rectangle.width / 2:
                segments.append(tuple(current))
                current = []
            current.append(point)
        if current:
            segments.append(tuple(current))
        cache[key] = tuple(segments)
    return cache[key]


def seir_route_segments(start, end, rectangle, cache):
    return seir_path_segments((start, end), rectangle, cache)


def draw_seir_plane(pygame, screen, centre, direction, colour, outline, accent):
    """Draw one oriented flight marker from its projected centre and tangent"""
    angle = math.atan2(direction[1], direction[0])

    def point(forward, side):
        return (
            round(
                centre[0] + math.cos(angle) * forward - math.sin(angle) * side
            ),
            round(
                centre[1] + math.sin(angle) * forward + math.cos(angle) * side
            ),
        )

    shape = (
        point(11, 0),
        point(5, -2),
        point(1, -2),
        point(-2, -10),
        point(-4, -10),
        point(-2, -2),
        point(-7, -1),
        point(-9, -4),
        point(-10, -4),
        point(-9, 0),
        point(-10, 4),
        point(-9, 4),
        point(-7, 1),
        point(-2, 2),
        point(-4, 10),
        point(-2, 10),
        point(1, 2),
        point(5, 2),
    )
    pygame.draw.polygon(
        screen, (2, 8, 14), tuple((x + 2, y + 2) for x, y in shape)
    )
    pygame.draw.polygon(screen, colour, shape)
    pygame.draw.polygon(
        screen,
        accent,
        (point(-7, -1), point(-9, -4), point(-10, -4), point(-9, 0)),
    )
    pygame.draw.lines(screen, outline, True, shape, 2)
    pygame.draw.circle(screen, (26, 40, 52), point(7, 0), 1)


def draw_seir_flights(
    pygame, screen, fonts, rectangle, position, flight_states, cache
):
    """Draw active flights and cache the static lines for their routes"""
    screen.set_clip(rectangle)
    weather = seir_weather(position, rectangle)
    occupied = set()
    for index, (source, destination, _begins, kind, _frequency) in enumerate(
        SEIR_ROUTES
    ):
        progress, affected, activity = flight_states[index]
        if round(activity * 100) <= (index * 37) % 100:
            continue
        start = SEIR_AIRPORTS[source][:2]
        end = SEIR_AIRPORTS[destination][:2]
        colour = seir_mix((248, 250, 252), (239, 68, 68), affected / 24)
        operator = SEIR_AIRLINES_BY_CODE.get(SEIR_ROUTE_OPERATORS[index])
        centre, direction = seir_route_pose(start, end, progress, rectangle)
        centre = seir_weather_offset(
            centre, weather, rectangle.width, position, index, 5.0
        )
        cell = (
            int((centre[0] - rectangle.left) // 24),
            int((centre[1] - rectangle.top) // 18),
        )
        if cell in occupied:
            continue
        occupied.add(cell)
        draw_seir_plane(
            pygame,
            screen,
            centre,
            direction,
            colour,
            SEIR_FLIGHT_OUTLINES[kind],
            operator[4] if operator else (148, 163, 184),
        )
        if operator and (index + int(position)) % 17 == 0:
            draw_text(
                pygame,
                screen,
                fonts,
                operator[0],
                (centre[0] + 12, centre[1] + 8),
                9,
                operator[4],
                1.0,
                True,
                "topleft",
            )
    screen.set_clip(None)


def draw_seir_ship(pygame, screen, centre, direction, kind):
    """Draw one oriented cargo or passenger ship marker"""
    angle = math.atan2(direction[1], direction[0])

    def point(forward, side):
        return (
            round(
                centre[0] + math.cos(angle) * forward - math.sin(angle) * side
            ),
            round(
                centre[1] + math.sin(angle) * forward + math.cos(angle) * side
            ),
        )

    outline, _, profile = SEIR_SHIP_STYLES[kind]
    hull = (
        point(10, 0),
        point(5, -4),
        point(-8, -3),
        point(-10, 0),
        point(-8, 3),
        point(5, 4),
    )
    pygame.draw.polygon(
        screen, (2, 8, 14), tuple((x + 2, y + 2) for x, y in hull)
    )
    pygame.draw.polygon(screen, (226, 232, 240), hull)
    pygame.draw.lines(screen, outline, True, hull, 2)
    if profile == "container":
        for forward, colour in (
            (-4, (236, 72, 153)),
            (0, (245, 158, 11)),
            (4, (29, 78, 216)),
        ):
            pygame.draw.polygon(
                screen,
                colour,
                (
                    point(forward - 1.5, -2.5),
                    point(forward + 1.5, -2.5),
                    point(forward + 1.5, 2.5),
                    point(forward - 1.5, 2.5),
                ),
            )
    elif profile == "tanker":
        for forward in (-4, 0, 4):
            pygame.draw.circle(screen, (120, 128, 140), point(forward, 0), 2)
    elif profile == "bulk":
        for forward in (-4, 0, 4):
            pygame.draw.line(
                screen, (76, 89, 99), point(forward, -2), point(forward, 2), 2
            )
    else:
        pygame.draw.polygon(
            screen,
            (81, 158, 185),
            (point(-3, -2.5), point(3, -2), point(3, 2), point(-3, 2.5)),
        )


def draw_seir_ships(pygame, screen, rectangle, position, ship_states, cache):
    screen.set_clip(rectangle)
    weather = seir_weather(position, rectangle)
    for index, (source, destination, _begins, kind, _density) in enumerate(
        SEIR_SHIP_ROUTES
    ):
        progress, load, deviation = ship_states[index]
        if load <= 0:
            continue
        path = seir_ship_path(source, destination, position)
        centre, direction = seir_path_pose(path, progress, rectangle, deviation)
        centre = seir_weather_offset(
            centre, weather, rectangle.width, position, index, 3.0
        )
        draw_seir_ship(pygame, screen, centre, direction, kind)
    screen.set_clip(None)


def draw_seir_ocean(pygame, screen, rectangle, cache):
    """Draw calm latitude shading under the real coastline geometry"""
    key = "seir-ocean", rectangle.size
    if key not in cache:
        ocean = pygame.Surface(rectangle.size)
        denominator = max(1, rectangle.height - 1)
        for row in range(rectangle.height):
            latitude = abs(1.0 - 2.0 * row / denominator)
            colour = seir_mix((4, 37, 59), (7, 22, 40), latitude * 0.72)
            pygame.draw.line(ocean, colour, (0, row), (rectangle.width, row))
        cache[key] = ocean
    screen.blit(cache[key], rectangle.topleft)


def draw_seir_transport_nodes(pygame, screen, fonts, rectangle):
    """Draw airports and ports above the route layer"""
    screen.set_clip(rectangle)
    for longitude, latitude, _country in SEIR_AIRPORTS.values():
        pygame.draw.circle(
            screen,
            (151, 216, 237),
            seir_project(longitude, latitude, rectangle),
            2,
        )
    for longitude, latitude in SEIR_PORTS.values():
        x, y = seir_project(longitude, latitude, rectangle)
        pygame.draw.rect(screen, (245, 188, 74), (x - 2, y - 2, 4, 4))
    draw_text(
        pygame,
        screen,
        fonts,
        f"{len(SEIR_AIRPORTS)} AIRPORTS · {len(SEIR_PORTS)} PORTS · "
        f"{len(SEIR_ROUTES) + len(SEIR_SHIP_ROUTES)} SOURCED CORRIDORS · "
        "SIMULATED MOTION",
        (rectangle.left + 10, rectangle.bottom - 9),
        11,
        (185, 215, 227),
        1.0,
        True,
        "bottomleft",
    )
    for label, longitude, latitude in (
        ("AFRICA", 20, 4),
        ("RUSSIA", 92, 61),
        ("LATAM", -61, -17),
        ("SOUTH-EAST ASIA", 110, 7),
        ("ARCTIC OPS", 0, 73),
        ("ANTARCTIC OPS", 18, -75),
        ("PACIFIC", -148, 5),
        ("ATLANTIC", -31, 8),
        ("INDIAN OCEAN", 76, -28),
    ):
        draw_text(
            pygame,
            screen,
            fonts,
            label,
            seir_project(longitude, latitude, rectangle),
            9,
            (101, 153, 173),
            0.78,
            True,
        )
    screen.set_clip(None)


def draw_seir_transport_routes(pygame, screen, rectangle, position, cache):
    """Blit static corridors and cached conflict restriction overlays."""
    layer_key = "seir-transport-route-layer", rectangle.size
    if layer_key not in cache:
        local = pygame.Rect(0, 0, *rectangle.size)
        layer = pygame.Surface(rectangle.size, pygame.SRCALPHA)
        war = pygame.Surface(rectangle.size, pygame.SRCALPHA)
        gulf = pygame.Surface(rectangle.size, pygame.SRCALPHA)
        gulf_countries = {
            "ARE",
            "IRN",
            "IRQ",
            "ISR",
            "JOR",
            "LBN",
            "OMN",
            "QAT",
            "SAU",
        }
        for source, destination, _begins, kind, frequency in SEIR_ROUTES:
            colour = seir_mix(
                (7, 23, 41), SEIR_FLIGHT_OUTLINES[kind], 0.25 + 0.45 * frequency
            )
            path = (SEIR_AIRPORTS[source][:2], SEIR_AIRPORTS[destination][:2])
            countries = {
                SEIR_AIRPORTS[source][2],
                SEIR_AIRPORTS[destination][2],
            }
            for segment in seir_path_segments(path, local, cache):
                if len(segment) > 1:
                    pygame.draw.lines(layer, (*colour, 48), False, segment, 1)
                    if "UKR" in countries or (
                        "RUS" in countries and len(countries) > 1
                    ):
                        pygame.draw.lines(
                            war, (239, 68, 68, 150), False, segment, 2
                        )
                    if countries & gulf_countries:
                        pygame.draw.lines(
                            gulf, (245, 158, 11, 150), False, segment, 2
                        )
        for source, destination, _begins, kind, density in SEIR_SHIP_ROUTES:
            colour = seir_mix(
                (7, 23, 41),
                SEIR_SHIP_STYLES[kind][1],
                0.35 + 0.65 * density,
            )
            path = SEIR_SHIP_PATHS[source, destination]
            for segment in seir_path_segments(path, local, cache):
                if len(segment) > 1:
                    pygame.draw.aalines(layer, (*colour, 72), False, segment)
                    if source in {"ODS", "NOV"}:
                        pygame.draw.aalines(
                            war, (239, 68, 68, 170), False, segment
                        )
                    if (source, destination) in SEIR_HORMUZ_ROUTES:
                        pygame.draw.aalines(
                            gulf, (239, 68, 68, 190), False, segment
                        )
        cache[layer_key] = layer
        cache["seir-transport-war-layer", rectangle.size] = war
        cache["seir-transport-gulf-layer", rectangle.size] = gulf
    detour_key = "seir-transport-detour-layer", rectangle.size
    if detour_key not in cache:
        local = pygame.Rect(0, 0, *rectangle.size)
        layer = pygame.Surface(rectangle.size, pygame.SRCALPHA)
        for path in SEIR_SHIP_DETOURS.values():
            for segment in seir_path_segments(path, local, cache):
                if len(segment) > 1:
                    pygame.draw.aalines(
                        layer, (236, 72, 153, 130), False, segment
                    )
        cache[detour_key] = layer
    screen.blit(cache[layer_key], rectangle.topleft)
    if position >= 32:
        screen.blit(
            cache["seir-transport-war-layer", rectangle.size],
            rectangle.topleft,
        )
    if position >= 54:
        screen.blit(cache[detour_key], rectangle.topleft)
    if position >= 81:
        screen.blit(
            cache["seir-transport-gulf-layer", rectangle.size],
            rectangle.topleft,
        )


def seir_prepare_region_layer(
    pygame, parts, states, rectangle, month, cache, budget=None
):
    """Paint a bounded slice of one cached Admin-1 health-fill surface."""
    key = "seir-region-next-layer"
    prepared = cache.get(key)
    if (
        not prepared
        or prepared["month"] != month
        or prepared["size"] != rectangle.size
    ):
        prepared = {
            "month": month,
            "size": rectangle.size,
            "surface": pygame.Surface(rectangle.size, pygame.SRCALPHA),
            "index": 0,
        }
        cache[key] = prepared
    limit = (
        len(parts)
        if budget is None
        else min(len(parts), prepared["index"] + budget)
    )
    global_active = sum(state[0] + state[1] for state in states.values()) / max(
        1, len(states)
    )
    for index, region, ring in parts[prepared["index"] : limit]:
        active = seir_region_share(month, index, region, states, global_active)
        vaccinated = seir_region_vaccinated(month, index, region, states)
        if active > 0.0 or vaccinated > 0.0:
            pygame.draw.polygon(
                prepared["surface"],
                seir_region_colour(active, vaccinated),
                ring,
            )
    prepared["index"] = limit
    return prepared


def seir_region_layer(
    pygame, parts, states, next_states, rectangle, month, cache
):
    """Return the current fill layer and incrementally prepare its successor."""
    region_layer = cache.get("seir-region-layer")
    prepared = cache.get("seir-region-next-layer")
    promoted = False
    # 128 per draw finishes 8k+ rings before the flip even near 30 fps.
    budget = cache.get("seir-region-build-budget", 128)
    if not region_layer:
        prepared = seir_prepare_region_layer(
            pygame, parts, states, rectangle, month, cache
        )
        region_layer = (month, rectangle.size, prepared["surface"])
        cache["seir-region-layer"] = region_layer
        cache.pop("seir-region-next-layer", None)
    elif region_layer[:2] != (month, rectangle.size):
        if (
            prepared
            and prepared["month"] == month
            and prepared["size"] == rectangle.size
            and prepared["index"] == len(parts)
        ):
            region_layer = (month, rectangle.size, prepared["surface"])
            cache["seir-region-layer"] = region_layer
            cache.pop("seir-region-next-layer", None)
            promoted = True
        else:
            # Seeking keeps the last complete image visible while its target fills.
            prepared = seir_prepare_region_layer(
                pygame, parts, states, rectangle, month, cache, budget
            )
    if (
        not promoted
        and region_layer[:2] == (month, rectangle.size)
        and next_states is not None
        and month < SEIR_LAST_MONTH
    ):
        prepared = seir_prepare_region_layer(
            pygame, parts, next_states, rectangle, month + 1, cache, budget
        )
    return region_layer, prepared, promoted


def draw_seir_map(
    pygame,
    screen,
    fonts,
    rectangle,
    position,
    features,
    regions,
    states,
    flight_states,
    ship_states,
    cache,
    next_states=None,
):
    """Draw the regional health map, transport network, and country labels

    The function decodes snapshots, then paints geography, overlays, and motion
    It raises when entities no longer match static map or route tables
    """
    pygame.draw.rect(screen, (7, 23, 41), rectangle, border_radius=14)
    pygame.draw.rect(screen, (58, 124, 164), rectangle, 2, border_radius=14)
    atlas_width = rectangle.width - 8
    atlas_height = min(rectangle.height - 8, atlas_width // 2)
    atlas_width = min(atlas_width, atlas_height * 2)
    atlas = pygame.Rect(0, 0, atlas_width, atlas_height)
    atlas.center = rectangle.center
    pygame.draw.rect(screen, (4, 20, 38), atlas, border_radius=9)
    draw_seir_ocean(pygame, screen, atlas, cache)
    local_atlas = pygame.Rect(0, 0, atlas.width, atlas.height)
    # Cache projected GeoJSON and the static base layer by map size
    country_key = "seir-projected-countries", atlas.size
    if country_key not in cache:
        cache[country_key] = tuple(
            (
                code,
                tuple(
                    tuple(
                        seir_project(lon, lat, local_atlas) for lon, lat in ring
                    )
                    for ring in rings
                ),
            )
            for code, _, rings in features
        )
    region_key = "seir-projected-regions", atlas.size
    if region_key not in cache:
        cache[region_key] = tuple(
            tuple(
                tuple(seir_project(lon, lat, local_atlas) for lon, lat in ring)
                for ring in region[2]
            )
            for region in regions
        )
    claims = seir_india_claims(cache)
    claim_key = "seir-projected-india-claims", atlas.size
    if claim_key not in cache:
        cache[claim_key] = tuple(
            (
                name,
                tuple(
                    tuple(
                        seir_project(lon, lat, local_atlas) for lon, lat in ring
                    )
                    for ring in rings
                ),
            )
            for name, rings in claims
        )
    part_key = "seir-projected-region-parts", atlas.size
    if part_key not in cache:
        cache[part_key] = tuple(
            (index, region, ring)
            for index, (region, rings) in enumerate(
                zip(regions, cache[region_key])
            )
            if region[0] != "ATA"
            for ring in rings
        ) + tuple(
            (
                len(regions) + index,
                (
                    "IND",
                    name,
                    (),
                    (
                        sum(point[0] for ring in source_rings for point in ring)
                        / sum(len(ring) for ring in source_rings),
                        sum(point[1] for ring in source_rings for point in ring)
                        / sum(len(ring) for ring in source_rings),
                    ),
                ),
                ring,
            )
            for index, (
                (name, source_rings),
                (_projected_name, rings),
            ) in enumerate(zip(claims, cache[claim_key]))
            for ring in rings
        )
    if (base_key := ("seir-map-base", atlas.size)) not in cache:
        base = pygame.Surface(atlas.size, pygame.SRCALPHA)
        for code, rings in cache[country_key]:
            for ring in rings:
                pygame.draw.polygon(
                    base, (8, 31, 47) if code == "ATA" else (13, 49, 66), ring
                )
        for region, rings in zip(regions, cache[region_key]):
            if region[0] == "ATA":
                continue
            for ring in rings:
                pygame.draw.polygon(base, (15, 56, 74), ring)
        for _, rings in cache[claim_key]:
            for ring in rings:
                pygame.draw.polygon(base, (15, 56, 74), ring)
        cache[base_key] = base
    # Land fills mask the faint corridor layer; routes remain readable at sea.
    draw_seir_transport_routes(pygame, screen, atlas, position, cache)
    screen.blit(cache[base_key], atlas.topleft)
    # Paint the exact following month over ordinary frames, never at rollover.
    month = min(SEIR_LAST_MONTH, max(0, int(position)))
    region_layer, prepared, _promoted = seir_region_layer(
        pygame, cache[part_key], states, next_states, atlas, month, cache
    )
    screen.blit(region_layer[2], atlas.topleft)
    _current, _following, flip, _year, _year_flip = seir_calendar_values(
        position
    )
    if (
        flip
        and prepared
        and prepared["month"] == month + 1
        and prepared["size"] == atlas.size
        and prepared["index"] == len(cache[part_key])
    ):
        prepared["surface"].set_alpha(round(255 * flip))
        screen.blit(prepared["surface"], atlas.topleft)
        prepared["surface"].set_alpha(None)
    # Outlines, including the India claim boundary, are static.
    if (outline_key := ("seir-map-outlines", atlas.size)) not in cache:
        outlines = pygame.Surface(atlas.size, pygame.SRCALPHA)
        for region, rings in zip(regions, cache[region_key]):
            if region[0] == "ATA":
                continue
            for ring in rings:
                pygame.draw.lines(outlines, (50, 102, 122), True, ring, 1)
        for code, rings in cache[country_key]:
            if code in {"CHN", "IND", "PAK"}:
                continue
            for ring in rings:
                pygame.draw.lines(
                    outlines,
                    (42, 82, 99) if code == "ATA" else (104, 167, 187),
                    True,
                    ring,
                    1,
                )
        for _name, rings in cache[claim_key]:
            for ring in rings:
                pygame.draw.lines(outlines, (104, 167, 187), True, ring, 1)
        cache[outline_key] = outlines
    screen.blit(cache[outline_key], atlas.topleft)
    # Motion and transport labels sit above map fills and political outlines
    draw_seir_transport_nodes(pygame, screen, fonts, atlas)
    legend_x = atlas.right - 530
    draw_text(
        pygame,
        screen,
        fonts,
        "AIR OUTLINE",
        (legend_x, atlas.top + 10),
        11,
        (183, 213, 226),
        1.0,
        True,
        "topleft",
    )
    for label, colour, offset in (
        ("INTL", SEIR_FLIGHT_OUTLINES["international"], 95),
        ("DOM", SEIR_FLIGHT_OUTLINES["domestic"], 158),
        ("CARGO", SEIR_FLIGHT_OUTLINES["cargo"], 218),
        ("SHIP CARGO", SEIR_SHIP_STYLES["cargo"][0], 305),
        ("FUEL SHIP", SEIR_SHIP_STYLES["fuel"][0], 432),
    ):
        pygame.draw.rect(
            screen,
            colour,
            (legend_x + offset, atlas.top + 11, 9, 9),
            border_radius=2,
        )
        draw_text(
            pygame,
            screen,
            fonts,
            label,
            (legend_x + offset + 13, atlas.top + 9),
            10,
            colour,
            1.0,
            True,
            "topleft",
        )
    for label, colour, offset in (
        ("OPEN", (72, 187, 216), 0),
        ("RESTRICTED", (245, 158, 11), 72),
        ("BANNED", (239, 68, 68), 178),
        ("REROUTED", (236, 72, 153), 248),
    ):
        pygame.draw.rect(
            screen,
            colour,
            (legend_x + offset, atlas.top + 30, 9, 9),
            border_radius=2,
        )
        draw_text(
            pygame,
            screen,
            fonts,
            label,
            (legend_x + offset + 13, atlas.top + 28),
            10,
            colour,
            1.0,
            True,
            "topleft",
        )
    draw_seir_ships(pygame, screen, atlas, position, ship_states, cache)
    draw_seir_flights(
        pygame, screen, fonts, atlas, position, flight_states, cache
    )
    draw_seir_disasters(pygame, screen, fonts, atlas, position)
    origin = seir_project(114.208, 30.7838, atlas)
    pulse = 8 + round(5 * (0.5 + 0.5 * math.sin(position * math.pi)))
    pygame.draw.circle(screen, (255, 197, 92), origin, pulse, 2)
    pygame.draw.circle(screen, (255, 238, 188), origin, 3)
    draw_text(
        pygame,
        screen,
        fonts,
        "Wuhan",
        (origin[0] + 10, origin[1] - 9),
        13,
        (255, 231, 174),
        1.0,
        True,
        "bottomleft",
    )


def draw_seir_flag(pygame, screen, code, rectangle, cache):
    key = "seir-flag", code, rectangle.size
    if key not in cache:
        source = image_surface(
            pygame, CHRONUS_FLAGS / f"{code.lower()}.svg", cache
        )
        cache[key] = pygame.transform.smoothscale(source, rectangle.size)
    screen.blit(cache[key], rectangle)
    pygame.draw.rect(screen, (214, 229, 238), rectangle, 1)


def draw_seir_chicken(pygame, screen, centre, scale=1.0, code="", cache=None):
    x, y = centre
    cache = {} if cache is None else cache
    sprite = entity_sprite(pygame, CHRONUS_CHICKEN, round(54 * scale), cache)
    screen.blit(sprite, sprite.get_rect(center=(round(x), round(y))))
    if code and (CHRONUS_FLAGS / f"{code.lower()}.svg").is_file():
        flag = pygame.Rect(
            0, 0, max(12, round(18 * scale)), max(7, round(10 * scale))
        )
        flag.center = (
            round(x),
            round(y + 15 * scale),
        )
        draw_seir_flag(pygame, screen, code, flag, cache)


def draw_seir_event_icon(pygame, screen, centre, colour=(103, 211, 235)):
    pygame.draw.circle(screen, (20, 67, 91), centre, 14)
    pygame.draw.circle(screen, colour, centre, 14, 2)
    pygame.draw.line(
        screen,
        (218, 247, 255),
        (centre[0], centre[1] - 7),
        (centre[0], centre[1] + 3),
        3,
    )
    pygame.draw.circle(screen, (218, 247, 255), (centre[0], centre[1] + 8), 2)


def draw_seir_disasters(pygame, screen, fonts, rectangle, position):
    """Pulse dated disasters, storms, closures, bans, and reopenings."""
    screen.set_clip(rectangle)
    for month, longitude, latitude, label in SEIR_DISASTERS:
        if not month <= position < month + 2:
            continue
        centre = seir_project(longitude, latitude, rectangle)
        pulse = 18 + round(4 * math.sin((position - month) * math.pi * 3))
        pygame.draw.circle(screen, (248, 113, 113), centre, pulse, 2)
        draw_seir_event_icon(pygame, screen, centre, (248, 113, 113))
        draw_text(
            pygame,
            screen,
            fonts,
            label,
            (centre[0] + 19, centre[1] - 7),
            10,
            (254, 202, 202),
            1.0,
            True,
            "bottomleft",
        )
    for begins, ends, longitude, latitude, label, radius in SEIR_STORMS:
        if not begins <= position < ends:
            continue
        centre = seir_project(longitude, latitude, rectangle)
        reach = round(radius * rectangle.height / 180.0)
        pulse = round(reach * (0.82 + 0.08 * math.sin(position * 18.0)))
        pygame.draw.circle(screen, (56, 189, 248), centre, pulse, 2)
        pygame.draw.circle(
            screen, (125, 211, 252), centre, max(8, pulse // 2), 1
        )
        draw_seir_event_icon(pygame, screen, centre, (56, 189, 248))
        draw_text(
            pygame,
            screen,
            fonts,
            f"{label} · TURBULENCE + SEA STORM",
            (centre[0] + 19, centre[1] - 7),
            10,
            (186, 230, 253),
            1.0,
            True,
            "bottomleft",
        )
    colours = {
        "OPEN": (74, 222, 128),
        "RESTRICTED": (245, 158, 11),
        "BANNED": (239, 68, 68),
        "REROUTED": (236, 72, 153),
    }
    banner_row = 0
    for begins, ends, longitude, latitude, label, status in SEIR_OPERATIONS:
        if not begins <= position < ends:
            continue
        colour = colours[status]
        if longitude is not None:
            centre = seir_project(longitude, latitude, rectangle)
            draw_seir_event_icon(pygame, screen, centre, colour)
        banner = pygame.Rect(
            rectangle.left + 10,
            rectangle.top + 50 + banner_row * 27,
            280,
            22,
        )
        banner_row += 1
        pygame.draw.rect(screen, (7, 23, 41), banner, border_radius=5)
        pygame.draw.rect(screen, colour, banner, 2, border_radius=5)
        draw_text(
            pygame,
            screen,
            fonts,
            label,
            banner.center,
            10,
            colour,
            1.0,
            True,
        )
    screen.set_clip(None)


def seir_feed_events(position, slots=8):
    """Select recent feed events visible at a fractional month position"""
    eligible = tuple(event for event in SEIR_FEED if event[0] <= position)
    selected, seen = [], set()

    def add(event):
        identity = event[2] or event[3]
        if identity not in seen and len(selected) < slots:
            selected.append(event)
            seen.add(identity)

    for event in reversed(eligible):
        if event[1] == "DISASTER":
            add(event)
            break

    geography = (
        frozenset(
            ("AO", "CD", "CI", "EG", "ET", "GH", "KE", "MZ", "NG", "SN", "ZA")
        ),
        frozenset(("AR", "BO", "BR", "BS", "CL", "CO", "EC", "MX", "PE")),
        frozenset(("AF", "BD", "BT", "IN", "LK", "MV", "NP", "PK")),
        frozenset(("RU", "UA")),
        frozenset(("CN", "ID", "JP", "KR", "MM", "MY", "PH", "SG", "TH")),
        frozenset(("DE", "ES", "EU", "FR", "GB", "IT", "PL")),
        frozenset(
            (
                "AE",
                "BH",
                "IL",
                "IQ",
                "IR",
                "JO",
                "KW",
                "LB",
                "OM",
                "PS",
                "QA",
                "SA",
                "SY",
                "YE",
            )
        ),
        frozenset(("AQ", "ARC")),
        frozenset(("AU",)),
    )
    offset = int(position) % len(geography)
    geography = geography[offset:] + geography[:offset]
    for index in range(max(len(geography), 4)):
        if index < 4:
            for event in reversed(eligible):
                if event[1] == ("SPORTS", "TECH", "BUSINESS", "FUNNY")[index]:
                    add(event)
                    break
        if index >= len(geography):
            continue
        codes = geography[index]
        for event in reversed(eligible):
            if event[2] in codes:
                add(event)
                break
    for event in reversed(eligible):
        add(event)
    return tuple(sorted(selected))


def draw_seir_feed(pygame, screen, fonts, rectangle, position, cache):
    """Draw the scrolling feed from events selected for the current month"""
    # Draw panel chrome before selecting the current visible event window
    pygame.draw.rect(screen, (8, 25, 43), rectangle, border_radius=14)
    pygame.draw.rect(screen, (58, 124, 164), rectangle, 2, border_radius=14)
    draw_seir_chicken(
        pygame,
        screen,
        (rectangle.left + 24, rectangle.top + 28),
        0.75,
        cache=cache,
    )
    draw_text(
        pygame,
        screen,
        fonts,
        "WORLD FEED",
        (rectangle.left + 49, rectangle.top + 18),
        20,
        (222, 239, 247),
        1.0,
        True,
        "topleft",
    )
    events = seir_feed_events(position)
    if not events:
        draw_text(
            pygame,
            screen,
            fonts,
            "Waiting for the first major event…",
            (rectangle.centerx, rectangle.centery),
            16,
            (137, 177, 198),
        )
    # Each selected event becomes one card with a source-specific icon
    kind_colours = {
        "GOV": (96, 205, 234),
        "INFLUENCER": (250, 204, 21),
        "NEWS": (251, 146, 60),
        "EVENT": (167, 139, 250),
        "DISASTER": (248, 113, 113),
        "SPORTS": (74, 222, 128),
        "TECH": (56, 189, 248),
        "BUSINESS": (251, 191, 36),
        "FUNNY": (244, 114, 182),
    }
    kind_icons = {"SPORTS": "SP", "TECH": "{}", "BUSINESS": "$", "FUNNY": ":)"}
    for row, (month, kind, code, label, summary) in enumerate(events):
        top = rectangle.top + 58 + row * 90
        card = pygame.Rect(rectangle.left + 12, top, rectangle.width - 24, 84)
        pygame.draw.rect(screen, (12, 37, 59), card, border_radius=9)
        pygame.draw.rect(
            screen,
            (92, 166, 194) if row == len(events) - 1 else (40, 81, 108),
            card,
            1,
            border_radius=9,
        )
        icon = pygame.Rect(card.left + 11, card.top + 11, 36, 27)
        if kind == "NEWS":
            draw_seir_chicken(pygame, screen, icon.center, 0.82, code, cache)
        elif kind == "DISASTER":
            draw_seir_event_icon(
                pygame, screen, icon.center, kind_colours[kind]
            )
        elif kind in kind_icons:
            pygame.draw.rect(screen, (20, 67, 91), icon, border_radius=7)
            pygame.draw.rect(
                screen, kind_colours[kind], icon, 2, border_radius=7
            )
            draw_text(
                pygame,
                screen,
                fonts,
                kind_icons[kind],
                icon.center,
                12,
                kind_colours[kind],
                1.0,
                True,
            )
        elif code and (CHRONUS_FLAGS / f"{code.lower()}.svg").is_file():
            draw_seir_flag(pygame, screen, code, icon, cache)
        else:
            draw_seir_event_icon(pygame, screen, icon.center)
        draw_text(
            pygame,
            screen,
            fonts,
            label,
            (card.left + 55, card.top + 10),
            14,
            (235, 244, 248),
            1.0,
            True,
            "topleft",
        )
        draw_text(
            pygame,
            screen,
            fonts,
            seir_date(month).strftime("%b %Y").upper(),
            (card.right - 10, card.top + 11),
            11,
            (132, 179, 201),
            1.0,
            True,
            "topright",
        )
        draw_text(
            pygame,
            screen,
            fonts,
            kind,
            (card.left + 55, card.top + 27),
            11,
            kind_colours.get(kind, (103, 211, 235)),
            1.0,
            True,
            "topleft",
        )
        font = font_for(pygame, fonts, 13, False)
        for line, text_line in enumerate(
            caption_lines(font, summary, card.width - 22)[:2]
        ):
            draw_text(
                pygame,
                screen,
                fonts,
                text_line,
                (card.left + 11, card.top + 45 + line * 17),
                13,
                (218, 232, 240),
                1.0,
                False,
                "topleft",
            )


def draw_seir_flip_card(
    pygame, screen, fonts, rectangle, current, following, progress
):
    """Draw an information card that flips between its two text faces"""
    card = pygame.Surface(rectangle.size, pygame.SRCALPHA)
    card.fill((8, 11, 17, 252))
    size = min(58, max(31, round(rectangle.width * 1.9 / len(current))))
    font = font_for(pygame, fonts, size, True)

    def content(text):
        surface = pygame.Surface(rectangle.size, pygame.SRCALPHA)
        surface.fill((8, 11, 17, 252))
        rendered = font.render(text, True, (245, 248, 252))
        surface.blit(
            rendered, rendered.get_rect(center=surface.get_rect().center)
        )
        return surface

    present, future = content(current), content(following)
    card.blit(future, (0, 0))
    half = rectangle.height // 2
    progress = max(0.0, min(1.0, progress))
    progress = progress * progress * (3.0 - 2.0 * progress)
    if progress < 0.5:
        card.blit(present, (0, half), (0, half, rectangle.width, half))
        height = round(half * (1.0 - progress * 2.0))
        if height:
            flap = pygame.transform.smoothscale(
                present.subsurface((0, 0, rectangle.width, half)),
                (rectangle.width, height),
            )
            card.blit(flap, (0, half - height))
            shade = pygame.Surface((rectangle.width, height), pygame.SRCALPHA)
            shade.fill((0, 0, 0, round(105 * progress * 2.0)))
            card.blit(shade, (0, half - height))
    else:
        card.blit(present, (0, half), (0, half, rectangle.width, half))
        height = round(half * ((progress - 0.5) * 2.0))
        if height:
            flap = pygame.transform.smoothscale(
                future.subsurface((0, half, rectangle.width, half)),
                (rectangle.width, height),
            )
            card.blit(flap, (0, half))
            shade = pygame.Surface((rectangle.width, height), pygame.SRCALPHA)
            shade.fill((0, 0, 0, round(70 * (1.0 - progress) * 2.0)))
            card.blit(shade, (0, half))
    pygame.draw.line(card, (65, 73, 84), (0, half), (rectangle.width, half), 2)
    pygame.draw.rect(card, (79, 92, 108), card.get_rect(), 2, border_radius=10)
    for x in (6, rectangle.width - 6):
        pygame.draw.circle(card, (96, 108, 122), (x, half), 5)
        pygame.draw.circle(card, (25, 31, 40), (x, half), 2)
    screen.blit(card, rectangle)


def seir_calendar_values(position):
    current = seir_date(position)
    following = seir_date(min(SEIR_LAST_MONTH, int(position) + 1))
    fraction = position - math.floor(position)
    progress = 0.0 if fraction < 0.84 else (fraction - 0.84) / 0.16
    if int(position) >= SEIR_LAST_MONTH:
        progress = 0.0
    year_progress = progress if current.month == 12 else 0.0
    next_year = following.year if current.month == 12 else current.year
    return current, following, progress, next_year, year_progress


def draw_seir_calendar(pygame, screen, fonts, rectangle, position):
    """Draw the current month and phase of the Chronus timeline"""
    current, following, progress, next_year, year_progress = (
        seir_calendar_values(position)
    )
    draw_seir_flip_card(
        pygame,
        screen,
        fonts,
        pygame.Rect(rectangle.left, rectangle.top, 218, rectangle.height),
        current.strftime("%B").upper(),
        following.strftime("%B").upper(),
        progress,
    )
    draw_seir_flip_card(
        pygame,
        screen,
        fonts,
        pygame.Rect(rectangle.left + 230, rectangle.top, 132, rectangle.height),
        str(current.year),
        str(next_year),
        year_progress,
    )


def seir_count_text(value):
    for threshold, suffix in (
        (1_000_000_000, "B"),
        (1_000_000, "M"),
        (1_000, "K"),
    ):
        if value >= threshold:
            return f"{value / threshold:.1f}{suffix}"
    return str(round(value))


def draw_seir_health(pygame, screen, fonts, rectangle, features, states):
    """Draw global health totals and the selected regional population shares"""
    pygame.draw.rect(screen, (8, 25, 43), rectangle, border_radius=14)
    pygame.draw.rect(screen, (58, 124, 164), rectangle, 2, border_radius=14)
    population = sum(feature[1] for feature in features)
    totals = [
        sum(feature[1] * states[feature[0]][index] for feature in features)
        for index in range(5)
    ]
    draw_text(
        pygame,
        screen,
        fonts,
        "SIMULATED POPULATION STATES",
        (rectangle.left + 20, rectangle.top + 14),
        18,
        (222, 239, 247),
        1.0,
        True,
        "topleft",
    )
    bar = pygame.Rect(
        rectangle.left + 20, rectangle.top + 43, rectangle.width - 40, 30
    )
    pygame.draw.rect(screen, (24, 48, 65), bar, border_radius=8)
    left = bar.left
    for total, colour in zip(totals, SEIR_COLOURS):
        width = round(bar.width * total / population)
        if total and not width:
            width = 1
        pygame.draw.rect(screen, colour, (left, bar.top, width, bar.height))
        left += width
    pygame.draw.rect(screen, (177, 209, 223), bar, 1, border_radius=8)
    column = rectangle.width / 5
    for index, (label, colour, total) in enumerate(
        zip(SEIR_LABELS, SEIR_COLOURS, totals)
    ):
        x = round(rectangle.left + column * index + 20)
        pygame.draw.rect(
            screen, colour, (x, rectangle.top + 91, 10, 10), border_radius=2
        )
        draw_text(
            pygame,
            screen,
            fonts,
            label,
            (x + 16, rectangle.top + 87),
            14,
            (188, 216, 229),
            1.0,
            True,
            "topleft",
        )
        draw_text(
            pygame,
            screen,
            fonts,
            f"{seir_count_text(total)}  {100 * total / population:.2f}%",
            (x, rectangle.top + 109),
            18,
            (241, 247, 250),
            1.0,
            True,
            "topleft",
        )


def draw_seir_scene(
    pygame, screen, fonts, frame, position, cache, next_frame=None
):
    """Compose the Chronus scene from map, feed, calendar, and health panels"""
    if "seir-preloaded-flags" not in cache:
        cache["seir-preloaded-flags"] = tuple(
            image_surface(pygame, path, cache)
            for path in CHRONUS_FLAGS.glob("*.svg")
        )
    screen.fill((3, 11, 24))
    glow = pygame.Surface(screen.get_size(), pygame.SRCALPHA)
    for radius, alpha in ((420, 8), (270, 13), (140, 20)):
        pygame.draw.circle(glow, (76, 192, 240, alpha), (960, -50), radius)
    screen.blit(glow, (0, 0))
    pygame.draw.rect(
        screen, (10, 28, 47), (14, 14, 1892, 1052), border_radius=18
    )
    pygame.draw.rect(
        screen, (72, 143, 176), (14, 14, 1892, 1052), 2, border_radius=18
    )
    draw_text(
        pygame,
        screen,
        fonts,
        "Chronus",
        (38, 27),
        48,
        (232, 246, 252),
        1.0,
        True,
        "topleft",
    )
    features = seir_features(cache)
    regions = seir_regions(cache)
    states, flight_states, ship_states = seir_snapshot(frame, features)
    next_states = None
    if next_frame is not None:
        next_key = "seir-next-states", next_frame.number
        if next_key not in cache:
            cache[next_key] = seir_snapshot(next_frame, features)[0]
        next_states = cache[next_key]
    draw_seir_map(
        pygame,
        screen,
        fonts,
        pygame.Rect(36, 88, 1518, 798),
        position,
        features,
        regions,
        states,
        flight_states,
        ship_states,
        cache,
        next_states,
    )
    draw_seir_feed(
        pygame,
        screen,
        fonts,
        pygame.Rect(1570, 88, 314, 798),
        position,
        cache,
    )
    draw_seir_calendar(
        pygame, screen, fonts, pygame.Rect(36, 906, 362, 142), position
    )
    draw_seir_health(
        pygame,
        screen,
        fonts,
        pygame.Rect(420, 906, 1464, 142),
        features,
        states,
    )


draw_scene = draw_seir_scene
