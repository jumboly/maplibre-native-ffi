# 09. フェーズ 2 検証結果（数学層移植）

[04-recommended-design.md](04-recommended-design.md) §8 フェーズ 2 の記録。GL JS
版（maplibre-vector-printer `e71940d`）の純粋数学層を `src/crs/` に C++
移植し、外部検証フィクスチャで合格した。

## 実行環境（2026-08-23）

- ホストプリセット: macos-arm64-metal（`mise run test` で c-api + crs-math の 2
  ターゲットが green）
- tmerc 実装: 自前 Krüger n⁶（Poder–Engsager。PROJ デフォルト `tmerc` / proj4js
  `etmerc` と同一アルゴリズム、係数は同実装から 1:1 転記）。依存ゼロ
- テスト: Unity（既存 FetchContent v2.6.1 に相乗り）+ 専用ターゲット
  `mln_ffi_crs_tests`（28 テスト）。`src/crs/` ソースを直接コンパイルし
  `maplibre_native_c` をリンクしない構成で、maplibre 非依存をビルドで強制

## 成果物

```
src/crs/
  types.hpp          LngLat / EastNorth / Point2（軸規約を型で分離）
  tmerc.{hpp,cpp}    汎用 Krüger n⁶（楕円体・原点・縮尺係数を引数化）
  jprcs.{hpp,cpp}    系原点テーブル（告示第 1613 号、除算式のまま保持）・EPSG
  homography.{hpp,cpp}  4 点 DLT（Hartley 正規化）・mat4 埋め込み
  extent.{hpp,cpp}   RenderExtent・回転付き紙面/NDC 変換・equivalent_zoom
  tests/             Unity テスト 4 ファイル + 外部フィクスチャ 84 レコード
```

ビルド統合: `MLN_FFI_C_API_SOURCES` に 4 .cpp を追加（全プリセットで -Werror /
clang-tidy が掛かる。シンボルはエクスポートリスト外なので ABI 不変）。テストは
`mln_ffi_add_crs_test()`（`cmake/mln_ffi_tests.cmake` 末尾）で、c_api
スイートと同じ `MLN_FFI_TEST_SUPPORTED` gate・glob + main.cpp
テキスト検査・プリセット分岐（Emscripten は node 実行、iOS/tvOS は
シミュレータラッパ、android/ohos は mise.toml のエミュレータ直叩き分岐に
追記）。crs テストが走らない経路は c_api テストが走らない経路と完全一致。

## 外部検証の実測値（合格）

| 検証                                    | 実測           | 契約許容 |
| --------------------------------------- | -------------- | -------- |
| pyproj 全 19 系 × 4 点（76 点）max diff | **0.047 mm**   | 5 mm     |
| GSI 測量計算 API 公式値 8 都市 max diff | **0.044 mm**   | 5 mm     |
| 真北方向角（数値微分、8 都市）max diff  | **9.3e-6 度**  | 5e-4 度  |
| 往復恒等（76 点、経緯度）max diff       | **2.8e-14 度** | 1e-11 度 |

実測が pyproj フィクスチャの丸め（0.1 mm）未満に収まっており、アルゴリズム
自体は一致していることを裏付ける。GL JS 版の許容（±5mm / 1e-9 度）に対し
往復恒等は 1e-11 度へ締めて固定した。

## 移植時の設計決定（GL JS 版からの変更）

1. **DLT に Hartley 正規化を追加**: TS 版の絶対ピボット閾値 1e-15 は入力
   スケール依存で、列スケール混在系（タイル座標 ~1e4 と定数 1）では単純な
   相対化も機能しない。src/dst を重心 0・平均距離 √2 に正規化してから解き
   相似変換で戻す標準手法に置き換え、縮退判定を相対閾値
   `kRelativePivotEpsilon = 1e-13`（正規化後系）で行う。×1e6 / ×1e-6
   スケール下の縮退検出・非縮退求解をテストで固定
2. **mat4 は double のまま**（TS 版は Float32Array 丸め）。mat4 一致テストの
   許容を ±1e-5 → ±1e-12 に締めた。`Mat4` は `std::array<double, 16>`
   column-major の自前 alias（mbgl 非依存。統合層で `static_assert` 予定）
3. **equivalent_zoom の再定式化**:
   `log2(C·cosφ / (512 · mpp ·
   pixel_ratio))`。旧 `scale·0.0254/96`
   との恒等変形で数値的に厳密等価。 「dpi
   は式に現れない」契約は「(scale·0.0254/96, 1.0) ≡ (scale·0.0254/300,
   300/96)」テストとして置き換え（[06](06-decisions.md) §3 の印刷語彙排除に
   準拠）
4. **rotation を extent に新規実装**（GL JS 版は north-up 固定）: `rotation_deg`
   = 出力画像の上方向が指す座標北からの方位角（時計回り正、 度）。θ=0
   で北が上、θ=90 で東が上。JPRCS→紙面直交系の段で合成し、θ=0 で GL JS
   版の全式に厳密退化する。**mbgl `bearing` との符号照合はフェーズ 3
   の統合層で行い、万一逆でも数学層の規約は変えず統合層で吸収する** （extent.hpp
   に明記済み）
5. **`extentToPolygon`（GeoJSON）は `boundary_east_north` に置き換え**:
   閉リング（4s+1 点、紙面空間でサンプリング）の EastNorth を返す。経緯度化
   は呼び出し側が tmerc と合成（extent → jprcs のモジュール間依存を切断）。
   このリングはフェーズ 3 の tile_cover 余白フックの入力そのもの
6. **コンバータキャッシュは移植しない**: 係数計算は軽量で、グローバル状態
   ゼロによりスレッド安全がタダで手に入る
7. **`metersPerPixel(scale, dpi)` / `extentFromCenterLngLat` は消滅**:
   primitive（meters_per_pixel）がそのものになり、構築は 1 行に退化

## フェーズ 3 への持ち越し

- rotation の符号を mbgl `bearing` と照合（上記 4）
- ホモグラフィの cameraToCenterDistance 倍率設計（[08](08-phase1-results.md)
  の持ち越し事項。同次スカラー倍の機構はフェーズ 1 で実証済み）
- `boundary_east_north` + `TransverseMercator::inverse` → Mercator AABB +
  余白、を tile_cover フックに接続
- world_file（.pgw / .aux.xml）はフェーズ 2 スコープ外とした（QGIS 検証は phase0
  の Python パイプラインを流用可能。必要になった時点で置き場を決定）

## 開発メモ

- clang-tidy はビルドでは既定 OFF だが clangd 経由でエディタに出るため全て
  解消した。数学コードで効いた規則: `readability-identifier-length`
  （パラメータは 3 文字以上）、`readability-math-missing-parentheses`、
  `modernize-avoid-c-arrays` / designated initializers、可変添字は `.at()`
  で回避（固定 8×8 では最適化で消える）。Unity マクロ由来の
  `cppcoreguidelines-avoid-do-while` はテスト固有のノイズで、既存 c_api
  スイートと同じく許容
- Android/OHOS エミュレータ経路はローカル SDK 未導入のため CI での実行確認
  に委ねる（ランナースクリプトの複数バイナリ対応は確認済み）
