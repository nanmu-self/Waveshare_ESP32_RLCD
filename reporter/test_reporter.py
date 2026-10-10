# Syna project-specific code and modifications: Copyright (c) 2026 黑沐.
# SPDX-License-Identifier: MIT; third-party notices remain applicable.
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import reporter
from quota_providers import PROVIDER_CATALOG


class ReporterIdentityTests(unittest.TestCase):
    def test_identity_is_stable(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(
            os.environ, {"SYNA_REPORTER_DATA_DIR": root}, clear=False
        ):
            first = reporter.load_identity()
            second = reporter.load_identity()
            self.assertEqual(first["reporter_id"], second["reporter_id"])
            self.assertEqual(first["pairing_token"], second["pairing_token"])


class CodexLoginTests(unittest.TestCase):
    def test_missing_auth_clears_quota_and_requests_login(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(
            os.environ,
            {"CODEX_HOME": str(Path(root) / "missing"), "OPENAI_API_KEY": ""},
            clear=False,
        ), patch.object(reporter.sys, "platform", "win32"):
            self.assertFalse(reporter.codex_auth_available())
            quota = reporter.CodexQuotaCollector()._read_snapshot()
            agent = reporter.CodexAgentMonitor().snapshot()
            self.assertEqual(quota.source, "login_required")
            self.assertIsNone(quota.short_remaining_percent)
            self.assertEqual(agent.state, "login_required")
            self.assertEqual(agent.task, "请登录 Codex")


class QuotaRecoveryTests(unittest.TestCase):
    def test_failure_cache_backoff_expiry_and_recovery(self):
        collector = reporter.CodexQuotaCollector()
        with patch('reporter.time.monotonic', return_value=100) as clock:
            good = reporter.QuotaSnapshot(week_remaining_percent=65, updated_at=10, source='codex_app_server')
            self.assertEqual(collector._accept_snapshot(good), 30)
            failure = reporter.QuotaSnapshot(source='unavailable', updated_at=20)
            self.assertEqual([collector._accept_snapshot(failure) for _ in range(5)], [5, 10, 20, 30, 30])
            cached = collector.snapshot()
            self.assertEqual((cached.week_remaining_percent, cached.short_remaining_percent, cached.updated_at, cached.stale), (65, None, 10, True))
            clock.return_value = 220
            self.assertIsNone(collector.snapshot().week_remaining_percent)
            clock.return_value = 221
            collector._accept_snapshot(reporter.QuotaSnapshot(week_remaining_percent=60, source='codex_app_server'))
            self.assertFalse(collector.snapshot().stale)
            self.assertEqual(collector.snapshot().week_remaining_percent, 60)
            self.assertEqual(collector._accept_snapshot(failure), 5)

    def test_logout_clears_cache_and_later_failure_cannot_restore_it(self):
        collector = reporter.CodexQuotaCollector()
        collector._accept_snapshot(reporter.QuotaSnapshot(week_remaining_percent=65, source='codex_app_server'))
        collector._accept_snapshot(reporter.QuotaSnapshot(source='login_required'))
        self.assertEqual(collector.snapshot().source, 'login_required')
        collector._accept_snapshot(reporter.QuotaSnapshot(source='unavailable'))
        self.assertIsNone(collector.snapshot().week_remaining_percent)

    def test_cached_window_is_cleared_at_its_reset(self):
        collector = reporter.CodexQuotaCollector()
        collector._accept_snapshot(reporter.QuotaSnapshot(short_remaining_percent=0, short_resets_at=100,
            week_remaining_percent=65, week_resets_at=200, source='codex_app_server'))
        collector._accept_snapshot(reporter.QuotaSnapshot(source='unavailable'))
        with patch('reporter.time.time', return_value=100):
            self.assertIsNone(collector.snapshot().short_remaining_percent)
            self.assertEqual(collector.snapshot().week_remaining_percent, 65)


class QuotaProviderRegistryTests(unittest.TestCase):
    def test_catalog_providers_are_all_registered(self):
        for spec in PROVIDER_CATALOG:
            self.assertIn(spec['id'], reporter.QUOTA_PROVIDERS)
            self.assertEqual(reporter.QUOTA_PROVIDERS[spec['id']].provider_name, spec['id'])

    def test_factory_defaults_to_codex_and_falls_back_on_unknown(self):
        self.assertIsInstance(reporter.make_quota_collector({}), reporter.CodexQuotaCollector)
        self.assertIsInstance(
            reporter.make_quota_collector({'quota_provider': 'something-new'}),
            reporter.CodexQuotaCollector)

    def test_factory_builds_ark_from_config(self):
        collector = reporter.make_quota_collector({
            'quota_provider': 'ark',
            'ark_access_key_id': 'AKLTdemo',
            'ark_secret_access_key': 'sk-secret',
        })
        self.assertIsInstance(collector, reporter.ArkQuotaCollector)
        self.assertEqual(collector.provider_name, 'ark')
        self.assertEqual((collector._access_key_id, collector._secret_access_key),
                         ('AKLTdemo', 'sk-secret'))

    def test_factory_builds_opencode_from_config(self):
        collector = reporter.make_quota_collector({
            'quota_provider': 'opencode',
            'opencode_api_key': ' oc-key ',
        })
        self.assertIsInstance(collector, reporter.OpenCodeQuotaCollector)
        self.assertEqual(collector.provider_name, 'opencode')
        self.assertEqual(collector._api_key, 'oc-key')
        self.assertEqual(collector._POLL_SECONDS, 300)

    def test_unknown_provider_falls_back_to_codex(self):
        collector = reporter.make_quota_collector({'quota_provider': 'something-new'})
        self.assertIsInstance(collector, reporter.CodexQuotaCollector)


class ArkQuotaCollectorTests(unittest.TestCase):
    # 由参考实现（火山官方签名算法的 Node 移植）用固定输入预先生成，
    # 用于防止签名移植被无意改动。
    _REFERENCE_AUTH = ('HMAC-SHA256 Credential=volc-test-access-key-0000000000000001/'
                       '20260101/cn-beijing/ark/request, SignedHeaders=x-date, '
                       'Signature=3229619b7d1954155b5268cbd3a3ee6b0faba092520fa526ffc54cf74b05cdb7')

    def test_authorization_matches_reference_implementation(self):
        self.assertEqual(
            reporter.volc_authorization_header(
                'volc-test-access-key-0000000000000001', 'test-secret-key',
                '20260101T000000Z',
                {'Action': 'GetCodingPlanUsage', 'Version': '2024-01-01'}),
            self._REFERENCE_AUTH)

    def test_parse_maps_session_weekly_and_monthly(self):
        snapshot = reporter.ArkQuotaCollector._parse_snapshot({
            'Status': 'Running',
            'QuotaUsage': [
                {'Level': 'session', 'Percent': 50.780128, 'ResetTimestamp': 1791460408,
                 'Cap': 100, 'RewardTotalPercent': 0},
                {'Level': 'weekly', 'Percent': 22.203463, 'ResetTimestamp': 1791734400,
                 'Cap': 100, 'RewardTotalPercent': 0},
                {'Level': 'monthly', 'Percent': 70.2665, 'ResetTimestamp': 1791647999,
                 'Cap': 100, 'RewardTotalPercent': 0},
            ],
        })
        self.assertEqual(snapshot.source, 'ark_api')
        self.assertEqual(snapshot.short_remaining_percent, 49)
        self.assertEqual(snapshot.week_remaining_percent, 78)
        self.assertEqual(snapshot.month_remaining_percent, 30)
        self.assertEqual((snapshot.short_resets_at, snapshot.week_resets_at),
                         (1791460408, 1791734400))
        self.assertEqual(snapshot.month_resets_at, 1791647999)

    def test_parse_tolerates_missing_and_malformed_entries(self):
        snapshot = reporter.ArkQuotaCollector._parse_snapshot({
            'QuotaUsage': [{'Level': 'weekly', 'Percent': 10}, 'junk',
                           {'Level': 'session', 'Percent': 'bad'}],
        })
        self.assertIsNone(snapshot.short_remaining_percent)
        self.assertEqual(snapshot.week_remaining_percent, 90)
        self.assertIsNone(snapshot.month_remaining_percent)

    def test_parse_accepts_unknown_short_level_as_fallback(self):
        snapshot = reporter.ArkQuotaCollector._parse_snapshot({
            'QuotaUsage': [{'Level': '5h', 'Percent': 40, 'ResetTimestamp': 5},
                           {'Level': 'weekly', 'Percent': 20, 'ResetTimestamp': 6}],
        })
        self.assertEqual(snapshot.short_remaining_percent, 60)
        self.assertEqual(snapshot.week_remaining_percent, 80)

    def test_missing_keys_report_not_configured_without_network(self):
        snapshot = reporter.ArkQuotaCollector('', '  ')._read_snapshot()
        self.assertEqual(snapshot.source, 'ark_not_configured')
        self.assertIsNone(snapshot.short_remaining_percent)

    def test_polling_stays_conservative_for_cloud_rate_limits(self):
        # 云端接口频控阈值未公开，轮询不得快于 5 分钟，缓存需覆盖两个周期。
        self.assertGreaterEqual(reporter.ArkQuotaCollector._POLL_SECONDS, 300)
        self.assertGreaterEqual(reporter.ArkQuotaCollector._CACHE_SECONDS,
                                2 * reporter.ArkQuotaCollector._POLL_SECONDS)


class OpenCodeQuotaCollectorTests(unittest.TestCase):
    _SAMPLE = {
        'subscriberUserId': 'user_01M2SGMGMP6VJJJPXAXXWC3DD4',
        'product': 'go',
        'resumability': 'renewing',
        'cancelAtPeriodEnd': False,
        'access': {
            'startsAt': '2026-10-09T07:52:25.000Z',
            'endsAt': '2026-11-09T07:52:25.000Z',
            'cancelAtPeriodEnd': False,
            'meters': {
                'fiveHour': {'startsAt': '2026-10-09T07:58:34.000Z',
                             'resetsAt': '2026-10-09T12:58:34.000Z',
                             'limitMicroCents': '1200000000',
                             'usedMicroCents': '31914768'},
                'week': {'startsAt': '2026-10-05T00:00:00.000Z',
                         'resetsAt': '2026-10-12T00:00:00.000Z',
                         'limitMicroCents': '3000000000',
                         'usedMicroCents': '31914768'},
                'month': {'resetsAt': '2026-11-09T07:52:25.000Z',
                          'limitMicroCents': '6000000000',
                          'usedMicroCents': '31914768'},
            },
        },
    }

    def test_parse_maps_five_hour_week_month_and_micro_cents(self):
        snapshot = reporter.OpenCodeQuotaCollector._parse_snapshot(
            self._SAMPLE)
        self.assertEqual(snapshot.source, 'opencode_api')
        # 五小时窗口：已用 31.914768/1200 美分 ≈ 2.66%，剩余 97%
        self.assertEqual(snapshot.short_remaining_percent, 97)
        self.assertEqual(snapshot.week_remaining_percent, 99)
        self.assertEqual(snapshot.month_remaining_percent, 99)
        self.assertEqual(snapshot.short_resets_at, 1791550714)  # 2026-10-09T12:58:34Z
        self.assertEqual(snapshot.week_resets_at, 1791763200)  # 2026-10-12T00:00:00Z
        self.assertIsNotNone(snapshot.month_resets_at)

    def test_parse_tolerates_missing_windows_and_bad_values(self):
        snapshot = reporter.OpenCodeQuotaCollector._parse_snapshot({
            'access': {'meters': {
                'fiveHour': {'limitMicroCents': '100', 'usedMicroCents': 'bad'},
                'week': {'limitMicroCents': '0', 'usedMicroCents': '0'},
            }},
        })
        self.assertIsNone(snapshot.short_remaining_percent)
        self.assertIsNone(snapshot.week_remaining_percent)
        self.assertIsNone(snapshot.month_remaining_percent)
        self.assertIsNone(snapshot.month_resets_at)

    def test_plan_summary_exposes_expiry_and_money(self):
        plan = reporter.OpenCodeQuotaCollector._plan_summary(self._SAMPLE)
        self.assertEqual(plan['product'], 'go')
        self.assertEqual(plan['resumability'], 'renewing')
        self.assertFalse(plan['cancel_at_period_end'])
        self.assertEqual(plan['ends_at'], 1794210745)  # 2026-11-09T07:52:25Z
        self.assertAlmostEqual(plan['windows']['fiveHour']['limit_usd'], 12.0)
        self.assertAlmostEqual(plan['windows']['fiveHour']['used_usd'], 0.31914768)

    def test_plan_info_roundtrip_via_read_snapshot(self):
        collector = reporter.OpenCodeQuotaCollector('oc-key')
        sample = json.dumps(self._SAMPLE).encode('utf-8')
        response = io.BytesIO(sample)
        with patch.object(reporter.urllib.request, 'urlopen', return_value=response):
            snapshot = collector._read_snapshot()
        self.assertEqual(snapshot.source, 'opencode_api')
        self.assertEqual(collector.plan_info()['product'], 'go')

    def test_missing_key_reports_not_configured_without_network(self):
        snapshot = reporter.OpenCodeQuotaCollector('  ')._read_snapshot()
        self.assertEqual(snapshot.source, 'opencode_not_configured')
        self.assertIsNone(snapshot.short_remaining_percent)

    def test_http_401_maps_to_auth_failed(self):
        collector = reporter.OpenCodeQuotaCollector('oc-key')
        with patch.object(reporter.urllib.request, 'urlopen') as urlopen_mock:
            urlopen_mock.side_effect = reporter.urllib.error.HTTPError(
                'url', 401, 'Unauthorized', {}, io.BytesIO(b''))
            snapshot = collector._read_snapshot()
        self.assertEqual(snapshot.source, 'opencode_auth_failed')

    def test_http_500_maps_to_unavailable(self):
        collector = reporter.OpenCodeQuotaCollector('oc-key')
        with patch.object(reporter.urllib.request, 'urlopen') as urlopen_mock:
            urlopen_mock.side_effect = reporter.urllib.error.HTTPError(
                'url', 500, 'Server Error', {}, io.BytesIO(b''))
            snapshot = collector._read_snapshot()
        self.assertEqual(snapshot.source, 'unavailable')

    def test_polling_stays_conservative_for_cloud_rate_limits(self):
        self.assertGreaterEqual(reporter.OpenCodeQuotaCollector._POLL_SECONDS, 300)
        self.assertGreaterEqual(reporter.OpenCodeQuotaCollector._CACHE_SECONDS,
                                2 * reporter.OpenCodeQuotaCollector._POLL_SECONDS)

class DiscoveryPayloadTests(unittest.TestCase):
    def test_quota_reset_timestamps_are_published(self):
        class StubQuotaCollector:
            provider_name = "ark"
            @staticmethod
            def snapshot():
                return reporter.QuotaSnapshot(
                    short_remaining_percent=51, week_remaining_percent=78,
                    month_remaining_percent=30,
                    short_resets_at=1791460408, week_resets_at=1791734400,
                    month_resets_at=1791647999,
                    updated_at=10, source="ark_api")

        with tempfile.TemporaryDirectory() as root, patch.dict(
            os.environ, {"SYNA_REPORTER_DATA_DIR": root}, clear=False
        ):
            state = reporter.ReporterState(
                {"reporter_id": "x", "computer_name": "PC", "pairing_token": "t"},
                reporter.MetricsCollector(), reporter.CodexAgentMonitor(),
                StubQuotaCollector(), 8765)
            with patch('reporter.time.time', return_value=1791400000):
                payload = state.discovery("127.0.0.1")
        self.assertEqual(payload["codex_short_remaining"], 51)
        self.assertEqual(payload["codex_week_remaining"], 78)
        self.assertEqual(payload["codex_month_remaining"], 30)
        self.assertEqual(payload["codex_short_resets_at"], 1791460408)
        self.assertEqual(payload["codex_week_resets_at"], 1791734400)
        self.assertEqual(payload["codex_month_resets_at"], 1791647999)
        # 剩余秒数（板子时钟基准不可靠，倒计时用相对值）
        self.assertEqual(payload["codex_short_resets_in"], 60408)
        self.assertEqual(payload["codex_week_resets_in"], 334400)
        self.assertEqual(payload["codex_month_resets_in"], 247999)
        self.assertEqual(payload["quota_provider"], "ark")
        self.assertEqual(payload["quota_provider_title"], "火山方舟")

    def test_missing_reset_timestamps_send_sentinel(self):
        class StubQuotaCollector:
            provider_name = "codex"
            @staticmethod
            def snapshot():
                return reporter.QuotaSnapshot(updated_at=10, source="unavailable")

        with tempfile.TemporaryDirectory() as root, patch.dict(
            os.environ, {"SYNA_REPORTER_DATA_DIR": root}, clear=False
        ):
            state = reporter.ReporterState(
                {"reporter_id": "x", "computer_name": "PC", "pairing_token": "t"},
                reporter.MetricsCollector(), reporter.CodexAgentMonitor(),
                StubQuotaCollector(), 8765)
            payload = state.discovery("127.0.0.1")
        self.assertEqual(payload["codex_short_resets_at"], -1)
        self.assertEqual(payload["codex_week_resets_at"], -1)
        self.assertEqual(payload["codex_month_remaining"], -1)
        self.assertEqual(payload["codex_month_resets_at"], -1)
        self.assertEqual(payload["quota_provider"], "codex")
        self.assertEqual(payload["quota_provider_title"], "Codex")


class ThermalParsingTests(unittest.TestCase):
    def tearDown(self):
        reporter._cpu_temp_cached = None
        reporter._cpu_temp_checked_at = 0.0

    def test_parse_thermal_numbers_skips_noise_and_nulls(self):
        text = "3012\n\r\n 2985 \nnot a number\n3010"
        self.assertEqual(reporter.parse_thermal_numbers(text), [3012.0, 2985.0, 3010.0])

    def test_normalize_accepts_tenths_kelvin_and_whole_kelvin(self):
        self.assertEqual(reporter.normalize_thermal_celsius(3012), 28.1)
        self.assertEqual(reporter.normalize_thermal_celsius(301), 27.9)

    def test_normalize_rejects_out_of_range_values(self):
        self.assertIsNone(reporter.normalize_thermal_celsius(0))
        self.assertIsNone(reporter.normalize_thermal_celsius(99999))

    @patch.object(reporter.psutil, "sensors_temperatures", create=True, side_effect=AttributeError)
    def test_windows_fallback_reads_cim_zones(self, _sensors):
        with patch.object(reporter.os, "name", "nt"), patch.object(
            reporter, "windows_thermal_numbers", return_value=[3012.0, 2985.0]
        ):
            reporter._cpu_temp_checked_at = 0.0
            self.assertEqual(reporter.cpu_temperature(), 28.1)

    @patch.object(reporter.psutil, "sensors_temperatures", create=True, side_effect=AttributeError)
    def test_missing_reading_retries_on_slow_window(self, _sensors):
        with patch.object(reporter.os, "name", "nt"), patch.object(
            reporter, "windows_thermal_numbers", return_value=[]
        ) as windows_probe:
            reporter._cpu_temp_cached = None
            reporter._cpu_temp_checked_at = 100.0
            with patch("reporter.time.monotonic", return_value=110.0):
                self.assertIsNone(reporter.cpu_temperature())
                windows_probe.assert_not_called()
            with patch("reporter.time.monotonic", return_value=140.0):
                self.assertIsNone(reporter.cpu_temperature())
                windows_probe.assert_called_once()


class GpuEngineAggregationTests(unittest.TestCase):
    def test_busy_engine_type_wins(self):
        items = [
            ("pid_1_luid_0_phys_0_eng_0_engtype_3D", 40.0),
            ("pid_2_luid_0_phys_0_eng_1_engtype_3D", 20.0),
            ("pid_1_luid_0_phys_0_eng_0_engtype_VideoDecode", 5.0),
        ]
        self.assertEqual(reporter.aggregate_gpu_engine_utilization(items), 60.0)

    def test_result_is_capped_at_100(self):
        items = [("eng_0_engtype_3D", 80.0), ("eng_1_engtype_3D", 60.0)]
        self.assertEqual(reporter.aggregate_gpu_engine_utilization(items), 100.0)

    def test_empty_instances_report_no_data(self):
        self.assertIsNone(reporter.aggregate_gpu_engine_utilization([]))

    def test_unknown_engine_names_land_in_other_group(self):
        self.assertEqual(
            reporter.aggregate_gpu_engine_utilization([("mystery", 7.0)]), 7.0)


class GpuAdapterClassificationTests(unittest.TestCase):
    def test_intel_igpu_alone_is_integrated(self):
        self.assertTrue(reporter.classify_gpu_adapters(["Intel(R) UHD Graphics 770"]))

    def test_amd_apu_naming_is_integrated(self):
        self.assertTrue(reporter.classify_gpu_adapters(["AMD Radeon(TM) Graphics"]))
        self.assertTrue(reporter.classify_gpu_adapters(["AMD Radeon 780M"]))

    def test_discrete_cards_are_not_integrated(self):
        self.assertFalse(reporter.classify_gpu_adapters(["NVIDIA GeForce RTX 4070"]))
        self.assertFalse(reporter.classify_gpu_adapters(["Intel(R) Arc(TM) A750"]))
        self.assertFalse(reporter.classify_gpu_adapters(["AMD Radeon RX 6700"]))

    def test_mixed_adapters_are_not_integrated(self):
        self.assertFalse(reporter.classify_gpu_adapters(
            ["Intel(R) UHD Graphics 770", "AMD Radeon RX 6600"]))

    def test_virtual_adapters_are_ignored_not_veto(self):
        # 向日葵 OrayIddDriver + RDP 虚拟显卡不应否决真核显
        self.assertTrue(reporter.classify_gpu_adapters(
            ["OrayIddDriver Device", "Intel(R) UHD Graphics 730",
             "Microsoft Remote Display Adapter"]))
        self.assertTrue(reporter.classify_gpu_adapters(
            ["ToDesk Virtual Display", "AMD Radeon(TM) Graphics"]))

    def test_unknown_or_absent_adapters_are_rejected(self):
        self.assertFalse(reporter.classify_gpu_adapters([]))
        self.assertFalse(reporter.classify_gpu_adapters(["Microsoft Basic Display Adapter"]))
        self.assertFalse(reporter.classify_gpu_adapters(["VMware SVGA 3D"]))


if __name__ == "__main__":
    unittest.main()
