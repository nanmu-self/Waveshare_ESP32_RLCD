#include "lunar.h"

#include <cstdint>

namespace syna {

namespace {

// lunarInfo[year - 1900]：每年 12/13 个月的大小月与闰月位（1900-2100）。
constexpr uint32_t kLunarInfo[201] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0,

    0x09ad0, 0x055d2, 0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540,

    0x0d6a0, 0x0ada2, 0x095b0, 0x14977, 0x04970, 0x0a4b0, 0x0b4b5, 0x06a50,

    0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970, 0x06566, 0x0d4a0,

    0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,

    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2,

    0x0a950, 0x0b557, 0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573,

    0x052b0, 0x0a9a8, 0x0e950, 0x06aa0, 0x0aea6, 0x0ab50, 0x04b60, 0x0aae4,

    0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0, 0x096d0, 0x04dd5,

    0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6,

    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46,

    0x0ab60, 0x09570, 0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58,

    0x05ac0, 0x0ab60, 0x096d5, 0x092e0, 0x0c960, 0x0d954, 0x0d4a0, 0x0da50,

    0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5, 0x0a950, 0x0b4a0,

    0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,

    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260,

    0x0ea65, 0x0d530, 0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0,

    0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, 0x0b5a0, 0x056d0, 0x055b2, 0x049b0,

    0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, 0x14b63, 0x09370,

    0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,

    0x092e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0,

    0x0a6d0, 0x055d4, 0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50,

    0x055a0, 0x0aba4, 0x0a5b0, 0x052b0, 0x0b273, 0x06930, 0x07337, 0x06aa0,

    0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160, 0x0e968, 0x0d520,

    0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a4d0, 0x0d150, 0x0f252,

    0x0d520,
};

int LeapMonth(int year) { return static_cast<int>(kLunarInfo[year - 1900] & 0xF); }

int LeapDays(int year) {
    if (LeapMonth(year) == 0) return 0;
    return (kLunarInfo[year - 1900] & 0x10000) != 0 ? 30 : 29;
}

int MonthDays(int year, int month) {
    return (kLunarInfo[year - 1900] & (0x10000 >> month)) != 0 ? 30 : 29;
}

int YearDays(int year) {
    int sum = 348;
    for (uint32_t mask = 0x8000; mask > 0x8; mask >>= 1) {
        sum += (kLunarInfo[year - 1900] & mask) != 0 ? 1 : 0;
    }
    return sum + LeapDays(year);
}

// 儒略日数（无时区依赖），用于计算与 1900-01-31 的天数差。
int JulianDay(int year, int month, int day) {
    const int a = (14 - month) / 12;
    const int y = year + 4800 - a;
    const int m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400;
}

}  // namespace

bool LunarFromSolar(int solar_year, int solar_month, int solar_day,
                    int* lunar_month, int* lunar_day, bool* is_leap) {
    if (solar_year < 1901 || solar_year > 2100 || lunar_month == nullptr ||
        lunar_day == nullptr || is_leap == nullptr) {
        return false;
    }
    int offset = JulianDay(solar_year, solar_month, solar_day) -
                 JulianDay(1900, 1, 31);
    int year = 1900;
    while (year < 2101 && offset > 0) {
        const int days = YearDays(year);
        offset -= days;
        if (offset < 0) {
            offset += days;
            break;
        }
        ++year;
    }
    if (year > 2100) return false;

    const int leap = LeapMonth(year);
    bool leap_month = false;
    int month = 1;
    while (month < 13 && offset > 0) {
        int days;
        if (leap > 0 && month == leap + 1 && !leap_month) {
            --month;
            leap_month = true;
            days = LeapDays(year);
        } else {
            days = MonthDays(year, month);
        }
        if (leap_month && month == leap + 1) leap_month = false;
        offset -= days;
        if (offset < 0) {
            offset += days;
            break;
        }
        ++month;
    }
    *lunar_month = month;
    *lunar_day = offset + 1;
    *is_leap = leap_month && month == leap;
    return *lunar_day >= 1 && *lunar_day <= 30;
}

void LunarDayName(int lunar_day, char* buf, int buf_size) {
    static const char* const kNames[] = {
        "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八",
        "初九", "初十", "十一", "十二", "十三", "十四", "十五", "十六",
        "十七", "十八", "十九", "二十", "廿一", "廿二", "廿三", "廿四",
        "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
    };
    if (buf == nullptr || buf_size <= 0) return;
    if (lunar_day < 1 || lunar_day > 30) {
        buf[0] = '\0';
        return;
    }
    int position = 0;
    const char* name = kNames[lunar_day - 1];
    while (name[position] != '\0' && position < buf_size - 1) {
        buf[position] = name[position];
        ++position;
    }
    buf[position] = '\0';
}

}  // namespace syna

extern "C" int syna_lunar_day(int solar_year, int solar_month, int solar_day,
                              char* buf, int buf_size) {
    int lunar_month = 0;
    int lunar_day = 0;
    bool is_leap = false;
    if (buf == nullptr || buf_size <= 0 ||
        !syna::LunarFromSolar(solar_year, solar_month, solar_day, &lunar_month,
                              &lunar_day, &is_leap)) {
        if (buf != nullptr && buf_size > 0) buf[0] = '\0';
        return 0;
    }
    syna::LunarDayName(lunar_day, buf, buf_size);
    return 1;
}
