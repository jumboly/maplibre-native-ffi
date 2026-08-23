"""実験パッチ（MLN_TILE_MATRIX_WARP）の歪みケースを一括レンダする。

歪み行列 W は world mercator pixel（論理 px）空間で作り、画面中心周りの共役
T(c)·A·T(-c) で組む。パッチ側は column-major の 16 数値を左乗算するだけなので、
ケースの意味論（回転・せん断・射影）はすべてここで定義する。

検証の自動化部分:
- baseline_stdstyle（env 未設定・phase0 スタイル）が phase0 の既存出力と
  画素一致すること → パッチが未設定時に完全に従来動作である証拠
- identity（W=I）が baseline_nowarp（env 未設定・phase1 スタイル）と画素一致する
  こと → 注入経路（パース + 乗算）自体が無害である証拠
- projective_x2 と projective の目視比較は README のチェックリストで行う
  （幾何は同次スカラー倍で不変、symbol サイズだけ変わるはず）
"""

import math
import os
import subprocess
import sys
from pathlib import Path

PHASE0_DIR = Path(__file__).resolve().parent.parent / "phase0"
sys.path.insert(0, str(PHASE0_DIR))

from cases import CASES, Case

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_MBGL_RENDER = REPO_ROOT / "build" / "mbgl-render" / "bin" / "mbgl-render"
PHASE0_BASE = REPO_ROOT / "build" / "render-crs-phase0"
BASE_DIR = REPO_ROOT / "build" / "render-crs-phase1"
ALLLAYERS_STYLE = BASE_DIR / "style" / "alllayers.json"
OUT_DIR = BASE_DIR / "out"

CASE = next(c for c in CASES if c.name == "tocho_jprcs9_1-25000_150dpi")

Mat4 = list[list[float]]  # 行優先の 4×4。環境変数へは column-major で流し込む


def identity() -> Mat4:
    return [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]


def multiply(a: Mat4, b: Mat4) -> Mat4:
    return [
        [sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)
    ]


def translate(tx: float, ty: float) -> Mat4:
    m = identity()
    m[0][3] = tx
    m[1][3] = ty
    return m


def about_center(linear: Mat4, cx: float, cy: float) -> Mat4:
    """画面中心 c を不動点にする共役 T(c)·A·T(-c)。"""
    return multiply(translate(cx, cy), multiply(linear, translate(-cx, -cy)))


def to_env(m: Mat4) -> str:
    return ",".join(f"{m[r][c]:.17g}" for c in range(4) for r in range(4))


def center_world_px(case: Case) -> tuple[float, float]:
    """カメラ中心の world mercator pixel（論理 px）座標。matrixFor の出力空間。"""
    world = 512 * 2**case.zoom
    x = (case.center_lng + 180) / 360 * world
    lat = math.radians(case.center_lat)
    y = (1 - math.log(math.tan(math.pi / 4 + lat / 2)) / math.pi) / 2 * world
    return x, y


def warp_cases(case: Case) -> dict[str, Mat4 | None]:
    cx, cy = center_world_px(case)

    rotation = identity()
    theta = math.radians(10)
    # world px は y 下向きなので、この符号で画面上は反時計回りに見える
    rotation[0][0] = math.cos(theta)
    rotation[0][1] = math.sin(theta)
    rotation[1][0] = -math.sin(theta)
    rotation[1][1] = math.cos(theta)

    shear = identity()
    shear[0][0] = 0.95
    shear[0][1] = 0.2
    shear[1][1] = 1.05

    # w' = 1 + p·(x-cx) が画面左右端で 1±0.25 になる射影成分
    projective = identity()
    projective[3][0] = 0.25 / (case.render_width / 2)

    projective_x2 = [[v * 2.0 for v in row] for row in about_center(projective, cx, cy)]

    return {
        "baseline_nowarp": None,  # 環境変数そのものを渡さない
        "identity": identity(),
        "rotate10": about_center(rotation, cx, cy),
        "shear": about_center(shear, cx, cy),
        "projective": about_center(projective, cx, cy),
        "projective_x2": projective_x2,
    }


def render(
    mbgl_render: Path, style: Path, out_png: Path, warp: Mat4 | None, case: Case
) -> None:
    command = [
        str(mbgl_render),
        "--style",
        str(style),
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
        str(PHASE0_BASE / "cache.sqlite"),  # 温まった vector タイルを流用
        "--output",
        str(out_png),
    ]
    env = dict(os.environ)
    env.pop("MLN_TILE_MATRIX_WARP", None)
    if warp is not None:
        env["MLN_TILE_MATRIX_WARP"] = to_env(warp)
    subprocess.run(command, check=True, env=env)
    print(f"wrote {out_png.name}")


def find_gdal_prefix() -> str:
    result = subprocess.run(
        ["mise", "where", "conda:gdal"], capture_output=True, text=True, check=True
    )
    return result.stdout.strip()


DIFF_SCRIPT = """
import sys
import numpy as np
from osgeo import gdal
gdal.UseExceptions()
def load(p):
    ds = gdal.Open(p)
    bands = [ds.GetRasterBand(i + 1).ReadAsArray() for i in range(ds.RasterCount)]
    return np.stack(bands, -1).astype(np.int32)
a, b = load(sys.argv[1]), load(sys.argv[2])
diff = np.abs(a - b)
print(int((diff.max(-1) > 0).sum()), int(diff.max()))
"""

# Metal レンダは同条件でも GPU ラスタライズの LSB ノイズが数画素乗る（実測:
# 2.5M 画素中 8 画素・最大 1LSB）。「一致」はこのノイズ床までを許容する。
MAX_DIFF_PIXELS = 250  # ≈0.01%
MAX_CHANNEL_DELTA = 2


def assert_same_pixels(a: Path, b: Path, label: str) -> None:
    python = Path(find_gdal_prefix()) / "bin" / "python"
    result = subprocess.run(
        [str(python), "-c", DIFF_SCRIPT, str(a), str(b)],
        capture_output=True,
        text=True,
        check=True,
    )
    diff_pixels, max_delta = (int(v) for v in result.stdout.split())
    if diff_pixels > MAX_DIFF_PIXELS or max_delta > MAX_CHANNEL_DELTA:
        sys.exit(
            f"NG: {label}: {a.name} と {b.name} が不一致"
            f"（diff px={diff_pixels}, max delta={max_delta}）"
        )
    print(
        f"OK: {label}: {a.name} == {b.name} (diff px={diff_pixels}, max delta={max_delta})"
    )


def main() -> None:
    mbgl_render = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MBGL_RENDER
    if not mbgl_render.is_file():
        sys.exit(f"mbgl-render がありません: {mbgl_render}")
    if not ALLLAYERS_STYLE.is_file():
        sys.exit(f"スタイルがありません: {ALLLAYERS_STYLE}（先に make_style.py）")
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # パッチ無害性の検証: phase0 の出力（パッチ前バイナリ産）と画素一致すること
    std_style = PHASE0_BASE / "style" / "std-xyz.json"
    baseline_std = OUT_DIR / "baseline_stdstyle.png"
    render(mbgl_render, std_style, baseline_std, None, CASE)
    phase0_png = PHASE0_BASE / "out" / f"{CASE.name}.merc.png"
    if phase0_png.is_file():
        assert_same_pixels(baseline_std, phase0_png, "パッチ前後の一致")
    else:
        print(f"skip: {phase0_png} が無いためパッチ前後比較は省略")

    # raster タイルのコールドキャッシュ初回はフェッチが間に合わず画素が揺れる
    # （実測）。捨てレンダで温めてから比較対象を出す。
    render(mbgl_render, ALLLAYERS_STYLE, OUT_DIR / "warmup.png", None, CASE)

    for name, warp in warp_cases(CASE).items():
        render(mbgl_render, ALLLAYERS_STYLE, OUT_DIR / f"{name}.png", warp, CASE)

    assert_same_pixels(
        OUT_DIR / "identity.png", OUT_DIR / "baseline_nowarp.png", "identity の無害性"
    )


if __name__ == "__main__":
    main()
