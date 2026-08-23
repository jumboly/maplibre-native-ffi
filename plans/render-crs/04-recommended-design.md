# 04. 推奨アーキテクチャ: 最小フック・パッチ + 外部 CRS モジュール

[03](03-approaches.md) の案 E の詳細。GL JS 版「設計 1（タイル毎ホモグラフィ +
mercator シェーダ流用）」の MapLibre Native 移植。

> **統合先の決定**:
> パッチと外部モジュールの実装先は本リポジトリ（maplibre-native-ffi
> fork。既存のパッチ機構・C ABI・9
> 言語バインディングを持つ）とする。本書の「fork + パッチ」は
> `patches/maplibre-native/` 機構で実現し、maplibre-native 本体の独自 fork
> は持たない。詳細は
> [05-ffi-integration.md](05-ffi-integration.md)。命名・スコープは
> [06-decisions.md](06-decisions.md)
> で確定済み（`render_crs`・静止画・pitch=0・rotation 自由）。

## 1. 全体構成

```
┌─────────────────────────────────────────────────────────┐
│ 本リポジトリ src/crs/（外部モジュール = ポリシー層）            │
│                                                           │
│  純粋数学層（maplibre 非依存・単体テスト対象）                  │
│   jprcs.{hpp,cpp}      系原点テーブル・tmerc 正逆変換          │
│   homography.{hpp,cpp} 4点対応 DLT → mat4                    │
│   extent.{hpp,cpp}     縮尺⇄ズーム・出力面 NDC・回転           │
│                                                           │
│  統合層                                                    │
│   JprcsTileMatrixProvider  フック実装（タイル毎ホモグラフィ）    │
│   src/c_api/render_crs.cpp C API（mln_map_set_render_crs）   │
│                                                           │
│  ※ world_file（.pgw / .aux.xml / GeoTIFF タグ）はコア API 外  │
│    の上位ユーティリティ。静止画レンダは既存 static-image パス    │
└──────────────┬──────────────────────────────────────────┘
               │ フック API（数十行のパッチで追加）
┌──────────────▼──────────────────────────────────────────┐
│ maplibre-native（submodule + 最小パッチ）                    │
│   TransformState: タイル行列プロバイダの差し替え点              │
│   placement/collision: symbol 用 CPU 投影の差し替え点         │
│   tile_cover: 被覆範囲の余白フック                            │
└─────────────────────────────────────────────────────────┘
```

設計原則:
**コアに入れるのは機構（フック）のみ、平面直角座標系の知識（ポリシー）は全て外部モジュール**。フック部分は汎用（任意
CRS 対応）に設計し、将来の上流 design-proposal（"Projector Component"
構想への具体化）の土台にする。

## 2. フックが必要な箇所（GL JS 版上書きメソッド表との対応）

| # | Native 側の注入点                                                                                                                                                           | 差し替え内容                                                                                                                                         | GL JS 版の対応                                 |
| - | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| 1 | `TransformState::matrixFor()`（`src/mln/map/transform_state.cpp:115`）またはその消費側 `LayerTweaker::getTileMatrix()` / `RenderTile::prepare`（`render_tile.cpp:138-141`） | タイル→NDC 行列を「4 隅を Mercator→経緯度→JPRCS→紙面 NDC で厳密投影して解いたホモグラフィ」に差し替え。projMatrix との合成をバイパスまたは単位行列化 | `getProjectionData()` の mainMatrix 差し替え   |
| 2 | `src/mln/text/placement.cpp:1503` 周辺（symbol 配置・collision の CPU 投影）                                                                                                | 同じホモグラフィで投影。`signedDistanceFromCamera` 相当には `cameraToCenterDistance` を返す                                                          | `projectTileCoordinates()`                     |
| 3 | `src/mln/util/tile_cover.cpp`                                                                                                                                               | 紙面外周を 8 分割サンプリング → Mercator フットプリント AABB + 2% 余白で被覆                                                                         | `getCameraFrustum()`                           |
| 4 | ヒットテスト（`feature_index.cpp:30`）・`latLngToScreenCoordinate` 等                                                                                                       | 静止画スコープでは**不要**（インタラクションは non-goal → [06](06-decisions.md) §2）。当面フックしない                                               | `locationToScreenPoint` 等（プレビュー用のみ） |

実装形態の候補（フェーズ 1 で決定）:

1. `TransformState` に `std::function<bool(mat4&, const UnwrappedTileID&)>`
   型の任意プロバイダを持たせる（null なら従来動作。差分が最小・ABI 影響小）
2. `matrixFor` / placement 投影を virtual
   化して派生クラス注入（上流化しやすいが影響大）

GL JS 版 `docs/07:87`
の教訓に従い、**フックは「既存計算の結果を差し替える」形にし、構造体の新フィールドに追随できる設計を維持する**。

## 3. 移植時の必須補正（GL JS 版で「黙って壊れる」と実証済みの 3 点）

1. **ホモグラフィ全体を `cameraToCenterDistance` 倍する** — Native の symbol
   シェーダも Mapbox 2017 年由来の `u_camera_to_center_distance / gl_Position.w`
   サイズ補正を持つ（`shaders/`
   で最初に確認すること）。同次座標のスカラー倍なので投影結果は不変
2. **collision の透視比** — `perspectiveRatio = 0.5 + 0.5·(ccd/w)` 相当の計算に
   `cameraToCenterDistance` を供給
3. **Transform/State のコピー箇所** — GL JS の `clone()` 問題に相当。Native で
   `TransformState` が値コピーされる箇所（Placement・Snapshotter
   等）でフックが失われないか確認。`std::function`
   メンバ方式ならコピーで自然に伝播する利点がある

## 4. レンダリングパイプライン（静止画 1 枚）

```
① extent 構築（系番号・中心 JPRCS 座標・meters_per_pixel・rotation・出力 px）
   ※ 縮尺・dpi からの換算（meters_per_pixel = 縮尺分母 × 0.0254 / dpi）は呼び出し側
② equivalentZoom(scale, centerLat) で mercator 意味論の zoom を決定
   （zoom はタイル選択・スタイル評価・symbol スケール用。描画行列は extent から）
③ HeadlessFrontend + Map(MapMode::Static) を出力 px × pixelRatio で生成
④ JprcsTileMatrixProvider をフックに登録
⑤ renderStill / MapSnapshotter で 1 枚レンダ（タイルロード + placement 完了待ち）
⑥ PremultipliedImage → PNG（.pgw / .aux.xml / GeoTIFF 化は上位ユーティリティ）
```

rotation は①の JPRCS→出力 NDC 段に 2D 回転として合成する。基準は座標北 （grid
north）。tile_cover フック（§2 #3）は外周サンプリング → AABB 方式なので
回転をそのまま吸収する。symbol（viewport-align ラベルの直立・collision box の
向き）は E2E の受け入れ基準に含める（§6）。

GL JS 版との違い（Native の利点）:

- `maxCanvasSize`・キャンバス寸法量子化の問題が消える（上限は GPU
  テクスチャサイズのみ。超過時のタイル分割レンダは将来課題として同じ）
- ブラウザ不要 → サーバサイド・バッチ処理に直結（`bin/render.cpp` / Node
  バインディングが雛形）
- スタイル再ロードによるフック消失問題（GL JS 版 `install.ts:48-73`）は Native
  では発生しない見込みだが、Style 再設定時の挙動は要確認

## 5. 座標変換の実装選択

proj4js の代替。依存の重さと精度要件（外部検証 ±5mm）から:

1. **自前 Gauss-Krüger 級数実装（推奨）** — 平面直角座標系は全系
   `tmerc, k=0.9999, GRS80` で datum shift 不要。GeographicLib の
   TransverseMercator 相当（Krüger 級数 6 次）を 1
   ファイルで実装可能。依存ゼロ、GL JS 版の外部検証フィクスタで正しさを担保
2. PROJ 本体をリンク — 確実だが依存が重い（sqlite
   等）。検証用リファレンスとしては有用
3. GeographicLib（MIT）をベンダリング — 中間案

## 6. 検証戦略

- **単体（数学層)**: GL JS 版 `jprcs.external-fixtures.ts` を C++ に転写。GSI
  測量計算 API 公式値（8 系 8 都市、±5mm）+ pyproj 全 19 系 × 4 点 +
  真北方向角（数値微分、±0.001°）+ 全系往復恒等
- **ホモグラフィ**: 4
  隅の厳密一致、タイル内部点の誤差が誤差解析表の上限内であること
- **レンダ（E2E)**: `render-test/` の画像差分ランナーを流用。都心（IX
  系、γ≈0.08° → Mercator とほぼ同一が正解）と系の端（根室 XIII 系、γ≈0.92° →
  明確に回転）の 2 ケースを GL JS 版 `examples/` の出力と相互比較。加えて
  **rotation ケース**（回転出力で viewport-align ラベルが直立し collision
  が破綻しないこと）
- **GIS ループ**: PNG + .pgw + .aux.xml を QGIS で GL JS
  版出力・基盤地図情報と重畳
- **比較リファレンス**: 案 C（gdalwarp
  後処理）の出力を先に整備し、幾何の正解値および品質差の実証に使う

## 7. リスク

| リスク                                                                                                      | 影響                                         | 緩和策                                                                                                                                       |
| ----------------------------------------------------------------------------------------------------------- | -------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Native の描画パスがタイル毎 mat4 に完全集約されていない（レイヤ種別ごとの例外、fill-extrusion・pattern 等） | 一部レイヤだけ mercator のまま描かれる       | フェーズ 1 で `shaders/` と各 LayerTweaker を横断確認。対象レイヤを GL JS 版と同じ fill/line/circle/symbol に限定                            |
| symbol シェーダの補正項が GL JS と異なる                                                                    | ラベルサイズ・collision の破綻               | 不変条件 1〜3 を最初の PoC 受け入れ基準に含める                                                                                              |
| fork の追随コスト                                                                                           | 上流の構造リファクタでパッチが衝突           | フックを最小・局所に保つ。パッチファイル管理 + 追随チェックリスト（GL JS 版 `docs/07` 方式）。中期的に design-proposal 提出で上流化          |
| 4 グラフィクスバックエンドの差                                                                              | 行列差し替えで足りない箇所がバックエンド依存 | 行列は CPU 側（LayerTweaker/UBO）で完結する設計を維持し、シェーダは触らない。まず 1 バックエンド（開発環境で使う OpenGL または Metal）で PoC |
| `mbgl`→`mln` リネーム直後で上流が流動的                                                                     | パス・名前空間の頻繁な変更                   | 対象コミットを固定して開発、追随は明示的に実施                                                                                               |

## 8. ロードマップ

1. **フェーズ 0 — 比較基盤**: `mbgl-render` + gdalwarp で後処理ワープ版（案
   C）を動かし、QGIS 検証ループと正解データを整備
2. **フェーズ 1 — 仮説検証**:
   ビルド環境（`mise run build`）を整え、実験用パッチ（`matrixFor`
   に歪み行列を注入）を `patches/` 機構で当てて「タイル毎 mat4
   集約」の仮説を全レイヤで確認（回転入りホモグラフィのケースを含める）。symbol
   シェーダの補正項を確認。フック形態を決定
3. **フェーズ 2 — 数学層移植**: jprcs / homography / extent を `src/crs/` に C++
   化、外部検証フィクスチャで合格（world_file はコア API
   外の上位ユーティリティとして扱い、置き場は実装フェーズで決定）
4. **フェーズ 3 — PoC**: フック・パッチ + 既存 static-map パス（`still-image.c`
   雛形）で都庁 IX 系 1:5000 相当（meters_per_pixel ≈ 0.4233）を出力、GL JS 版
   `examples/gis/` と重畳一致
5. **フェーズ 4 — 製品化**: symbol 系不変条件の E2E、根室 XIII
   系ケース、rotation ケース、C API ドメイン `mln_map_set_render_crs` 整備 +
   必要な言語バインディング追加、パッチ追随チェックリスト
6. **フェーズ 5（任意）— 上流化**: フック・パッチを汎用 CRS フックとして
   maplibre-native へ design-proposal 提出（"Projector Component"
   構想の具体化として）。マージされ次第こちらのパッチを削除（本リポジトリのパッチ運用方針とも一致）
