# Repository Guidelines

## Project Structure & Module Organization
- `qmk_firmware/keyboards/split_ortho4x6/`: QMK board files (C code, `keyboard.json`).
- `qmk_firmware/keyboards/split_ortho4x6/keymaps/vial/`: Active keymap, Vial config, custom logic (`tb.c/.h`).
- `case/`, `foam/`, `images/`, `pcb/`: CAD assets, foam, renders, and PCB JSONs.
- Root JSON: `split-ortho4x6.json`, `target.json` (Auto-KDK source/targets).

## Build, Test, and Development Commands
- Build (QMK CLI): `cd qmk_firmware && qmk compile -kb split_ortho4x6 -km vial`
  - Produces UF2/hex for RP2040. Use `qmk flash` to flash if supported, or enter bootloader and drag-drop UF2.
- Alternative (Make): `cd qmk_firmware && make split_ortho4x6:vial`
- Lint/check: `cd qmk_firmware && qmk lint -kb split_ortho4x6`
- Format C (optional): `clang-format -i <files>`. Keep diffs focused.

## Coding Style & Naming Conventions
- Language: C for firmware; JSON for keyboard/layout; STL/SVG for assets.
- Indentation: 4 spaces; no tabs. Line width ~100.
- Filenames: snake_case for C/headers (`paw3222.c`, `tb.c`), lower-case dirs.
- QMK config lives in `rules.mk`, `config.h`, `keyboard.json`.

## Testing Guidelines
- Sanity: `qmk lint -kb split_ortho4x6` before PRs.
- Build both: `qmk compile -kb split_ortho4x6 -km vial` and verify UF2 flashes on both halves.
- Functional: validate pointing device (PAW3222) and Vial layers after flash.
- Test naming: for keymap changes, keep layers clearly commented in `keymap.c`.

## Commit & Pull Request Guidelines
- Commits: imperative, scoped subject. Examples: `firmware: fix split matrix`, `keymap: add scroll toggle`, `case: update left plate`.
- PRs: include motivation, summary of changes, build output snippet, and screenshots/renders when touching `images/` or `case/`.
- Link related issues/discussions. Keep one logical change per PR.

## Security & Configuration Tips
- `VIAL_INSECURE = yes` is set for development. Do not ship production builds with insecure Vial unless intended.
- Target: RP2040, split enabled; custom pointing driver `paw3222` and trackball logic `tb.c`. Adjust only if you understand the split/serial implications.
