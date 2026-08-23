"""フェーズ 0 比較ケースの定義と共有数式。

中心座標は GSI 測量計算 API（bl2xy.pl, refFrame=2）の公式値。zoom は GL JS 版
print-extent.ts の equivalentZoom と同一式で、dpi は pixelRatio = dpi/96 が担う
（zoom に dpi を混ぜるとスタイルの zoom 依存表現が縮尺に対して不自然になるため）。
"""

import math
from dataclasses import dataclass

EARTH_CIRCUMFERENCE_M = 40075016.686
HALF_CIRCUMFERENCE_M = EARTH_CIRCUMFERENCE_M / 2


@dataclass(frozen=True)
class Case:
    name: str
    epsg: int  # ワープ先（JGD2011 平面直角座標系 = 6668 + 系番号）
    center_lng: float
    center_lat: float
    center_e: float  # JPRCS 中心 [m]（GIS 慣例の E=x / N=y 軸順）
    center_n: float
    scale: int  # 縮尺分母
    dpi: int
    # ワープ先グリッド [px]。用紙寸法 × dpi（A3 横 = 420×297mm、A4 横 = 297×210mm）
    target_width_px: int
    target_height_px: int
    # mbgl-render へ渡す論理ピクセル寸法。w·r と h·r が整数になるよう選ぶ
    # （HeadlessFrontend は floor(w·r) に切り捨てるため、非整数はスケール歪みになる）。
    # 物理寸法はワープ先より 3〜8% 大きく取り、根室の回転（γ≈0.92° → 端で約 9px）と
    # 端部リサンプリングの余白にする。
    render_width: int
    render_height: int

    @property
    def pixel_ratio(self) -> float:
        return self.dpi / 96

    @property
    def meters_per_pixel(self) -> float:
        """ワープ先グリッドの解像度 [m/px]。"""
        return self.scale * 0.0254 / self.dpi

    @property
    def zoom(self) -> float:
        return equivalent_zoom(self.scale, self.center_lat)


def equivalent_zoom(scale: int, center_lat: float) -> float:
    """縮尺と中心緯度から Mercator 意味論の zoom を求める（96dpi の CSS px 基準）。"""
    meters_per_css_pixel = scale * 0.0254 / 96
    return math.log2(
        EARTH_CIRCUMFERENCE_M
        * math.cos(math.radians(center_lat))
        / (512 * meters_per_css_pixel)
    )


def lng_lat_to_mercator(lng: float, lat: float) -> tuple[float, float]:
    """経緯度 → EPSG:3857 [m]。"""
    x = lng / 180 * HALF_CIRCUMFERENCE_M
    y = (
        math.log(math.tan(math.pi / 4 + math.radians(lat) / 2))
        / math.pi
        * HALF_CIRCUMFERENCE_M
    )
    return x, y


CASES = [
    Case(
        name="tocho_jprcs9_1-5000_300dpi",
        epsg=6677,
        center_lng=139.6917,
        center_lat=35.6895,
        center_e=-12818.777,
        center_n=-34439.1888,
        scale=5000,
        dpi=300,
        target_width_px=4962,  # A3 横 300dpi
        target_height_px=3509,
        render_width=1632,  # r=25/8 → 8 の倍数。物理 5100×3625
        render_height=1160,
    ),
    Case(
        name="tocho_jprcs9_1-25000_150dpi",
        epsg=6677,
        center_lng=139.6917,
        center_lat=35.6895,
        center_e=-12818.777,
        center_n=-34439.1888,
        scale=25000,
        dpi=150,
        target_width_px=1754,  # A4 横 150dpi
        target_height_px=1240,
        render_width=1184,  # r=25/16 → 16 の倍数。物理 1850×1350
        render_height=864,
    ),
    Case(
        name="nemuro_jprcs13_1-5000_96dpi",
        epsg=6681,
        center_lng=145.585,
        center_lat=43.33,
        center_e=108262.579,
        center_n=-73567.8159,
        scale=5000,
        dpi=96,
        target_width_px=1123,  # A4 横 96dpi
        target_height_px=794,
        render_width=1223,  # r=1
        render_height=894,
    ),
]
