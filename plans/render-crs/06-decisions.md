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
  - フェーズ 2 で数学層規約を確定（→ [09](09-phase2-results.md)）:
    `rotation_deg` は「出力画像の上方向が指す座標北からの方位角（時計回り正、
    度）」。`bearing` との符号照合のみフェーズ 3 に残し、逆でも統合層で吸収する
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

## 5. フック形態: `TransformState` の `std::function` メンバ（2026-08-23、フェーズ 1 で確定）

タイル行列フックは **`TransformState` に `std::function`
型のプロバイダをメンバ追加し、`matrixFor()` 末尾で呼ぶ**形とする（04 §2 の候補
1）。根拠はフェーズ 1 の実証（→ [08](08-phase1-results.md)）:

- `matrixFor` 1 箇所への注入で、描画 3 チョークポイント・CPU
  placement/collision・ヒットテスト・ステンシルクリップマスクのすべてが
  一貫して追従する（全レイヤで目視確認済み）
- `TransformState` は暗黙メンバワイズコピーのため、`std::function` メンバは
  Placement / CollisionIndex / TileCoverParameters / Snapshotter への値コピーで
  自然に伝播する
- 不採用: `matrixFor` / placement 投影の virtual 化（04 §2 の候補 2）。
  影響範囲が広く、伝播もコピー方式より複雑になる

付随する確定事項:

- tile_cover はタイル選択が行列に追従しない（z15.55 + 10° 回転で欠けを実測）
  ため、04 §2 #3 の余白フックを予定どおり実装する
- projMatrix の合成はチョークポイント側で行われるため、フェーズ 3 の
  ホモグラフィ完全適用は「プロバイダが `P⁻¹·H` を返す」案と「合成バイパスの 第 2
  フック」案から実装時に選ぶ

## 6. フェーズ 3 の確定事項（2026-08-23、→ [10](10-phase3-results.md)）

- **projMatrix 打ち消しは「プロバイダが P⁻¹·H を返す」案を採用**（§5 の
  二択に決着）。深度 ε・nearClipped・aligned の 3 変種すべてで代数的に
  無害と実証。第 2 フック案は ε 適用の再実装が要るため不採用
- **H の NDC z は 0.5·w にピン留め**する（z=0 のままだと Metal の クリップ範囲
  [0, w] と深度 ε シフトの合流で深度テスト有効レイヤが全滅）
- **tile_cover 余白フックは `std::optional<LatLngBounds>` データメンバ**
  （関数にしない）。パッチは 0005（タイル行列フック）/ 0006（被覆 override）の 2
  本で、0004 は削除
- **`mln_map_set_render_crs` はカメラ（center/zoom、bearing=0, pitch=0）も
  導出して設定する**。zoom/center は equivalent_zoom + tmerc 逆変換でしか
  導出できない「導出値」であり、呼び出し側に任せると絵とフックが不整合に
  なるため。clear はカメラを触らない
- **rotation の符号は数学層の規約のまま確定**（統合層での反転は不要）。 カメラ
  bearing は常に 0 でフックが回転を担うため、§2 の「bearing との
  符号照合」はワールドファイル経由の重畳一致で決着
