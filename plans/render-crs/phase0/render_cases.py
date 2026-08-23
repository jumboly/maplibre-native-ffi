"""mbgl-render で各ケースを Mercator レンダし、ワールドファイルを添えて保存する。

出力 PNG は EPSG:3857。左上ピクセル中心原点の .pgw と GDAL PAM の .png.aux.xml を
書くので、QGIS にそのまま読み込める（GL JS 版 world-file.ts の移植）。
"""

import struct
import subprocess
import sys
from pathlib import Path

from cases import CASES, EARTH_CIRCUMFERENCE_M, Case, lng_lat_to_mercator

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_MBGL_RENDER = REPO_ROOT / "build" / "mbgl-render" / "bin" / "mbgl-render"
BASE_DIR = REPO_ROOT / "build" / "render-crs-phase0"
STYLE_PATH = BASE_DIR / "style" / "std-xyz.json"
OUT_DIR = BASE_DIR / "out"


def png_size(path: Path) -> tuple[int, int]:
    """PNG の IHDR から寸法を読む（出力が期待どおり floor(w·r) かの検算用）。"""
    header = path.read_bytes()[:24]
    width, height = struct.unpack(">II", header[16:24])
    return width, height


def mercator_world_file(case: Case, width_px: int, height_px: int) -> str:
    """EPSG:3857 レンダの .pgw を組み立てる。

    ピクセルサイズは zoom から導出した Mercator メートル/物理 px。原点は左上
    ピクセルの中心（GDAL のワールドファイル規約どおり半ピクセル内側）。
    """
    world_phys_px = 512 * 2**case.zoom * case.pixel_ratio
    merc_per_px = EARTH_CIRCUMFERENCE_M / world_phys_px
    center_x, center_y = lng_lat_to_mercator(case.center_lng, case.center_lat)
    top_left_x = center_x - merc_per_px * (width_px / 2 - 0.5)
    top_left_y = center_y + merc_per_px * (height_px / 2 - 0.5)
    values = [merc_per_px, 0.0, 0.0, -merc_per_px, top_left_x, top_left_y]
    return "".join(f"{value:.17g}\n" for value in values)


def gdal_pam_aux_xml(epsg: int) -> str:
    return f"<PAMDataset>\n  <SRS>EPSG:{epsg}</SRS>\n</PAMDataset>\n"


def render_case(mbgl_render: Path, case: Case) -> None:
    out_png = OUT_DIR / f"{case.name}.merc.png"
    command = [
        str(mbgl_render),
        "--style",
        str(STYLE_PATH),
        "--lon",
        f"{case.center_lng:.17g}",
        "--lat",
        f"{case.center_lat:.17g}",
        "--zoom",
        f"{case.zoom:.17g}",
        "--ratio",
        f"{case.pixel_ratio:.17g}",
        "--width",
        str(case.render_width),
        "--height",
        str(case.render_height),
        "--mode",
        "static",
        "--cache",
        str(BASE_DIR / "cache.sqlite"),
        "--output",
        str(out_png),
    ]
    print(
        f"[{case.name}] z={case.zoom:.4f} r={case.pixel_ratio:g} "
        f"{case.render_width}x{case.render_height} (logical)"
    )
    subprocess.run(command, check=True)

    width_px, height_px = png_size(out_png)
    expected = (
        int(case.render_width * case.pixel_ratio),
        int(case.render_height * case.pixel_ratio),
    )
    if (width_px, height_px) != expected:
        sys.exit(
            f"[{case.name}] 出力寸法 {width_px}x{height_px} が期待 {expected} と不一致"
        )

    out_png.with_suffix(".pgw").write_text(
        mercator_world_file(case, width_px, height_px)
    )
    (OUT_DIR / f"{out_png.name}.aux.xml").write_text(gdal_pam_aux_xml(3857))
    print(
        f"[{case.name}] wrote {out_png.name} ({width_px}x{height_px}) + .pgw + .aux.xml"
    )


def main() -> None:
    mbgl_render = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MBGL_RENDER
    if not mbgl_render.is_file():
        sys.exit(
            f"mbgl-render が見つかりません: {mbgl_render}（README のビルド手順を参照）"
        )
    if not STYLE_PATH.is_file():
        sys.exit(f"スタイルがありません: {STYLE_PATH}（先に fetch_style.py を実行）")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    names = set(sys.argv[2:])
    for case in CASES:
        if names and case.name not in names:
            continue
        render_case(mbgl_render, case)


if __name__ == "__main__":
    main()
