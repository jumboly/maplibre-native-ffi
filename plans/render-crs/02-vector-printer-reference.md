# 02. 先行プロジェクト maplibre-vector-printer（GL JS 版）の要約と流用資産

対象: maplibre-vector-printer HEAD
`e71940d`（本リポジトリの外にある参照用リポジトリ）。MapLibre **GL JS v6.5.0**
を fork せず、モンキーパッチで JGD2011 平面直角座標系の高解像度印刷を実現した
PoC 完了プロジェクト。詳細な設計仕様は同リポジトリの `docs/00〜07` にある。

## 1. 核心となる設計（「設計 1」）

**タイル毎ホモグラフィ + mercator シェーダ流用。**

根拠となる発見（同 `docs/00-summary.md`）: MapLibre の GPU 投影は「タイルごとの
4×4 行列 1 個」に集約されている（mercator 頂点シェーダは
`u_projection_matrix * vec4(pos, 0, 1)` のみ）。そこで:

1. タイル 4 隅 `[0,0][8192,0][8192,8192][0,8192]` を
   `タイルローカル → Mercator 正規化 → 経緯度 → JPRCS [E,N]（proj4 tmerc） → 紙面 NDC`
   のチェーンで厳密投影（`src/transform.ts:43-56`）
2. 4 点対応の DLT（ガウス消去）でホモgraphyを解き、mat4
   に埋め込んでタイル行列を差し替え（`src/math/homography.ts`）
3. ジオメトリ・タイル取得・Worker パースは**一切触らない**。タイル選択も
   mercator quadtree のまま（視錐台 `getCameraFrustum()` のみ上書き）

隣接タイルは共有 2
隅が厳密一致し、射影変換は直線を直線に写すため**タイル境界のシームが数学的に発生しない**。

### zoom は mercator 意味論のまま保持

`equivalentZoom(scale, centerLat) = log2(地球周長·cosφ / (512 · scale·0.0254/96))`。
zoom は (a) タイル選択 (b) スタイル評価（minzoom/maxzoom・expression） (c)
symbol の label plane スケールに使われるため据え置き、**描画行列だけ**を印刷
extent から決める。dpi は式に現れず `pixelRatio = dpi/96` が担う。

### 誤差解析（投影数学のみに依存 → Native でもそのまま有効）

タイル内非線形性の支配項は mercator スケールの緯度勾配（φ=35° で
≈9.0×10⁻⁸/m）。紙面上の最大内部誤差:

| 縮尺 | 1:2,500 | 1:5,000 | 1:25,000 | 1:250,000 | 1:1,000,000 |
| ---- | ------- | ------- | -------- | --------- | ----------- |
| 誤差 | ≈0.5µm  | ≈1µm    | ≈5µm     | ≈52µm     | ≈0.2mm      |

300–600dpi の 1 ドット（42–85µm）を下回るのは **1:250,000
程度まで**。それを超える小縮尺のみ設計 2（シェーダ内再投影 +
subdivision）が必要。

## 2. アーキテクチャ: 2 層分離

| 層                                | ファイル                                                                                                                                                                         | Native への移植性                                                                                      |
| --------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| **純粋数学層**（maplibre 非依存） | `src/projection/jprcs.ts`（proj4 定義・EPSG:6669-6687）、`src/math/homography.ts`（DLT）、`src/print-extent.ts`（縮尺⇄ズーム・紙面座標）、`src/world-file.ts`（.pgw / .aux.xml） | **機械的に C++ 移植可能**                                                                              |
| **maplibre 統合層**               | `src/transform.ts`（カスタム Transform 動的生成）、`src/install.ts`（モンキーパッチ注入）、`src/poc-render.ts`（使い捨て印刷 Map）、`src/smoke-test.ts`（内部 API 存在チェック） | 注入方式は C++ で成立しない。**上書きしたメソッドの一覧が「Native で必要なフック一覧」の設計図になる** |

### 上書きメソッド一覧（= Native 版で差し替えが必要な機能の対応表)

| GL JS で上書きした箇所                                           | 役割                                           | Native の対応物                                                                         |
| ---------------------------------------------------------------- | ---------------------------------------------- | --------------------------------------------------------------------------------------- |
| `getProjectionData()` — mainMatrix/fallbackMatrix のみ差し替え   | GPU 投影の唯一の入口                           | `TransformState::matrixFor()` + `LayerTweaker::getTileMatrix()` / `RenderTile`（01 §2） |
| `projectTileCoordinates()`                                       | symbol 配置・collision・line ラベル用 CPU 投影 | `src/mln/text/placement.cpp` 周辺                                                       |
| `getFastPathSimpleProjectionMatrix()` → undefined                | mercator 高速パス無効化                        | （要調査: 相当する最適化パスの有無）                                                    |
| `getCameraFrustum()` — 紙面外周 8 分割 → mercator AABB + 2% 余白 | タイル被覆                                     | `src/mln/util/tile_cover.cpp`                                                           |
| `locationToScreenPoint` / `screenPointToLocation` / `getBounds`  | プレビュー・デバッグ                           | `TransformState::latLngToScreenCoordinate` 等                                           |
| `clone()` — 自クラス返却 + 実行時 assert                         | Placement が Transform をコピーする箇所対策    | Native で Transform/State をコピーする箇所の特定が必要                                  |

## 3. 実装検証で確定した不変条件（Native 版でも要チェック）

同 `CLAUDE.md:41-56` より。**破ると黙って壊れる（エラーが出ない）**項目:

1. `clone()` は必ず自クラスを返す — 漏れると symbol 配置だけが無言で mercator
   に戻る
2. `projectTileCoordinates` の `signedDistanceFromCamera` は
   `cameraToCenterDistance` を返す — 1 を返すと collision box
   が膨張（`perspectiveRatio = 0.5 + 0.5·(ccd/w)` のため）
3. **ホモグラフィ行列は全体を `cameraToCenterDistance` 倍する** — symbol
   シェーダが `u_camera_to_center_distance / gl_Position.w`
   でサイズ補正するため、w≈1 のままだと ratio
   がクランプ上限まで振り切れラベルが拡大。同次座標のスカラー倍なので投影は不変
4. `maxCanvasSize` 明示指定必須（既定 4096² で A3 300dpi が黙って縮小）—
   **Native の headless レンダでは解消見込み**
5. キャンバス寸法は `floor(pixelRatio × 整数CSS幅)` に量子化 — 同上、Native
   では解消見込み
6. `equivalentZoom` に dpi は現れない（dpi は pixelRatio が担う）
7. 座標軸規約: ライブラリ内部は `[E(東), N(北)]`、測量・GSI 公式は X=北 / Y=東

3 と 2 は Mapbox 2017 年由来の symbol シェーダ設計に起因し、**Native の symbol
シェーダも同系統なので同じ 2
補正が必要になる可能性が高い**（最重要チェックポイント）。

## 4. 座標変換の実装と外部検証（そのまま流用可能）

- proj 定義:
  `+proj=tmerc +lat_0=… +lon_0=… +k=0.9999 +x_0=0 +y_0=0 +ellps=GRS80 +units=m +no_defs`（全
  19 系共通で k=0.9999 / GRS80）。系原点テーブルは国土交通省告示第 1613
  号の値（`src/projection/jprcs.ts:14-34`）。EPSG コード = `6668 + 系番号`
- datum shift は意図的に省略（JGD2011⇔WGS84 は cm 級で印刷用途では無視可）
- **外部検証フィクスチャ**（`src/projection/jprcs.external-fixtures.ts`）:
  - 国土地理院 測量計算 API（`bl2xy.pl`, refFrame=2）の公式値 8 系 8 都市 +
    真北方向角
  - pyproj（PROJ 公式 EPSG 定義）全 19 系 × 4 点
  - 許容誤差 5mm（`jprcs.external.test.ts:14`）
  - → **C++ テストにそのまま転用できる**
- 検証の勘所: JPRCS と Mercator の描画は都心（IX 系中央子午線付近、真北方向角
  γ≈0.08°）ではほぼ同一に見えるのが正しい。差の確認は**系の端**（例: 根室 + XIII
  系、γ≈0.92°）で行う

## 5. 出力・GIS 連携（そのまま流用可能）

- PNG + `.pgw` ワールドファイル（原点は左上ピクセル中心 = 半ピクセル内側）+
  `.png.aux.xml`（GDAL PAM、`<SRS>EPSG:6677</SRS>` 明示）→ QGIS
  に無設定で読み込める
- `examples/gis/` に検証済み出力例 4 組（都庁 IX 系 1:5000/1:25000、根室 XIII
  系、Mercator 比較）

## 6. プロセス面で踏襲すべき点

1. 調査資料を先に書き設計仕様として固定（`docs/00-07`）。CLAUDE.md
   に「確定済み設計判断（再提案禁止）」を明記
2. 「破ると黙って壊れる」不変条件を CLAUDE.md に列挙
3. 自己整合性チェックでは不十分 — **独立実装（GSI API /
   PROJ）との突き合わせ**を恒久テスト化
4. 依存する内部 API の安定性を git 履歴で定量分析（同 `docs/07`。GL JS の依存
   API は約 20 か月安定、壊れ方 3 パターンを整理済み）
5. GIS 検証ループ: PNG + ワールドファイルを QGIS で既存都市計画図と重ねる
