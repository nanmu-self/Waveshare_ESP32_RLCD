# Copyright (c) 2026 黑沐. SPDX-License-Identifier: MIT
"""Run with QT_QPA_PLATFORM=offscreen; never touches the user's real worker."""
import os
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch, MagicMock
try:
    from PySide6.QtWidgets import QApplication, QLineEdit, QDialog, QPushButton
except ImportError:
    raise unittest.SkipTest("Windows UI tests require PySide6-Essentials")
from PySide6.QtCore import QEventLoop, QTimer
from windows_ui import (ReporterWindow, number, launch_command, set_login, login_enabled,
                        read_settings, write_settings, read_pairing_token)

app = QApplication.instance() or QApplication([])

class WindowTests(unittest.TestCase):
    def setUp(self):
        self.window = ReporterWindow(preview=True)

    def tearDown(self):
        self.window.wanted = False
        if self.window.worker:
            self.window.worker.kill()
            self.window.worker.waitForFinished(3000)
        self.window.deleteLater()
        app.processEvents()

    def test_missing_metrics_are_not_zero(self):
        self.window.render({'performance': {'cpu_percent': 0}, 'agent': {}, 'devices': []})
        self.assertEqual(self.window.labels['cpu'].text(), '0.0%')
        self.assertEqual(self.window.labels['gpu'].text(), '—')
        self.assertEqual(number(float('nan')), '—')

    def test_stale_quota_is_marked_and_cleared_on_recovery(self):
        self.window.render({'codex_quota': {'week_remaining_percent': 65, 'stale': True}})
        self.assertIn('显示上次数据', self.window.labels['quota_status'].text())
        self.assertIn('65', self.window.labels['quota'].text())
        self.window.render({'codex_quota': {'week_remaining_percent': 60, 'stale': False}})
        self.assertEqual(self.window.labels['quota_status'].text(), 'CODEX')
        self.window.offline()
        self.assertNotIn('60', self.window.labels['quota'].text())

    def test_quota_provider_title_follows_status_payload(self):
        self.window.render({'quota_provider': 'ark',
                            'codex_quota': {'short_remaining_percent': 49, 'week_remaining_percent': 78}})
        self.assertEqual(self.window.labels['quota_status'].text(), '方舟')
        self.window.render({'quota_provider': 'ark',
                            'codex_quota': {'source': 'ark_not_configured'}})
        self.assertEqual(self.window.labels['quota_status'].text(), '方舟 · 未配置密钥')
        self.window.render({'quota_provider': 'ark',
                            'codex_quota': {'source': 'ark_auth_failed'}})
        self.assertEqual(self.window.labels['quota_status'].text(), '方舟 · 密钥无效')
        self.window.render({'quota_provider': 'codex', 'codex_quota': {}})
        self.assertEqual(self.window.labels['quota_status'].text(), 'CODEX')

    def test_settings_roundtrip_in_isolated_dir(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(
                os.environ, {'SYNA_REPORTER_DATA_DIR': root}, clear=False):
            write_settings({'quota_provider': 'ark', 'ark_secret_access_key': 'sk'})
            data = read_settings()
            self.assertEqual(data['quota_provider'], 'ark')
            self.assertEqual(data['ark_secret_access_key'], 'sk')
            write_settings({'ark_access_key_id': 'AK'})
            self.assertEqual(read_settings().get('quota_provider'), 'ark')  # 不丢已有键

    def test_quota_source_dialog_lists_all_providers_and_prefills(self):
        dialog, combo, editors = ReporterWindow._build_quota_dialog(
            {'quota_provider': 'ark', 'ark_access_key_id': 'AKLTdemo',
             'ark_secret_access_key': 'sk-secret'})
        # 回归防护：下拉框必须已加入表单布局（曾漏 addRow 导致只剩两行文字）。
        self.assertIs(combo.parent(), dialog)
        self.assertEqual([combo.itemData(i) for i in range(combo.count())],
                         ['codex', 'ark', 'opencode'])
        self.assertEqual(combo.currentData(), 'ark')
        self.assertEqual(editors['ark_access_key_id'].text(), 'AKLTdemo')
        self.assertEqual(editors['ark_secret_access_key'].echoMode(), QLineEdit.EchoMode.Password)
        # 切到 Codex 时方舟字段隐藏，切回时恢复。
        combo.setCurrentIndex(0)
        self.assertFalse(editors['ark_access_key_id'].isVisibleTo(dialog))
        combo.setCurrentIndex(1)
        self.assertTrue(editors['ark_access_key_id'].isVisibleTo(dialog))
        dialog.deleteLater()

    def test_disconnect_clears_all_live_values(self):
        self.window.render({'computer_name': 'old host', 'performance': {'gpu_percent': 99}, 'devices':[{'ip':'test'}]})
        self.window.offline()
        self.assertEqual(self.window.labels['gpu'].text(), '—')
        self.assertNotIn('test', self.window.labels['device'].text())
        self.assertFalse(self.window.diagnostic.isEnabled())
        self.assertEqual(self.window.labels['host'].text(), '')

    def test_pairing_token_is_generated_once_and_reused(self):
        """令牌首次生成后持久复用；规则与 worker 的 load_identity 兼容（token_urlsafe(18)）。"""
        with tempfile.TemporaryDirectory() as folder:
            with patch('windows_ui.application_data_dir', return_value=Path(folder)):
                first = read_pairing_token()
                second = read_pairing_token()
            self.assertEqual(first, second)
            self.assertRegex(first, r'^[A-Za-z0-9_-]{24}$')
            stored = json.loads((Path(folder) / 'reporter.json').read_text(encoding='utf-8'))
            self.assertEqual(stored['pairing_token'], first)

    def test_pairing_token_dialog_is_reachable(self):
        buttons = [button.text() for button in self.window.content.findChildren(QPushButton)]
        self.assertIn('配对令牌', buttons)
        self.assertTrue(callable(self.window.pairing_token))
        self.assertTrue(callable(self.window.show_pairing_token))

    def test_pairing_token_dialog_builds_and_copies(self):
        """对话框可正常构建；复制动作写入系统剪贴板。"""
        with tempfile.TemporaryDirectory() as folder:
            with patch('windows_ui.application_data_dir', return_value=Path(folder)):
                token = read_pairing_token()
                stub = MagicMock()

                def fake_exec(dialog_self):
                    for button in dialog_self.findChildren(QPushButton):
                        if button.text() == '复制令牌':
                            button.click()
                            break
                    return 0

                with patch.object(QDialog, 'exec', fake_exec), \
                     patch.object(QApplication, 'clipboard', return_value=stub):
                    self.window.pairing_token()
        stub.setText.assert_called_once_with(token)

    def test_small_work_area_keeps_bottom_controls_reachable(self):
        self.window.resize(700, 450)
        self.window.show()
        app.processEvents()
        self.assertGreater(self.window.scroll.verticalScrollBar().maximum(), 0)
        self.window.scroll.ensureWidgetVisible(self.window.login)
        app.processEvents()
        point = self.window.login.mapTo(self.window.scroll.viewport(), self.window.login.rect().center())
        self.assertTrue(self.window.scroll.viewport().rect().contains(point))
        self.window.hide()

    def test_external_worker_is_read_only(self):
        self.window.render({'performance': {}})
        self.assertFalse(self.window.toggle.isEnabled())
        self.assertIsNone(self.window.worker)

    def test_stop_only_owned_process(self):
        worker = MagicMock()
        self.window.worker = worker
        self.window.toggle_service()
        worker.kill.assert_called_once()
        self.assertFalse(self.window.wanted)
        self.window.worker = None

    def test_registry_round_trip_and_quoted_path(self):
        registry = MagicMock()
        with patch.dict(sys.modules, {'winreg': registry}), patch('windows_ui.launch_command', return_value=['C:\\Program Files\\Syna Reporter\\SynaReporter.exe']):
            set_login(True)
            command = registry.SetValueEx.call_args.args[-1]
            self.assertTrue(command.startswith('"C:\\Program Files'))
            registry.QueryValueEx.return_value = (command, 1)
            self.assertTrue(login_enabled())
            set_login(False)
            registry.DeleteValue.assert_called_once()

    def test_worker_crash_schedules_recovery_but_stop_does_not(self):
        worker = MagicMock()
        self.window.worker = worker
        with patch('windows_ui.QTimer.singleShot') as schedule:
            self.window.worker_finished(worker)
            schedule.assert_called_once()
        self.window.worker = worker
        self.window.wanted = False
        with patch('windows_ui.QTimer.singleShot') as schedule:
            self.window.worker_finished(worker)
            schedule.assert_not_called()

    def test_real_child_lifecycle(self):
        with patch('windows_ui.launch_command', return_value=[sys.executable, '-c', 'import time; time.sleep(30)']):
            self.window.start_service()
            process = self.window.worker
            self.assertTrue(process.waitForStarted(3000))
            self.window.toggle_service()
            process.waitForFinished(3000)
            app.processEvents()
            self.assertIsNone(self.window.worker)
            self.assertFalse(self.window.wanted)

if __name__ == '__main__':
    unittest.main()
