# Syna project-specific code and modifications: Copyright (c) 2026 黑沐.
# SPDX-License-Identifier: MIT; third-party notices remain applicable.
"""额度数据源目录：Reporter worker 与状态窗口 UI 共用的纯数据描述。

协议约定：开发板只认 codex_short_remaining / codex_week_remaining 等字段名，
不关心数据源，因此新增套餐只需两步、两侧协议零改动：
  1. 在 reporter.py 实现一个 QuotaCollectorBase 子类，挂 @register_quota_provider，
     provider_name 与本目录条目的 id 一致，并实现 from_config 类方法；
  2. 在 PROVIDER_CATALOG 追加一条描述（展示名、说明、需要用户填写的字段）。

字段约定：id 用于配置与 UDP 协议；display/title 用于状态窗口；
board 用于开发板屏幕上的套餐来源展示。
"""

from __future__ import annotations

DEFAULT_PROVIDER_ID = "codex"

PROVIDER_CATALOG: tuple[dict, ...] = (
    {
        "id": "codex",
        "display": "Codex（本机 Codex 登录）",
        "title": "CODEX",
        "board": "Codex",
        "hint": "读取本机已登录 Codex 的额度，无需额外配置。",
        "fields": (),
    },
    {
        "id": "ark",
        "display": "火山方舟 Coding Plan",
        "title": "方舟",
        "board": "火山方舟",
        "hint": "密钥获取：火山引擎控制台 → 访问控制 IAM → API 访问密钥。\n"
                "AK/SK 只保存在本机 reporter.json，仅用于只读查询额度用量。",
        "fields": (
            {"key": "ark_access_key_id", "label": "方舟 AccessKeyId",
             "placeholder": "AKLT…", "secret": False},
            {"key": "ark_secret_access_key", "label": "方舟 SecretAccessKey",
             "placeholder": "SK", "secret": True},
        ),
    },
    {
        "id": "opencode",
        "display": "OpenCode Go 套餐",
        "title": "OPENCODE",
        "board": "OpenCode",
        "hint": "API Key 获取：opencode.ai → Zen Go 控制台。\n"
                "Key 只保存在本机 reporter.json，仅用于只读查询额度用量。",
        "fields": (
            {"key": "opencode_api_key", "label": "OpenCode API Key",
             "placeholder": "Bearer Key", "secret": True},
        ),
    },
)


def provider_spec(provider_id: str) -> dict | None:
    """按 id 查找目录条目；未知 id（如旧配置残留）返回 None。"""
    for spec in PROVIDER_CATALOG:
        if spec["id"] == provider_id:
            return spec
    return None
