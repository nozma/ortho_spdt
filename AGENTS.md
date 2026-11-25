# Repository Guidelines

## プロジェクト構成とモジュール
- `qmk_firmware/keyboards/split_ortho4x6/`: QMK 基板定義（C コード、`keyboard.json`）。
- `qmk_firmware/keyboards/split_ortho4x6/keymaps/vial/`: 利用中のキーマップと Vial 設定、カスタム処理（`tb.c/.h`）。
- `case/`, `foam/`, `images/`, `pcb/`: ケース/STL、フォーム、レンダ画像、PCB JSON。
- ルート JSON: `split-ortho4x6.json`, `target.json`（Auto-KDK 用のソース/ターゲット）。

## ビルド・テスト・開発コマンド
- ビルドは `scripts/local_build_vial.sh` を使用すること。ファームウェアの出力先（`.vial-qmk/.build/`）はスクリプトのデフォルト設定から変更しない。
- 定義・キーマップチェック: リポジトリルートで `scripts/local_check_vial.sh` を実行。
  - 初回は `scripts/local_build_vial.sh` で固定版 Vial QMK と Python 仮想環境を準備する。
  - チェックはビルドと同じ固定リビジョンを検証し、QMK CLI の個人設定を読み込まない。
  - 現在のソースを一時ツリーにコピーし、固定版 QMK の strict lint を適用する。キャッシュのキーボード定義は変更しない。
  - `vial` キーマップ名の禁止と `default` キーマップ必須の規約のみ適用しない。検査対象は `target.json` の keyboard/keymap。
  - JSON 定義・レイアウト・コードヘッダー等を検査する。C のコンパイルや実機動作は別途確認する。
  - `VIAL_QMK_DIR` を指定する場合は、ビルドとチェックの両方で同じディレクトリを使う。
- CI: PR 作成・更新時は `Check Keyboard Definitions` が定義・キーマップを自動チェックする。
- CI でのファームウェアビルド: GitHub Actions の `Build Keyboard Firmware` → `Run workflow` から対象ブランチを選んで手動実行する。push / PR では自動ビルドしない。左右の UF2 は実行結果の Artifacts から取得する。
- C フォーマット（任意）: `clang-format -i <files>`（差分は最小に）。

## 実機へのファームウェア書き込み
- 標準手順は [docs/firmware_flashing.md](docs/firmware_flashing.md) に従う。GUI操作は原則不要。
- エージェントは対象側のVial設定バックアップ・UF2ドライブ監視・転送・更新確認・設定比較/復元を担当する。
- ユーザーには必要なUSB差し替えと物理 `QK_BOOT` 押下のみ依頼する。押下後の文字入力や選択肢の送信は求めず、監視でドライブ出現を検出する。監視開始後に「返信不要」と案内する。
- USB接続側と左右UF2の対応を必ず確認する。左右の保存設定は独立しているため、それぞれ更新直前にバックアップする。
- 現在の固定版・`VIAL_INSECURE = yes` ではUSBブートローダー移行コマンドは使えない。物理 `QK_BOOT` を使用する。
- 更新でVial設定が初期値に戻った実績がある。再接続後は全設定を比較し、互換性を確認して差分を復元・再検証する。独自ゲイン/CPI設定は [docs/trackball_settings.md](docs/trackball_settings.md) の専用APIとスクリプトで側ごとにバックアップ・復元する。未対応の旧版では取得不可。

## コーディング規約・命名
- 言語: ファームウェアは C、レイアウトは JSON、資産は STL/SVG。
- インデント: スペース4、タブ不可。行幅目安は約100。
- ファイル名: C/ヘッダは snake_case（例: `paw3222.c`, `tb.c`）、ディレクトリは小文字。
- QMK 設定は `rules.mk` / `config.h` / `keyboard.json` に配置。
- `keymap.c` に定義されているキーマップは変更しない。

## テスト指針
- 事前チェック: `scripts/local_check_vial.sh` を PR 前に実行。通常の `qmk lint` は異なる QMK や個人設定を参照するため使用しない。
- ビルド検証: `scripts/local_build_vial.sh` で左右の UF2 を生成し、両手側で UF2 書き込みを確認。
- 独自設定API・バックアップの単体テスト: `python3 -m unittest discover -s tests -v`（実機への書き込みなし）。
- 機能確認: ポインティングデバイス（PAW3222）動作と Vial レイヤの切替を検証。
- 変更可視化: `keymap.c` のレイヤコメントを明確に維持。

## コミット・プルリク方針
- コミット: 命令形・範囲を絞った件名。メッセージ本文も含め、必ず日本語で記述する。
- PR: 目的/概要、主要差分、ビルド結果の抜粋を日本語で記載。`images/` や `case/` 変更時はスクリーンショット/レンダを添付。
- 関連 Issue/議論をリンク。1 PR = 1 論点を原則。

## セキュリティ・設定の注意
- 開発用途として `VIAL_INSECURE = yes` を有効化中。本番配布では必要性を再検討。
- ターゲット: RP2040、分割有効、カスタムポインティング `paw3222` と `tb.c`。分割/シリアル挙動を理解した上で変更すること。

## コミュニケーションと言語
- このリポジトリでは、回答・レビュー・ドキュメントは原則「日本語」で行います（上流 QMK 連携や外部公開時は英語併記可）。
