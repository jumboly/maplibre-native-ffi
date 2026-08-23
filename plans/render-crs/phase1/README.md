# フェーズ 1: 仮説検証（タイル行列への歪み注入）

> **注**: 実験パッチ 0004 はフェーズ 3 で本番のタイル行列フック
> （`0005-tile-matrix-hook.patch`）に置き換えて削除した。本手順を再現する 場合は
> 0004 が存在した当時のコミット（フェーズ 2 完了時点 `5c2c18d7`）を checkout
> すること。

[04-recommended-design.md](../04-recommended-design.md) §8 フェーズ 1 の実装。
実験パッチ `patches/maplibre-native/0004-experimental-tile-matrix-warp.patch` が
`TransformState::matrixFor()` の出力へ環境変数 `MLN_TILE_MATRIX_WARP`
（column-major の 16 数値、world mercator pixel 空間の mat4）を左乗算する。
これで「タイル毎 mat4 集約」仮説を全レイヤについて目視・画素比較で検証する。

- 検証結果と結論: [../08-phase1-results.md](../08-phase1-results.md)
- 生成物はすべて `build/render-crs-phase1/`（gitignore 済み）
- 前提: フェーズ 0 の mbgl-render ビルドツリーとタイルキャッシュ
  （[../phase0/README.md](../phase0/README.md) §1〜2 を先に実行）

## 実行

```bash
cd plans/render-crs/phase1

# ① 全レイヤ検証スタイルを生成（GSI std + raster + circle 格子）
mise exec -- python make_style.py

# ② ベースライン + 歪み 5 ケースをレンダし、無害性 2 件を画素比較で自動判定
mise exec -- python run_cases.py
```

出力:

```
build/render-crs-phase1/
  style/alllayers.json      ← ①
  out/baseline_stdstyle.png ← ②（フェーズ 0 出力とのパッチ前後比較用）
  out/baseline_nowarp.png / identity.png / rotate10.png / shear.png
     /projective.png / projective_x2.png
```

## ケースの意味

| ケース          | W                                     | 見るもの                                                 |
| --------------- | ------------------------------------- | -------------------------------------------------------- |
| baseline_nowarp | 環境変数なし                          | 比較基準。フェーズ 0 出力とも比較（パッチ前後の一致）    |
| identity        | 単位行列                              | 注入経路の無害性（baseline と画素一致）                  |
| rotate10        | 画面中心周り 10° 回転                 | 全レイヤの一体追従・ラベル直立・タイル境界               |
| shear           | せん断 0.2 + 異方スケール 0.95 / 1.05 | ホモグラフィのアフィン部の一般形                         |
| projective      | 第 4 行に射影成分（端で w′ = 1±0.25） | w 依存の symbol / circle サイズ変化                      |
| projective_x2   | 上を全体 2 倍                         | 同次スカラー倍の幾何不変と、w 絶対値によるサイズ補正機構 |

自動判定は「差分画素 ≤250 かつ ≤2 LSB」。Metal は同条件でも GPU ラスタライズの
LSB ノイズが数画素乗るため（実測 2〜8 画素・最大 1 LSB）、完全一致ではなく
ノイズ床で判定する。

## 目視の追加観点

- タイル欠けの境界条件は、ズームを上げた臨時レンダで探る（結果 08 のとおり
  z15.55 + 10° で画面隅に欠け。`run_cases.py` の部品を流用して
  `dataclasses.replace(CASE, center_lng=…, scale=5000)` で任意地点・ズームを
  レンダできる）
- map-aligned ラベル（等高線数値）は都心に無いため、高尾山周辺 （139.2440,
  35.6252）の 1:5000 相当で確認した
