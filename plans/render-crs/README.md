# render_crs: 平面直角座標系レンダリング 計画資料

MapLibre Native を日本の平面直角座標系（JGD2011 I〜XIX 系、EPSG:6669〜6687）で
レンダリングできるようにする計画。Web Mercator の MVT + MapLibre Style
を入力に、
平面直角座標系で幾何学的に正しい地図画像を出力する（印刷はユースケースの一つで
あって機能名ではない）。先行プロジェクト maplibre-vector-printer（MapLibre GL JS
版 PoC）の設計を移植する。

- 調査日: 2026-08-23（元は maplibre-native-vector-printer リポジトリの docs/。
  本リポジトリ完結の方針により `plans/render-crs/` へ移設し、以後こちらが source
  of truth）
- 調査対象:
  - maplibre-native — 調査時 HEAD `2a8ebc4906df`（android-v13.5.1 直後。
    **`mbgl` → `mln` へディレクトリ・名前空間リネーム済み**の状態。上流
    ドキュメントや古い資料は `mbgl` 表記なので読み替えること）。本リポジトリの
    submodule pin は `550f64be…` で調査時より新しいため、**各資料の行番号は
    参照時に要再確認**
  - maplibre-vector-printer — HEAD `e71940d`（maplibre-gl v6.5.0 対象の PoC
    完了状態。本リポジトリの外にある参照用リポジトリ）

## 結論（TL;DR）

1. **MapLibre Native に投影法のプラグイン機構は存在しない。** `Projection` は
   全メンバ static の非仮想クラス、`TransformState` も非仮想で、Web Mercator が
   言語レベル（コンパイル時）で全域にハードコードされている。GL JS 版で使った
   モンキーパッチ（実行時クラス差し替え）は C++ では原理的に不可能。
2. ただし GL JS 版の核心である **「GPU 投影はタイル毎の 4×4 行列 1 個に集約され
   ている」という構造は Native にも成立する**（`TransformState::matrixFor()` →
   `LayerTweaker` / `RenderTile` で行列合成）。したがって「タイル毎ホモグラフィ
   - mercator シェーダ流用」方式（GL JS 版の設計 1）は Native にも移植可能。
3. 移植には**最小限のコア改造（フック追加パッチ）が必須**。現実的な形は「タイル
   行列を外部から差し替えられるフックを数ファイル・数十行のパッチで追加し、平面
   直角座標系のロジック本体は外部モジュールに置く」構成。純粋な外付けプラグイン
   だけでは実現できない。**このパッチ方式は本リポジトリが標準装備する
   `patches/maplibre-native/` 機構にそのまま乗り**、maplibre-native 本体の fork
   保守が不要になる（→ [05](05-ffi-integration.md)）。
4. 静止画出力に限定すれば、Native は `HeadlessFrontend` / `MapSnapshotter` /
   `mbgl-render` によるオフスクリーンレンダリングを標準装備しており、GL JS 版が
   苦労した canvas 上限・寸法量子化の問題が解消する。**サーバサイド・バッチ出力
   こそ Native 版の主戦場**。
5. GL JS 版の純粋数学層（proj4 定義・ホモグラフィ DLT・extent 計算・ワールド
   ファイル・外部検証フィクスチャ）は maplibre 非依存であり、C++ へ機械的に移植
   できる。誤差解析の結論（1:250,000 まで印刷 1 ドット未満）もそのまま有効。

推奨アプローチと段階計画は
[04-recommended-design.md](04-recommended-design.md)、 確定済みの設計判断は
[06-decisions.md](06-decisions.md) を参照。

## 資料構成

| ファイル                                                             | 内容                                                                                                                                           |
| -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| [01-maplibre-native-projection.md](01-maplibre-native-projection.md) | MapLibre Native の投影アーキテクチャ調査。Mercator ハードコード箇所、拡張ポイント一覧、CRS 対応の現状                                          |
| [02-vector-printer-reference.md](02-vector-printer-reference.md)     | maplibre-vector-printer（GL JS 版）の設計要約と、Native 版へ流用できる資産・不変条件                                                           |
| [03-approaches.md](03-approaches.md)                                 | 実現アプローチ 5 案の比較評価と推奨                                                                                                            |
| [04-recommended-design.md](04-recommended-design.md)                 | 推奨アーキテクチャ（最小フック・パッチ + 外部 CRS モジュール）、検証戦略、リスク、ロードマップ                                                 |
| [05-ffi-integration.md](05-ffi-integration.md)                       | 本リポジトリ（maplibre-native-ffi fork）での実装設計。パッチ機構への統合、C API ドメイン `render_crs`、各言語バインディングのコスト、fork 運用 |
| [06-decisions.md](06-decisions.md)                                   | 確定済み設計判断の記録（再提案禁止）。命名・スコープ・API 形状・fork 運用                                                                      |
| [07-phase0-results.md](07-phase0-results.md)                         | フェーズ 0（比較基盤）の検証結果。パイプラインは [phase0/](phase0/README.md)                                                                   |
| [08-phase1-results.md](08-phase1-results.md)                         | フェーズ 1（仮説検証）の結果。タイル毎 mat4 集約の実証とフック形態の確定。手順は [phase1/](phase1/README.md)                                   |
| [09-phase2-results.md](09-phase2-results.md)                         | フェーズ 2（数学層移植）の結果。`src/crs/` の外部検証合格と移植時の設計決定                                                                    |
| [10-phase3-results.md](10-phase3-results.md)                         | フェーズ 3（PoC）の結果。フック・パッチ + 最小 C API で都庁 IX 系 1:5000 を出力し受け入れ合格。手順は [phase3/](phase3/README.md)              |

## 前提・スコープ

- 入力: Web Mercator XYZ の MVT（`/{z}/{x}/{y}.pbf`）+ MapLibre Style をそのまま
  利用（タイルの再生成はしない）
- 出力: 平面直角座標系で正しい形状・縮尺の高解像度ラスタ（PNG。ワールドファイル
  / GeoTIFF 化は呼び出し側ユーティリティ）
- スコープ: 静止画（`MLN_MAP_MODE_STATIC`）・pitch=0・**rotation（回転）は自由**
  （座標北基準。GL JS 版の north-up 固定から緩和 → [06](06-decisions.md)）
- non-goals: pitch、インタラクティブ操作（ヒットテスト・ジェスチャ・カメラ
  アニメーション）、terrain、globe
- 測地系: JGD2011 と WGS84 の差は cm 級のため datum shift は省略（本用途では
  無視できる）

## ライセンス上の重要な注意

MapLibre Native の `FORK.md` は **Mapbox の非フリーライセンス化以降のコード
（Mapbox GL JS v2+ / Mapbox GL Native の Globe・カスタム投影実装等）のバック
ポートを固く禁止**している。参照してよいのは MapLibre GL JS（BSD-3）、PROJ
（MIT）、GeographicLib（MIT）等の自由ライセンス実装のみ。maplibre-vector-printer
は MapLibre GL JS v6.5.0（BSD-3）を対象とした調査・実装なので参照可。
