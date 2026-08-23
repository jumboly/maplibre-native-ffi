# 06. 確定済み設計判断（再提案禁止）

決定日: 2026-08-23。以降の作業はこの決定を前提とする。覆す場合は本ファイルを
更新し、理由を追記すること。

## 1. 機能名: `render_crs`

「print」は ①投影差し替え機構 ②JPRCS 数学 ③印刷ワークフロー（縮尺⇄dpi・
ワールドファイル）の 3 つを 1 語で束ねた誤称だった。機能の正体は
**「レンダリングの対象 CRS を Web Mercator から差し替えること」**であり、③は
既存 static-image パス + 呼び出し側計算で完結しコア API に入らない。

- C API ドメイン名: `render_crs`（`mln_map_set_render_crs` /
  `mln_map_clear_render_crs`）。「描画のみが対象 CRS になる」実態と境界
  （クエリ系・ジェスチャは対象外）が名前に出ることを重視した
- 不採用: `crs`（クエリ系が Mercator のままなので約束過大）、`display_crs`
  （QGIS 語彙で遠回し）、`print_crs`（旧称）
- 既存名との衝突回避: `mln_projection_mode`（軸測変換）、`mln_map_projection`
  （transform スナップショット）とは別概念であることに注意
- 名前の波及: `src/print/` → **`src/crs/`**、ヘッダは
  `include/maplibre_native_c/render_crs.h`、enum は
  `MLN_CRS_JAPAN_PLANE_RECTANGULAR`。パッチ側のフック層は汎用名
  （`TileMatrixProvider`）、JPRCS 実装は `JprcsTileMatrixProvider`

## 2. スコープ: 静止画・pitch=0・rotation 自由

- **rotation（回転）は解除する**: ホモグラフィの JPRCS→出力 NDC 段に 2D 回転を 1
  枚挟むだけで数学層のコストはほぼゼロ。tile_cover フックは既に外周サンプリング
  → AABB 方式なので回転を無償で吸収する。検証が要るのは symbol （viewport-align
  ラベルの直立・collision box の向き）のみ
- rotation の基準は**座標北（grid north）**。真北基準にしたい呼び出し側は子午線
  収差 γ を自分で足す。回転方向は既存 `bearing` の規約に合わせて実装時に確定
- **pitch は non-goal**: 「w = cameraToCenterDistance 定数で透視を殺す」核心
  トリックが崩れ、symbol 遠近補正・collision perspectiveRatio・視錐台被覆が
  すべて再開封になるため
- **インタラクティブ操作は non-goal**: ヒットテスト逆変換・ジェスチャ・カメラ
  アニメーションの JPRCS 意味論定義が必要になるため。静止画出力が目的である限り
  不要
- options は先頭 `size` + 前方互換方式なので、将来の拡張で ABI は壊れない

## 3. options のパラメータ化: center + meters_per_pixel + rotation

印刷語彙（`scale_denominator` / `dpi`）をコア API から排除する。コアの primitive
は「CRS + 出力範囲 + 解像度 + 回転」:

```c
typedef struct mln_render_crs_options {
  uint32_t size;             // ABI 前方互換（先頭 size 方式）
  uint32_t crs_kind;         // MLN_CRS_JAPAN_PLANE_RECTANGULAR
  uint32_t zone;             // 1..19
  double center_easting;     // 出力中心 [E, N]（メートル）
  double center_northing;
  double meters_per_pixel;   // 出力解像度
  double rotation;           // 座標北基準の回転角
} mln_render_crs_options;
```

縮尺 1:5000 × 300dpi → `meters_per_pixel = 5000 * 0.0254 / 300` は呼び出し側の 1
行。ワールドファイル生成と同様「便利計算は上位層」で、リポジトリの 「便利 API
は除外」方針と一貫させる。

## 4. リポジトリ運用: 本リポジトリ（fork）完結

計画資料・実装・検証フィクスチャ・上位ユーティリティのすべてを本リポジトリで
持つ。旧 maplibre-native-vector-printer リポジトリはアーカイブとして残し、
source of truth は `plans/render-crs/`。

### ブランチモデル

- `main` — upstream（maplibre/maplibre-native-ffi）追従専用。
  `git fetch upstream && git merge --ff-only upstream/main` のみ。自分の
  コミットは一切乗せない
- `render-crs` — 長期統合ブランチ。実装はすべてここに集約。機能作業は feature
  ブランチ → fork 内 PR で `render-crs` へ
- `git config rerere.enabled true` を設定済み（衝突の再解決を自動化）

### 追従サイクル（週 1 回を目安）

1. `main` を upstream に ff 追従
2. `render-crs` に `git merge main`
3. 衝突は既知の 4 点に局所化されるはず: `.mise/bin/sync-submodules` のパッチ
   配列、`cmake/mln_ffi_c_api.cmake`、`maplibre_native_c.h`、
   `ci/snapshots.toml`
4. pin bump が来ていたら `mise run build`。パッチが apply 不能なら
   `sync-submodules` が fail-loud で止まる

### 保守方式の選択（見直しトリガー付き）

- `sync-submodules` のパッチ配列は **glob 化せず現行作法どおり追記**する。
  追従のたびに衝突して煩わしくなったら、そのとき glob 化を upstream に PR する
- submodule パッチは **`.patch` ファイルを直接保守**する。pin bump でパッチが
  大きく壊れる事態が 2 回続いたら「submodule 内作業ブランチ + `git format-patch`
  再生成」方式へ切り替える
- Kotlin の jextract 許可リスト（手動保守・衝突しやすい）は必要になるまで
  触らない

### 資料の置き場

- 作業計画・調査資料は `plans/render-crs/`（upstream の AI_POLICY が定める
  「計画ドキュメントはブランチにコミット」の作法。前例: `plans/executor.md`）。
  `ci/snapshots.toml` は `*.md` を docs コンポーネントに分類済みのため CI 設定の
  変更は不要
- `docs/src/content/docs/`（Starlight サイト）に入れるのはフェーズ 4 で書く
  利用者向けガイドのみ（docs-writing スキル準拠・英語）
- fork 内の作業資料は日本語で可。upstream へ出す部分（design-proposal・PR）は
  その時点で英語化

### 出口戦略（2 段階の upstream 化）

1. フック・パッチ → maplibre-native へ design-proposal（"Projector Component"
   構想の具体化として）
2. `render_crs` ドメイン → コアにフックが入った後で ffi 本体へ提案（ffi の
   スコープは「MapLibre Native の概念を直接露出」であり、コアに概念が存在しない
   間は fork に置くしかない、という順序依存がある）
