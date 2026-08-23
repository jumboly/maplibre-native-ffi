"""Mercator レンダを gdalwarp で平面直角座標系へ再投影する。

ワープ先グリッドはケースパラメータ（JPRCS 中心・meters_per_pixel・用紙 px）から
導出し、-te/-ts で固定する。後続フェーズ（フック方式）の出力も同じグリッドで
生成すれば、ピクセル単位の重畳比較ができる。品質評価のためリサンプラ 3 変種を出す。
"""

import os
import subprocess
import sys
from pathlib import Path

from cases import CASES, Case

REPO_ROOT = Path(__file__).resolve().parents[3]
BASE_DIR = REPO_ROOT / "build" / "render-crs-phase0"
OUT_DIR = BASE_DIR / "out"
WARP_DIR = BASE_DIR / "warp"

RESAMPLERS = ["bilinear", "cubic", "lanczos"]


def find_gdalwarp() -> str:
    """conda:gdal の gdalwarp を解決する。

    mise の conda バックエンドは Python スクリプト系しか PATH に乗せないため、
    examples/c-map の conda:sdl3 と同じく `mise where` で prefix を取る。
    """
    if override := os.environ.get("GDALWARP"):
        return override
    result = subprocess.run(
        ["mise", "where", "conda:gdal"], capture_output=True, text=True, check=False
    )
    if result.returncode == 0:
        candidate = Path(result.stdout.strip()) / "bin" / "gdalwarp"
        if candidate.is_file():
            return str(candidate)
    return "gdalwarp"  # PATH 任せ（mise 外での実行向け）


def warp_case(gdalwarp: str, case: Case) -> None:
    source = OUT_DIR / f"{case.name}.merc.png"
    if not source.is_file():
        sys.exit(f"入力がありません: {source}（先に render_cases.py を実行）")

    mpp = case.meters_per_pixel
    half_width_m = mpp * case.target_width_px / 2
    half_height_m = mpp * case.target_height_px / 2
    ulx = case.center_e - half_width_m
    uly = case.center_n + half_height_m
    lrx = case.center_e + half_width_m
    lry = case.center_n - half_height_m

    for resampler in RESAMPLERS:
        target = WARP_DIR / f"{case.name}.warp-{resampler}.tif"
        command = [
            gdalwarp,
            "-overwrite",
            "-s_srs",
            "EPSG:3857",
            "-t_srs",
            f"EPSG:{case.epsg}",
            "-te",
            f"{ulx:.17g}",
            f"{lry:.17g}",
            f"{lrx:.17g}",
            f"{uly:.17g}",
            "-ts",
            str(case.target_width_px),
            str(case.target_height_px),
            "-r",
            resampler,
            # 既定の近似変換（誤差 0.125px）を切り、幾何そのものを評価対象にする
            "-et",
            "0",
            "-of",
            "GTiff",
            "-co",
            "COMPRESS=DEFLATE",
            str(source),
            str(target),
        ]
        subprocess.run(command, check=True, capture_output=True, text=True)
        print(
            f"[{case.name}] wrote {target.name} "
            f"({case.target_width_px}x{case.target_height_px} @ {mpp:g} m/px)"
        )


def main() -> None:
    gdalwarp = find_gdalwarp()
    WARP_DIR.mkdir(parents=True, exist_ok=True)
    names = set(sys.argv[1:])
    for case in CASES:
        if names and case.name not in names:
            continue
        warp_case(gdalwarp, case)


if __name__ == "__main__":
    main()
