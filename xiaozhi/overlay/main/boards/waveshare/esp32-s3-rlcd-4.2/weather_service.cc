#include "weather_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <nvs.h>

#include "lunar.h"
#include "puff.h"
#include "settings.h"

namespace {

constexpr char kTag[] = "Weather";
constexpr size_t kMaximumResponseBytes = 12 * 1024;
constexpr uint32_t kRefreshMs = 30U * 60U * 1000U;   // 免费额度 1000 次/天，30 分钟足够
constexpr uint32_t kRetryMs = 60U * 1000U;
constexpr uint32_t kMissingKeyMs = 5U * 60U * 1000U; // 未配置 Key 时低频等待门户写入
constexpr int64_t kLocationCacheMs = 24LL * 60LL * 60LL * 1000LL;
constexpr char kApiHost[] = "https://devapi.qweather.com";
constexpr char kGeoHost[] = "https://geoapi.qweather.com";

// GB2312 汉字顺序表可以不要——星期用蔡勒式算法直接从日期算，不依赖本地化。
int WeekdayFromYmd(int year, int month, int day) {
    static const int kOffset[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) --year;
    const int weekday = year + year / 4 - year / 100 + year / 400 +
                        kOffset[month - 1] + day;
    return weekday % 7;  // 0 = 周日
}

const char* WeekdayName(int weekday) {
    static const char* kNames[] = {"周日", "周一", "周二", "周三",
                                   "周四", "周五", "周六"};
    return kNames[weekday % 7];
}

std::string Trim(std::string value) {
    auto visible = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), visible));
    value.erase(std::find_if(value.rbegin(), value.rend(), visible).base(), value.end());
    return value;
}

void Copy(char* destination, size_t size, const std::string& source) {
    if (size == 0) return;
    snprintf(destination, size, "%s", source.c_str());
}

std::string UrlEncode(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (unsigned char character : value) {
        if (std::isalnum(character) || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[character >> 4]);
            encoded.push_back(kHex[character & 0x0F]);
        }
    }
    return encoded;
}

bool ReadString(const char* name_space, const char* key, std::string& value) {
    nvs_handle_t handle = 0;
    if (nvs_open(name_space, NVS_READONLY, &handle) != ESP_OK) return false;
    char buffer[64];
    size_t length = sizeof(buffer);
    const esp_err_t error = nvs_get_str(handle, key, buffer, &length);
    nvs_close(handle);
    if (error != ESP_OK) return false;
    value.assign(buffer);
    return true;
}

void WriteString(const char* name_space, const char* key, const std::string& value) {
    nvs_handle_t handle = 0;
    if (nvs_open(name_space, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_str(handle, key, value.c_str()) == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
}

bool ReadInt64(const char* name_space, const char* key, int64_t& value) {
    nvs_handle_t handle = 0;
    if (nvs_open(name_space, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t length = sizeof(value);
    const esp_err_t error = nvs_get_blob(handle, key, &value, &length);
    nvs_close(handle);
    return error == ESP_OK && length == sizeof(value);
}

void WriteInt64(const char* name_space, const char* key, int64_t value) {
    nvs_handle_t handle = 0;
    if (nvs_open(name_space, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_blob(handle, key, &value, sizeof(value)) == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
}

// 紫外线指数 → 等级文字（QWeather UVI 分级惯例）。
std::string UvLevelText(int uv_index) {
    if (uv_index <= 2) return "弱";
    if (uv_index <= 5) return "中等";
    if (uv_index <= 7) return "高";
    if (uv_index <= 10) return "很高";
    return "极高";
}

// 提取 "2026-10-08T14:32+08:00" 中的 "14:32"。
std::string IsoTime(const std::string& iso) {
    const size_t hour = iso.find('T');
    if (hour == std::string::npos || hour + 6 > iso.size()) return {};
    if (!std::isdigit(static_cast<unsigned char>(iso[hour + 1]))) return {};
    return iso.substr(hour + 1, 5);
}

// 提取 "2026-10-09" 为 "10/09" 和星期名。
std::string FormatForecastDate(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return date;
    const int year = atoi(date.substr(0, 4).c_str());
    const int month = atoi(date.substr(5, 2).c_str());
    const int day = atoi(date.substr(8, 2).c_str());
    if (year <= 0 || month < 1 || month > 12 || day < 1 || day > 31) return date;
    char formatted[16];
    snprintf(formatted, sizeof(formatted), "%s %02d/%02d",
             WeekdayName(WeekdayFromYmd(year, month, day)), month, day);
    return formatted;
}

}  // namespace

WeatherService& WeatherService::GetInstance() {
    static WeatherService instance;
    return instance;
}

bool WeatherService::Start() {
    if (task_ != nullptr) return true;
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) return false;
    snapshot_ = PanelWeatherSnapshot{};
    return xTaskCreatePinnedToCore(TaskEntry, "weather", 14336, this, 1,
                                   &task_, 0) == pdPASS;
}

void WeatherService::TaskEntry(void* context) {
    static_cast<WeatherService*>(context)->Run();
}

void WeatherService::Run() {
    while (true) {
        if (!WifiReady()) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }
        const Config config = LoadConfig();
        PanelWeatherSnapshot result;
        if (config.api_key.empty()) {
            result = PanelWeatherSnapshot{};
            SetStatus(result, "未配置和风天气 Key");
            Publish(result);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kMissingKeyMs));
            continue;
        }
        result.configured = true;

        Location location;
        result.location_ok = ResolveLocation(config, location);
        if (!result.location_ok) {
            SetStatus(result, "定位失败，稍后重试");
            Publish(result);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kRetryMs));
            continue;
        }
        result.available = FetchWeather(config, location, result);
        if (result.available) FetchAirQuality(config, location, result);
        Publish(result);
        ulTaskNotifyTake(pdTRUE,
                         pdMS_TO_TICKS(result.available ? kRefreshMs : kRetryMs));
    }
}

void WeatherService::RequestRefresh() {
    if (task_ != nullptr) xTaskNotifyGive(task_);
}

bool WeatherService::GetSnapshot(PanelWeatherSnapshot& snapshot) {
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }
    snapshot = snapshot_;
    xSemaphoreGive(mutex_);
    return true;
}

void WeatherService::Publish(PanelWeatherSnapshot result) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
    result.generation = snapshot_.generation + 1;
    snapshot_ = result;
    xSemaphoreGive(mutex_);
}

void WeatherService::SetStatus(PanelWeatherSnapshot& result, const char* status) {
    Copy(result.status, sizeof(result.status), status);
}

WeatherService::Config WeatherService::LoadConfig() const {
    Settings settings("wifi-config");
    Config config;
    config.api_key = Trim(settings.GetString("wx-key", ""));
    const std::string mode = Trim(settings.GetString("wx-mode", "auto"));
    config.manual = mode == "manual";
    config.manual_location = Trim(settings.GetString("wx-loc", ""));
    return config;
}

bool WeatherService::ResolveLocation(const Config& config, Location& location) {
    if (config.manual) {
        if (config.manual_location.empty()) return false;
        Location cached;
        if (LoadCachedLocation(cached) && cached.id == config.manual_location) {
            location = cached;
            return true;
        }
        // 反查城市显示名；GeoAPI 失败时直接用 ID 兼作显示名。
        if (GeoLookup(config, config.manual_location, location)) {
            SaveCachedLocation(location);
        } else {
            location.id = config.manual_location;
            location.name = config.manual_location;
        }
        return true;
    }

    // 缓存有效期 24 小时，未过期直接使用，避免重复消耗定位与 GeoAPI 额度。
    if (LoadCachedLocation(location)) {
        int64_t cached_at_ms = 0;
        ReadInt64("wifi-config", "wx-id-time", cached_at_ms);
        if (cached_at_ms > 0 &&
            (int64_t)(esp_timer_get_time() / 1000) - cached_at_ms < kLocationCacheMs) {
            return true;
        }
    }

    std::string query;
    if (LocateByIp(query) && GeoLookup(config, query, location)) {
        SaveCachedLocation(location);
        return true;
    }
    // 重新定位失败时降级使用历史缓存（宁可显示位置稍旧，也不要没有天气）。
    Location cached;
    if (LoadCachedLocation(cached)) {
        location = cached;
        return true;
    }
    return false;
}

bool WeatherService::LoadCachedLocation(Location& location) const {
    std::string id;
    std::string name;
    if (!ReadString("wifi-config", "wx-id", id) || id.empty()) return false;
    ReadString("wifi-config", "wx-name", name);
    location.id = id;
    location.name = name.empty() ? id : name;
    return true;
}

void WeatherService::SaveCachedLocation(const Location& location) const {
    WriteString("wifi-config", "wx-id", location.id);
    WriteString("wifi-config", "wx-name", location.name);
    WriteInt64("wifi-config", "wx-id-time", esp_timer_get_time() / 1000);
}

// IP 定位：优先 ip-api.com（免费、含经纬度，可到区县级），失败退回
// myip.ipip.net（HTTPS、返回中文市名）。两者都无需鉴权。
bool WeatherService::LocateByIp(std::string& query) {
    std::string response;
    if (PerformRequest(
            "http://ip-api.com/json/?lang=zh-CN&fields=status,lat,lon,city", "",
            response) &&
        !response.empty()) {
        cJSON* root = cJSON_ParseWithLength(response.c_str(), response.size());
        if (root != nullptr) {
            const cJSON* status = cJSON_GetObjectItemCaseSensitive(root, "status");
            const cJSON* lat = cJSON_GetObjectItemCaseSensitive(root, "lat");
            const cJSON* lon = cJSON_GetObjectItemCaseSensitive(root, "lon");
            if (cJSON_IsString(status) && status->valuestring != nullptr &&
                strcmp(status->valuestring, "success") == 0 &&
                cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
                char coordinate[40];
                snprintf(coordinate, sizeof(coordinate), "%.2f,%.2f",
                         lon->valuedouble, lat->valuedouble);
                query = coordinate;
            }
            cJSON_Delete(root);
        }
    }
    if (!query.empty()) return true;

    response.clear();
    if (PerformRequest("https://myip.ipip.net/json", "", response) &&
        !response.empty()) {
        cJSON* root = cJSON_ParseWithLength(response.c_str(), response.size());
        if (root != nullptr) {
            const cJSON* location =
                cJSON_GetObjectItemCaseSensitive(root, "location");
            if (cJSON_IsArray(location) && cJSON_GetArraySize(location) > 2) {
                const cJSON* city = cJSON_GetArrayItem(location, 2);
                if (cJSON_IsString(city) && city->valuestring != nullptr &&
                    city->valuestring[0] != '\0') {
                    query = city->valuestring;
                }
            }
            cJSON_Delete(root);
        }
    }
    return !query.empty();
}

// 和风 GeoAPI：query 支持经纬度（"lon,lat"）或中文城市名 / LocationID。
bool WeatherService::GeoLookup(const Config& config, const std::string& query,
                               Location& location) {
    std::string response;
    const std::string url = std::string(kGeoHost) +
                            "/v2/city/lookup?location=" + UrlEncode(query);
    if (!PerformRequest(url, config.api_key, response) || response.empty()) {
        return false;
    }
    cJSON* root = cJSON_ParseWithLength(response.c_str(), response.size());
    if (root == nullptr) return false;
    bool found = false;
    const cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON* places = cJSON_GetObjectItemCaseSensitive(root, "location");
    if (cJSON_IsString(code) && code->valuestring != nullptr &&
        strcmp(code->valuestring, "200") == 0 && cJSON_IsArray(places) &&
        cJSON_GetArraySize(places) > 0) {
        const cJSON* place = cJSON_GetArrayItem(places, 0);
        const cJSON* id = cJSON_GetObjectItemCaseSensitive(place, "id");
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(place, "name");
        if (cJSON_IsString(id) && id->valuestring != nullptr &&
            id->valuestring[0] != '\0') {
            location.id = id->valuestring;
            location.name = cJSON_IsString(name) && name->valuestring != nullptr
                ? name->valuestring : id->valuestring;
            found = true;
        }
    }
    cJSON_Delete(root);
    return found;
}

bool WeatherService::FetchWeather(const Config& config, const Location& location,
                                  PanelWeatherSnapshot& result) {
    Copy(result.city, sizeof(result.city), location.name);
    const std::string query = "location=" + UrlEncode(location.id);

    std::string now_json;
    if (!PerformRequest(std::string(kApiHost) + "/v7/weather/now?" + query,
                        config.api_key, now_json) ||
        now_json.empty()) {
        ESP_LOGW(kTag, "Fetching current weather failed");
        return false;
    }

    std::string daily_json;
    if (!PerformRequest(std::string(kApiHost) + "/v7/weather/3d?" + query,
                        config.api_key, daily_json) ||
        daily_json.empty()) {
        ESP_LOGW(kTag, "Fetching daily forecast failed");
        return false;
    }

    bool ok = false;
    cJSON* now_root = cJSON_ParseWithLength(now_json.c_str(), now_json.size());
    if (now_root != nullptr) {
        const cJSON* code = cJSON_GetObjectItemCaseSensitive(now_root, "code");
        const cJSON* now = cJSON_GetObjectItemCaseSensitive(now_root, "now");
        const cJSON* updated =
            cJSON_GetObjectItemCaseSensitive(now_root, "updateTime");
        if (cJSON_IsString(code) && code->valuestring != nullptr &&
            strcmp(code->valuestring, "200") == 0 && cJSON_IsObject(now)) {
            const auto text_of = [&now](const char* key) {
                const cJSON* item = cJSON_GetObjectItemCaseSensitive(now, key);
                return cJSON_IsString(item) && item->valuestring != nullptr
                    ? std::string(item->valuestring) : std::string();
            };
            Copy(result.temp, sizeof(result.temp), text_of("temp").empty()
                ? "--" : text_of("temp") + "\xC2\xB0" "C");
            Copy(result.text, sizeof(result.text), text_of("text"));
            Copy(result.icon, sizeof(result.icon), text_of("icon"));
            const char* wind360_text = text_of("wind360").c_str();
            result.wind360 = static_cast<int16_t>(atoi(wind360_text));
            Copy(result.feels_like, sizeof(result.feels_like), text_of("feelsLike").empty()
                ? "--" : text_of("feelsLike") + "\xC2\xB0" "C");
            Copy(result.humidity, sizeof(result.humidity), text_of("humidity").empty()
                ? "--" : text_of("humidity") + "%");
            const std::string wind_dir = text_of("windDir");
            const std::string wind_scale = text_of("windScale");
            if (!wind_dir.empty()) {
                Copy(result.wind, sizeof(result.wind),
                     wind_scale.empty() ? wind_dir : wind_dir + " " + wind_scale + " 级");
            }
            if (cJSON_IsString(updated) && updated->valuestring != nullptr) {
                const std::string updated_at = IsoTime(updated->valuestring);
                if (!updated_at.empty()) {
                    Copy(result.update_time, sizeof(result.update_time), updated_at);
                }
            }
            ok = true;
        }
        cJSON_Delete(now_root);
    }
    if (!ok) {
        ESP_LOGW(kTag, "Unexpected current-weather payload");
        return false;
    }

    bool daily_ok = false;
    cJSON* daily_root = cJSON_ParseWithLength(daily_json.c_str(), daily_json.size());
    if (daily_root != nullptr) {
        const cJSON* code = cJSON_GetObjectItemCaseSensitive(daily_root, "code");
        const cJSON* daily = cJSON_GetObjectItemCaseSensitive(daily_root, "daily");
        if (cJSON_IsString(code) && code->valuestring != nullptr &&
            strcmp(code->valuestring, "200") == 0 && cJSON_IsArray(daily)) {
            const int count = std::min(cJSON_GetArraySize(daily), kWeatherDailyCount);
            for (int index = 0; index < count; ++index) {
                const cJSON* item = cJSON_GetArrayItem(daily, index);
                const auto text_of = [&item](const char* key) {
                    const cJSON* field = cJSON_GetObjectItemCaseSensitive(item, key);
                    return cJSON_IsString(field) && field->valuestring != nullptr
                        ? std::string(field->valuestring) : std::string();
                };
                PanelWeatherDay& day = result.daily[index];
                Copy(day.date, sizeof(day.date),
                     FormatForecastDate(text_of("fxDate")));
                Copy(day.text_day, sizeof(day.text_day), text_of("textDay"));
                Copy(day.icon_day, sizeof(day.icon_day), text_of("iconDay"));
                Copy(day.precip, sizeof(day.precip), text_of("precip"));
                Copy(day.temp_max, sizeof(day.temp_max), text_of("tempMax").empty()
                    ? "--" : text_of("tempMax") + "\xC2\xB0");
                Copy(day.temp_min, sizeof(day.temp_min), text_of("tempMin").empty()
                    ? "--" : text_of("tempMin") + "\xC2\xB0");
                int fx_year = 0, fx_month = 0, fx_day = 0;
                const std::string& fx = text_of("fxDate");
                if (fx.size() == 10) {
                    fx_year = atoi(fx.substr(0, 4).c_str());
                    fx_month = atoi(fx.substr(5, 2).c_str());
                    fx_day = atoi(fx.substr(8, 2).c_str());
                }
                int lunar_month = 0, lunar_day = 0;
                bool lunar_leap = false;
                if (fx_year > 0 &&
                    syna::LunarFromSolar(fx_year, fx_month, fx_day,
                                         &lunar_month, &lunar_day,
                                         &lunar_leap)) {
                    char lunar_text[16] = {0};
                    syna::LunarDayName(lunar_day, lunar_text,
                                       static_cast<int>(sizeof(lunar_text)));
                    Copy(day.lunar_day, sizeof(day.lunar_day), lunar_text);
                }
                if (index == 0) {
                    Copy(result.temp_max, sizeof(result.temp_max), day.temp_max);
                    Copy(result.temp_min, sizeof(result.temp_min), day.temp_min);
                    const int uv_index = atoi(text_of("uvIndex").c_str());
                    Copy(result.uv_level, sizeof(result.uv_level), UvLevelText(uv_index));
                }
            }
            daily_ok = count > 0;
        }
        cJSON_Delete(daily_root);
    }
    if (!daily_ok) {
        ESP_LOGW(kTag, "Unexpected daily-forecast payload");
        return false;
    }
    SetStatus(result, "");
    return true;
}

// 实时空气质量：免费开发版可用；失败时保留 "--"，不影响天气主数据。
bool WeatherService::FetchAirQuality(const Config& config,
                                     const Location& location,
                                     PanelWeatherSnapshot& result) {
    std::string air_json;
    const std::string query = "location=" + UrlEncode(location.id);
    if (!PerformRequest(std::string(kApiHost) + "/v7/air/now?" + query,
                        config.api_key, air_json) ||
        air_json.empty()) {
        ESP_LOGW(kTag, "Fetching air quality failed");
        return false;
    }
    bool ok = false;
    cJSON* root = cJSON_ParseWithLength(air_json.c_str(), air_json.size());
    if (root != nullptr) {
        const cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "code");
        const cJSON* now = cJSON_GetObjectItemCaseSensitive(root, "now");
        if (cJSON_IsString(code) && code->valuestring != nullptr &&
            strcmp(code->valuestring, "200") == 0 && cJSON_IsObject(now)) {
            const auto text_of = [&now](const char* key) {
                const cJSON* item = cJSON_GetObjectItemCaseSensitive(now, key);
                return cJSON_IsString(item) && item->valuestring != nullptr
                    ? std::string(item->valuestring) : std::string();
            };
            const std::string aqi = text_of("aqi");
            if (!aqi.empty()) {
                Copy(result.aqi, sizeof(result.aqi), aqi);
                Copy(result.aqi_category, sizeof(result.aqi_category),
                     text_of("category"));
                ok = true;
            }
        }
        cJSON_Delete(root);
    }
    if (!ok) {
        Copy(result.aqi, sizeof(result.aqi), "--");
        result.aqi_category[0] = '\0';
    }
    return ok;
}

// 和风 API 无条件返回 gzip（Accept-Encoding: identity 也无法关闭），
// 固件手动剥离 gzip 头并用 puff 解压原始 deflate 流。
bool WeatherService::InflateGzip(const std::string& compressed, std::string& plain) {
    plain.clear();
    const unsigned char* bytes =
        reinterpret_cast<const unsigned char*>(compressed.data());
    if (compressed.size() < 18 || bytes[0] != 0x1F || bytes[1] != 0x8B ||
        bytes[2] != 0x08) {
        return false;
    }
    const unsigned char flags = bytes[3];
    size_t offset = 10;
    if (flags & 0x04) {  // FEXTRA
        if (offset + 2 > compressed.size()) return false;
        const size_t extra = static_cast<size_t>(bytes[offset]) |
                             (static_cast<size_t>(bytes[offset + 1]) << 8);
        offset += 2 + extra;
    }
    if (flags & 0x08) {  // FNAME
        while (offset < compressed.size() && bytes[offset] != 0) ++offset;
        ++offset;
    }
    if (flags & 0x10) {  // FCOMMENT
        while (offset < compressed.size() && bytes[offset] != 0) ++offset;
        ++offset;
    }
    if (flags & 0x02) offset += 2;  // FHCRC
    if (offset >= compressed.size()) return false;

    // ISIZE 位于尾部 4 字节（LE），是未压缩长度模 2^32；天气载荷很小，
    // 超过 64 KB 的声明直接拒绝，避免异常输入撑爆内存。
    if (compressed.size() < offset + 8) return false;
    const size_t declared =
        static_cast<size_t>(bytes[compressed.size() - 4]) |
        (static_cast<size_t>(bytes[compressed.size() - 3]) << 8) |
        (static_cast<size_t>(bytes[compressed.size() - 2]) << 16) |
        (static_cast<size_t>(bytes[compressed.size() - 1]) << 24);
    if (declared == 0 || declared > 64 * 1024) return false;

    std::vector<unsigned char> output(declared);
    unsigned long source_length = compressed.size() - offset;
    unsigned long output_length = output.size();
    const int error = puff(output.data(), &output_length,
                           bytes + offset, &source_length);
    if (error != 0) {
        ESP_LOGW(kTag, "gzip inflate failed: %d", error);
        return false;
    }
    plain.assign(reinterpret_cast<const char*>(output.data()), output_length);
    return true;
}

bool WeatherService::PerformRequest(const std::string& url,
                                    const std::string& api_key,
                                    std::string& response) {
    response.clear();
    response.reserve(2048);

    esp_http_client_config_t http_config = {};
    http_config.url = url.c_str();
    http_config.event_handler = [](esp_http_client_event_t* event) -> esp_err_t {
        if (event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr ||
            event->data_len <= 0 || event->user_data == nullptr) return ESP_OK;
        auto* context = static_cast<std::string*>(event->user_data);
        const size_t available =
            context->size() < kMaximumResponseBytes
                ? kMaximumResponseBytes - context->size() : 0;
        if (available > 0) {
            context->append(static_cast<const char*>(event->data),
                            std::min<size_t>(event->data_len, available));
        }
        return ESP_OK;
    };
    http_config.user_data = &response;
    http_config.timeout_ms = 10000;
    http_config.buffer_size = 1024;
    http_config.buffer_size_tx = 1024;
    if (url.rfind("https://", 0) == 0) {
        http_config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) return false;
    esp_http_client_set_header(client, "Accept", "application/json");
    if (!api_key.empty()) {
        esp_http_client_set_header(client, "X-QW-Api-Key", api_key.c_str());
    }
    const esp_err_t error = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (error != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(kTag, "HTTP %s: status=%d err=%s",
                 url.substr(0, url.find('?')).c_str(), status,
                 esp_err_to_name(error));
        response.clear();
        return false;
    }

    std::string plain;
    if (InflateGzip(response, plain)) response = plain;
    return true;
}

bool WeatherService::WifiReady() {
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    return netif != nullptr && esp_netif_get_ip_info(netif, &ip) == ESP_OK &&
           ip.ip.addr != 0;
}
