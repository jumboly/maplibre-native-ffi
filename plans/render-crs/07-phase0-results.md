# 07. フェーズ 0 検証結果

[04-recommended-design.md](04-recommended-design.md) §8 フェーズ 0 の記録。
手順とパイプラインは [phase0/README.md](phase0/README.md)。

## 実行環境（2026-08-23）

- maplibre-native submodule:
  `550f64be2232e09934ffc660a9acdbacff8b164a`（リポジトリ pin、パッチ 3
  本適用済みの状態）
- mbgl-render: `build/mbgl-render/`（Release、`MLN_WITH_METAL=ON`、macOS
  arm64）。 Metal ヘッドレスで問題なくレンダ完了する
- GDAL: conda:gdal 3.13.3（mise 管理）
- スタイル: GSI 最適化ベクトルタイル std（`std-xyz.json` へ XYZ 差し替え、
  vector ソースは `v` の 1 つ）

## パイプラインの検証（自動確認済み）

- **zoom の一致**: equivalentZoom の Python 移植が GL JS 版と同値を返す （都庁
  1:5000 → z15.5524、都庁 1:25000 → z13.2304、根室 1:5000 → z15.3933）
- **出力寸法の semantics**: HeadlessFrontend は `floor(論理px × ratio)` で確定。
  3 ケースとも期待どおり（5100×3625 / 1850×1350 / 1223×894）で、GL JS 版の
  キャンバス量子化補正は不要と実証
- **ワープ先グリッドの厳密一致**: gdalinfo の origin / pixel size が
  `center ± mpp·px/2` / `scale·0.0254/dpi` の導出値と全桁一致
  - 都庁 1:5000: origin (-13869.067, -33696.450467), 0.423333… m/px, EPSG:6677
  - 根室 1:5000: origin (107519.761292, -73042.617983), 1.322917… m/px,
    EPSG:6681
- **絵の妥当性**: 都庁スモーク（512×512）と根室 warp を目視確認。ラベル・道路・
  建物が正しく描画される

## QGIS 検証（2026-08-23 実施）

phase0/README.md §3 の手順で目視確認した。幾何は一致（定量の実測値は取って
いない。数値が必要になったらフェーズ 3 の受け入れ時に計測する）:

- [x] 都庁 IX 系: warp 結果が地理院タイル標準地図と整合（目視一致）
- [x] 都庁 IX 系: Mercator ベースラインとほぼ同一（γ≈0.08°、目視一致）
- [x] 根室 XIII 系: Mercator ベースラインに対する明確な回転を確認（目視）
- [ ] 品質評価: リサンプラ 3 変種（bilinear / cubic / lanczos）のラベル・細線の
      滲み比較クロップと所見

## 案 C の品質所見（未記入）

（ラベルの滲み、アンチエイリアス済み細線の潰れ、Mercator 基準のままのラベル向き
などを、フェーズ 3（フック方式 PoC）の受け入れ基準として言語化する。上の
品質評価チェックとあわせて記入する）
