"""render-crs フェーズ 3（PoC）のビルド・レンダ・検証ドライバ。

render_case.c を pkg-config でビルドし、都庁 IX 系 1:5000 ケースを
--mercator（ベースライン）と --render-crs（フック方式）でレンダする。
mercator 出力は phase0 の mbgl-render 出力と画素照合し（レンダ経路の
無害性）、render-crs 出力には JPRCS のワールドファイルを添えて QGIS /
crop_compare.py の重畳検証に回す。rotation=10 の符号スモークも出す。

前提: `mise run build`（macos-arm64-metal）と phase0 の fetch_style.py /
render_cases.py が済んでいること（スタイルとタイルキャッシュを流用する）。
"""

import math
import shutil
import subprocess
import sys
from pathlib import Path

PHASE0_DIR = Path(__file__).resolve().parent.parent / "phase0"
sys.path.insert(0, str(PHASE0_DIR))

from cases import CASES, Case

REPO_ROOT = Path(__file__).resolve().parents[3]
PRESET = "macos-arm64-metal"
INSTALL_DIR = REPO_ROOT / "build" / PRESET / "install"
PHASE0_BASE = REPO_ROOT / "build" / "render-crs-phase0"
BASE_DIR = REPO_ROOT / "build" / "render-crs-phase3"
OUT_DIR = BASE_DIR / "out"
RENDER_CASE_SRC = Path(__file__).resolve().parent / "render_case.c"
RENDER_CASE_BIN = BASE_DIR / "render_case"

CASE = next(c for c in CASES if c.name == "tocho_jprcs9_1-5000_300dpi")


def find_gdal_prefix() -> str:
    result = subprocess.run(
        ["mise", "where", "conda:gdal"], capture_output=True, text=True, check=True
    )
    return result.stdout.strip()


# phase1/run_cases.py の画素比較ハーネスの転写。PPM 由来の 3 バンドと
# mbgl-render PNG の 4 バンドを比較できるよう、共通バンド数（=RGB）に
# 切り詰める点だけが違う（背景は不透明なのでアルファに情報はない）。
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
n = min(a.shape[-1], b.shape[-1])
a, b = a[..., :n], b[..., :n]
diff = np.abs(a - b)
print(int((diff.max(-1) > 0).sum()), int(diff.max()))
"""

# Metal の GPU ラスタライズ LSB ノイズ床（phase1 の実測に基づく閾値）。
MAX_DIFF_PIXELS = 250
MAX_CHANNEL_DELTA = 2


def compare_pixels(a: Path, b: Path, label: str) -> None:
    python = Path(find_gdal_prefix()) / "bin" / "python"
    result = subprocess.run(
        [str(python), "-c", DIFF_SCRIPT, str(a), str(b)],
        capture_output=True,
        text=True,
        check=True,
    )
    diff_pixels, max_delta = (int(v) for v in result.stdout.split())
    verdict = (
        "OK"
        if diff_pixels <= MAX_DIFF_PIXELS and max_delta <= MAX_CHANNEL_DELTA
        else "NG"
    )
    print(
        f"{verdict}: {label}: {a.name} vs {b.name} "
        f"(diff px={diff_pixels}, max delta={max_delta})"
    )
    if verdict == "NG":
        sys.exit(1)


def build_render_case() -> None:
    pkgconfig_dir = INSTALL_DIR / "share" / "pkgconfig"
    if not pkgconfig_dir.is_dir():
        sys.exit(f"インストール先がありません: {pkgconfig_dir}（先に mise run build）")

    def pkg_config(*args: str) -> list[str]:
        result = subprocess.run(
            ["pkg-config", *args, "maplibre-native-c"],
            capture_output=True,
            text=True,
            check=True,
            env={
                "PKG_CONFIG_PATH": str(pkgconfig_dir),
                "PATH": "/usr/bin:/bin:/opt/homebrew/bin:/usr/local/bin",
            },
        )
        return result.stdout.split()

    command = [
        "cc",
        "-std=c17",
        "-Wall",
        "-Wextra",
        str(RENDER_CASE_SRC),
        *pkg_config("--cflags"),
        *pkg_config("--libs"),
        "-framework",
        "Metal",
        f"-Wl,-rpath,{INSTALL_DIR / 'lib'}",
        "-o",
        str(RENDER_CASE_BIN),
    ]
    subprocess.run(command, check=True)
    print(f"built {RENDER_CASE_BIN}")


def render(mode_args: list[str], out_ppm: Path) -> None:
    style_url = (PHASE0_BASE / "style" / "std-xyz.json").resolve().as_uri()
    # phase0 のキャッシュはコアが in-place 更新し得るので、原本を守るため
    # コピーを使う。
    cache = BASE_DIR / "cache.sqlite"
    if not cache.is_file():
        source = PHASE0_BASE / "cache.sqlite"
        if not source.is_file():
            sys.exit(f"キャッシュがありません: {source}（先に phase0 を実行）")
        shutil.copyfile(source, cache)

    command = [
        str(RENDER_CASE_BIN),
        style_url,
        str(cache),
        str(CASE.render_width),
        str(CASE.render_height),
        f"{CASE.pixel_ratio:.17g}",
        str(out_ppm),
        *mode_args,
    ]
    subprocess.run(command, check=True)


def to_png(ppm: Path) -> Path:
    png = ppm.with_suffix(".png")
    gdal_translate = Path(find_gdal_prefix()) / "bin" / "gdal_translate"
    subprocess.run(
        [str(gdal_translate), "-q", "-of", "PNG", str(ppm), str(png)], check=True
    )
    # gdal_translate の PNG ドライバが置く PAM を、後で書く SRS 入りと衝突
    # しないよう消しておく。
    (png.parent / f"{png.name}.aux.xml").unlink(missing_ok=True)
    return png


def jprcs_world_file(case: Case, rotation_deg: float) -> str:
    """JPRCS レンダの .pgw。原点は左上ピクセル中心、回転項付き。

    画像の上方向が座標北から時計回りに rotation_deg を指すとき、列方向
    （画像右）は方位角 θ+90°、行方向（画像下）は θ+180° を指す。方位角 α の
    単位ベクトルは (E, N) = (sin α, cos α)。
    """
    theta = math.radians(rotation_deg)
    width_px = round(case.render_width * case.pixel_ratio)
    height_px = round(case.render_height * case.pixel_ratio)
    mpp = case.meters_per_pixel
    col_e, col_n = mpp * math.cos(theta), -mpp * math.sin(theta)
    row_e, row_n = -mpp * math.sin(theta), -mpp * math.cos(theta)
    top_left_e = (
        case.center_e - (width_px / 2 - 0.5) * col_e - (height_px / 2 - 0.5) * row_e
    )
    top_left_n = (
        case.center_n - (width_px / 2 - 0.5) * col_n - (height_px / 2 - 0.5) * row_n
    )
    values = [col_e, col_n, row_e, row_n, top_left_e, top_left_n]
    return "".join(f"{value:.17g}\n" for value in values)


def gdal_pam_aux_xml(epsg: int) -> str:
    return f"<PAMDataset>\n  <SRS>EPSG:{epsg}</SRS>\n</PAMDataset>\n"


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    build_render_case()

    # ① Mercator ベースライン: レンダ経路（FFI texture session）が phase0 の
    # mbgl-render と同じ絵を出すことの確認。
    merc_ppm = OUT_DIR / f"{CASE.name}.merc.ppm"
    render(
        [
            "mercator",
            f"{CASE.center_lng:.17g}",
            f"{CASE.center_lat:.17g}",
            f"{CASE.zoom:.17g}",
        ],
        merc_ppm,
    )
    merc_png = to_png(merc_ppm)
    phase0_png = PHASE0_BASE / "out" / f"{CASE.name}.merc.png"
    if phase0_png.is_file():
        compare_pixels(merc_png, phase0_png, "mercator ベースラインの一致")
    else:
        print(f"skip: {phase0_png} が無いため phase0 比較は省略")

    # ② render-crs 本番: JPRCS ワールドファイル付きで出力し、QGIS /
    # crop_compare.py の重畳検証に回す。
    crs_ppm = OUT_DIR / f"{CASE.name}.crs.ppm"
    render(
        [
            "render-crs",
            str(CASE.epsg - 6668),
            f"{CASE.center_e:.17g}",
            f"{CASE.center_n:.17g}",
            f"{CASE.meters_per_pixel:.17g}",
            "0",
        ],
        crs_ppm,
    )
    crs_png = to_png(crs_ppm)
    crs_png.with_suffix(".pgw").write_text(jprcs_world_file(CASE, 0.0))
    (OUT_DIR / f"{crs_png.name}.aux.xml").write_text(gdal_pam_aux_xml(CASE.epsg))
    print(f"wrote {crs_png.name} + .pgw + .aux.xml (EPSG:{CASE.epsg})")

    # ③ rotation=10° の符号スモーク: 出力上で座標北が反時計回りに 10° 傾いて
    # 見える（=北を向く道路が左に倒れる）ことを目視確認する。ワールド
    # ファイル付きなので QGIS 重畳でも照合できる。
    rot_ppm = OUT_DIR / f"{CASE.name}.crs-rot10.ppm"
    render(
        [
            "render-crs",
            str(CASE.epsg - 6668),
            f"{CASE.center_e:.17g}",
            f"{CASE.center_n:.17g}",
            f"{CASE.meters_per_pixel:.17g}",
            "10",
        ],
        rot_ppm,
    )
    rot_png = to_png(rot_ppm)
    rot_png.with_suffix(".pgw").write_text(jprcs_world_file(CASE, 10.0))
    (OUT_DIR / f"{rot_png.name}.aux.xml").write_text(gdal_pam_aux_xml(CASE.epsg))
    print(f"wrote {rot_png.name} + .pgw + .aux.xml (EPSG:{CASE.epsg}, rot10)")


if __name__ == "__main__":
    main()
