# Repository Guidelines

## プロジェクト構成とモジュール
- `qmk_firmware/keyboards/split_ortho4x6/`: QMK 基板定義（C コード、`keyboard.json`）。
- `qmk_firmware/keyboards/split_ortho4x6/keymaps/vial/`: 利用中のキーマップと Vial 設定、カスタム処理（`tb.c/.h`）。
- `case/`, `foam/`, `images/`, `pcb/`: ケース/STL、フォーム、レンダ画像、PCB JSON。
- ルート JSON: `split-ortho4x6.json`, `target.json`（Auto-KDK 用のソース/ターゲット）。

## ビルド・テスト・開発コマンド
- ビルド（QMK CLI）: `cd qmk_firmware && qmk compile -kb split_ortho4x6 -km vial`
  - RP2040 向け UF2/hex を生成。`qmk flash` で書き込み、またはブートローダに入り UF2 をドラッグ&ドロップ。
- 代替（Make）: `cd qmk_firmware && make split_ortho4x6:vial`
- 構文/定義チェック: `cd qmk_firmware && qmk lint -kb split_ortho4x6`
- C フォーマット（任意）: `clang-format -i <files>`（差分は最小に）。

## コーディング規約・命名
- 言語: ファームウェアは C、レイアウトは JSON、資産は STL/SVG。
- インデント: スペース4、タブ不可。行幅目安は約100。
- ファイル名: C/ヘッダは snake_case（例: `paw3222.c`, `tb.c`）、ディレクトリは小文字。
- QMK 設定は `rules.mk` / `config.h` / `keyboard.json` に配置。

## テスト指針
- 事前チェック: `qmk lint -kb split_ortho4x6` を PR 前に実行。
- ビルド検証: `qmk compile -kb split_ortho4x6 -km vial` を通し、両手側で UF2 書き込みを確認。
- 機能確認: ポインティングデバイス（PAW3222）動作と Vial レイヤの切替を検証。
- 変更可視化: `keymap.c` のレイヤコメントを明確に維持。

## コミット・プルリク方針
- コミット: 命令形・範囲を絞った件名。例: `firmware: fix split matrix`, `keymap: add scroll toggle`, `case: update left plate`。
- PR: 目的/概要、主要差分、ビルド結果の抜粋を記載。`images/` や `case/` 変更時はスクリーンショット/レンダを添付。
- 関連 Issue/議論をリンク。1 PR = 1 論点を原則。

## セキュリティ・設定の注意
- 開発用途として `VIAL_INSECURE = yes` を有効化中。本番配布では必要性を再検討。
- ターゲット: RP2040、分割有効、カスタムポインティング `paw3222` と `tb.c`。分割/シリアル挙動を理解した上で変更すること。

## コミュニケーションと言語
- このリポジトリでは、回答・レビュー・ドキュメントは原則「日本語」で行います（上流 QMK 連携や外部公開時は英語併記可）。
