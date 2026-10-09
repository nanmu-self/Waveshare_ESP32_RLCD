#ifndef AI_PANEL_UI_H
#define AI_PANEL_UI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_WIFI_UNCONFIGURED,
    UI_WIFI_CONNECTING,
    UI_WIFI_PROVISIONING,
    UI_WIFI_CONNECTED,
} ui_wifi_state_t;

#define UI_MAX_COMPUTERS 8
#define UI_MAX_TODOS 4
#define UI_WEATHER_DAILY 3

typedef struct {
    char name[40];
    char agent_state[16];
    bool online;
} ui_computer_info_t;

typedef struct {
    char date[16];      /* "周三 10/09" */
    char text_day[24];  /* 白天天气现象 */
    char icon_day[8];   /* 白天天气图标代码 */
    char precip[8];     /* 预报降水量 mm */
    char lunar_day[16]; /* 农历日 */
    char high[8];       /* "25°" */
    char low[8];        /* "12°" */
} ui_weather_day_t;

typedef struct {
    bool available;     /* 天气数据有效 */
    bool configured;    /* 已配置和风 Key */
    bool location_ok;   /* 位置已解析 */
    char status[48];    /* 状态提示（空 = 正常） */
    char city[24];
    char temp[8];       /* "25°C" */
    char text[24];
    char icon[8];       /* 和风天气图标代码 */
    char feels_like[8];
    char humidity[8];
    char wind[44];
    char temp_max[8];
    char temp_min[8];
    char aqi[8];         /* AQI 数值 */
    char aqi_category[16]; /* AQI 等级文字 */
    char uv_level[16];     /* 紫外线等级文字 */
    int wind360;           /* 风向角度，-1 未知 */
    char update_time[8]; /* "HH:MM" */
    ui_weather_day_t daily[UI_WEATHER_DAILY];
} ui_weather_info_t;

typedef struct {
    unsigned int id;
    char text[64];
    bool completed;
} ui_todo_item_t;

void ui_init(void);
void ui_show_dashboard(void);
void ui_show_performance(void);
void ui_show_syna(void);
void ui_show_weather(void);
void ui_show_computers(void);
void ui_toggle_page(void);
void ui_select_computer(int direction);
int ui_confirm_computer(void);
void ui_update_computer_list(const ui_computer_info_t *items, int count,
                             int current_index);
void ui_update_clock(void);
void ui_update_environment(float temperature_c, float humidity_percent,
                           int battery_percent, bool environment_valid,
                           bool battery_valid);
void ui_update_wifi_state(ui_wifi_state_t state);
void ui_update_pc_connected(bool connected);
void ui_update_agent_state(const char *state);
void ui_update_codex_quota(int short_remaining_percent,
                           int week_remaining_percent, bool connected, bool stale);
void ui_update_media(bool available, const char *status, const char *title,
                     const char *artist, int position_seconds,
                     int duration_seconds, const char *lyric);
void ui_show_assistant_overlay(const char *state, const char *text);
void ui_hide_assistant_overlay(void);
void ui_update_syna_conversation(const char *user_text,
                                 const char *assistant_text);
void ui_update_api_balance(const char *provider, const char *balance);
void ui_update_weather(const ui_weather_info_t *info);
void ui_update_todos(const ui_todo_item_t *items, int count);
void ui_update_performance(float cpu_percent, float memory_percent,
                           float gpu_percent, bool gpu_valid,
                           float disk_percent, float cpu_temperature_c,
                           bool cpu_temperature_valid,
                           float gpu_temperature_c,
                           bool gpu_temperature_valid,
                           int latency_ms, float upload_mb_per_second,
                           float download_mb_per_second, bool connected);
bool ui_is_computer_page(void);
bool ui_is_syna_page(void);

#ifdef __cplusplus
}
#endif

#endif
