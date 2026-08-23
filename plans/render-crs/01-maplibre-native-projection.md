# 01. MapLibre Native の投影アーキテクチャ

対象: maplibre-native HEAD `2a8ebc4906df`（v5.4.0 系、`mbgl` → `mln`
リネーム直後）。 以下のパスはすべて同リポジトリ（本リポジトリでは submodule
`third_party/maplibre-native`）からの相対パス。 **注意**: 本リポジトリの
submodule pin（`550f64be…`）は調査時 HEAD
より新しいため、行番号は参照時に要再確認。

## 1. 結論: Web Mercator は抽象化されずに静的ハードコード

投影法の切り替え機構（interface / virtual / strategy / registry）は存在しない。

### Projection クラス — 全メンバ static・非仮想

`include/mln/util/projection.hpp:41` `class Projection` — コメントに
`/// Spherical Mercator projection` と明記。

- `worldSize(scale)` = `scale * util::tileSize_D`（`:44`）
- `getMetersPerPixelAtLatitude()`（`:46`）、`projectedMetersForLatLng()`（`:55`）、`latLngForProjectedMeters()`（`:68`）
- `project()`（`:79,83`）/ `unproject()`（`:85`）/ `project_()`（`:96`）—
  `log(tan(π/4 + φπ/360))` を直書き、緯度は ±85.051129° にクランプ

`ProjectedMeters` 型（`projection.hpp:13`）は CRS 情報を持たない（暗黙に
EPSG:3857 メートル）。

定数（`include/mln/util/constants.hpp`）:
`tileSize_D = 512`（`:13`）、`EXTENT = 8192`（`:27`）、`EARTH_RADIUS_M = 6378137`（`:30`）、`LATITUDE_MAX = 85.051128779806604`（`:31`）。

### TransformState — 非仮想具体クラス、Mercator キャッシュ値を直持ち

`src/mln/map/transform_state.hpp:127`。継承・差し替え前提のフックはゼロ。メンバに
`double Bc = Projection::worldSize(scale) / DEGREES_MAX; double Cc = worldSize / M2PI;`
（`:326-327`、「spherical mercator math
のキャッシュ」とコメント付き）が直接埋め込まれている。

実装 `src/mln/map/transform_state.cpp` の要点:

| 箇所        | 内容                                                                                                                                                        |
| ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `:115-126`  | **`matrixFor(mat4&, UnwrappedTileID)`** — タイル→ワールド行列。`s = worldSize / 2^z` の平行移動 + 等方スケールのみ。「タイル ID = Mercator 正方格子」が前提 |
| `:128-215`  | `getProjMatrix()` — 透視投影行列。`maxMercatorHorizonAngle = 89.25°` クランプ等                                                                             |
| `:762-776`  | `latLngToScreenCoordinate()` — `Projection::project` 直呼び                                                                                                 |
| `:778-815`  | `screenCoordinateToTileCoordinate()` / `screenCoordinateToLatLng()` — mapbox-gl-js `pointCoordinate()` 相当                                                 |
| `:897-1011` | `cameraForLatLngBounds` 系すべて `Projection::project/unproject` 直呼び                                                                                     |

### タイル座標・タイル選択

- `src/mln/util/tile_coordinate.hpp:18` `TileCoordinate` — コメント「GL JS の
  MercatorCoordinate に相当」
- `include/mln/tile/tile_id.hpp:24` `CanonicalTileID {z, x, y}` — quadtree
  固定。`:260` `pixelsToTileUnits()` は 2^z ピラミッド前提
- `src/mln/util/tile_cover.cpp` — 可視タイル決定。`:192, :323-327, :348-382` で
  `Projection::` 直呼び

### `Projection::` 直呼び箇所（コア改造時の影響範囲）

コア 14 ファイル + プラットフォーム 5 ファイル、約 40 箇所:

```
src/mln/map/transform_state.cpp（多数） src/mln/map/transform.cpp:155-376
src/mln/util/tile_cover.cpp / tile_cover_impl.cpp / tile_range.hpp / camera.cpp / tile_coordinate.hpp
src/mln/geometry/feature_index.cpp:30（ヒットテスト）
src/mln/gfx/drawable_builder_impl.cpp:140,154
src/mln/style/layers/custom_drawable_layer.cpp:535
src/mln/renderer/layers/render_location_indicator_layer.cpp:585,591,664
platform/ios/src/MLNMapView.mm ほか iOS/macOS/Android バインディング
```

## 2. 行列パイプライン — 再投影を差し込むならここ

タイル 1 枚の描画行列が通る箇所（**GL JS 版設計 1 の注入点に対応する場所**）:

- `src/mln/renderer/paint_parameters.cpp:111` `PaintParameters::matrixForTile()`
  → `state.matrixFor()`
- `src/mln/renderer/layer_tweaker.cpp:38` / 宣言
  `include/mln/renderer/layer_tweaker.hpp:51`
  `static mat4 LayerTweaker::getTileMatrix(...)`、`:65-70`
  `multiplyWithProjectionMatrix(...)`
- `src/mln/renderer/render_tile.cpp:138-141` `state.matrixFor(matrix, id)` →
  `matrix::multiply(matrix, transform.projMatrix, matrix)`
- その他:
  `render_tile_source.cpp:115`、`render_image_source.cpp:59`、`geometry_tile.cpp:521`、`src/mln/text/placement.cpp:1503`（symbol
  配置）、`custom_drawable_layer.cpp:227`

**重要**: 現状の `matrixFor()` はアフィン変換（平行移動 +
等方スケール）のみ。タイル毎の行列を「4
隅対応から解いたホモグラフィ（射影変換）」に差し替えれば、GL JS 版と同じ原理で
mercator シェーダを流用したまま平面直角座標系へ再投影できる（→
[03](03-approaches.md) 案 E）。

## 3. Globe 投影は未実装、style spec の `projection` も未パース

- Globe のレンダリング実装コードは存在しない（ドキュメント上の将来構想のみ）
- `include/mln/map/projection_mode.hpp:12` `ProjectionMode` は axonometric /
  xSkew / ySkew（ビル押し出しの軸測投影）のみで **CRS 切替とは無関係**
- style spec
  参照ファイル（`scripts/style-spec-reference/v8.json:128, :5663-5666`）には
  `projection` ルートプロパティが定義されているが、**Native のパーサ
  `src/mln/style/parser.cpp` は `projection` を完全に無視**（`terrain`, `sky`
  も未パース）

## 4. TileJSON / ソースに CRS の概念がない

- `include/mln/util/tileset.hpp:15` `Tileset` — `scheme (XYZ|TMS)`, `tiles`,
  `zoomRange`, `bounds (LatLngBounds)` のみ。**`crs`/`srs`
  相当のフィールドなし**
- パーサ `src/mln/style/conversion/tileset.cpp` も CRS
  系キーを一切読まない。`bounds` は緯度経度前提の検証つき（`:106,113`）
- style spec の `source_vector.scheme` doc に「The global-mercator (aka
  Spherical Mercator) profile is assumed」と明記
- タイル URL トークン（`src/mln/storage/resource.cpp:87-120`）:
  `{z} {x} {y} {quadkey} {bbox-epsg-3857} {prefix} {ratio}`。`{bbox-epsg-XXXX}`
  の汎用形はなく、bbox 計算は球面メルカトルをハードコード（`:24-40`）
- `SourceType` は閉じた
  enum（`include/mln/style/types.hpp:10-19`）で、`LayerManager` に相当する
  `SourceManager` は存在しない →
  **ソース型の外部プラグイン追加は不可**（`CustomVectorSource` 自身、追加時に
  enum に値を足している）

## 5. 利用可能な拡張ポイント一覧

| 拡張点                      | 場所                                                                                                                                     | できること / 限界                                                                                                                                  |
| --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| **PluginLayer**（実装済み） | `src/mln/plugin/plugin_layer*.{hpp,cpp}`、登録は `LayerManager::addLayerTypeCoreOnly`（`include/mln/layermanager/layer_manager.hpp:77`） | 実行時にレイヤ型を登録し `OnRenderLayer(PaintParameters&)` で描画。公開 API は **iOS のみ**（`MLNMapView.h:2323`）。Android 未対応                 |
| **CustomLayer**             | `include/mln/style/layers/custom_layer.hpp:12`、`custom_layer_render_parameters.hpp:18`                                                  | 生 GPU 描画。`projectionMatrix` / `nearClippedProjectionMatrix` を受け取れる。ラベル衝突・ヒットテスト・タイル選択はコアの Mercator ロジックのまま |
| **CustomDrawableLayer**     | `include/mln/style/layers/custom_drawable_layer.hpp`                                                                                     | drawable API を借りて line/fill/symbol を追加。`addPolyline(GeometryCoordinates&,...)`（`custom_drawable_layer.cpp:557`）はタイル座標直渡しが可能  |
| **CustomGeometrySource**    | `include/mln/style/sources/custom_geometry_source.hpp:24`                                                                                | `CanonicalTileID` 単位で GeoJSON を供給                                                                                                            |
| **CustomVectorSource**      | `include/mln/style/sources/custom_vector_source.hpp:28`（design-proposal 2025-07-05）                                                    | `CanonicalTileID` 単位で生 MVT バイナリを供給。ただしタイル ID は Mercator quadtree として解釈される                                               |
| **ShaderRegistry**          | `include/mln/gfx/shader_registry.hpp:48` `replaceShader()`                                                                               | 実行時シェーダ差し替え。ただし GL/Metal/Vulkan/WebGPU の 4 バックエンド分必要                                                                      |
| **LayerTweaker**            | `include/mln/renderer/layer_tweaker.hpp:34`                                                                                              | 毎フレームのレイヤ単位 UBO 更新フック。タイル行列を扱う最薄の層だが、派生 Tweaker は組み込みレイヤに固定紐付けで外部登録機構なし                   |

→ **「投影法そのもの」を差し替えられる拡張点は 1 つもない。**
レイヤ単位の描画注入は充実しているため「Mercator ベースマップ +
平面直角座標系オーバレイ」のハイブリッドは無改造で可能だが、地図全体の再投影にはコアへの手入れが必須。

## 6. オフスクリーンレンダリング装備（静止画出力に直結）

- `platform/default/src/mln/map/map_snapshotter.cpp` —
  `MapSnapshotter`（オフスクリーン静止画）
- `bin/render.cpp` — `mbgl-render` CLI
- `platform/node/src/node_map.cpp` — Node.js バインディング（`renderStill`
  ベース）
- `render-test/` — 画像差分テストランナー（回帰検証にそのまま使える）

GL JS 版で問題になった `maxCanvasSize`
既定値による無言縮小・キャンバス寸法量子化（[02](02-vector-printer-reference.md)
不変条件 4/5）は、headless レンダでは発生しない見込み。上限は GPU
のテクスチャ/レンダバッファサイズのみ。

## 7. 上流の動向

- `design-proposals/`（9 件）に**投影法・CRS 関連の提案はゼロ**
- 唯一の構想は
  `docs/mdbook/src/design/archictural-problems-and-recommendations.md:28-36, :105-118`
  の **"Projector Component"**（複数投影・CRS 対応を担う新コンポーネント。GCJ-02
  datum 対応にも言及）。ただし散文レベルで API 案・実装なし
- 上流マージを狙うなら `design-proposals/2022-09-02-design-proposal-template.md`
  に沿った提案提出が事実上必須
- 公式ドキュメント `docs/mdbook/src/design/coordinate-system.md`（377 行）が
  EPSG:3857 前提の座標系解説として有用

## 8. プラットフォームバインディング概観

`platform/` = android / ios / macos / darwin(共通) / default(共通実装) / node /
qt / glfw / linux / windows。 CRS を扱うプラットフォーム
API（`MLNMapProjection`、Android `Projection.java` 等）はすべて EPSG:3857 固定。
ARCHITECTURE.md の要点: モノレポ、Impl イディオム（公開クラスと `Impl`
の並行階層）、`Immutable<T>` + style diffing、スレッドモデル（main / worker×4 /
FileSource）。ビルド記述は古く、現行は CMake + Bazel。
