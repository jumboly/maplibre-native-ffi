"""全レイヤ検証スタイルを生成する。

GSI std（background/fill/line/symbol のみ）に、raster レイヤ（GSI 標準地図タイル、
半透明）と circle レイヤ（500 m 格子の GeoJSON 点群）を足す。歪み注入が
レイヤ種別を問わず一様に効くか（タイル境界の割れ・レイヤ間のずれが無いか）を
1 枚の絵で目視するための素材で、格子は歪みの形状リファレンスも兼ねる。
"""

import json
import math
import sys
from pathlib import Path

PHASE0_DIR = Path(__file__).resolve().parent.parent / "phase0"
sys.path.insert(0, str(PHASE0_DIR))

from cases import CASES, EARTH_CIRCUMFERENCE_M, lng_lat_to_mercator

REPO_ROOT = Path(__file__).resolve().parents[3]
BASE_STYLE = REPO_ROOT / "build" / "render-crs-phase0" / "style" / "std-xyz.json"
OUT_PATH = REPO_ROOT / "build" / "render-crs-phase1" / "style" / "alllayers.json"

CASE = next(c for c in CASES if c.name == "tocho_jprcs9_1-25000_150dpi")
GRID_SPACING_M = 500  # Mercator メートル。1:25000 では約 61px 間隔
RASTER_TILES = "https://cyberjapandata.gsi.go.jp/xyz/std/{z}/{x}/{y}.png"


def mercator_to_lng_lat(x: float, y: float) -> tuple[float, float]:
    half = EARTH_CIRCUMFERENCE_M / 2
    lng = x / half * 180
    lat = math.degrees(2 * math.atan(math.exp(y / half * math.pi)) - math.pi / 2)
    return lng, lat


def grid_geojson() -> dict:
    """ケース中心の周囲を覆う 500 m 格子点。

    覆う範囲はレンダ範囲（論理 px × zoom のメートル換算）+ 余白 1 割。歪みで
    画面内へ入ってくる分も拾えるよう、レンダ範囲より広めに出しておく。
    """
    world_px = 512 * 2**CASE.zoom
    merc_per_px = EARTH_CIRCUMFERENCE_M / world_px
    half_w = CASE.render_width / 2 * merc_per_px * 1.1
    half_h = CASE.render_height / 2 * merc_per_px * 1.1
    cx, cy = lng_lat_to_mercator(CASE.center_lng, CASE.center_lat)
    features = []
    nx = int(half_w // GRID_SPACING_M)
    ny = int(half_h // GRID_SPACING_M)
    for i in range(-nx, nx + 1):
        for j in range(-ny, ny + 1):
            lng, lat = mercator_to_lng_lat(
                cx + i * GRID_SPACING_M, cy + j * GRID_SPACING_M
            )
            features.append(
                {
                    "type": "Feature",
                    "geometry": {"type": "Point", "coordinates": [lng, lat]},
                    "properties": {},
                }
            )
    return {"type": "FeatureCollection", "features": features}


def main() -> None:
    if not BASE_STYLE.is_file():
        sys.exit(
            f"ベーススタイルがありません: {BASE_STYLE}（phase0/fetch_style.py を先に）"
        )
    style = json.loads(BASE_STYLE.read_text())

    style["sources"]["phase1-raster"] = {
        "type": "raster",
        "tiles": [RASTER_TILES],
        "tileSize": 256,
        "maxzoom": 18,
        "attribution": "国土地理院",
    }
    style["sources"]["phase1-grid"] = {"type": "geojson", "data": grid_geojson()}

    # raster は background 直後（他レイヤが透けるよう半透明）、circle は最上位
    raster_layer = {
        "id": "phase1-raster",
        "type": "raster",
        "source": "phase1-raster",
        # fade 途中の α が静止画へ混入すると同条件レンダの画素が揺れる（実測）
        "paint": {"raster-opacity": 0.35, "raster-fade-duration": 0},
    }
    circle_layer = {
        "id": "phase1-grid",
        "type": "circle",
        "source": "phase1-grid",
        "paint": {
            "circle-radius": 4,
            "circle-color": "#e60012",
            "circle-stroke-width": 1,
            "circle-stroke-color": "#ffffff",
        },
    }
    layers = style["layers"]
    background_index = next(
        i for i, layer in enumerate(layers) if layer["type"] == "background"
    )
    layers.insert(background_index + 1, raster_layer)
    layers.append(circle_layer)

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(style, ensure_ascii=False, indent=1))
    grid_count = len(style["sources"]["phase1-grid"]["data"]["features"])
    print(f"wrote {OUT_PATH} (grid points: {grid_count})")


if __name__ == "__main__":
    main()
