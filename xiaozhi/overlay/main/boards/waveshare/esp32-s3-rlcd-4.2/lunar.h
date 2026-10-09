#pragma once

// 公历 → 农历日转换（查表法，覆盖 1900-2100）。
// 农历数据表取自 solarlunar（MIT，Copyright sintune & jjonline），
// 算法为社区通用实现；C++ 移植与封装 Copyright (c) 2026 黑沐. MIT。

namespace syna {

// 返回 true 时 *lunar_month（1-12）、*lunar_day（1-30）有效；is_leap 为闰月标记。
bool LunarFromSolar(int solar_year, int solar_month, int solar_day,
                    int* lunar_month, int* lunar_day, bool* is_leap);

// 农历日中文名（初一..三十），写入 buf（含结束符）。
void LunarDayName(int lunar_day, char* buf, int buf_size);

}  // namespace syna
