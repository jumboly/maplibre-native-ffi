# フェーズ 3: PoC（フック方式で都庁 IX 系 1:5000 を出力）

[04-recommended-design.md](../04-recommended-design.md) §8 フェーズ 3 の実装。
タイル行列フック（`patches/maplibre-native/0005`）+ 被覆 override（同 `0006`）+
統合層（`src/crs/provider.cpp`）+ 最小 C API
（`mln_map_set_render_crs`）で、都庁 IX 系（EPSG:6677）1:5000 相当の静止画を
公開 C API だけで出力する。

- 検証結果と結論: [../10-phase3-results.md](../10-phase3-results.md)
- 生成物はすべて `build/render-crs-phase3/`（gitignore 済み）
- 前提: `mise run build`（macos-arm64-metal）と、フェーズ 0 の `fetch_style.py`
  / `render_cases.py`（スタイル・タイルキャッシュ・比較用 Mercator
  出力を流用する。[../phase0/README.md](../phase0/README.md)）

## 実行

```bash
mise exec -- python3 plans/render-crs/phase3/run_cases.py
```

`render_case.c`（still-image.c 雛形 + Metal owned texture）を pkg-config で
ビルドし、次の 3 枚をレンダする:

1. `*.merc.png` — `mln_map_jump_to` による Mercator ベースライン。phase0 の
   mbgl-render 出力との画素照合（≤250 px かつ ≤2 LSB）まで自動で行う
2. `*.crs.png` + `.pgw` + `.aux.xml` — `mln_map_set_render_crs` によるフック
   方式の本番出力（EPSG:6677、原点は左上ピクセル中心）
3. `*.crs-rot10.png` + `.pgw`（回転項付き）— rotation=10° の符号スモーク

## 検証（10-phase3-results.md に記録済みの手順）

- 幾何一致: phase0 の Mercator 出力を同一グリッドへ
  `gdalwarp -t_srs EPSG:6677 -te … -ts 5100 3625` した参照と、中心・隅の窓で
  ブレンド重畳（ゴーストが出ないこと）
- GL JS 版との重畳:
  `maplibre-vector-printer/examples/gis/
  tocho_jprcs9_1-5000_300dpi.png`
  とはグリッドが同一 mpp・整数オフセット (69, 58) px で整合する
- rotation 符号: rot10 出力を自身のワールドファイルで通常グリッドへ `gdalwarp`
  し、θ=0 出力と重なること（= レンダの回転が数学層の規約・
  ワールドファイルの回転項と一致する証明）
- エッジシャープネス: phase0 `crop_compare.py` と同じ窓のクロップを
  `build/render-crs-phase3/crops/` に生成し、warp 変種と並置
- QGIS: `.pgw` + `.aux.xml` 付きなので無設定で読み込める。地理院タイル・ phase0
  warp 出力・GL JS 版と重畳する
