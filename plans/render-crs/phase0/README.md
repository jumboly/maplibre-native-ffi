# フェーズ 0: 比較基盤（案 C — Mercator レンダ + gdalwarp 後処理ワープ）

[04-recommended-design.md](../04-recommended-design.md) §8 フェーズ 0 の実装。
Mercator で静止画をレンダし、gdalwarp で平面直角座標系（JGD2011）へ再投影する。
目的は後続フェーズ（タイル行列フックによるベクタ級再投影）の**比較リファレンス**と
**QGIS 検証ループ**の整備であり、これ自体は製品機能ではない。

- ケース定義と数式: [cases.py](cases.py)（3 ケース: 都庁 IX 系 1:5000/1:25000、
  根室 XIII 系 1:5000）
- 検証結果: [../07-phase0-results.md](../07-phase0-results.md)
- 生成物はすべて `build/render-crs-phase0/`（gitignore 済み）

## 1. mbgl-render の単独ビルド

本リポジトリのプリセットは `MLN_WITH_CORE_ONLY=ON`（FORCE）でコアのみをビルド
するため、`mbgl-render` CLI はサブモジュールを source dir にした別ビルドツリーで
作る。configure には sync-submodules が意図的にスキップする vendor
サブモジュール 4 個の init が必要（クリーンなら sync-submodules
に巻き戻されない）。

```bash
git -C third_party/maplibre-native submodule update --init --depth 1 \
  vendor/args vendor/googletest vendor/benchmark vendor/cpp-httplib

# mise 環境内で実行すると sccache が効く
mise exec -- cmake -S third_party/maplibre-native -B build/mbgl-render -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMLN_WITH_CORE_ONLY=OFF \
  -DMLN_WITH_METAL=ON \
  -DMLN_WITH_GLFW=OFF \
  -DMLN_WITH_WERROR=OFF
mise exec -- cmake --build build/mbgl-render --target mbgl-render
# → build/mbgl-render/bin/mbgl-render
```

Metal ヘッドレスがレンダ完了を返さない場合は `MLN_WITH_METAL=ON` を
`MLN_WITH_OPENGL=ON`（CGL ヘッドレス）に替えてビルドし直す。

## 2. パイプラインの実行

gdalwarp はルート `mise.toml` の `conda:gdal` で入る（`mise install` 後に
`"$(mise where conda:gdal)/bin/gdalwarp" --version` で確認）。conda
バックエンドは バイナリを PATH に乗せないため、warp_cases.py が `mise where`
で解決する （環境変数 `GDALWARP` で上書き可）。

```bash
cd plans/render-crs/phase0

# ① GSI std スタイルを取得し pmtiles → XYZ pbf へ差し替え
mise exec -- python fetch_style.py

# ② 3 ケースを Mercator でレンダ（PNG + .pgw + .png.aux.xml、EPSG:3857）
#    引数でケース名を並べると絞り込める
mise exec -- python render_cases.py

# ③ gdalwarp で EPSG:6677 / 6681 へ再投影（bilinear / cubic / lanczos の 3 変種）
mise exec -- python warp_cases.py
```

出力:

```
build/render-crs-phase0/
  style/std-xyz.json   ← ①
  cache.sqlite         ← ② のタイルキャッシュ
  out/<case>.merc.png / .merc.pgw / .merc.png.aux.xml   ← ②（Mercator ベースライン）
  warp/<case>.warp-{bilinear,cubic,lanczos}.tif         ← ③
```

zoom は `equivalentZoom(scale, centerLat)`（96dpi の CSS px 基準）、物理解像度は
`--ratio dpi/96` が担う。HeadlessFrontend の出力は `floor(論理px × ratio)`
なので、 論理寸法は積が整数になる値を cases.py が持つ（GL JS
版のキャンバス量子化補正は 不要）。

## 3. QGIS 検証ループ

1. プロジェクト CRS を対象系（都庁 EPSG:6677、根室 EPSG:6681）にする。
2. `warp/*.tif` を追加。`gdalinfo` の origin / pixel size が cases.py の
   `center ± mpp·px/2` / `meters_per_pixel` と一致することを先に確認。
3. 第三者リファレンスとして地理院タイル標準地図
   `https://cyberjapandata.gsi.go.jp/xyz/std/{z}/{x}/{y}.png` を XYZ
   レイヤで追加 （QGIS がオンザフライ再投影する）。透過 50% / 差の絶対値 /
   表示トグルで 道路網の整合を目視。
4. `out/*.merc.png` は .pgw + .aux.xml により EPSG:3857 のまま読み込める。
   ワープ結果との判定基準:
   - **都庁（IX 系、真北方向角 γ≈0.08°）**: Mercator レンダとほぼ同一が正解
     （端部変位は数 px 以内）。
   - **根室（XIII 系、γ≈0.92°）**: Mercator レンダに対し**明確な回転**が正解
     （画像端で ≈9px の変位。計測ツールで確認する）。
5. 品質評価: `mise exec -- python crop_compare.py` が同一箇所の 4 倍拡大クロップ
   （Mercator 元画像 + リサンプラ 3 変種、3 領域）を `crops/` に生成する。並置で
   滲み・細線の潰れを記録 → 所見は
   [../07-phase0-results.md](../07-phase0-results.md) に記入済み。案 C
   の品質限界の実証が、フック方式（フェーズ 3）の受け入れ基準になる。

方眼の定量検証が必要になったら、GL JS 版 `demo/grid.ts`（100m 格子 GeoJSON の
スタイル注入）の移植で足りる。フェーズ 0 では行わない。
