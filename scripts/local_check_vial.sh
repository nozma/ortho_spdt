#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEFAULT_CACHE_BASE="${XDG_CACHE_HOME:-$HOME/.cache}/ortho_spdt64"
VIAL_QMK_DIR="${VIAL_QMK_DIR:-$DEFAULT_CACHE_BASE/vial-qmk}"

if [[ ! -x "$VIAL_QMK_DIR/.venv/bin/python" ]]; then
    echo "[!] Vial QMK の仮想環境がありません。先に scripts/local_build_vial.sh を実行してください。" >&2
    exit 1
fi

# QMK CLI の個人設定を読み込まず、固定版の Python ライブラリを直接使用する。
exec "$VIAL_QMK_DIR/.venv/bin/python" -I "$REPO_ROOT/scripts/check_vial.py" "$REPO_ROOT" "$VIAL_QMK_DIR"
