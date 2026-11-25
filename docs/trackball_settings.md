# 独自トラックボール設定のバックアップ・復元

`scripts/trackball_settings.py` はUSB Raw HIDで `tb.c` の現在値を取得し、
JSONへ保存・復元する。Vial GUIは使用しない。キー配置やVialマクロ等のバックアップとは別に使う。

## 前提

- このAPIを搭載したファームウェアが必要。旧版では未対応として停止する。
  APIを初めて導入する更新より前の設定を、この機能でさかのぼって取得することはできない。
- 左右のEEPROMは独立している。USB接続している側を `--side left` または `--side right` で指定する。
  実機が返す左右と異なる場合は停止し、反対側への自動転送は行わない。
- 対象は `0xFEED:0x0000`、Raw HID usage page `0xFF60` / usage `0x61`、
  UID `a18b9b77327e7a6c`。複数候補がある場合は停止する。
- USBデバイスを直接開くためのアクセス権限が必要。通信が競合する場合はVialを終了する。
- 復元はVialがアンロック済みの場合だけ可能。スクリプトはロック解除しない。
  現在の `VIAL_INSECURE = yes` ビルドは常時アンロック状態。

## コマンド

リポジトリルートで依存を準備する。Python 3.9以降を使用する。

```sh
python3 -m venv .vial-qmk/.hid-venv
.vial-qmk/.hid-venv/bin/python -m pip install -r scripts/requirements-hid.txt
mkdir -p .vial-qmk/backups
```

右側USB接続時の例:

```sh
.vial-qmk/.hid-venv/bin/python scripts/trackball_settings.py --side right show
.vial-qmk/.hid-venv/bin/python scripts/trackball_settings.py --side right backup .vial-qmk/backups/right-trackball.json
.vial-qmk/.hid-venv/bin/python scripts/trackball_settings.py --side right restore .vial-qmk/backups/right-trackball.json
```

左側はUSBを差し替えて `--side left` と別の保存先を指定する。
`show` / `backup` はGETのみを送信し、2回の取得値の一致を確認する。
バックアップは既存ファイルを上書きしない。保存先の親ディレクトリは事前に作る。

`restore` はファイル形式・UID・左右・値の範囲・表示値との整合を検証してからUSBへ接続する。
実機のUID・左右・API対応もGETで確認した後、必要な場合だけSETを送る。
復元値はRAMとEEPROMへ反映され、GETによる2回の読み戻しで一致を検証する。
同じ値ならホストもファームウェアもEEPROMを書き換えない。
設定を復元した場合は古い平滑化・移動・スクロールの蓄積値を破棄する。
通信失敗時は自動再送・初期化・ロック解除を行わず停止する。
SET後に通信エラーとなった場合は、書き込み済みの可能性があるため `show` で現在値を確認する。

## 保存する内容

JSONにはファイル形式版、USBプロトコル版、UID、USB接続側、取得日時、
32ビットの `packed_config` と、次の人が確認できる `settings` を含む。

- 左右のCPI、回転角度、スクロールモードフラグ
- スクロール反転、除数、gamma、斜めスクロール、ゲイン
- カーソル加速度の有効／無効

`packed_config` と `settings` の不一致は拒否する。バックアップをそのまま復元する用途を想定し、
手編集用の設定エディターは提供しない。既存EEPROMのビット配置は変更しない。
右側のスクロールモードフラグも保存するが、現行の右側動作は従来どおりカーソル固定。

以前の `tb_load()` は値0を未初期化とみなしていたが、全インデックス0も有効な設定である。
現在はQMKの `eeconfig_init_kb()` で明示的に初期値を書き、値0は正常な設定として読み込む。
範囲外のEEPROM値を検出した場合は従来どおり初期値に戻す。
ファームウェア更新時にEEPROMが初期化される可能性は残るため、更新前に必ずバックアップする。

## USBプロトコル v1

要求・応答は32バイト（HID report IDの0はこの外側に付ける）。
独自コマンド `0x40` はVIAの未処理メッセージ用 `raw_hid_receive_kb()` で扱う。
Vial本体や固定版QMKのソースは変更しない。

| バイト | 要求 | 応答 |
|---|---|---|
| 0 | `0x40` | 同じ |
| 1–2 | ASCII `TB` | 同じ |
| 3 | プロトコル版 `1` | 同じ |
| 4 | GET=`1`、SET=`2` | 同じ |
| 5 | GETでは0、SETでは設定値の最下位バイト | ステータス |
| 6 | GETでは0、SETでは設定値の次のバイト | USB接続側: 左=`1`、右=`2` |
| 7–8 | GETでは0、SETでは設定値の残り2バイト | 成功時の現在設定値の先頭2バイト |
| 9–10 | 0 | 成功時の現在設定値の残り2バイト |
| 11–31 | 0 | 0 |

SET要求の設定値はbytes 5–8、成功応答の設定値はbytes 7–10、いずれもlittle endian。
エラー応答の設定領域は0。ステータスは0=成功、1=版不一致、2=値不正、3=ロック中、
4=操作不明、5=パケット不正。長さ・コマンド・マジック不一致はVIAの未処理応答 `0xFF`。
予約領域が非0の要求は拒否する。GETはEEPROMやロック状態を変更しない。

| 設定ビット | 内容・許容範囲 |
|---|---|
| 0–3 / 10–13 | 左右CPI index: 0–4 → 200, 400, 800, 1600, 3200 |
| 4–8 / 14–18 | 左右回転 index: 0–24 → −180°〜＋180°、15°刻み |
| 9 / 19 | 左右スクロールモードフラグ |
| 20 | スクロール反転 |
| 21–23 | 除数index: 0–5 → 2, 4, 8, 16, 32, 64 |
| 24–26 | gamma index: 0–4 → 0.50〜1.50、0.25刻み |
| 27 | 斜めスクロール許可 |
| 28–30 | ゲインindex: 0–6 → 0.50〜2.00、0.25刻み |
| 31 | カーソル加速度無効（1で無効） |

## 検証

```sh
python3 -m unittest discover -s tests -v
scripts/local_check_vial.sh
scripts/local_build_vial.sh
```

単体テストは実機にアクセスしない。Python側の入力検証・GET/SET・左右判定と、
実際の `tb.c` をスタブ環境でコンパイルしたCテストを実行する。
Cテストはロック、境界値、不正要求、EEPROM保存・再読込、平滑化状態の破棄を確認する。
実際のUSB通信・再接続後のEEPROM保持は、対応版を書き込んだ実機で別途確認する。
