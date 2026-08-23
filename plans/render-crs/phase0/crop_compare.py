"""品質比較用の拡大クロップを生成する。

Mercator 元画像と warp 変種から同一箇所を切り出し、4 倍拡大（nearest）で
build/render-crs-phase0/crops/ に保存する。フェーズ 3 の受け入れ時に、フック方式の
出力へ同じ窓を適用して並置比較する。窓は warp グリッド基準で、Mercator 側は
中心対応（両画像とも同一地点を中心に持つ）から換算した値。
"""

import subprocess
import sys
from pathlib import Path

from warp_cases import find_gdalwarp

REPO_ROOT = Path(__file__).resolve().parents[3]
BASE_DIR = REPO_ROOT / "build" / "render-crs-phase0"
CROP_DIR = BASE_DIR / "crops"

ZOOM = 4
# (領域名, ケース名, warp 窓 x/y, merc 窓 x/y, 幅, 高さ)
REGIONS = [
    # 都庁ラベル（画像中心付近 = ワープ変位がほぼゼロの領域）
    ("tocho-label", "tocho_jprcs9_1-5000_300dpi", 2400, 1690, 2469, 1748, 130, 100),
    # 都庁 左上隅（γ≈0.08° の回転変位 ≈3.5px が効く領域）
    ("tocho-corner", "tocho_jprcs9_1-5000_300dpi", 200, 200, 269, 258, 130, 100),
    # 根室駅ラベル（γ≈0.92° → 全面で実質的な回転リサンプリング）
    ("nemuro-station", "nemuro_jprcs13_1-5000_96dpi", 375, 590, 425, 640, 130, 100),
]


def gdal_translate_path() -> str:
    return str(Path(find_gdalwarp()).with_name("gdal_translate"))


def crop(
    translate: str, source: Path, out: Path, x: int, y: int, w: int, h: int
) -> None:
    subprocess.run(
        [
            translate,
            "-q",
            "-of",
            "PNG",
            "-srcwin",
            str(x),
            str(y),
            str(w),
            str(h),
            "-outsize",
            str(w * ZOOM),
            str(h * ZOOM),
            "-r",
            "nearest",
            str(source),
            str(out),
        ],
        check=True,
    )
    print(f"wrote {out.relative_to(BASE_DIR)}")


def main() -> None:
    translate = gdal_translate_path()
    CROP_DIR.mkdir(parents=True, exist_ok=True)
    for region, case, wx, wy, mx, my, w, h in REGIONS:
        merc = BASE_DIR / "out" / f"{case}.merc.png"
        if not merc.is_file():
            sys.exit(f"入力がありません: {merc}（先に render_cases.py を実行）")
        crop(translate, merc, CROP_DIR / f"{region}.merc.png", mx, my, w, h)
        for resampler in ["bilinear", "cubic", "lanczos"]:
            warp = BASE_DIR / "warp" / f"{case}.warp-{resampler}.tif"
            crop(translate, warp, CROP_DIR / f"{region}.{resampler}.png", wx, wy, w, h)


if __name__ == "__main__":
    main()
