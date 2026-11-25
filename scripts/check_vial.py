#!/usr/bin/env python3
"""固定版 QMK の lint を、このリポジトリの Vial 構成に適用する。"""

import importlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def main():
    repo, qmk_root = (Path(arg).resolve() for arg in sys.argv[1:])
    # ビルドスクリプトを唯一の固定リビジョン定義とする。
    match = re.search(r'^VIAL_QMK_REF="([0-9a-f]{40})"$',
                      (repo / "scripts/local_build_vial.sh").read_text(), re.MULTILINE)
    if match is None:
        raise RuntimeError("ビルドスクリプトの固定リビジョンを取得できません")
    revision = subprocess.check_output(
        ["git", "-C", str(qmk_root), "rev-parse", "HEAD"], text=True).strip()
    if revision != match[1]:
        raise RuntimeError("Vial QMK のリビジョンが不一致です。先に local_build_vial.sh を実行してください")
    print(f"[i] Vial QMK: {revision}", flush=True)

    # キャッシュの keyboards を変更せず、毎回ワークスペースの現状を検査する。
    # 定数が cwd から決まるため、QMK の import より先に一時ツリーへ移動する。
    with tempfile.TemporaryDirectory(prefix="ortho-vial-check-") as tmp:
        tree = Path(tmp)
        for child in qmk_root.iterdir():
            if child.name not in {".git", "keyboards", ".venv", ".build"}:
                (tree / child.name).symlink_to(child, target_is_directory=child.is_dir())
        shutil.copytree(repo / "qmk_firmware/keyboards", tree / "keyboards")
        subprocess.run(["git", "init", "-q", str(tree)], check=True)
        os.chdir(tree)
        os.environ["ORIG_CWD"] = str(tree)
        os.environ.pop("QMK_USERSPACE", None)
        sys.path.insert(0, str(qmk_root / "lib/python"))

        from milc import cli
        from qmk.json_schema import json_load

        lint = importlib.import_module("qmk.cli.lint")
        # vial 名だけを許可する。他のキーマップ規約・コードチェックは維持する。
        lint.INVALID_KM_NAMES = [name for name in lint.INVALID_KM_NAMES if name != "vial"]
        cli.config.lint.strict = True
        targets = json.loads((repo / "target.json").read_text())["include"]
        pairs = sorted({(target["keyboard"], target["keymap"]) for target in targets})
        if not pairs:
            raise RuntimeError("target.json にチェック対象がありません")
        okay = True
        for keyboard in sorted({kb for kb, _ in pairs}):
            print(f"[i] keyboard: {keyboard}", flush=True)
            if not lint.keyboard_check(keyboard):
                okay = False
        # lint() の default 必須判定・個人設定の補完を経由せず実際の対象を検査する。
        for keyboard, keymap in pairs:
            print(f"[i] keymap: {keyboard}:{keymap}", flush=True)
            if not lint.keymap_check(keyboard, keymap):
                okay = False
            vial_json = tree / "keyboards" / keyboard / "keymaps" / keymap / "vial.json"
            if vial_json.exists():
                if not isinstance(json_load(vial_json), dict):
                    raise RuntimeError(f"Vial 定義がオブジェクトではありません: {vial_json}")
        if not okay:
            return 1
    print("[OK] 定義・キーマップのチェックに成功しました（C のコンパイルは別途ビルドで確認）。")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"[!] {error}", file=sys.stderr)
        sys.exit(1)
