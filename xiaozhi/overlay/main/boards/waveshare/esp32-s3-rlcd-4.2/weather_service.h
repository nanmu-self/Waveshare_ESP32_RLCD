#pragma once

// 和风天气服务：板子直连和风 HTTPS API 拉取实况与 3 天预报。
// 位置支持自动 IP 定位（缓存 24 小时）或设置门户手动填 LocationID。
// Key 等配置保存在 NVS（wifi-config 命名空间），由设置门户写入。

#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

constexpr int kWeatherDailyCount = 3;

struct PanelWeatherDay {
    char date[16] = {};        // "周三 10/09"
    char text_day[24] = {};    // 白天天气现象
    char icon_day[8] = {};     // 白天天气图标代码
    char precip[8] = "--";     // 预报降水量 mm（daily 接口无降水概率）
    char lunar_day[16] = {};   // 农历日（初一..三十）
    char temp_max[8] = "--";
    char temp_min[8] = "--";
};

struct PanelWeatherSnapshot {
    uint32_t generation = 0;
    bool available = false;    // 天气数据有效
    bool configured = false;   // 已配置和风 Key
    bool location_ok = false;  // 位置已解析（定位或手动）
    char status[48] = {};      // 面向用户的简短状态（空 = 正常）
    char city[24] = "--";
    char temp[8] = "--";
    char text[24] = "--";
    char icon[8] = {};         // 和风天气图标代码（100 晴、101 多云…）
    char feels_like[8] = "--";
    char humidity[8] = "--";
    char wind[44] = "--";
    char temp_max[8] = "--";
    char temp_min[8] = "--";
    char aqi[8] = "--";        // 空气质量指数数值
    char aqi_category[16] = {};  // 空气质量等级文字（优/良/轻度污染…）
    char uv_level[16] = "--";  // 紫外线等级文字（弱/中等/高/很高/极高）
    int16_t wind360 = -1;      // 风向角度（0-359，-1 未知）；箭头指向下风向
    char update_time[8] = "--";  // "HH:MM"
    PanelWeatherDay daily[kWeatherDailyCount];
};

class WeatherService {
public:
    static WeatherService& GetInstance();

    bool Start();
    bool GetSnapshot(PanelWeatherSnapshot& snapshot);
    void RequestRefresh();

private:
    struct Config {
        std::string api_key;
        bool manual = false;
        std::string manual_location;
    };

    struct Location {
        std::string id;    // 和风 LocationID
        std::string name;  // 城市显示名
    };

    WeatherService() = default;
    WeatherService(const WeatherService&) = delete;
    WeatherService& operator=(const WeatherService&) = delete;

    static void TaskEntry(void* context);
    void Run();
    Config LoadConfig() const;
    bool ResolveLocation(const Config& config, Location& location);
    bool LoadCachedLocation(Location& location) const;
    void SaveCachedLocation(const Location& location) const;
    bool LocateByIp(std::string& query);
    bool GeoLookup(const Config& config, const std::string& query,
                   Location& location);
    bool FetchWeather(const Config& config, const Location& location,
                      PanelWeatherSnapshot& result);
    bool FetchAirQuality(const Config& config, const Location& location,
                         PanelWeatherSnapshot& result);
    bool FetchAndParse(const Config& config, const std::string& url,
                       std::string& response);
    bool PerformRequest(const std::string& url, const std::string& api_key,
                        std::string& response);
    void Publish(PanelWeatherSnapshot result);
    void SetStatus(PanelWeatherSnapshot& result, const char* status);

    static bool InflateGzip(const std::string& compressed, std::string& plain);
    static bool WifiReady();

    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    PanelWeatherSnapshot snapshot_ = {};
};
