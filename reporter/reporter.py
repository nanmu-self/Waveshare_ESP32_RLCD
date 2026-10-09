# Syna project-specific code and modifications: Copyright (c) 2026 黑沐.
# SPDX-License-Identifier: MIT; third-party notices remain applicable.
from __future__ import annotations

import argparse
import ctypes
import hashlib
import hmac
import json
import logging
from logging.handlers import RotatingFileHandler
import os
import queue
import re
import secrets
import signal
import sqlite3
import socket
import socketserver
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
from dataclasses import dataclass, asdict
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

import psutil

from media_monitor import NeteaseMediaMonitor
from macos_metrics import MacMetrics
from codex_runtime import read_activity
from quota_providers import DEFAULT_PROVIDER_ID, provider_spec
from platform_support import application_data_dir, disk_root, codex_executable, acquire_posix_instance


HTTP_PORT = 8765
DISCOVERY_PORT = 8766
DISCOVERY_REQUEST = b"AI_PANEL_DISCOVER_V1"
PROTOCOL_VERSION = 1

_cpu_temp_cached: float | None = None
_cpu_temp_checked_at = 0.0


def config_path() -> Path:
    root = application_data_dir()
    root.mkdir(parents=True, exist_ok=True)
    return root / "reporter.json"


def data_root() -> Path:
    return config_path().parent


def read_config() -> dict[str, Any]:
    """读取 reporter.json（容忍文件缺失或损坏），供额度数据源等设置使用。"""
    try:
        data = json.loads(config_path().read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def configure_logging() -> None:
    handler = RotatingFileHandler(
        data_root() / "reporter.log", maxBytes=1_000_000, backupCount=3,
        encoding="utf-8",
    )
    handler.setFormatter(logging.Formatter(
        "%(asctime)s %(levelname)s %(threadName)s %(message)s"
    ))
    logging.basicConfig(level=logging.INFO, handlers=[handler], force=True)


def agent_state_root() -> Path:
    return config_path().parent / "agents"


def load_identity() -> dict[str, str]:
    path = config_path()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        data = {}
    changed = False
    if not data.get("reporter_id"):
        data["reporter_id"] = str(uuid.uuid4())
        changed = True
    if not data.get("computer_name"):
        data["computer_name"] = socket.gethostname()
        changed = True
    if not data.get("pairing_token"):
        data["pairing_token"] = secrets.token_urlsafe(18)
        changed = True
    if changed:
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        temporary.replace(path)
    return data


def codex_auth_available() -> bool | None:
    """None means login must be checked through app-server (macOS Keychain)."""
    if sys.platform == "darwin":
        return None
    if os.environ.get("OPENAI_API_KEY", "").strip():
        return True
    auth_path = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "auth.json"
    try:
        payload = json.loads(auth_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    return bool(payload.get("OPENAI_API_KEY") or payload.get("tokens"))


def local_ip_for(peer_ip: str) -> str:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((peer_ip, 9))
        return probe.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        probe.close()


def nvidia_metrics() -> tuple[float | None, float | None]:
    if sys.platform == "darwin":
        return None, None
    command = [
        "nvidia-smi",
        "--query-gpu=utilization.gpu,temperature.gpu",
        "--format=csv,noheader,nounits",
    ]
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=1.5,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            check=False,
        )
        if result.returncode != 0 or not result.stdout.strip():
            return None, None
        utilization, temperature = result.stdout.splitlines()[0].split(",", 1)
        return float(utilization.strip()), float(temperature.strip())
    except (OSError, ValueError, subprocess.SubprocessError):
        return None, None


_PDH_FMT_DOUBLE = 0x00000200
_PDH_MORE_DATA = 0x800007D2
_GPU_ENGINE_COUNTER = r"\GPU Engine(*)\Utilization Percentage"


def aggregate_gpu_engine_utilization(items: list[tuple[str, float]]) -> float | None:
    """Reduce wildcard GPU Engine samples to one adapter utilization.

    Task Manager sums the engines of each type (3D, Copy, VideoDecode, ...)
    and reports the busiest type; instances are per process per engine.
    """
    totals: dict[str, float] = {}
    for name, value in items:
        marker = name.rfind("engtype_")
        engine = name[marker + len("engtype_"):] if marker >= 0 else "other"
        totals[engine] = totals.get(engine, 0.0) + max(0.0, value)
    if not totals:
        return None
    return round(min(100.0, max(totals.values())), 1)


class _PdhFmtCounterValue(ctypes.Structure):
    # PDH_FMT_COUNTERVALUE: DWORD tag, then an 8-byte value union.
    _fields_ = [("CntyValue", ctypes.c_uint32), ("doubleValue", ctypes.c_double)]


class _PdhCounterValueItem(ctypes.Structure):
    # PDH_FMT_COUNTERVALUE_ITEM_W for wildcard counter arrays.
    _fields_ = [("szName", ctypes.c_wchar_p), ("FmtValue", _PdhFmtCounterValue)]


class WindowsGpuUtilization:
    """GPU usage for every adapter, integrated GPUs included.

    Talks to pdh.dll directly so no extra dependency is needed, and adds the
    counter through PdhAddEnglishCounterW so localized counter names on
    non-English Windows do not break the query. Rate counters need two
    collected samples before the first formatted read.
    """

    def __init__(self) -> None:
        self._ready = sys.platform == "win32"
        self._pdh = None
        self._query = ctypes.c_void_p(0)
        self._counter = ctypes.c_void_p(0)
        self._buffer = None
        self._samples = 0
        if self._ready:
            self._open()

    def _open(self) -> None:
        try:
            pdh = ctypes.WinDLL("pdh")
            # PDH statuses are unsigned DWORDs. Without an explicit restype
            # ctypes treats them as signed ints, so PDH_MORE_DATA
            # (0x800007D2) arrives negative and every formatted read would
            # be mistaken for a failure.
            for name in ("PdhOpenQueryW", "PdhAddEnglishCounterW",
                         "PdhCollectQueryData", "PdhGetFormattedCounterArrayW"):
                getattr(pdh, name).restype = ctypes.c_uint32
            query = ctypes.c_void_p(0)
            counter = ctypes.c_void_p(0)
            if pdh.PdhOpenQueryW(None, 0, ctypes.byref(query)) != 0:
                raise OSError("PdhOpenQueryW failed")
            if pdh.PdhAddEnglishCounterW(
                query, _GPU_ENGINE_COUNTER, 0, ctypes.byref(counter)
            ) != 0:
                raise OSError("PdhAddEnglishCounterW failed")
            self._pdh = pdh
            self._query = query
            self._counter = counter
        except (OSError, AttributeError):
            self._ready = False

    def read(self) -> float | None:
        if not self._ready:
            return None
        pdh = self._pdh
        try:
            if pdh.PdhCollectQueryData(self._query) != 0:
                raise OSError("PdhCollectQueryData failed")
            self._samples += 1
            if self._samples < 2:
                return None
            size = ctypes.c_ulong(0)
            count = ctypes.c_ulong(0)
            status = pdh.PdhGetFormattedCounterArrayW(
                self._counter, _PDH_FMT_DOUBLE,
                ctypes.byref(size), ctypes.byref(count), None)
            if status == _PDH_MORE_DATA:
                self._buffer = (ctypes.c_char * size.value)()
                status = pdh.PdhGetFormattedCounterArrayW(
                    self._counter, _PDH_FMT_DOUBLE,
                    ctypes.byref(size), ctypes.byref(count), self._buffer)
            if status != 0:
                raise OSError("PdhGetFormattedCounterArrayW failed")
            if count.value == 0:
                return None
            items = ctypes.cast(
                self._buffer, ctypes.POINTER(_PdhCounterValueItem))
            pairs = [
                (items[index].szName or "", items[index].FmtValue.doubleValue)
                for index in range(count.value)
            ]
            return aggregate_gpu_engine_utilization(pairs)
        except (OSError, ValueError):
            # Broken counters stay broken; stop paying the sampling cost.
            self._ready = False
            return None


_VIRTUAL_ADAPTER_MARKERS = (
    "virtual", "basic display", "remote display", "indirect display",
    "idd", "oray", "todesk", "teamviewer", "parsec", "mirror", "hyper-v",
)


def classify_gpu_adapters(names: list[str]) -> bool:
    """True only when every real adapter is an integrated GPU on the CPU die.

    Remote-desktop software (Oray, RDP, ToDesk, ...) installs indirect
    display adapters that must be ignored, or they would veto the real iGPU.
    """
    if not names:
        return False
    saw_integrated = False
    for raw in names:
        name = raw.strip().lower()
        if not name or any(marker in name for marker in _VIRTUAL_ADAPTER_MARKERS):
            continue  # shadow display adapters carry no die of their own
        if "nvidia" in name:
            return False
        if "intel" in name:
            if "arc" in name:
                return False  # discrete card, separate die
            saw_integrated = True
        elif "amd" in name or "radeon" in name:
            # APUs ship as generic "Radeon(TM) Graphics"/"Radeon 780M";
            # RX/Pro/WX/Fury naming marks discrete cards with their own die.
            if re.search(r"\brx\b|\bpro\b|\bwx\b|fury", name):
                return False
            saw_integrated = True
        else:
            return False  # unknown vendor: never guess temperatures
    return saw_integrated


_integrated_gpu_cached: bool | None = None
_integrated_gpu_checked_at = 0.0


def integrated_gpu_only() -> bool:
    global _integrated_gpu_cached, _integrated_gpu_checked_at
    now = time.monotonic()
    if _integrated_gpu_cached is not None and now - _integrated_gpu_checked_at < 600.0:
        return _integrated_gpu_cached
    result = False
    if os.name == "nt":
        try:
            probe = subprocess.run(
                ["powershell", "-NoProfile", "-NonInteractive", "-Command",
                 "(Get-CimInstance Win32_VideoController).Name"],
                capture_output=True,
                text=True,
                timeout=3.0,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                check=False,
            )
            result = classify_gpu_adapters(probe.stdout.splitlines())
        except (OSError, subprocess.SubprocessError):
            result = False
    _integrated_gpu_cached = result
    _integrated_gpu_checked_at = now
    return result


_CPU_TEMP_PROBE_SECONDS = 10.0   # cache window after a successful reading
_CPU_TEMP_MISSING_SECONDS = 30.0 # slower retry while no source works


def parse_thermal_numbers(text: str) -> list[float]:
    """Extract one float per non-empty line, ignoring CIM nulls and noise."""
    numbers: list[float] = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            numbers.append(float(line))
        except ValueError:
            continue
    return numbers


def normalize_thermal_celsius(raw: float) -> float | None:
    """ACPI zones report Kelvin, usually in tenths; some drivers round."""
    temperature = raw / 10.0 - 273.15 if raw > 1000 else raw - 273.15
    if 0 < temperature < 130:
        return round(temperature, 1)
    return None


def windows_thermal_numbers() -> list[float]:
    """Read ACPI thermal zones through PowerShell CIM.

    The previous wmic fallback broke on Windows 11 24H2 where wmic.exe was
    removed; Get-CimInstance reaches the same zones on every supported
    Windows version. One spawn serves both counter sources.
    """
    if os.name != "nt":
        return []
    command = [
        "powershell", "-NoProfile", "-NonInteractive", "-Command",
        "$zones = Get-CimInstance -Namespace root/wmi "
        "-ClassName MSAcpi_ThermalZoneTemperature -ErrorAction SilentlyContinue; "
        "if ($zones) { $zones | ForEach-Object { $_.CurrentTemperature } }; "
        "$counters = Get-CimInstance -ClassName "
        "Win32_PerfFormattedData_Counters_ThermalZoneInformation "
        "-ErrorAction SilentlyContinue; "
        "if ($counters) { $counters | ForEach-Object { "
        "$_.HighPrecisionTemperature; $_.Temperature } }",
    ]
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=3.0,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return []
    return parse_thermal_numbers(result.stdout)


def cpu_temperature() -> float | None:
    global _cpu_temp_cached, _cpu_temp_checked_at
    now = time.monotonic()
    window = (_CPU_TEMP_PROBE_SECONDS if _cpu_temp_cached is not None
              else _CPU_TEMP_MISSING_SECONDS)
    if now - _cpu_temp_checked_at < window:
        return _cpu_temp_cached

    candidates: list[float] = []
    try:
        groups = psutil.sensors_temperatures(fahrenheit=False)
    except (AttributeError, OSError):
        groups = {}
    candidates.extend(
        float(entry.current)
        for entries in groups.values()
        for entry in entries
        if entry.current is not None and 0 < float(entry.current) < 130
    )

    # psutil does not expose temperatures on most Windows machines. The ACPI
    # thermal zone is the closest hardware source available without loading a
    # monitoring driver; some boards answer, others stay silent.
    if not candidates and os.name == "nt":
        for raw in windows_thermal_numbers():
            temperature = normalize_thermal_celsius(raw)
            if temperature is not None:
                candidates.append(temperature)

    _cpu_temp_cached = round(max(candidates), 1) if candidates else None
    _cpu_temp_checked_at = now
    return _cpu_temp_cached


@dataclass
class PerformanceSnapshot:
    cpu_percent: float = 0.0
    memory_percent: float = 0.0
    gpu_percent: float | None = None
    disk_percent: float = 0.0
    cpu_temp_c: float | None = None
    gpu_temp_c: float | None = None
    upload_bytes_per_sec: float = 0.0
    download_bytes_per_sec: float = 0.0


class MetricsCollector:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._snapshot = PerformanceSnapshot()
        self._mac_metrics = MacMetrics() if sys.platform == "darwin" else None
        self._gpu_util = WindowsGpuUtilization()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="metrics", daemon=True)

    def start(self) -> None:
        psutil.cpu_percent(interval=None)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)

    def snapshot(self) -> PerformanceSnapshot:
        with self._lock:
            return PerformanceSnapshot(**asdict(self._snapshot))

    def _run(self) -> None:
        psutil.cpu_percent(interval=None)
        previous_net = psutil.net_io_counters()
        previous_time = time.monotonic()
        while not self._stop.wait(1.0):
            now = time.monotonic()
            current_net = psutil.net_io_counters()
            elapsed = max(now - previous_time, 0.001)
            if self._mac_metrics is not None:
                gpu_percent, cpu_temp, gpu_temp = self._mac_metrics.read()
            else:
                gpu_percent, gpu_temp = nvidia_metrics()
                if gpu_percent is None:
                    gpu_percent = self._gpu_util.read()
                    if gpu_temp is None and gpu_percent is not None and integrated_gpu_only():
                        # An iGPU shares the CPU die, so the CPU reading is
                        # the closest temperature available without vendor SDKs.
                        gpu_temp = cpu_temperature()
                cpu_temp = cpu_temperature()
            snapshot = PerformanceSnapshot(
                cpu_percent=round(psutil.cpu_percent(interval=None), 1),
                memory_percent=round(psutil.virtual_memory().percent, 1),
                gpu_percent=gpu_percent,
                disk_percent=round(psutil.disk_usage(disk_root()).percent, 1),
                cpu_temp_c=cpu_temp,
                gpu_temp_c=gpu_temp,
                upload_bytes_per_sec=max(0.0, (current_net.bytes_sent - previous_net.bytes_sent) / elapsed),
                download_bytes_per_sec=max(0.0, (current_net.bytes_recv - previous_net.bytes_recv) / elapsed),
            )
            with self._lock:
                self._snapshot = snapshot
            previous_net = current_net
            previous_time = now


@dataclass
class AgentSnapshot:
    state: str = "offline"
    task: str = ""
    active_count: int = 0
    updated_at: int = 0
    source: str = "process"


class CodexAgentMonitor:
    """Prefer live lifecycle markers; fall back to the persisted turn history."""

    _DONE_VISIBLE_SECONDS = 120
    _ACTIVE_STALE_SECONDS = 30 * 60

    def __init__(self) -> None:
        self._runtime_lock = threading.Lock()
        self._runtime_checked_at = float("-inf")
        self._runtime_activity = None

    def _runtime_snapshot(self):
        with self._runtime_lock:
            if time.monotonic() - self._runtime_checked_at < 0.5:
                return self._runtime_activity
            processes = {}
            for process in psutil.process_iter(["name", "ppid", "create_time"]):
                try:
                    if ((process.info.get("name") or "").lower() in {"codex", "codex.exe"}
                            and process.info.get("ppid") != os.getpid()
                            and process.info.get("create_time") is not None):
                        processes[process.pid] = process.info["create_time"]
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    continue
            self._runtime_activity = read_activity(self._codex_home(), processes, time.time())
            self._runtime_checked_at = time.monotonic()
            return self._runtime_activity

    @staticmethod
    def _codex_home() -> Path:
        return Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))

    @staticmethod
    def _codex_running() -> bool:
        for process in psutil.process_iter(["name", "ppid"]):
            try:
                if ((process.info.get("name") or "").lower() in {"codex.exe", "codex"}
                        and process.info.get("ppid") != os.getpid()):
                    return True
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        return False

    def snapshot(self) -> AgentSnapshot:
        now = time.time()
        if codex_auth_available() is False:
            return AgentSnapshot(
                state="login_required", task="请登录 Codex", source="auth"
            )
        running = self._codex_running()
        if not running:
            return AgentSnapshot()

        runtime = self._runtime_snapshot()
        if runtime is not None:
            return AgentSnapshot(
                state=runtime.state,
                task="Codex" if runtime.state in {"working", "done"} else "",
                active_count=runtime.active_count,
                updated_at=runtime.updated_at,
                source="codex_lifecycle",
            )

        history_path = self._codex_home() / "thread_history_1.sqlite"
        if not history_path.exists():
            return AgentSnapshot(state="idle", source="process")

        try:
            connection = sqlite3.connect(
                history_path.resolve().as_uri() + "?mode=ro",
                uri=True,
                timeout=0.25,
            )
            turns = connection.execute(
                "SELECT thread_id, status, started_at, completed_at "
                "FROM thread_turns "
                "ORDER BY COALESCE(completed_at, started_at, 0) DESC LIMIT 64"
            ).fetchall()
            connection.close()
        except sqlite3.Error:
            return AgentSnapshot(state="idle", source="process")

        active = [
            turn for turn in turns
            if str(turn[1]).lower() in {"inprogress", "in_progress", "running"}
            and now - float(turn[2] or 0) <= self._ACTIVE_STALE_SECONDS
        ]
        if active:
            selected = max(active, key=lambda turn: float(turn[2] or 0))
            workspace = self._workspace_for_thread(str(selected[0]))
            return AgentSnapshot(
                state="working",
                task=f"Codex · {workspace}" if workspace else "Codex",
                active_count=len(active),
                updated_at=int(float(selected[2] or now)),
                source="codex_store",
            )

        completed = [turn for turn in turns if turn[3] is not None]
        if completed:
            selected = max(completed, key=lambda turn: float(turn[3] or 0))
            completed_at = float(selected[3] or 0)
            if now - completed_at <= self._DONE_VISIBLE_SECONDS:
                workspace = self._workspace_for_thread(str(selected[0]))
                return AgentSnapshot(
                    state="done",
                    task=f"Codex · {workspace}" if workspace else "Codex",
                    updated_at=int(completed_at),
                    source="codex_store",
                )
        return AgentSnapshot(state="idle", source="codex_store")

    def _workspace_for_thread(self, thread_id: str) -> str:
        state_path = self._codex_home() / "state_5.sqlite"
        if not state_path.exists():
            return ""
        try:
            connection = sqlite3.connect(
                state_path.resolve().as_uri() + "?mode=ro",
                uri=True,
                timeout=0.25,
            )
            row = connection.execute(
                "SELECT cwd FROM threads WHERE id = ?", (thread_id,)
            ).fetchone()
            connection.close()
            if row and row[0]:
                return Path(str(row[0]).removeprefix("\\\\?\\")).name[:48]
        except (sqlite3.Error, OSError):
            pass
        return ""


@dataclass
class QuotaSnapshot:
    short_remaining_percent: int | None = None
    week_remaining_percent: int | None = None
    month_remaining_percent: int | None = None
    short_resets_at: int | None = None
    week_resets_at: int | None = None
    month_resets_at: int | None = None
    updated_at: int = 0
    source: str = "unavailable"
    stale: bool = False


# 额度数据源注册表：provider_name -> 收集器类。新增套餐见 quota_providers.py 模块注释。
QUOTA_PROVIDERS: dict[str, type["QuotaCollectorBase"]] = {}


def register_quota_provider(cls: type["QuotaCollectorBase"]) -> type["QuotaCollectorBase"]:
    """注册额度数据源类；provider_name 对应 quota_providers.PROVIDER_CATALOG 的 id。"""
    QUOTA_PROVIDERS[cls.provider_name] = cls
    return cls


class QuotaCollectorBase:
    """额度数据源的共用轮询/缓存逻辑（Codex 本机登录与火山方舟 API 共用）。"""

    _POLL_SECONDS = 30
    _CACHE_SECONDS = 120
    _SUCCESS_SOURCES: frozenset[str] = frozenset()
    provider_name = ""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._snapshot = QuotaSnapshot()
        self._success_at = float('-inf')
        self._failures = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="quota-collector", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=3)

    def snapshot(self) -> QuotaSnapshot:
        with self._lock:
            result = QuotaSnapshot(**asdict(self._snapshot))
            if result.stale:
                if time.monotonic() - self._success_at >= self._CACHE_SECONDS:
                    return QuotaSnapshot(source="unavailable", updated_at=result.updated_at)
                now = time.time()
                if result.short_resets_at is not None and now >= result.short_resets_at:
                    result.short_remaining_percent = None
                if result.week_resets_at is not None and now >= result.week_resets_at:
                    result.week_remaining_percent = None
            return result

    def _accept_snapshot(self, snapshot: QuotaSnapshot) -> int:
        with self._lock:
            if snapshot.source == "unavailable":
                self._failures = min(self._failures + 1, 4)
                previous = self._snapshot
                if (time.monotonic() - self._success_at < self._CACHE_SECONDS
                        and (previous.short_remaining_percent is not None
                             or previous.week_remaining_percent is not None)):
                    self._snapshot = QuotaSnapshot(**{**asdict(previous), "stale": True})
                else:
                    self._snapshot = snapshot
                return min(5 * 2 ** (self._failures - 1), self._POLL_SECONDS)
            self._failures = 0
            self._snapshot = snapshot
            # 认证类失败与未找到数据源必须使缓存立即失效。
            self._success_at = time.monotonic() if snapshot.source in self._SUCCESS_SOURCES else float('-inf')
            return self._POLL_SECONDS

    def _run(self) -> None:
        while not self._stop.is_set():
            snapshot = self._read_snapshot()
            delay = self._accept_snapshot(snapshot)
            if self._stop.wait(delay):
                break

    def _read_snapshot(self) -> QuotaSnapshot:  # pragma: no cover - 由子类实现
        raise NotImplementedError


@register_quota_provider
class CodexQuotaCollector(QuotaCollectorBase):
    """Poll the local Codex app-server account rate-limit snapshot."""

    provider_name = "codex"
    _SUCCESS_SOURCES = frozenset({"codex_app_server"})

    @classmethod
    def from_config(cls, config: dict[str, Any]) -> "QuotaCollectorBase":
        return cls()

    def __init__(self) -> None:
        super().__init__()
        self._process_lock = threading.Lock()
        self._process = None

    def stop(self) -> None:
        self._stop.set()
        with self._process_lock:
            process = self._process
            if process is not None and process.poll() is None:
                try:
                    process.terminate()
                except ProcessLookupError:
                    pass
        if process is not None:
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)
        super().stop()

    @staticmethod
    def _codex_executable() -> str | None:
        return codex_executable()

    def _read_snapshot(self) -> QuotaSnapshot:
        if codex_auth_available() is False:
            return QuotaSnapshot(
                updated_at=int(time.time()), source="login_required"
            )
        executable = self._codex_executable()
        if executable is None:
            return QuotaSnapshot(
                updated_at=int(time.time()), source="codex_not_found"
            )
        process: subprocess.Popen[str] | None = None
        try:
            process = subprocess.Popen(
                [executable, "app-server", "--stdio"],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                encoding="utf-8",
                errors="replace",
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            with self._process_lock:
                self._process = process
                if self._stop.is_set():
                    process.terminate()
            responses: queue.Queue[str] = queue.Queue()

            def read_stdout() -> None:
                assert process is not None and process.stdout is not None
                for line in process.stdout:
                    responses.put(line)
                # Wake the response waiter if the app-server exits or is stopped.
                responses.put("")

            threading.Thread(target=read_stdout, name="codex-quota-output", daemon=True).start()

            def send(message: dict[str, Any]) -> None:
                assert process is not None and process.stdin is not None
                process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
                process.stdin.flush()

            send({
                "id": 1,
                "method": "initialize",
                "params": {
                    "clientInfo": {
                        "name": "ai-agent-panel-reporter",
                        "title": "AI Agent Panel Reporter",
                        "version": "0.1.0",
                    },
                    "capabilities": {"experimentalApi": True},
                },
            })
            self._wait_for_response(responses, 1, 10)
            send({"method": "initialized"})
            send({"id": 3, "method": "account/read", "params": {"refreshToken": False}})
            account = self._wait_for_response(responses, 3, 10).get("result", {})
            if account.get("account") is None:
                return QuotaSnapshot(updated_at=int(time.time()), source="login_required")
            send({"id": 2, "method": "account/rateLimits/read", "params": None})
            response = self._wait_for_response(responses, 2, 15)
            return self._parse_snapshot(response.get("result", {}))
        except ValueError as error:
            message = str(error).casefold()
            source = "login_required" if any(
                token in message for token in ("login", "auth", "unauthorized", "account")
            ) else "unavailable"
            logging.warning("Codex quota unavailable: %s", error)
            return QuotaSnapshot(updated_at=int(time.time()), source=source)
        except (OSError, queue.Empty, subprocess.SubprocessError) as error:
            logging.warning("Codex quota unavailable: %s", error)
            return QuotaSnapshot(
                updated_at=int(time.time()), source="unavailable"
            )
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
            with self._process_lock:
                self._process = None
            if process is not None:
                for pipe in (process.stdin, process.stdout):
                    if pipe is not None:
                        try:
                            pipe.close()
                        except OSError:
                            pass

    @staticmethod
    def _wait_for_response(responses: queue.Queue[str], request_id: int,
                           timeout: float) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise queue.Empty
            message = json.loads(responses.get(timeout=remaining))
            if message.get("id") == request_id:
                if "error" in message:
                    raise ValueError(str(message["error"]))
                return message

    @staticmethod
    def _parse_snapshot(result: dict[str, Any]) -> QuotaSnapshot:
        buckets = result.get("rateLimitsByLimitId") or {}
        limits = buckets.get("codex") if isinstance(buckets, dict) else None
        if not isinstance(limits, dict):
            limits = result.get("rateLimits") or {}

        short: tuple[int, int | None] | None = None
        week: tuple[int, int | None] | None = None
        unknown: list[tuple[int, int | None]] = []
        for key in ("primary", "secondary"):
            window = limits.get(key) if isinstance(limits, dict) else None
            if not isinstance(window, dict) or "usedPercent" not in window:
                continue
            remaining = max(0, min(100, 100 - int(window["usedPercent"])))
            resets_at = window.get("resetsAt")
            resets_at = int(resets_at) if resets_at is not None else None
            duration = window.get("windowDurationMins")
            item = (remaining, resets_at)
            if duration is None:
                unknown.append(item)
            elif int(duration) < 24 * 60:
                short = item
            else:
                week = item
        if short is None and unknown:
            short = unknown.pop(0)
        if week is None and unknown:
            week = unknown.pop(0)
        return QuotaSnapshot(
            short_remaining_percent=short[0] if short else None,
            week_remaining_percent=week[0] if week else None,
            short_resets_at=short[1] if short else None,
            week_resets_at=week[1] if week else None,
            updated_at=int(time.time()),
            source="codex_app_server",
        )


class ArkQuotaAuthError(Exception):
    """方舟接口拒绝了凭证（AK/SK 无效或签名不匹配）。"""


class OpenCodeAuthError(Exception):
    """OpenCode 接口拒绝了凭证（API Key 无效或过期）。"""


ARK_HOST = "ark.cn-beijing.volcengineapi.com"
ARK_REGION = "cn-beijing"
ARK_SERVICE = "ark"
_ARK_ACTION_QUERY = {"Action": "GetCodingPlanUsage", "Version": "2024-01-01"}


def volc_uri_encode(value: str) -> str:
    """火山引擎 V4 要求的 URI 编码：仅 A-Za-z0-9-_.~ 不转义，其余转大写十六进制。"""
    safe = frozenset(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~")
    return "".join(
        char if char in safe
        else "".join(f"%{byte:02X}" for byte in char.encode("utf-8"))
        for char in value
    )


def volc_canonical_query(query: dict[str, str]) -> str:
    """构造规范查询串（按 key 排序）。"""
    return "&".join(
        f"{volc_uri_encode(key)}={volc_uri_encode(query[key])}" for key in sorted(query))


def volc_signing_key(secret_key: bytes, short_date: str, region: str, service: str) -> bytes:
    """派生签名密钥：第一轮直接用 SK 作 HMAC 密钥（不加算法名前缀）。"""
    key = hmac.new(secret_key, short_date.encode("ascii"), hashlib.sha256).digest()
    key = hmac.new(key, region.encode("ascii"), hashlib.sha256).digest()
    key = hmac.new(key, service.encode("ascii"), hashlib.sha256).digest()
    return hmac.new(key, b"request", hashlib.sha256).digest()


def volc_authorization_header(access_key_id: str, secret_access_key: str, x_date: str,
                              query: dict[str, str], region: str = ARK_REGION,
                              service: str = ARK_SERVICE, method: str = "GET",
                              body: bytes = b"") -> str:
    """构造火山引擎 V4 签名的 Authorization 头（GET 请求仅 x-date 参与签名）。"""
    short_date = x_date[:8]
    canonical_query = volc_canonical_query(query)
    body_hash = hashlib.sha256(body).hexdigest()
    canonical_request = "\n".join(
        [method, "/", canonical_query, f"x-date:{x_date}\n", "x-date", body_hash])
    credential_scope = f"{short_date}/{region}/{service}/request"
    string_to_sign = "\n".join([
        "HMAC-SHA256", x_date, credential_scope,
        hashlib.sha256(canonical_request.encode("utf-8")).hexdigest(),
    ])
    signature = hmac.new(
        volc_signing_key(secret_access_key.encode("utf-8"), short_date, region, service),
        string_to_sign.encode("utf-8"), hashlib.sha256,
    ).hexdigest()
    return (f"HMAC-SHA256 Credential={access_key_id}/{credential_scope}, "
            f"SignedHeaders=x-date, Signature={signature}")


@register_quota_provider
class ArkQuotaCollector(QuotaCollectorBase):
    """通过火山引擎 V4 签名 HTTPS 接口查询「Coding Plan」额度用量。

    云端接口存在频控（官方未公开具体阈值；实测 ~6 QPS 突发可用，
    但长周期配额未知），故采用保守轮询：5 分钟一次（288 次/天），
    失败时指数退避（封顶同轮询间隔）；陈旧缓存覆盖两个轮询周期，
    避免单次失败就让屏幕从“上次数据”跌成“--”。
    """

    provider_name = "ark"
    _POLL_SECONDS = 300
    _CACHE_SECONDS = 600
    _SUCCESS_SOURCES = frozenset({"ark_api"})
    _AUTH_TOKENS = ("auth", "signature", "accesskey")

    def __init__(self, access_key_id: str = "", secret_access_key: str = "") -> None:
        super().__init__()
        self._access_key_id = access_key_id.strip()
        self._secret_access_key = secret_access_key.strip()

    @classmethod
    def from_config(cls, config: dict[str, Any]) -> "QuotaCollectorBase":
        return cls(
            str(config.get("ark_access_key_id") or ""),
            str(config.get("ark_secret_access_key") or ""))

    def _read_snapshot(self) -> QuotaSnapshot:
        if not self._access_key_id or not self._secret_access_key:
            return QuotaSnapshot(updated_at=int(time.time()), source="ark_not_configured")
        try:
            result = self._request_usage()
        except ArkQuotaAuthError as error:
            logging.warning("Ark quota rejected: %s", error)
            return QuotaSnapshot(updated_at=int(time.time()), source="ark_auth_failed")
        except (OSError, ValueError) as error:
            logging.warning("Ark quota unavailable: %s", error)
            return QuotaSnapshot(updated_at=int(time.time()), source="unavailable")
        return self._parse_snapshot(result)

    def _request_usage(self) -> dict[str, Any]:
        x_date = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
        authorization = volc_authorization_header(
            self._access_key_id, self._secret_access_key, x_date, _ARK_ACTION_QUERY)
        request = urllib.request.Request(
            f"https://{ARK_HOST}/?{volc_canonical_query(_ARK_ACTION_QUERY)}",
            headers={
                "x-date": x_date,
                "Authorization": authorization,
                "User-Agent": "SynaReporter/1.0",
            },
            method="GET",
        )
        try:
            with urllib.request.urlopen(request, timeout=15.0) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as error:
            code = self._error_code(error.read().decode("utf-8", "replace"))
            if any(token in code.casefold() for token in self._AUTH_TOKENS):
                raise ArkQuotaAuthError(f"HTTP {error.code} {code}") from error
            # 429（限流）与其它 4xx/5xx 一致走 unavailable，由基类指数退避减速。
            raise OSError(f"HTTP {error.code}: {code or error.reason}") from error
        if not isinstance(payload, dict):
            raise ValueError("Ark response is not an object")
        error = (payload.get("ResponseMetadata") or {}).get("Error")
        if isinstance(error, dict):
            code = str(error.get("Code") or "")
            if any(token in code.casefold() for token in self._AUTH_TOKENS):
                raise ArkQuotaAuthError(f"{code}: {error.get('Message') or code}")
            raise ValueError(f"Ark API error {code}: {error.get('Message') or code}")
        result = payload.get("Result")
        if not isinstance(result, dict):
            raise ValueError("Ark response missing Result")
        return result

    @staticmethod
    def _error_code(body: str) -> str:
        try:
            payload = json.loads(body)
        except ValueError:
            return ""
        error = (payload.get("ResponseMetadata") or {}).get("Error")
        return str(error.get("Code") or "") if isinstance(error, dict) else ""

    @staticmethod
    def _parse_snapshot(result: dict[str, Any]) -> QuotaSnapshot:
        """QuotaUsage 的 Level 映射：session→短周期，weekly→周额度（monthly 忽略）。"""
        items = result.get("QuotaUsage")
        usage: dict[str, tuple[int, int | None]] = {}
        for item in items if isinstance(items, list) else []:
            if not isinstance(item, dict) or "Percent" not in item:
                continue
            try:
                remaining = max(0, min(100, round(100 - float(item["Percent"]))))
            except (TypeError, ValueError):
                continue
            reset = item.get("ResetTimestamp")
            usage[str(item.get("Level") or "").casefold()] = (
                remaining, int(reset) if reset is not None else None)
        short = usage.get("session")
        if short is None:
            short = next((value for level, value in usage.items()
                          if level not in {"weekly", "monthly"}), None)
        week = usage.get("weekly")
        month = usage.get("monthly")
        return QuotaSnapshot(
            short_remaining_percent=short[0] if short else None,
            week_remaining_percent=week[0] if week else None,
            month_remaining_percent=month[0] if month else None,
            short_resets_at=short[1] if short else None,
            week_resets_at=week[1] if week else None,
            month_resets_at=month[1] if month else None,
            updated_at=int(time.time()),
            source="ark_api",
        )


@register_quota_provider
class OpenCodeQuotaCollector(QuotaCollectorBase):
    """通过 OpenCode Zen Go 的只读接口查询套餐额度用量。

    云端接口频控阈值未公开，与方舟源同采用保守轮询（5 分钟）与
    指数退避；resetsAt 为 ISO 8601 字符串，换算为 Unix 秒后下发。
    """

    provider_name = "opencode"
    _POLL_SECONDS = 300
    _CACHE_SECONDS = 600
    _SUCCESS_SOURCES = frozenset({"opencode_api"})
    _ENDPOINT = "https://opencode.ai/zen/go/v1/usage"

    def __init__(self, api_key: str = "") -> None:
        super().__init__()
        self._api_key = api_key.strip()

    @classmethod
    def from_config(cls, config: dict[str, Any]) -> "QuotaCollectorBase":
        return cls(str(config.get("opencode_api_key") or ""))

    def _read_snapshot(self) -> QuotaSnapshot:
        if not self._api_key:
            return QuotaSnapshot(updated_at=int(time.time()),
                                 source="opencode_not_configured")
        try:
            usage = self._request_usage()
        except OpenCodeAuthError as error:
            logging.warning("OpenCode quota rejected: %s", error)
            return QuotaSnapshot(updated_at=int(time.time()),
                                 source="opencode_auth_failed")
        except (OSError, ValueError) as error:
            logging.warning("OpenCode quota unavailable: %s", error)
            return QuotaSnapshot(updated_at=int(time.time()), source="unavailable")
        return self._parse_snapshot(usage)

    def _request_usage(self) -> dict[str, Any]:
        request = urllib.request.Request(
            self._ENDPOINT,
            headers={
                "Authorization": f"Bearer {self._api_key}",
                "Accept": "application/json",
                "User-Agent": "SynaReporter/1.0",
            },
            method="GET")
        try:
            with urllib.request.urlopen(request, timeout=15.0) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as error:
            if error.code in (401, 403):
                raise OpenCodeAuthError(f"HTTP {error.code}") from error
            raise OSError(f"HTTP {error.code}: {error.reason}") from error
        usage = payload.get("usage") if isinstance(payload, dict) else None
        if not isinstance(usage, dict):
            raise ValueError("OpenCode response missing usage")
        return usage

    @staticmethod
    def _parse_iso_utc(value: Any) -> int | None:
        """ISO 8601（如 2026-10-12T00:00:00.000Z）→ Unix 秒；非法返回 None。"""
        if not isinstance(value, str) or not value:
            return None
        try:
            moment = datetime.fromisoformat(value.replace("Z", "+00:00"))
        except ValueError:
            return None
        if moment.tzinfo is None:
            moment = moment.replace(tzinfo=timezone.utc)
        return int(moment.timestamp())

    @staticmethod
    def _parse_snapshot(usage: dict[str, Any]) -> QuotaSnapshot:
        """rolling→短周期、weekly→周额度、monthly→月额度；percent 为已用。"""
        windows: dict[str, tuple[int, int | None]] = {}
        for level in ("rolling", "weekly", "monthly"):
            item = usage.get(level)
            if not isinstance(item, dict) or "percent" not in item:
                continue
            try:
                remaining = max(0, min(100, round(100 - float(item["percent"]))))
            except (TypeError, ValueError):
                continue
            windows[level] = (remaining, OpenCodeQuotaCollector._parse_iso_utc(item.get("resetsAt")))
        short = windows.get("rolling")
        week = windows.get("weekly")
        month = windows.get("monthly")
        return QuotaSnapshot(
            short_remaining_percent=short[0] if short else None,
            week_remaining_percent=week[0] if week else None,
            month_remaining_percent=month[0] if month else None,
            short_resets_at=short[1] if short else None,
            week_resets_at=week[1] if week else None,
            month_resets_at=month[1] if month else None,
            updated_at=int(time.time()),
            source="opencode_api",
        )


def make_quota_collector(config: dict[str, Any] | None = None) -> QuotaCollectorBase:
    """按 reporter.json 的 quota_provider 构建额度数据源；未知值回退默认并告警。"""
    cfg = read_config() if config is None else config
    provider = str(cfg.get("quota_provider") or DEFAULT_PROVIDER_ID).casefold()
    factory = QUOTA_PROVIDERS.get(provider)
    if factory is None:
        logging.warning("未知的额度数据源 %r，回退到 %s", provider, DEFAULT_PROVIDER_ID)
        factory = QUOTA_PROVIDERS[DEFAULT_PROVIDER_ID]
    return factory.from_config(cfg)


class ReporterState:
    def __init__(self, identity: dict[str, str], collector: MetricsCollector,
                 agent_monitor: CodexAgentMonitor,
                 quota_collector: QuotaCollectorBase,
                 media_monitor: NeteaseMediaMonitor, http_port: int = HTTP_PORT) -> None:
        self.http_port = http_port
        self.reporter_id = identity["reporter_id"]
        self.computer_name = identity["computer_name"]
        self.pairing_token = identity["pairing_token"]
        self.pairing_hash = hashlib.sha256(self.pairing_token.encode("utf-8")).hexdigest()
        self.collector = collector
        self.agent_monitor = agent_monitor
        self.quota_collector = quota_collector
        self.media_monitor = media_monitor
        self.started_at = int(time.time())
        self._devices_lock = threading.Lock()
        self._devices = {}

    def record_device(self, peer_ip: str, board_id: str = "") -> None:
        if peer_ip.startswith("127."):
            return
        now = time.monotonic()
        with self._devices_lock:
            self._devices = {ip: entry for ip, entry in self._devices.items()
                             if now - entry["seen"] < 15}
            self._devices[peer_ip] = {"seen": now, "board_id": board_id[:32]}
            if len(self._devices) > 32:
                oldest = min(self._devices, key=lambda ip: self._devices[ip]["seen"])
                del self._devices[oldest]

    def recent_devices(self) -> list[dict[str, Any]]:
        now = time.monotonic()
        with self._devices_lock:
            return [{"ip": ip, "last_seen_seconds": round(now - entry["seen"], 1)}
                    for ip, entry in self._devices.items() if now - entry["seen"] < 15]

    def _agent_snapshot(self) -> AgentSnapshot:
        if self.quota_collector.snapshot().source == "login_required":
            return AgentSnapshot(state="login_required", task="请登录 Codex", source="auth")
        return self.agent_monitor.snapshot()

    def status(self) -> dict[str, Any]:
        return {
            "version": PROTOCOL_VERSION,
            "reporter_id": self.reporter_id,
            "pairing_hash": self.pairing_hash,
            "computer_name": self.computer_name,
            "online": True,
            "timestamp": int(time.time()),
            "service": {"pid": os.getpid(), "started_at": self.started_at},
            "devices": self.recent_devices(),
            "performance": asdict(self.collector.snapshot()),
            "agent": asdict(self._agent_snapshot()),
            "codex_quota": asdict(self.quota_collector.snapshot()),
            "quota_provider": self.quota_collector.provider_name,
            "media": asdict(self.media_monitor.snapshot()),
        }

    def discovery(self, peer_ip: str) -> dict[str, Any]:
        agent = self._agent_snapshot()
        quota = self.quota_collector.snapshot()
        media = self.media_monitor.snapshot()
        return {
            "type": "AI_PANEL_REPORTER_V1",
            "version": PROTOCOL_VERSION,
            "reporter_id": self.reporter_id,
            "pairing_hash": self.pairing_hash,
            "computer_name": self.computer_name,
            "agent_state": agent.state,
            "agent_task": agent.task,
            "agent_count": agent.active_count,
            "codex_login_required": agent.state == "login_required" or
                                      quota.source == "login_required",
            "codex_short_remaining": quota.short_remaining_percent
            if quota.short_remaining_percent is not None else -1,
            "codex_week_remaining": quota.week_remaining_percent
            if quota.week_remaining_percent is not None else -1,
            "codex_short_resets_at": quota.short_resets_at
            if quota.short_resets_at is not None else -1,
            "codex_week_resets_at": quota.week_resets_at
            if quota.week_resets_at is not None else -1,
            "codex_month_remaining": quota.month_remaining_percent
            if quota.month_remaining_percent is not None else -1,
            "codex_month_resets_at": quota.month_resets_at
            if quota.month_resets_at is not None else -1,
            "quota_provider": self.quota_collector.provider_name,
            "quota_provider_title": str(
                (provider_spec(self.quota_collector.provider_name) or {}).get("board")
                or self.quota_collector.provider_name),
            "codex_quota_stale": quota.stale,
            "media_available": media.available,
            "media_source": media.source,
            "media_title": media.title[:48],
            "media_artist": media.artist[:32],
            "media_status": media.playback_status,
            "media_position": media.position_seconds,
            "media_duration": media.duration_seconds,
            "media_lyric": media.lyric[:64],
            "performance": asdict(self.collector.snapshot()),
            "ip": local_ip_for(peer_ip),
            "http_port": self.http_port,
        }


def make_handler(state: ReporterState):
    class Handler(BaseHTTPRequestHandler):
        server_version = "AIPanelReporter/0.1"

        def do_GET(self) -> None:  # noqa: N802
            if os.environ.get("AI_PANEL_REPORTER_LOG_REQUESTS") == "1":
                print(f"HTTP {self.client_address[0]} {self.path}", flush=True)
            if self.path in ("/", "/api/v1/health"):
                self._send_json({"ok": True, "service": "ai-panel-reporter", "version": PROTOCOL_VERSION})
            elif self.path == "/api/v1/status":
                self._send_json(state.status())
            elif self.path == "/api/v1/pairing" and self.client_address[0] in {"127.0.0.1", "::1"}:
                self._send_json({"pairing_token": state.pairing_token})
            else:
                self._send_json({"error": "not_found"}, status=404)

        def _send_json(self, payload: dict[str, Any], status: int = 200) -> None:
            body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format: str, *args: Any) -> None:
            return

    return Handler


class DiscoveryServer:
    def __init__(self, state: ReporterState) -> None:
        self._state = state
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run_guarded, name="discovery", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)

    def _run_guarded(self) -> None:
        try:
            self._run()
        except Exception:
            logging.exception("Discovery server stopped unexpectedly")
            if not self._stop.is_set():
                # A silent dead discovery thread leaves HTTP looking healthy
                # while every panel is offline. Exit the worker so the
                # supervisor can rebuild the complete service.
                os._exit(70)

    def _run(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.bind(("0.0.0.0", DISCOVERY_PORT))
        sock.settimeout(0.5)
        try:
            while not self._stop.is_set():
                try:
                    payload, address = sock.recvfrom(1024)
                except socket.timeout:
                    continue
                request = payload.strip()
                board_id = ""
                if request != DISCOVERY_REQUEST:
                    try:
                        decoded = json.loads(request.decode("utf-8"))
                    except (UnicodeDecodeError, ValueError):
                        continue
                    if not isinstance(decoded, dict) or decoded.get("type") != DISCOVERY_REQUEST.decode("ascii"):
                        continue
                    board_id = str(decoded.get("board_id") or "")
                self._state.record_device(address[0], board_id)
                response = json.dumps(
                    self._state.discovery(address[0]),
                    ensure_ascii=False,
                    separators=(",", ":"),
                ).encode("utf-8")
                sock.sendto(response, address)
        finally:
            sock.close()


class ReporterHTTPServer(ThreadingHTTPServer):
    def server_bind(self) -> None:
        # HTTPServer resolves getfqdn() here. On macOS, a missing local DNS/mDNS
        # record can block startup before any collector or discovery thread runs.
        socketserver.TCPServer.server_bind(self)
        self.server_name = self.server_address[0]
        self.server_port = self.server_address[1]


def run_worker(port: int) -> int:
    identity = load_identity()
    collector = MetricsCollector()
    agent_monitor = CodexAgentMonitor()
    quota_collector = make_quota_collector()
    media_monitor = NeteaseMediaMonitor()
    state = ReporterState(identity, collector, agent_monitor, quota_collector, media_monitor, port)
    discovery = DiscoveryServer(state)
    http_server = ReporterHTTPServer(("0.0.0.0", port), make_handler(state))
    http_server.daemon_threads = True

    collector.start()
    quota_collector.start()
    media_monitor.start()
    discovery.start()
    print(f"Reporter ID: {state.reporter_id}", flush=True)
    print(f"Computer: {state.computer_name}", flush=True)
    print(f"HTTP: http://127.0.0.1:{port}/api/v1/status", flush=True)
    print(f"Discovery UDP: {DISCOVERY_PORT}", flush=True)
    try:
        http_server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        http_server.shutdown()
        http_server.server_close()
        discovery.stop()
        quota_collector.stop()
        media_monitor.stop()
        collector.stop()
    return 0


def worker_command(port: int) -> list[str]:
    if getattr(sys, "frozen", False):
        return [sys.executable, "--worker", "--port", str(port)]
    return [sys.executable, str(Path(__file__).resolve()), "--worker", "--port", str(port)]


def supervise(port: int) -> int:
    """Restart the serving worker after crashes with a bounded backoff."""
    backoff = 1.0
    while True:
        started = time.monotonic()
        logging.info("Starting Reporter worker")
        try:
            process = subprocess.Popen(
                worker_command(port),
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            exit_code = process.wait()
        except KeyboardInterrupt:
            if "process" in locals() and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            return 0
        except OSError as error:
            exit_code = -1
            logging.exception("Could not start Reporter worker: %s", error)
        runtime = time.monotonic() - started
        logging.warning("Reporter worker exited with %s after %.1fs", exit_code, runtime)
        backoff = 1.0 if runtime >= 30 else min(backoff * 2, 30.0)
        try:
            time.sleep(backoff)
        except KeyboardInterrupt:
            return 0


def acquire_single_instance(role: str):
    if os.name != "nt":
        return acquire_posix_instance(role)
    handle = ctypes.windll.kernel32.CreateMutexW(None, False, f"Local\\SynaReporter-{role}")
    if not handle or ctypes.windll.kernel32.GetLastError() == 183:
        if handle:
            ctypes.windll.kernel32.CloseHandle(handle)
        return None
    return handle


def self_test() -> int:
    identity = load_identity()
    assert identity["reporter_id"] == load_identity()["reporter_id"]
    assert len(hashlib.sha256(identity["pairing_token"].encode()).hexdigest()) == 64
    print(json.dumps({
        "ok": True,
        "reporter_id": identity["reporter_id"],
        "codex_logged_in": codex_auth_available(),
    }, ensure_ascii=False))
    return 0


def main() -> int:
    if os.name == "nt" and len(sys.argv) == 1:
        from windows_ui import run
        return run()
    if os.name != "nt":
        def terminate(signum, frame):
            raise KeyboardInterrupt
        signal.signal(signal.SIGTERM, terminate)
    parser = argparse.ArgumentParser(description="Syna 状态屏 Reporter（Windows / macOS）")
    parser.add_argument("--port", type=int, default=HTTP_PORT)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--no-supervisor", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    configure_logging()
    if args.self_test:
        return self_test()
    role = "worker" if args.worker or args.no_supervisor else "supervisor"
    instance = acquire_single_instance(role)
    if instance is None:
        logging.info("A %s instance is already running", role)
        return 0
    if args.worker or args.no_supervisor:
        return run_worker(args.port)
    return supervise(args.port)


if __name__ == "__main__":
    raise SystemExit(main())
