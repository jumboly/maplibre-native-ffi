# 08. フェーズ 1 検証結果（仮説検証）

[04-recommended-design.md](04-recommended-design.md) §8 フェーズ 1 の記録。
仮説「タイル毎の描画は `TransformState::matrixFor()` 由来の mat4 1 個に
集約されている」を、実験パッチ
`patches/maplibre-native/0004-experimental-tile-matrix-warp.patch` で実証した。
手順とスクリプトは [phase1/](phase1/README.md)。

## 実行環境（2026-08-23）

- maplibre-native submodule: `550f64be…`（パッチ 0002〜0004 適用済み）
- mbgl-render: `build/mbgl-render/`（Release、Metal、macOS arm64。フェーズ 0 と
  同一ビルドツリーの増分ビルド）
- ffi 本体: `mise run build`（macos-arm64-metal）がパッチ共存状態で成功
- スタイル: GSI std 派生の「全レイヤ検証スタイル」（raster = GSI 標準地図タイル
  半透明、circle = 500 m 格子 GeoJSON を追加。background 1 / raster 1 / fill 11
  / line 99 / symbol 12 / circle 1）

## 机上調査の確定事実（コード読解）

- `matrixFor` の呼び出し元は 11 箇所。描画は `RenderTile::prepare`
  （`render_tile.cpp:138`）・`LayerTweaker::getTileMatrix`
  （`layer_tweaker.cpp:38`）・`PaintParameters::matrixForTile`
  （`paint_parameters.cpp:113`）の 3 チョークポイントに集約され、いずれも
  「`matrixFor` の結果に projMatrix を左乗算」という同一形
- CPU 側も自動追従する: symbol placement / collision は `RenderTile::matrix`
  経由（`placement.cpp:280` ほか。1503 行の隣接タイル境界も `matrixFor`
  直呼び）、ヒットテスト（`geometry_tile.cpp:521`）、**ステンシルクリップ
  マスク**（`paint_parameters.cpp:169` ほか）
- 全 LayerTweaker（background 含む。background もタイル ID 付き drawable）が
  タイル行列に追従。追従しないのは RTT 系（`heatmap_texture` /
  `hillshade_prepare`）、`location_indicator`、`symbol-screen-space: true` の 4
  パスで、これらは歪ませないのが正しい挙動
- symbol の labelPlane / glCoord 行列（`symbol_projection.cpp:109-152`）は
  pitchWithMap の真偽どちらでも posMatrix がちょうど 1 回だけ効く構造
- `TransformState` は暗黙メンバワイズコピー（ユーザー定義コピー無し）。
  `UpdateParameters` → `RenderOrchestrator` / `PaintParameters` /
  `CollisionIndex` / `TileCoverParameters` / Snapshotter へ値コピーで伝播する
  ため、`std::function` メンバのフックはコピーで自然に伝播する
- 追従しないスカラー・経路（実験対象）: ① `tile_cover.cpp:211` のタイル選択
  （`getInvProjectionMatrix()` のみから視錐台を構築）、②
  `u_camera_to_center_distance`（`renderer_impl.cpp:305` の CPU スカラー。
  symbol サイズ `perspective_ratio = clamp(0.5 + 0.5·dist_ratio, 0, 4)`・circle
  半径・collision 透視比の分母）、③ `CollisionIndex` のスクリーン空間
  パディング（`collision_index.cpp:37-51`）
- レンダラは drawable 一択（legacy は削除済み）。`getCameraToTileDistance`
  （`transform_state.cpp:1053`）はコア内に呼び出し元が無く、フック追加の
  副作用対象にならない

## 実験結果（都庁 1:25000 ケース、Metal）

自動検証（`phase1/run_cases.py`。判定は画素差 ≤250 px かつ ≤2 LSB。Metal は
同条件でも GPU ラスタライズの LSB ノイズが数画素乗るため — 実測 2.5M 画素中 2〜8
画素・最大 1 LSB — 完全一致ではなくノイズ床で判定する）:

- [x] **パッチ前後の一致**: 環境変数未設定の出力がフェーズ 0 の既存出力
      （パッチ前バイナリ産）と一致。未設定時は完全に従来動作
- [x] **identity の無害性**: `W = I` の出力が未設定時と一致。注入経路 （パース +
      乗算）自体は画素に影響しない

目視検証（`build/render-crs-phase1/out/` と `crops/`）:

- [x] **rotation（10°）/ shear**: 全レイヤ（background / raster / fill / line /
      circle / symbol）が一体で歪む。タイル境界の割れ・レイヤ間のずれ・
      ステンシルの不整合は無い。**仮説成立**
- [x] **ラベルの向き**: viewport-align ラベル（地名・駅名・道路番号）は回転後も
      直立。map-aligned ラベル（等高線数値、`line-center` +
      `text-rotation-alignment: map`）は回転後の線の向きに追従（高尾山ケースで
      確認）。collision の破綻（ラベル重なり）は観測されない
- [x] **projective（画面端で w′ = 1±0.25）**: 幾何は射影変換どおり歪み、
      viewport-align の symbol / circle サイズが `0.5 + 0.5/w′` に従って変化
- [x] **projective × 2（同次スカラー倍）**: 幾何は projective と画素レベルで
      同一（同次座標のスカラー倍不変性を実証）。symbol / circle サイズだけが
      縮小し、collision box も縮むため配置されるラベルが増える。GL JS 版
      必須補正 1「ホモグラフィ全体を cameraToCenterDistance 倍する」が依存する 2
      つの機構（スカラー倍の幾何不変・w 依存のサイズ補正）を Native の
      シェーダでも確認
- [x] **タイル欠け（tile_cover 非追従）**: z13.2 では 45° 回転でも欠けなし
      （タイル粒度 512 論理 px の余白が吸収）。**z15.55 + 10° 回転で画面隅に
      白い楔形の欠けが発生**（ベースラインは欠けなし）。発生はズーム・歪み量・
      ビューポートとタイル境界の位相に依存し、保証は無い

## 結論

1. **仮説成立**: `matrixFor` の出力への左乗算 1 箇所で、対象レイヤすべて （fill
   / line / circle / symbol / raster / background）と CPU 側
   （placement・collision・ステンシル）が一貫して追従する。フェーズ 3 の
   タイル毎ホモグラフィはこの注入点で成立する
2. **tile_cover フックは必須**: タイル選択は行列に追従しない。今回のズーム・
   歪み量では概ね吸収されたが欠けの実測もあるため、04 §2 #3 の外周
   サンプリング + 余白フックを予定どおり実装する
3. **ccd 補正の適用条件を確認**: `u_camera_to_center_distance` は行列に
   追従しないスカラーで、`w` を変えない歪み（第 4 行 `(0,0,0,1)`）なら
   サイズ系は不変。射影成分を使う場合は同次スカラー倍で `w` の絶対値を
   合わせる（機構は実証済み。倍率の設計はフェーズ 3 のホモグラフィ構成で確定）
4. **フック形態を確定**（[06](06-decisions.md) §5 に記録): `TransformState` の
   `std::function` メンバを `matrixFor` 末尾で呼ぶ形。値コピーで Placement /
   tile_cover / Snapshotter まで自然伝播する
5. 実験パッチ 0004 と `phase1/` スクリプトは再現用にブランチへ残し、フェーズ 3
   の本フックパッチで 0004 を置き換える

## フェーズ 3 への持ち越し

- projMatrix はチョークポイント側で合成されるため、ホモグラフィを完全適用する
  にはプロバイダが `P⁻¹·H` を返して合成を打ち消す案と、合成をバイパスする 第 2
  フックを足す案がある。数値条件も含めフェーズ 3 で確定する
- Snapshotter に `static TransformState defaultTransformState`
  （`map_snapshotter.cpp:169`）があり、フック未設定の既定インスタンスが返る
  経路に注意
- `CollisionIndex` のスクリーン空間パディングは歪みを知らない。ラベルが
  ビューポート外へ押し出される構図で cull され得るため、E2E 受け入れ時に観察
- raster ソースは `raster-fade-duration: 0` にしないと静止画へフェード途中の α
  が混入し、同条件レンダの画素が揺れる（検証スタイルで実測）
