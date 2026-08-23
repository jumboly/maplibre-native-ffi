# 10. フェーズ 3 検証結果（PoC）

[04-recommended-design.md](04-recommended-design.md) §8 フェーズ 3 の記録。
実験パッチ 0004 を本番のタイル行列フック・パッチに置き換え、統合層 + 最小 C
API + PoC 実行ファイルで**都庁 IX 系（EPSG:6677）1:5000 相当（meters_per_pixel ≈
0.42333、5100×3625 物理 px）の静止画を公開 C API だけで出力**し、 受け入れ 3
条件（[07](07-phase0-results.md) 末尾）をすべて満たした。 手順とスクリプトは
[phase3/](phase3/README.md)。

## 実行環境（2026-08-23）

- ホストプリセット: macos-arm64-metal（Release、Metal、macOS arm64）
- maplibre-native submodule: `550f64be…` + パッチ 0002 / 0003 / 0005 / 0006
- スタイル・タイル: フェーズ 0 の GSI std（std-xyz.json）+ 温まったキャッシュ
- `mise run test`（c-api + crs-math）全 green、
  `mise run //examples/zig-readback:run` スモーク成功（フック未設定 = 従来動作）

## 成果物

```
patches/maplibre-native/
  0005-tile-matrix-hook.patch           TransformState の TileMatrixHook
                                        （matrixFor 末尾で呼ぶ std::function）+
                                        Map::setTileMatrixHook までの配線
  0006-tile-cover-bounds-override.patch TransformState の被覆 override
                                        （std::optional<LatLngBounds>）+
                                        tile_cover.cpp の交差判定 2 箇所の分岐
  （0004 は削除。再現は当時のコミットで → phase1/README.md）
src/crs/provider.{hpp,cpp}   統合層: make_jprcs_render_crs()（タイル毎
                             ホモグラフィのフック + 被覆 bounds + 導出カメラ）
include/maplibre_native_c/render_crs.h + src/c_api/render_crs.cpp
src/map/map.{hpp,cpp}        map_set_render_crs / map_clear_render_crs
plans/render-crs/phase3/     render_case.c（PoC 実行ファイル）+ run_cases.py
```

## フェーズ 3 で確定した設計判断

### 1. projMatrix 合成の打ち消し = プロバイダが P⁻¹·H を返す（第 2 フック不採用）

`matrixFor` の全消費者を実読した結果、案 (a) P⁻¹·H で全消費者が正しく動く:

- plain P（`paint_parameters.cpp` の matrixForTile・ステンシルクリップ、
  `placement.cpp:1503` の隣接タイル境界を含む）はフックが逆行列に使う
  `getInvProjectionMatrix()` の逆とビット同一 → **厳密に H**
- 深度 ε 変種（`layer_tweaker.cpp:61-74`、非 OpenGL）は要素 [14] のみの差で、 z
  出力への一様シフトとして現れ x/y/w は不変 → レイヤ深度順序が保存される。 案
  (b)（合成バイパスの第 2 フック）はこの ε 適用の再実装が必要になるうえ 最低 5
  ファイルに分岐が散るため不採用
- nearClipped は z 行のみの差（pitch=0・平面出力で無害）、aligned（raster 用）
  は ≤0.5 物理 px の一様平行移動（既知の制限 → 下記）

### 2. H の NDC z は 0 ではなく 0.5·w にピン留めする（実装で発見・修正）

`homography_to_mat4` は z 行を全 0 で返すが、そのまま使うと **Metal では
深度テスト有効な drawable（fill / line / background）が全てクリップされ、 symbol
だけが残る**画面になる（初回レンダで実測）。原因は 2 つの規約の合流:

- Metal のクリップ空間は z ∈ [0, w]（GL の [-w, w] と違い、z<0 が即クリップ）
- layer tweaker は深度順序のためにサブレイヤ毎の ε を **z −= ε·w** の形で
  掛ける。ccd 倍済みの H では w ≈ ccd（数千）なので、z=0 から引くと明確に負

修正は統合層で H の z 行 = 0.5 × w 行 とすること。平面出力に深度の意味は
なく、全タイル・全レイヤで同一比率なので ε による順序付けは不変のまま、
どのバックエンドのクリップ範囲にも収まる（`src/crs/provider.cpp` に理由
コメント付き）。

### 3. tile_cover 余白フック = `std::optional<LatLngBounds>` データメンバ

関数ではなくデータ。1 レンダにつき静的な矩形で十分で、値コピー伝播・
スレッド安全が自明になり、upstream 提案でも「明示的な cover bounds
override」として説明しやすい。`tile_cover.cpp` は override 設定時に Frustum
の代わりに z レベルタイル単位の AABB と交差判定する（粗判定・ 精密判定の 2
箇所を同一ラムダに集約）。供給側は `boundary_east_north(extent,
8)` →
`TransverseMercator::inverse` → 経緯度 AABB + スパン 2% パディング。

### 4. ccd 補正 = GL JS と同一「Homography 9 要素を ccd 倍」で成立

`state.getCameraToCenterDistance()` を 9 要素に掛けてから mat4 化。w ≈ ccd
になることで symbol サイズ補正（`ccd / gl_Position.w`）・collision
perspectiveRatio・CPU symbol 経路がすべて ≈1 に中立化する。placement 側の
別フックは不要（`projectAnchor` の w=ccd が同じ行列から自動成立）。

### 5. タイル毎ホモグラフィのサンプル窓 = タイル範囲 ∩ 被覆 AABB

Metal の `clearStencil` は タイル {0,0,0} の行列を要求する
（`paint_parameters.cpp:169`）ため、タイル全域を tmerc に渡すと発散する。
サンプル窓を被覆域にクランプして解く（4 点厳密なので窓の取り方は解の族を
変えず、可視域で最良近似になる）。空交差はタイル内の最近接位置に最小幅 1e-3
の窓を置く。

### 6. C API: set はカメラ（center/zoom, bearing=0, pitch=0）も導出して設定

zoom は `equivalent_zoom`、center は tmerc 逆変換でしか正しく導出できない
「導出値」であり、呼び出し側に任せると絵とフックが不整合になるため。
`mln_map_clear_render_crs` はフックと override を外すだけでカメラは触らない
（ヘッダに意味論を明記）。

### 7. rotation の符号 = 数学層の規約どおり（統合層での反転は不要）

`rotation_deg` = 出力画像の上方向が指す座標北からの方位角（時計回り正）。 rot10
出力を回転項付きワールドファイルで通常グリッドへ gdalwarp すると θ=0
出力と完全に重なることで、レンダ・数学層・ワールドファイルの 3 者の
規約一致を証明した（[09](09-phase2-results.md) §4 の持ち越しを解消。
06-decisions.md §2 の「bearing との符号照合」も、カメラ bearing を常に 0 に
保ちフックが回転を担う実装になったため、この形で決着）。

## 検証結果（すべて合格）

自動（`phase3/run_cases.py`）:

- [x] **Mercator ベースラインの一致**: FFI texture session のレンダが phase0 の
      mbgl-render 出力と画素一致（実測 diff 119 px / max Δ2。判定 ≤250 px かつ
      ≤2 LSB のノイズ床内）。パッチ 2 本 + C API を積んだ状態で
      フック未使用経路が完全に従来動作である証拠
- [x] c-api / crs-math テスト全 green、zig-readback スモーク成功

重畳（gdalwarp + ブレンド合成および QGIS 相当の照合。手順は phase3/README）:

- [x] **受け入れ① 幾何一致**: phase0 Mercator 出力を同一グリッド
      （EPSG:6677、5100×3625）へ lanczos warp した参照と、中心（都庁）・
      左上隅の 2 窓でブレンド重畳 → 道路中心線・建物輪郭にゴーストなし
      （サブピクセル一致）
- [x] **受け入れ② エッジシャープネス**: 同一窓の並置で、warp 参照に見える
      再サンプリングの滲み（隅の細線・ラベル輪郭）がフック方式には無い。 phase0
      `crop_compare.py` と同じ窓のクロップを `build/render-crs-phase3/crops/`
      に生成済み（中心部は warp でも ほぼ無劣化という phase0 の所見も追認）
- [x] **受け入れ③ ラベル直立**: viewport-align ラベルは θ=0 で直立。 rot10
      出力でも直立し（逆ワープするとラベルだけが 10° 傾いて見える
      ことがその証明）、collision の破綻も観測されない
- [x] **GL JS 版との重畳**: `maplibre-vector-printer/examples/gis/` の 都庁
      1:5000 出力とワールドファイル基準で重畳（グリッドは同一 mpp・
      整数オフセット (69, 58) px）→ 道路・建物の幾何が一致
- [x] **rotation=10° スモーク**: 全レイヤが一体で回転し、符号は上記 §7 の
      とおり規約と一致

## 既知の制限（フェーズ 4 以降で扱う）

- **aligned 変種**: raster タイル・image source は alignedProjMatrix 経由の ため
  ≤0.5 物理 px の一様平行移動が乗る（GSI std はベクタのみで影響なし）。
  必要になったら「フック設定時は aligned = plain」の 1 行拡張を検討
- **set 後のカメラ・サイズ変更は未規定**: extent は set 時点の map 寸法で
  固定される。resize 後は set を呼び直す（ヘッダに明記済み）
- `*-translate` 系ペイントプロパティの平行移動は Mercator 意味論のまま
  効く（歪み量が微小なので実害は未観測）
- CollisionIndex のスクリーン空間パディング（08 の持ち越し）は本ケースでは
  問題を観測せず。根室 XIII 系・rotation の定量 E2E とあわせてフェーズ 4 で

## フェーズ 4 への持ち越し

- ABI テスト（`src/c_api/tests/render_crs_abi.c`）、アンブレラ IWYU 精査、 docs
  ガイド + スニペット、必要な言語バインディング
- 根室 XIII 系ケース、rotation の symbol E2E（直立・collision 定量）
- upstream design-proposal（0005/0006 を汎用フックとして。深度 ε が P⁻¹·H
  で自動保存される代数と、z=0.5·w のピン留めは提案文書に載せる価値がある）
