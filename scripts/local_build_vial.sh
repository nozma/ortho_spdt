#!/usr/bin/env bash
set -euo pipefail

# Local build that mirrors your GitHub Actions workflow.
# - Uses vial-kb/vial-qmk at a pinned commit
# - Replaces its keyboards/ with this repo's keyboards/
# - Builds targets listed in target.json

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
TARGET_JSON="$REPO_ROOT/target.json"
VIAL_QMK_REF="0f70080d790c0c802729e2c9fbbe67596ba37aac"
VIAL_QMK_DIR="${VIAL_QMK_DIR:-$REPO_ROOT/.vial-qmk}"

if ! command -v git >/dev/null 2>&1; then
  echo "[!] git が見つかりません。" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "[!] python3 が見つかりません。" >&2
  exit 1
fi

if ! command -v make >/dev/null 2>&1; then
  echo "[!] make が見つかりません。" >&2
  exit 1
fi

if [[ ! -f "$TARGET_JSON" ]]; then
  echo "[!] target.json が見つかりません: $TARGET_JSON" >&2
  exit 1
fi

echo "[i] vial-qmk を取得/更新します -> $VIAL_QMK_DIR (@$VIAL_QMK_REF)"
if [[ -d "$VIAL_QMK_DIR/.git" ]]; then
  git -C "$VIAL_QMK_DIR" fetch --tags --prune --depth 1 origin
  git -C "$VIAL_QMK_DIR" fetch origin "$VIAL_QMK_REF" || true
  git -C "$VIAL_QMK_DIR" checkout -q "$VIAL_QMK_REF" || git -C "$VIAL_QMK_DIR" checkout -q origin/master
  git -C "$VIAL_QMK_DIR" reset --hard -q "$VIAL_QMK_REF" || true
else
  git clone --depth 1 https://github.com/vial-kb/vial-qmk.git "$VIAL_QMK_DIR"
  git -C "$VIAL_QMK_DIR" fetch origin "$VIAL_QMK_REF" || true
  git -C "$VIAL_QMK_DIR" checkout -q "$VIAL_QMK_REF" || true
fi

echo "[i] Python 仮想環境を作成して依存をインストールします (PEP 668回避)"
VENV_DIR="$VIAL_QMK_DIR/.venv"
if [[ ! -d "$VENV_DIR" ]]; then
  python3 -m venv "$VENV_DIR"
fi
source "$VENV_DIR/bin/activate"
python -m pip install -U pip setuptools wheel
python -m pip install -r "$VIAL_QMK_DIR/requirements.txt"

echo "[i] キーボード定義を置き換えます"
rm -rf "$VIAL_QMK_DIR/keyboards"
cp -r "$REPO_ROOT/qmk_firmware/keyboards" "$VIAL_QMK_DIR/"

cd "$VIAL_QMK_DIR"

echo "[i] target.json を読み込みます: $TARGET_JSON"
matrix=$(python3 - "$TARGET_JSON" <<'PY'
import json,sys
with open(sys.argv[1]) as f:
    data=json.load(f)
for ent in data.get('include',[]):
    keyboard=ent['keyboard']
    keymap=ent['keymap']
    target=ent.get('target','uf2')
    flags=ent.get('flags','')
    name=ent.get('name',f"{keyboard}_{keymap}")
    print(f"{keyboard}\t{keymap}\t{target}\t{name}\t{flags}")
PY
)

echo "[i] ビルドを開始します"
while IFS=$'\t' read -r keyboard keymap target name flags; do
  echo "[i] make $keyboard:$keymap:$target $flags"
  make $keyboard:$keymap:$target $flags
  src=".build/${keyboard}_${keymap}.${target}"
  dst=".build/${name}.${target}"
  if [[ -f "$src" ]]; then
    mv "$src" "$dst"
    echo "[i] -> $dst"
  else
    echo "[!] 生成物が見つかりません: $src" >&2
  fi
done <<< "$matrix"

echo "[i] 完了。生成物は $VIAL_QMK_DIR/.build にあります。"
deactivate || true
