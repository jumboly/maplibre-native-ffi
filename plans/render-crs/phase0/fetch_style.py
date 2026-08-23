"""GSI 最適化ベクトルタイル std スタイルを取得し、XYZ 配信へ差し替えて保存する。

公式 style.json のソースは pmtiles:// を指す。MapLibre Native は PMTiles を内蔵
するが、フェーズ 0 は GL JS 版 PoC とデータ経路を揃えるため XYZ の pbf 配信を
主経路にする（demo/gsi.ts と同じ差し替え）。
"""

import json
import sys
import urllib.request
from pathlib import Path

STYLE_URL = "https://gsi-cyberjapan.github.io/optimal_bvmap/style/std.json"
XYZ_TILES = "https://cyberjapandata.gsi.go.jp/xyz/optimal_bvmap-v1/{z}/{x}/{y}.pbf"

OUT_DIR = Path(__file__).resolve().parents[3] / "build" / "render-crs-phase0" / "style"


def main() -> None:
    with urllib.request.urlopen(STYLE_URL, timeout=60) as response:
        style = json.load(response)

    replaced = []
    for name, source in style.get("sources", {}).items():
        if source.get("type") != "vector":
            continue
        source["tiles"] = [XYZ_TILES]
        source.pop("url", None)  # TileJSON 参照が残ると tiles より優先されるため
        replaced.append(name)

    if not replaced:
        sys.exit("vector ソースが見つかりません（スタイル構造が変わった可能性）")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_path = OUT_DIR / "std-xyz.json"
    out_path.write_text(json.dumps(style, ensure_ascii=False, indent=1))
    print(f"wrote {out_path} (replaced sources: {', '.join(replaced)})")


if __name__ == "__main__":
    main()
