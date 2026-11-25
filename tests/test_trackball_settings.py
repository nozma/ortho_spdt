"""実機への書き込みを行わない、ファームウェアとホストのプロトコル検証。"""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('trackball_settings', ROOT / 'scripts/trackball_settings.py')
tb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tb)
DEFAULT = 3 | (18 << 4) | (1 << 9) | (3 << 10) | (6 << 14) | (1 << 20) | (4 << 21) | (1 << 24) | (3 << 28)


class FakeHID:
    def __init__(self, side=2):
        self.value = DEFAULT
        self.side = side
        self.status = 0
        self.writes = []
        self.sets = 0
        self.timeout = False
    def write(self, data):
        self.writes.append(data)
        return len(data)
    def read(self, length, timeout):
        if self.timeout:
            return []
        p = self.writes[-1][1:]
        reply = bytearray(32)
        if p[:2] == b'\xfe\x00':
            reply[4:12] = bytes.fromhex(tb.UID)
        else:
            reply[:5] = p[:5]
            reply[5] = self.status
            reply[6] = self.side
            if p[4] == tb.SET and self.status == 0:
                self.sets += 1
                self.value = struct.unpack_from('<I', p, 5)[0]
            struct.pack_into('<I', reply, 7, self.value)
        return reply


class ProtocolTests(unittest.TestCase):
    def test_backup_roundtrip_and_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'backup.json'
            for value in [0, DEFAULT, DEFAULT | (1 << 31)]:
                record = tb.backup_record('right', value)
                path.write_text(json.dumps(record))
                self.assertEqual(tb.load_backup(path, 'right'), value)
                with self.assertRaises(ValueError):
                    tb.load_backup(path, 'left')
            record['settings']['scroll_gain'] = 99
            path.write_text(json.dumps(record))
            with self.assertRaises(ValueError): tb.load_backup(path, 'right')
            record = tb.backup_record('right', DEFAULT)
            for field, bad in [('version', True), ('protocol_version', 2), ('keyboard_uid', 'other')]:
                invalid = dict(record); invalid[field] = bad
                path.write_text(json.dumps(invalid))
                with self.assertRaises(ValueError): tb.load_backup(path, 'right')
            path.write_text('{"format":1,"format":2}')
            with self.assertRaises(ValueError): tb.load_backup(path, 'right')

    def test_invalid_indices(self):
        for shift, invalid in [(0,5),(4,25),(10,5),(14,25),(21,6),(24,5),(28,7)]:
            with self.assertRaises(ValueError): tb.decode_config(invalid << shift)
        for value in [-1, 2**32, True]:
            with self.assertRaises(ValueError): tb.decode_config(value)

    def test_read_restore_and_noop(self):
        hid = FakeHID(); device = tb.Device(hid, 'right')
        device.identify()
        self.assertEqual(device.stable_read(), DEFAULT)
        self.assertEqual(hid.sets, 0)
        self.assertFalse(tb.restore(device, DEFAULT))
        self.assertEqual(hid.sets, 0)
        self.assertTrue(tb.restore(device, 0))
        self.assertEqual(hid.sets, 1)
        self.assertEqual(device.stable_read(), 0)

    def test_wrong_side_never_sends_set(self):
        hid = FakeHID(side=1); device = tb.Device(hid, 'right')
        with self.assertRaises(RuntimeError): tb.restore(device, 0)
        self.assertEqual(hid.sets, 0)

    def test_errors_and_command_allowlist(self):
        hid = FakeHID(); device = tb.Device(hid, 'right')
        for status in range(1,6):
            hid.status = status
            with self.assertRaises(RuntimeError): device.config()
        hid.status = 0; hid.timeout = True
        with self.assertRaises(RuntimeError): device.config()
        for command in [b'\x0b', b'\x0a', b'\xfe\x06', b'\xfe\x07']:
            with self.assertRaises(ValueError): device.exchange(command)

    def test_inconsistent_read(self):
        device = tb.Device(FakeHID(), 'right')
        with patch.object(device, 'config', side_effect=[DEFAULT, 0]):
            with self.assertRaises(RuntimeError): device.stable_read()

    def test_bad_file_rejected_before_usb(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'bad.json'; path.write_text('{}')
            with patch('sys.argv', ['trackball_settings.py','--side','right','restore',str(path)]), patch.object(tb, 'connect') as connect:
                with self.assertRaises(KeyError): tb.main()
                connect.assert_not_called()

    def test_backup_does_not_overwrite(self):
        from contextlib import contextmanager
        hid = FakeHID()
        @contextmanager
        def fake_connect(side):
            yield tb.Device(hid, side)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'backup.json'; path.write_text('keep this backup')
            with patch('sys.argv', ['trackball_settings.py','--side','right','backup',str(path)]), patch.object(tb, 'connect', fake_connect):
                with self.assertRaises(FileExistsError): tb.main()
            self.assertEqual(path.read_text(), 'keep this backup')
            self.assertEqual(hid.sets, 0)

    def test_uid_and_unknown_response(self):
        device = tb.Device(FakeHID(), 'right')
        with patch.object(device, 'exchange', return_value=bytes(32)):
            with self.assertRaises(RuntimeError): device.identify()
            with self.assertRaises(RuntimeError): device.config()
        reply = bytearray(32)
        reply[:5] = bytes([tb.COMMAND, 0x54, 0x42, tb.PROTOCOL_VERSION, tb.GET])
        reply[6] = 2; reply[31] = 1
        with patch.object(device, 'exchange', return_value=bytes(reply)):
            with self.assertRaises(RuntimeError): device.config()

    def test_firmware(self):
        compiler = shutil.which('cc')
        self.assertIsNotNone(compiler, 'C compiler is required')
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(Path(tmp) / 'test')
            subprocess.run([compiler, '-std=gnu11', '-Wall', '-I'+str(ROOT/'tests/trackball_stubs'),
                            str(ROOT/'tests/trackball_settings_test.c'), '-lm', '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
