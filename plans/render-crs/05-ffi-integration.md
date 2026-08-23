# 05. 本リポジトリ（maplibre-native-ffi fork）での実装設計

対象: 本リポジトリ（MapLibre Native の実験的 C ABI + 9
言語バインディングのモノレポの fork）。 [04](04-recommended-design.md) の案
E（最小フック・パッチ + 外部 CRS モジュール）を、この FFI
層から使えるようにするための調査結果と設計。命名・スコープ・API 形状は
[06-decisions.md](06-decisions.md) で確定済み。

## 1. 結論: 案 E は本リポジトリの既存機構にそのまま乗る

本リポジトリは既に「maplibre-native
にパッチを当てて使う」仕組みを標準装備している。maplibre-native 本体の独自 fork
の保守は不要で、**必要なのは (a) パッチの追加、(b) C API ドメインの追加、(c)
使いたい言語のバインディング追加**の 3 点。

| 案 E の要素                                        | 本リポジトリでの実現                                                                                                                        |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| コアへの最小フック・パッチ                         | `patches/maplibre-native/NNNN-*.patch`（既存機構、後述 §2）                                                                                 |
| 外部 CRS モジュール（数学層 + プロバイダ）         | `src/crs/` を新サブシステムとして実装                                                                                                       |
| 静止画レンダリング（HeadlessFrontend/Snapshotter） | **既存 C API で完結**: `MLN_MAP_MODE_STATIC` + owned texture + `mln_map_request_still_image` + `mln_texture_read_premultiplied_rgba8`（§4） |
| 各言語からの利用                                   | C API に薄いドメイン `render_crs` を 1 つ追加 → Rust/Zig/Swift/Kotlin/.NET/Dart/Go/Python バインディング                                    |

## 2. パッチ機構（既存）

- maplibre-native は git submodule（shallow、`ignore = dirty`）でコミット
  `550f64be…` に固定。Dependabot が pin を更新
- パッチ適用者は `.mise/bin/sync-submodules`（bash + `git apply`）。**mise
  タスク実行前に自動で走り**（`mise.toml` の
  `[deps.submodules] auto = true`）、`git apply --reverse --check`
  によるべき等判定、リスト外の手編集検出、**適用できなくなったパッチは skip せず
  sync を失敗させる**、という堅牢な設計
- パッチ編集は `mise.toml` の
  `[tasks.build-preset].sources`（`patches/**/*`）に含まれるため、編集すればネイティブビルドが再走する

### パッチ追加の作法（`patches/maplibre-native/README.md` に明文化済み）

1. `patches/maplibre-native/NNNN-kebab-case-description.patch`（`git format-patch`
   形式）
2. 同 README に「何をするか」を段落で記述
3. `.mise/bin/sync-submodules` の `mln_patches=(...)` 配列に追加（適用順もここ）

### 重要な制約: パッチは「上流に入るまでの一時措置」

repo の位置づけは「pin
は上流を追い続け、パッチは上流マージで落とす」。**大きな機能を単一パッチで長期に持ち回るのは想定外**（pin
bump のたびに `git apply` が壊れやすい）。既存パッチも小粒（Windows パス処理 3
ファイル、RunLoop への `setProcessGate` フック 1 ファイル）で、特に
`0003-run-loop-process-gate.patch` は**「フックだけコアに足し、ポリシーは C API
側に置く」という案 E と同型の前例**。

→ JGD2011 対応のパッチは機能単位で細分し（タイル行列フック / symbol 投影フック /
tile_cover 余白フックの 3 本程度）、それぞれ上流 design-proposal / PR
化を前提に設計する。これは 04 §8 フェーズ 5 の方針とも一致する。

## 3. C API 設計

### 命名（確定: `render_crs` → [06](06-decisions.md) §1）

既存名との衝突を避ける根拠:

- `mln_projection_mode`
  は既存で**軸測レンダ変換**（axonometric/xSkew/ySkew）を意味し、「地理座標モデルではない」と明記されている（`include/maplibre_native_c/map.h:860-864`）
- `mln_map_projection` は既存で「transform
  のスナップショットヘルパ」（`projection.h:37`）
- `mln_projected_meters_for_lat_lng`（`projection.h:172`）は球面メルカトル固定

→ 第 3 の名前として **`render_crs`** を採用。「描画のみが対象 CRS
になる」という実態と境界（クエリ系・ジェスチャは対象外）が名前に出る。

### API 形状（確定 → [06](06-decisions.md) §3）

repo の設計思想（「MapLibre 概念を直接露出、便利 API は除外」— snapshotter
を意図的に持たない）に合わせ、**新ドメインは「レンダリング CRS
の設定」のみ**に絞り、レンダリングは既存の static-map
パスを使う。印刷語彙（scale_denominator /
dpi）はコアから排除し、換算は呼び出し側の 1 行:

```c
// include/maplibre_native_c/render_crs.h（新規ドメイン）
typedef struct mln_render_crs_options {
  uint32_t size;             // ABI 前方互換の作法（先頭 size 方式）
  uint32_t crs_kind;         // MLN_CRS_JAPAN_PLANE_RECTANGULAR
  uint32_t zone;             // 1..19
  double center_easting;     // JPRCS [E, N]（内部軸規約は GL JS 版と同じ E,N）
  double center_northing;
  double meters_per_pixel;   // 出力解像度（縮尺 1:5000 × 300dpi → 5000*0.0254/300）
  double rotation;           // 座標北基準の回転角（方向規約は bearing に合わせ実装時確定）
} mln_render_crs_options;

MLN_API mln_status mln_map_set_render_crs(mln_map map, const mln_render_crs_options* options);
MLN_API mln_status mln_map_clear_render_crs(mln_map map);
```

実装は 3 層に分かれる:

1. **パッチ層**（`patches/maplibre-native/`）: `TransformState`
   にタイル行列プロバイダ（`std::function`）等のフックを追加（04 §2 の 3
   フック）
2. **サブシステム層**（`src/crs/`）: JPRCS
   数学（tmerc・ホモグラフィ・extent・回転）+ フックに登録するプロバイダ実装。GL
   JS 版の純粋数学層を C++ 化したもの
3. **C API 層**（`src/c_api/render_crs.cpp`）: `status_boundary` ラッパ 3〜5
   行（repo の定型）

ワールドファイル（.pgw/.aux.xml）生成は「便利 API 除外」の方針に照らすとコア API
には入れず、extent 情報を返す
getter（または呼び出し側計算）に留めるのが整合的。バインディングの上位層か本リポジトリ内の上位ユーティリティ（置き場は実装フェーズで決定）で提供する。

### 座標変換ユーティリティの公開（任意）

`mln_projected_meters_for_lat_lng` の JPRCS
版（`mln_jprcs_for_lat_lng(zone, coordinate, out)` /
逆変換）を同ドメインに置けば、レンダリングと独立に各言語から測量座標変換が使える。真北基準の回転を組みたい呼び出し側に子午線収差
γ の材料を渡す意味でも有用。AGENTS.md
は「プレリリースなので破壊的変更は許容・推奨」としており、API
追加のハードルは低い。

## 4. 静止画レンダリングは既存 C API で完結する

Snapshotter は意図的に存在しないが、等価パスが整備済み（完全な雛形:
`docs/snippets/c/still-image.c`、ガイド `render-a-static-image.mdx`、CI スモーク
`examples/zig-readback/`）:

```
mln_map_options_default() → width/height/scale_factor/map_mode=MLN_MAP_MODE_STATIC
→ mln_map_create
→ mln_map_set_event_mask(STILL_IMAGE_FINISHED | FAILED | MAP_LOADING_FAILED)  ※スタイルロード前に
→ mln_map_set_render_crs(map, …)       ← 本計画で追加する部分
→ owned texture attach（Metal/Vulkan/OpenGL/WebGPU 別）
→ mln_map_request_still_image
→ mln_runtime_pump + drain_events（finished && rendered の両方を待つ）
→ mln_texture_read_premultiplied_rgba8（NULL,0 で size probe → 本読み）
```

既知の落とし穴（`render_session.h:95-99`）: STATIC モードでは resize が map
に着地する前の静止画要求は `SIZE_PENDING` を返す。GL JS
版のキャンバス量子化問題は存在しないが、この pump 順序が Native
版の相当注意点になる。

## 5. 追加コストの見積り（1 ドメイン追加で触るもの）

| 層             | 作業                                                                                                                                                                                                                                                                                         |
| -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| パッチ         | `patches/maplibre-native/000N-*.patch` × 3 本程度 + README 追記 + `sync-submodules` 配列                                                                                                                                                                                                     |
| C API          | 新ヘッダ（アンブレラ `maplibre_native_c.h` に IWYU export 追加）+ `src/c_api/render_crs.cpp` + `src/crs/` 実装 + `cmake/mln_ffi_c_api.cmake` のソースリスト追記                                                                                                                              |
| ABI テスト     | `src/c_api/tests/render_crs_abi.c`（null/undersized/stale ハンドル系のみ。意味的テストはバインディング側）                                                                                                                                                                                   |
| バインディング | **9 言語すべてが必須ではない**が、「サポートを謳うドメインは全操作を露出」が規約。まず必要な言語だけに絞る判断が要る。自動生成側は Rust(bindgen)/Zig(translate-c)/Swift(直 import)/Dart(ffigen)/.NET(ClangSharp) はほぼ自動、**Kotlin は jextract の手動許可リスト（743 行）への追記が必要** |
| ドキュメント   | ガイド .mdx + `docs/snippets/c/*.c`（**ヘッダ変更で hk がスニペット構文検査を自動実行**するため、既存スニペットを壊すと即検出される）                                                                                                                                                        |
| CI             | 新規追跡パスは `ci/snapshots.toml` で分類必須（未分類だと hk が落ちる）                                                                                                                                                                                                                      |

開発ルール上の注意: CLAUDE.md（=AGENTS.md）に「レンダリングテストを CI
未対応を理由にスキップしない（環境を直す）」「バインディングは便利ヘルパを足さない」等の不変条件あり。AI_POLICY.md
により PR
は全行を自分で説明できること・計画ドキュメントはブランチにコミットする運用。

## 6. リポジトリ内の配置（確定: 本リポジトリ完結 → [06](06-decisions.md) §4）

すべてを本リポジトリで持つ。旧 maplibre-native-vector-printer
リポジトリはアーカイブ（本計画資料の移設元）。

```
本リポジトリ（render-crs ブランチ）
 ├ plans/render-crs/         ← 本計画資料（source of truth）
 ├ patches/maplibre-native/  ← コアフック 3 本（上流 PR 候補）
 ├ src/crs/                  ← JPRCS 数学 + プロバイダ（C++。GL JS 版数学層の移植先）
 │   └ 検証フィクスチャ       ← GSI API / pyproj 由来の正解値（テストデータとして持ち込む）
 ├ include/maplibre_native_c/render_crs.h + src/c_api/render_crs.cpp
 ├ bindings/<必要な言語のみ>
 └ 上位ユーティリティ         ← ワールドファイル生成、出力 CLI、GL JS 版出力との比較検証
                               （置き場は実装フェーズで決定。examples/ 配下が候補）
```

### fork 運用の規律

ブランチモデル・追従サイクル・保守方式の確定内容は
[06-decisions.md](06-decisions.md) §4 を参照。設計上の要点のみ再掲する:

1. **改造は「追加」で行う** —
   新ヘッダ・`src/crs/`・新パッチはすべて新規ファイルにし、`git merge main`（=
   upstream 追従）がほぼ素通りする状態を保つ
2. **既存ファイルへの変更を最小化** — 触るのは `maplibre_native_c.h`（IWYU
   export 1
   行）、`cmake/mln_ffi_c_api.cmake`（ソースリスト数行）、`.mise/bin/sync-submodules`（パッチ配列数行）、`ci/snapshots.toml`（必要時のみ）。ここだけがマージ衝突の候補
3. **パッチは機能単位で細分**（タイル行列 / symbol 投影 / tile_cover の 3 本）—
   本体 pin bump で壊れても影響を局所化
4. **バインディングは必要な言語だけ** — 特に Kotlin の jextract 許可リスト（手動
   743 行）は upstream と衝突しやすいので不要なら触らない
5. **追従は定期的・小刻みに** — upstream は pre-1.0
   で破壊的変更を推奨する方針のため、溜めるとマージコストが跳ねる
6. **upstream 規約を fork 内でも守る**（AI_POLICY.md / AGENTS.md: PR
   全行説明、レンダテストを skip しない等）— upstream
   提案時にそのまま出せる状態を保つ

fork を終わらせる出口は 2 段階の upstream 化（→ [06](06-decisions.md) §4）:
①フック・パッチ → maplibre-native へ design-proposal、②`render_crs` ドメイン →
コアにフックが入った後で ffi 本体へ提案（ffi のスコープは「MapLibre Native
の概念を直接露出」であり、コアに概念が存在しない間は fork
に置くしかない、という順序依存がある）。

## 7. 04 ロードマップへの反映

- フェーズ 1（仮説検証）: `mise run build`
  でビルドが通る環境を先に整え、**実験用パッチ（`matrixFor` に歪み行列を注入）を
  patches/ 機構で当てて全レイヤ追従を確認**する
- フェーズ 3（PoC）: 出力 CLI は `bin/render.cpp` 改造ではなく **still-image.c
  雛形ベースの C プログラム**（または zig-readback 改造）で作る
- フェーズ 4（製品化）: Node バインディングではなく **既存 9
  言語バインディングから必要なもの**を選ぶ（Python/.NET/Go
  等がゼロから書かずに手に入る）
- 注意: submodule の
  pin（`550f64be…`）は本調査（[01](01-maplibre-native-projection.md)）で読んだコミット（`2a8ebc49…`）より新しい。01
  の行番号は参照時に要再確認（`mln` リネームは両方に適用済み）
