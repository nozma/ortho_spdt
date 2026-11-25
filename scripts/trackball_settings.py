#!/usr/bin/env python3
"""独自トラックボール設定の表示・バックアップ・復元。Vial設定自体は変更しない。"""

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import sys

VID, PID = 0xFEED, 0x0000
UID = 'a18b9b77327e7a6c'
COMMAND, PROTOCOL_VERSION = 0x40, 1
GET, SET = 1, 2
FORMAT = 'split_ortho4x6.trackball'
FILE_VERSION = 1
SIDES = {'left': 1, 'right': 2}
CPI = (200, 400, 800, 1600, 3200)
ANGLES = tuple(range(-180, 181, 15))
SHIFTS = (1, 2, 3, 4, 5, 6)
GAINS = (0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0)
GAMMAS = (0.5, 0.75, 1.0, 1.25, 1.5)
STATUSES = {1: '未対応のプロトコル版', 2: '不正な設定値', 3: 'Vialがロックされています',
            4: '未対応の操作', 5: '不正な要求パケット'}


def decode_config(value):
    if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
        raise ValueError('設定値は32ビット符号なし整数である必要があります')
    def index(table, shift, mask):
        i = (value >> shift) & mask
        if i >= len(table):
            raise ValueError(f'設定インデックスが範囲外です（bit {shift}）')
        return table[i]
    return {
        'left': {'cpi': index(CPI, 0, 0xF), 'rotation_degrees': index(ANGLES, 4, 0x1F),
                 'scroll_mode': bool(value & (1 << 9))},
        'right': {'cpi': index(CPI, 10, 0xF), 'rotation_degrees': index(ANGLES, 14, 0x1F),
                  'scroll_mode': bool(value & (1 << 19))},
        'scroll_inverted': bool(value & (1 << 20)),
        'scroll_divisor': 1 << index(SHIFTS, 21, 7),
        'scroll_gamma': index(GAMMAS, 24, 7),
        'diagonal_scroll': bool(value & (1 << 27)),
        'scroll_gain': index(GAINS, 28, 7),
        'cursor_acceleration': not bool(value & (1 << 31)),
    }


class Device:
    def __init__(self, handle, side):
        self.handle = handle
        self.side = side

    def exchange(self, packet):
        # 誤ってリセット等を送らないよう、送信可能な要求をここで限定する。
        if packet != b'\xfe\x00' and not (
            len(packet) == 9 and packet[:4] == bytes([COMMAND, 0x54, 0x42, PROTOCOL_VERSION])
            and packet[4] in (GET, SET)
        ):
            raise ValueError('許可されていないUSBコマンドです')
        if self.handle.write(b'\x00' + packet.ljust(32, b'\x00')) != 33:
            raise RuntimeError('HID要求を完全に送信できませんでした')
        reply = bytes(self.handle.read(32, 3000))
        if len(reply) != 32:
            raise RuntimeError('HID応答がありません。Vialを閉じ、接続を確認してください')
        return reply

    def identify(self):
        reply = self.exchange(b'\xfe\x00')
        if reply[4:12].hex() != UID:
            raise RuntimeError('キーボードUIDが一致しません。書き込みは行いません')

    def config(self, operation=GET, value=0):
        if operation not in (GET, SET):
            raise ValueError('未対応の操作です')
        decode_config(value)
        packet = struct.pack('<5BI', COMMAND, 0x54, 0x42, PROTOCOL_VERSION, operation, value)
        reply = self.exchange(packet)
        if reply[:5] != packet[:5]:
            raise RuntimeError('独自設定APIの応答ではありません。対応ファームウェアが必要です')
        if reply[6] != SIDES[self.side]:
            raise RuntimeError(f'USB接続側が指定した {self.side} と一致しません')
        if reply[5] != 0:
            raise RuntimeError(STATUSES.get(reply[5], f'不明なエラー: {reply[5]}'))
        if any(reply[11:]):
            raise RuntimeError('未対応の応答データです')
        result = struct.unpack_from('<I', reply, 7)[0]
        decode_config(result)
        return result

    def stable_read(self):
        first = self.config()
        if first != self.config():
            raise RuntimeError('取得中に設定が変化しました。操作を止めて再実行してください')
        return first


@contextmanager
def connect(side):
    try:
        import hid
    except ImportError as error:
        raise RuntimeError('scripts/requirements-hid.txt の依存をインストールしてください') from error
    matches = [d for d in hid.enumerate(VID, PID)
               if d['usage_page'] == 0xFF60 and d['usage'] == 0x61
               and d['product_string'] == 'split_ortho4x6']
    if len(matches) != 1:
        raise RuntimeError(f'対象のRaw HIDが一意ではありません（{len(matches)}件）')
    handle = hid.device()
    try:
        handle.open_path(matches[0]['path'])
        device = Device(handle, side)
        device.identify()
        # SETより前に接続側・プロトコル対応を読み取りで検証する。
        device.config()
        yield device
    finally:
        handle.close()


def backup_record(side, value):
    return {'format': FORMAT, 'version': FILE_VERSION, 'protocol_version': PROTOCOL_VERSION,
            'keyboard_uid': UID, 'side': side, 'created_at': datetime.now(timezone.utc).isoformat(),
            'packed_config': f'0x{value:08x}', 'settings': decode_config(value)}


def load_backup(path, side):
    def unique_keys(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f'JSONに重複キーがあります: {key}')
            result[key] = value
        return result
    record = json.loads(path.read_text(), object_pairs_hook=unique_keys)
    if (record['format'] != FORMAT or type(record['version']) is not int
            or record['version'] != FILE_VERSION or type(record['protocol_version']) is not int
            or record['protocol_version'] != PROTOCOL_VERSION):
        raise ValueError('未対応のバックアップ形式・バージョンです')
    if record['keyboard_uid'] != UID or record['side'] != side:
        raise ValueError('バックアップのキーボードUIDまたは左右が一致しません')
    packed = record['packed_config']
    if not isinstance(packed, str) or len(packed) != 10 or not packed.startswith('0x'):
        raise ValueError('packed_configの形式が不正です')
    value = int(packed, 16)
    expected = decode_config(value)
    if json.dumps(record['settings'], sort_keys=True) != json.dumps(expected, sort_keys=True):
        raise ValueError('設定の表示値とpacked_configが一致しません')
    return value


def restore(device, value):
    previous = device.stable_read()
    if previous != value:
        if device.config(SET, value) != value:
            raise RuntimeError('復元応答が一致しません。現在値を取得して確認してください')
    if device.stable_read() != value:
        raise RuntimeError('復元後の読み戻しが一致しません')
    return previous != value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--side', choices=SIDES, required=True, help='USB接続している側')
    sub = parser.add_subparsers(dest='operation', required=True)
    sub.add_parser('show', help='現在値を表示（読み取りのみ）')
    sub.add_parser('backup', help='現在値をJSONへ保存（読み取りのみ）').add_argument('file', type=Path)
    sub.add_parser('restore', help='同じ側のJSONをRAMとEEPROMへ復元').add_argument('file', type=Path)
    args = parser.parse_args()
    # 不正なファイルはUSBアクセス前に拒否する。
    target = load_backup(args.file, args.side) if args.operation == 'restore' else None
    with connect(args.side) as device:
        if args.operation == 'restore':
            changed = restore(device, target)
            print('復元・読み戻し確認が完了しました。' if changed else '現在値と一致しています。書き込みは行いませんでした。')
        else:
            record = backup_record(args.side, device.stable_read())
            text = json.dumps(record, ensure_ascii=False, indent=2) + '\n'
            if args.operation == 'show':
                print(text, end='')
            else:
                # 既存バックアップは上書きしない。マクロ等のVial設定はこのファイルの対象外。
                import os
                with os.fdopen(os.open(args.file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as out:
                    out.write(text)
                print(f'バックアップを保存しました: {args.file}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print(f'[!] {error}', file=sys.stderr)
        sys.exit(1)
