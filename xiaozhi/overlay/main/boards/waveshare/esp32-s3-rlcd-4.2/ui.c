/* Project-specific additions Copyright (c) 2026 黑沐. MIT; upstream notices retained. */
#include "ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "assets.h"
#include "ui_fonts.h"

#ifdef ESP_PLATFORM
#include "esp_wifi.h"
#include "esp_netif.h"
#endif

typedef enum {
    PAGE_DASHBOARD,
    PAGE_PERFORMANCE,
    PAGE_SYNA,
    PAGE_WEATHER,
    PAGE_COMPUTERS,
    PAGE_ABOUT,
} page_t;

static const char *const kWeekdayNames[] = {"周日", "周一", "周二", "周三",
                                           "周四", "周五", "周六"};

/* Defined near the About page; used by the weather bottom bar too. */
static bool station_ip_text(char *buf, size_t len);

typedef struct {
    const char *name;
    const char *connection;
    const char *agent_state;
    const char *task;
    const char *elapsed;
    const char *deepseek_balance;
    int codex_short_percent;
    int codex_week_percent;
} computer_t;

typedef struct {
    float cpu_percent;
    float memory_percent;
    float gpu_percent;
    float disk_percent;
    float cpu_temperature_c;
    float gpu_temperature_c;
    float upload_mb_per_second;
    float download_mb_per_second;
    int latency_ms;
    bool gpu_valid;
    bool cpu_temperature_valid;
    bool gpu_temperature_valid;
    bool connected;
} performance_state_t;

static computer_t computers[] = {
    {
        .name = "--",
        .connection = "OFFLINE",
        .agent_state = "OFFLINE",
        .task = "--",
        .elapsed = "--:--:--",
        .deepseek_balance = "--",
        .codex_short_percent = -1,
        .codex_week_percent = -1,
    },
};

#define MOCK_COMPUTER_COUNT ((int)(sizeof(computers) / sizeof(computers[0])))
#define COMPUTER_ROWS_VISIBLE 3

#define COLOR_BLACK lv_color_black()
#define COLOR_WHITE lv_color_white()

static page_t current_page = PAGE_DASHBOARD;
static int current_computer = 0;
static int current_list_computer = 0;
static int selected_computer = 0;
static int computer_list_count = 0;
static int computer_list_offset = 0;
static ui_computer_info_t computer_list[UI_MAX_COMPUTERS];

static lv_obj_t *clock_label;
static lv_obj_t *dashboard_clock_label;
static lv_obj_t *performance_clock_label;
static lv_obj_t *dashboard_temperature_label;
static lv_obj_t *dashboard_humidity_label;
static lv_obj_t *dashboard_battery_label;
static lv_obj_t *performance_battery_label;
static lv_obj_t *syna_battery_label;
static lv_obj_t *dashboard_wifi_status_image;
static lv_obj_t *performance_wifi_status_image;
static lv_obj_t *syna_wifi_status_image;
static lv_obj_t *dashboard_pc_status_image;
static lv_obj_t *performance_pc_status_image;
static lv_obj_t *syna_pc_status_image;
static lv_obj_t *dashboard_agent_status_label;
static lv_obj_t *dashboard_quota_source_label;
static lv_obj_t *dashboard_quota_source_value;
static lv_obj_t *dashboard_quota_caption[3];
static lv_obj_t *dashboard_quota_countdown[3];
static lv_obj_t *dashboard_quota_percent[3];
static lv_obj_t *dashboard_quota_track[3];
static lv_obj_t *dashboard_quota_fill[3];
static lv_obj_t *dashboard_media_status_label;
static lv_obj_t *dashboard_media_title_label;
static lv_obj_t *dashboard_media_artist_label;
static lv_obj_t *dashboard_media_position_label;
static lv_obj_t *dashboard_media_duration_label;
static lv_obj_t *dashboard_media_lyric_label;
static lv_obj_t *dashboard_media_progress_knob;
static lv_timer_t *agent_done_blink_timer;
static lv_obj_t *performance_temperature_labels[2];
static lv_obj_t *performance_usage_labels[4];
static lv_obj_t *performance_usage_fills[4];
static lv_obj_t *performance_latency_label;
static lv_obj_t *performance_upload_label;
static lv_obj_t *performance_download_label;
static lv_obj_t *performance_network_chart;
static lv_chart_series_t *performance_network_series;
static lv_obj_t *dashboard_screen;
static lv_obj_t *performance_screen;
static lv_obj_t *syna_screen;
static lv_obj_t *syna_clock_label;
static lv_obj_t *syna_api_provider_label;
static lv_obj_t *syna_api_balance_label;
static lv_obj_t *syna_user_label;
static lv_obj_t *syna_assistant_label;
static lv_obj_t *syna_todo_count_label;
static lv_obj_t *syna_todo_images[UI_MAX_TODOS];
static lv_obj_t *syna_todo_labels[UI_MAX_TODOS];
static lv_obj_t *computers_screen;
static lv_obj_t *computer_rows[COMPUTER_ROWS_VISIBLE];
static lv_obj_t *computer_name_labels[COMPUTER_ROWS_VISIBLE];
static lv_obj_t *computer_state_labels[COMPUTER_ROWS_VISIBLE];
static lv_obj_t *computer_current_labels[COMPUTER_ROWS_VISIBLE];
static lv_obj_t *computer_found_label;
static lv_obj_t *assistant_overlay;
static lv_obj_t *assistant_overlay_label;
static bool assistant_active = false;
static char assistant_state[32] = "Syna-sama";
static char assistant_text[192] = "正在聆听…";
static char current_syna_user[160] = "";
static char current_syna_assistant[192] = "";
static char current_api_provider[32] = "API";
static char current_api_balance[48] = "--";
static ui_todo_item_t current_todos[UI_MAX_TODOS];
static int current_todo_count = 0;
static ui_wifi_state_t current_wifi_state = UI_WIFI_UNCONFIGURED;
static bool current_pc_connected = false;
static int current_battery_percent = -1;
static bool current_battery_valid = false;
static char current_agent_state[16] = "OFFLINE";
static performance_state_t current_performance_state;

/* Weather page battery label; referenced by update_battery_labels. */
static lv_obj_t *weather_battery_label;

static void style_screen(lv_obj_t *screen);
static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            int32_t x, int32_t y);
static lv_obj_t *make_value_label(lv_obj_t *parent, const lv_font_t *font,
                                  int32_t x, int32_t y, int32_t width,
                                  int32_t height);
static lv_obj_t *make_panel(lv_obj_t *parent, int32_t x, int32_t y,
                            int32_t width, int32_t height);
static lv_obj_t *make_performance_fill(lv_obj_t *parent, int32_t y, int percent);
static const char *agent_state_text(const char *state);
static const lv_image_dsc_t *wifi_asset_for(ui_wifi_state_t state);
static const lv_image_dsc_t *pc_asset_for(bool connected);
static void agent_done_blink_cb(lv_timer_t *timer);
static void update_computer_rows(void);
static void computer_row_clicked(lv_event_t *event);
static void sync_assistant_visibility(void);
static void refresh_syna_conversation(void);
static void refresh_syna_todos(void);

static void update_battery_labels(void)
{
    char value[8];
    if(current_battery_valid) {
        int percent = current_battery_percent;
        if(percent < 0) percent = 0;
        if(percent > 100) percent = 100;
        snprintf(value, sizeof(value), "%d%%", percent);
    }
    else {
        snprintf(value, sizeof(value), "--");
    }

    lv_obj_t *labels[] = {dashboard_battery_label, performance_battery_label,
                          syna_battery_label, weather_battery_label};
    for(size_t index = 0; index < sizeof(labels) / sizeof(labels[0]); ++index) {
        if(labels[index] != NULL && lv_obj_is_valid(labels[index]) &&
           strcmp(lv_label_get_text(labels[index]), value) != 0) {
            lv_label_set_text(labels[index], value);
        }
    }
}

static int network_chart_value(float upload_mb_per_second,
                               float download_mb_per_second)
{
    float kb_per_second = (upload_mb_per_second + download_mb_per_second) * 1024.0f;
    int value;
    if(kb_per_second < 1.0f) value = 0;
    else if(kb_per_second < 10.0f) value = 8 + (int)(kb_per_second * 2.0f);
    else if(kb_per_second < 100.0f) value = 28 + (int)(kb_per_second / 5.0f);
    else if(kb_per_second < 1000.0f) value = 48 + (int)(kb_per_second / 30.0f);
    else value = 78 + (int)(kb_per_second / 500.0f);
    if(value > 100) value = 100;
    return value;
}

static void ensure_assistant_overlay(void)
{
    if(assistant_overlay != NULL && lv_obj_is_valid(assistant_overlay)) return;
    assistant_overlay = make_panel(lv_layer_top(), 10, 237, 380, 54);
    lv_obj_set_style_border_width(assistant_overlay, 2, 0);
    lv_obj_set_style_radius(assistant_overlay, 10, 0);
    assistant_overlay_label = make_label(
        assistant_overlay, "小智  正在聆听…", &ui_font_14_cjk, 12, 16);
    lv_obj_set_size(assistant_overlay_label, 354, 22);
    lv_label_set_long_mode(assistant_overlay_label, LV_LABEL_LONG_CLIP);
    lv_obj_add_flag(assistant_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_show_assistant_overlay(const char *state, const char *text)
{
    ensure_assistant_overlay();
    assistant_active = true;
    char line[256];
    const char *prefix = (state != NULL && state[0] != '\0') ? state : "Syna-sama";
    const char *message = (text != NULL && text[0] != '\0') ? text : "正在聆听…";
    snprintf(assistant_state, sizeof(assistant_state), "%s", prefix);
    snprintf(assistant_text, sizeof(assistant_text), "%s", message);
    snprintf(line, sizeof(line), "%s  %s", prefix, message);
    lv_label_set_text(assistant_overlay_label, line);
    if(current_page == PAGE_SYNA) {
        snprintf(current_syna_assistant, sizeof(current_syna_assistant), "%s", message);
        refresh_syna_conversation();
    }
    sync_assistant_visibility();
}

void ui_hide_assistant_overlay(void)
{
    assistant_active = false;
    if(assistant_overlay != NULL && lv_obj_is_valid(assistant_overlay)) {
        lv_obj_add_flag(assistant_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void sync_assistant_visibility(void)
{
    if(assistant_overlay == NULL || !lv_obj_is_valid(assistant_overlay)) return;
    if(assistant_active && current_page != PAGE_SYNA) {
        lv_obj_remove_flag(assistant_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(assistant_overlay);
    }
    else {
        lv_obj_add_flag(assistant_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void style_screen(lv_obj_t *screen)
{
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, COLOR_BLACK, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            int32_t x, int32_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, COLOR_BLACK, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *make_value_label(lv_obj_t *parent, const lv_font_t *font,
                                  int32_t x, int32_t y, int32_t width,
                                  int32_t height)
{
    lv_obj_t *label = make_label(parent, "--", font, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_bg_color(label, COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static lv_obj_t *make_panel(lv_obj_t *parent, int32_t x, int32_t y,
                            int32_t width, int32_t height)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, COLOR_BLACK, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    return panel;
}

static lv_obj_t *make_performance_fill(lv_obj_t *parent, int32_t y, int percent)
{
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;

    lv_obj_t *fill = lv_obj_create(parent);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(fill, 18, y);
    lv_obj_set_size(fill, (150 * percent) / 100, 9);
    lv_obj_set_style_radius(fill, 0, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    lv_obj_set_style_bg_color(fill, COLOR_BLACK, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    return fill;
}

static const char *agent_state_text(const char *state)
{
    if(strcmp(state, "WORKING") == 0) return "工作中";
    if(strcmp(state, "DONE") == 0) return "已完成";
    if(strcmp(state, "IDLE") == 0) return "空闲";
    if(strcmp(state, "WAITING") == 0) return "等待输入";
    if(strcmp(state, "LOGIN_REQUIRED") == 0) return "请登录";
    return "离线";
}

static void agent_done_blink_cb(lv_timer_t *timer)
{
    (void)timer;
    if(dashboard_agent_status_label == NULL ||
       !lv_obj_is_valid(dashboard_agent_status_label)) return;

    if(lv_obj_has_flag(dashboard_agent_status_label, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(dashboard_agent_status_label, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(dashboard_agent_status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static const lv_image_dsc_t *wifi_asset_for(ui_wifi_state_t state)
{
    switch(state) {
        case UI_WIFI_CONNECTED: return &ui_wifi_connected;
        case UI_WIFI_CONNECTING: return &ui_wifi_connecting;
        case UI_WIFI_PROVISIONING: return &ui_wifi_provisioning;
        default: return &ui_wifi_unconfigured;
    }
}

static const lv_image_dsc_t *pc_asset_for(bool connected)
{
    return connected ? &ui_pc_connected : &ui_pc_disconnected;
}

static lv_obj_t *make_white_mask(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *mask = lv_obj_create(parent);
    lv_obj_remove_flag(mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(mask, x, y);
    lv_obj_set_size(mask, width, height);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_pad_all(mask, 0, 0);
    lv_obj_set_style_bg_color(mask, COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_COVER, 0);
    return mask;
}

void ui_show_dashboard(void)
{
    current_page = PAGE_DASHBOARD;
    sync_assistant_visibility();
    if(dashboard_screen != NULL) {
        clock_label = dashboard_clock_label;
        lv_screen_load(dashboard_screen);
        ui_update_clock();
        return;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    dashboard_screen = screen;
    style_screen(screen);

    lv_obj_t *base = lv_image_create(screen);
    lv_image_set_src(base, &ui_screen_base);
    lv_obj_set_pos(base, 0, 0);

    clock_label = make_label(screen, "--:--", &lv_font_montserrat_42, 40, 9);
    dashboard_clock_label = clock_label;
    lv_obj_set_width(clock_label, 129);
    lv_obj_set_style_text_align(clock_label, LV_TEXT_ALIGN_CENTER, 0);
    ui_update_clock();

    dashboard_temperature_label = make_value_label(screen, &ui_font_18_regular,
                                                    229, 31, 58, 22);
    dashboard_humidity_label = make_value_label(screen, &ui_font_18_regular,
                                                 330, 31, 56, 22);
    dashboard_battery_label = make_value_label(screen, &ui_font_11_regular,
                                                354, 268, 36, 18);
    lv_obj_set_style_text_align(dashboard_temperature_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(dashboard_humidity_label, LV_TEXT_ALIGN_LEFT, 0);

    dashboard_wifi_status_image = lv_image_create(screen);
    lv_image_set_src(dashboard_wifi_status_image, wifi_asset_for(current_wifi_state));
    lv_obj_set_pos(dashboard_wifi_status_image, 46, 270);
    dashboard_pc_status_image = lv_image_create(screen);
    lv_image_set_src(dashboard_pc_status_image, pc_asset_for(current_pc_connected));
    lv_obj_set_pos(dashboard_pc_status_image, 236, 270);

    /* 标题行自绘：“AI AGENT” + 行内状态徽标（圆点 + 文字，替代黑色胶囊）。 */
    make_white_mask(screen, 14, 82, 155, 26);
    make_label(screen, "AI AGENT", &ui_font_18_regular, 19, 84);
    dashboard_agent_status_label = make_label(
        screen, "● 离线", &ui_font_14_cjk, 76, 86);
    lv_obj_set_width(dashboard_agent_status_label, 92);
    lv_obj_set_style_text_align(dashboard_agent_status_label,
                                LV_TEXT_ALIGN_RIGHT, 0);

    /* 底图里蚀刻的旧状态胶囊区域遮白，留白分隔标题行与额度区。 */
    lv_obj_t *agent_status_mask = lv_obj_create(screen);
    lv_obj_remove_flag(agent_status_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(agent_status_mask, 46, 108);
    lv_obj_set_size(agent_status_mask, 88, 29);
    lv_obj_set_style_radius(agent_status_mask, 0, 0);
    lv_obj_set_style_border_width(agent_status_mask, 0, 0);
    lv_obj_set_style_pad_all(agent_status_mask, 0, 0);
    lv_obj_set_style_bg_color(agent_status_mask, COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(agent_status_mask, LV_OPA_COVER, 0);

    make_white_mask(screen, 221, 81, 78, 20);
    make_white_mask(screen, 255, 115, 127, 23);
    make_white_mask(screen, 276, 140, 106, 20);
    make_white_mask(screen, 268, 176, 9, 10);
    make_white_mask(screen, 192, 188, 43, 17);
    make_white_mask(screen, 347, 188, 37, 17);
    make_white_mask(screen, 190, 212, 192, 34);

    dashboard_media_status_label = make_label(
        screen, "未播放", &ui_font_14_cjk, 222, 82);
    lv_obj_set_size(dashboard_media_status_label, 77, 18);
    dashboard_media_title_label = make_label(
        screen, "网易云音乐", &ui_font_14_cjk, 257, 116);
    lv_label_set_long_mode(dashboard_media_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(dashboard_media_title_label, 124, 18);
    dashboard_media_artist_label = make_label(
        screen, "--", &ui_font_14_cjk, 277, 141);
    lv_label_set_long_mode(dashboard_media_artist_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(dashboard_media_artist_label, 104, 18);
    dashboard_media_position_label = make_label(
        screen, "--:--", &ui_font_11_regular, 193, 189);
    dashboard_media_duration_label = make_label(
        screen, "--:--", &ui_font_11_regular, 349, 189);
    lv_obj_set_width(dashboard_media_duration_label, 34);
    lv_obj_set_style_text_align(dashboard_media_duration_label, LV_TEXT_ALIGN_RIGHT, 0);
    dashboard_media_lyric_label = make_label(
        screen, "暂无歌词", &ui_font_14_cjk, 191, 216);
    lv_label_set_long_mode(dashboard_media_lyric_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(dashboard_media_lyric_label, 190, 20);
    lv_obj_set_style_text_align(dashboard_media_lyric_label, LV_TEXT_ALIGN_CENTER, 0);

    for(int dot = 0; dot < 3; ++dot) {
        lv_obj_t *progress_patch = lv_obj_create(screen);
        lv_obj_remove_flag(progress_patch, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(progress_patch, 269 + dot * 3, 180);
        lv_obj_set_size(progress_patch, 2, 1);
        lv_obj_set_style_radius(progress_patch, 0, 0);
        lv_obj_set_style_border_width(progress_patch, 0, 0);
        lv_obj_set_style_pad_all(progress_patch, 0, 0);
        lv_obj_set_style_bg_color(progress_patch, COLOR_BLACK, 0);
        lv_obj_set_style_bg_opa(progress_patch, LV_OPA_COVER, 0);
    }
    dashboard_media_progress_knob = lv_obj_create(screen);
    lv_obj_remove_flag(dashboard_media_progress_knob, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(dashboard_media_progress_knob, 191, 178);
    lv_obj_set_size(dashboard_media_progress_knob, 5, 5);
    lv_obj_set_style_radius(dashboard_media_progress_knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dashboard_media_progress_knob, 0, 0);
    lv_obj_set_style_pad_all(dashboard_media_progress_knob, 0, 0);
    lv_obj_set_style_bg_color(dashboard_media_progress_knob, COLOR_BLACK, 0);
    lv_obj_set_style_bg_opa(dashboard_media_progress_knob, LV_OPA_COVER, 0);

    /* 套餐来源行：填充标题下方空白区（键值排布，右缘与网格对齐）。 */
    make_white_mask(screen, 14, 108, 155, 30);
    dashboard_quota_source_label = make_label(
        screen, "套餐", &ui_font_14_cjk, 19, 113);
    dashboard_quota_source_value = make_label(
        screen, "--", &ui_font_14_cjk, 100, 113);
    lv_obj_set_width(dashboard_quota_source_value, 68);
    lv_obj_set_style_text_align(dashboard_quota_source_value,
                                LV_TEXT_ALIGN_RIGHT, 0);

    /* 额度区整体自绘：紧凑网格（标题 | 进度条 | 百分比 | 倒计时）。 */
    make_white_mask(screen, 14, 147, 153, 100);
    static const char *const kQuotaCaptions[3] = {"5h", "周", "月"};
    for(int index = 0; index < 3; ++index) {
        const int row_y = 152 + index * 30;
        dashboard_quota_caption[index] = make_label(
            screen, kQuotaCaptions[index], &ui_font_14_cjk, 19, row_y);
        lv_obj_t *track = lv_obj_create(screen);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(track, 46, row_y + 8);
        lv_obj_set_size(track, 40, 5);
        lv_obj_set_style_radius(track, 1, 0);
        lv_obj_set_style_border_width(track, 1, 0);
        lv_obj_set_style_border_color(track, COLOR_BLACK, 0);
        lv_obj_set_style_bg_color(track, COLOR_WHITE, 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(track, 0, 0);
        dashboard_quota_track[index] = track;
        lv_obj_t *fill = lv_obj_create(track);
        lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_size(fill, 0, 3);
        lv_obj_set_style_radius(fill, 0, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_set_style_bg_color(fill, COLOR_BLACK, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        dashboard_quota_fill[index] = fill;
        dashboard_quota_percent[index] = make_label(
            screen, "--", &ui_font_14_regular, 84, row_y + 1);
        lv_obj_set_width(dashboard_quota_percent[index], 42);
        lv_obj_set_style_text_align(dashboard_quota_percent[index],
                                    LV_TEXT_ALIGN_RIGHT, 0);
        dashboard_quota_countdown[index] = make_label(
            screen, "", &ui_font_14_cjk, 130, row_y + 1);
        lv_obj_set_width(dashboard_quota_countdown[index], 34);
        lv_obj_set_style_text_align(dashboard_quota_countdown[index],
                                    LV_TEXT_ALIGN_RIGHT, 0);
    }
    lv_screen_load(screen);
}

void ui_show_performance(void)
{
    current_page = PAGE_PERFORMANCE;
    sync_assistant_visibility();
    if(performance_screen != NULL) {
        clock_label = performance_clock_label;
        lv_screen_load(performance_screen);
        ui_update_clock();
        return;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    performance_screen = screen;
    style_screen(screen);
    lv_obj_t *base = lv_image_create(screen);
    lv_image_set_src(base, &ui_performance_base);
    lv_obj_set_pos(base, 0, 0);

    clock_label = make_label(screen, "--:--", &lv_font_montserrat_38, 47, 8);
    performance_clock_label = clock_label;
    lv_label_set_long_mode(clock_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(clock_label, 108, 44);
    lv_obj_set_style_text_align(clock_label, LV_TEXT_ALIGN_CENTER, 0);
    ui_update_clock();

    performance_temperature_labels[0] =
        make_value_label(screen, &ui_font_18_regular, 200, 31, 59, 22);
    performance_temperature_labels[1] =
        make_value_label(screen, &ui_font_18_regular, 318, 31, 60, 22);
    lv_obj_set_style_text_align(performance_temperature_labels[0], LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(performance_temperature_labels[1], LV_TEXT_ALIGN_LEFT, 0);

    const int usage[] = {0, 0, 0, 0};
    const int bar_y[] = {119, 158, 198, 237};
    const int label_y[] = {94, 134, 173, 213};
    // The metric names are baked into the base image. Move only their ink
    // rectangles, preserving the original glyphs and stationary bar outlines.
    const int name_y[] = {106, 146, 185, 224};
    const int name_height[] = {8, 9, 8, 9};
    for(int i = 0; i < 4; ++i) {
        lv_obj_t *mask = make_white_mask(screen, 17, name_y[i] - 2,
                                         68, name_height[i] + 2);
        lv_obj_t *clip = make_white_mask(mask, 0, 0, 68, name_height[i]);
        lv_obj_t *name = lv_image_create(clip);
        lv_image_set_src(name, &ui_performance_base);
        lv_obj_set_pos(name, -17, -name_y[i]);
    }
    for(int i = 0; i < 4; ++i) {
        char percent[8];
        snprintf(percent, sizeof(percent), "%d%%", usage[i]);
        lv_obj_t *value = make_label(screen, percent, &ui_font_18_regular,
                                     116, label_y[i]);
        performance_usage_labels[i] = value;
        lv_obj_set_size(value, 52, 22);
        lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        performance_usage_fills[i] = make_performance_fill(screen, bar_y[i], usage[i]);
    }

    lv_obj_t *latency = make_label(screen, "12 ms", &ui_font_11_regular, 334, 124);
    performance_latency_label = latency;
    lv_obj_set_width(latency, 47);
    lv_obj_set_style_text_align(latency, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *upload = make_label(screen, "2.4 MB/s", &ui_font_11_regular, 321, 146);
    performance_upload_label = upload;
    lv_obj_set_width(upload, 60);
    lv_obj_set_style_text_align(upload, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *download = make_label(screen, "18.7 MB/s", &ui_font_11_regular, 314, 168);
    performance_download_label = download;
    lv_obj_set_width(download, 67);
    lv_obj_set_style_text_align(download, LV_TEXT_ALIGN_RIGHT, 0);

    performance_network_chart = lv_chart_create(screen);
    lv_obj_set_pos(performance_network_chart, 200, 191);
    lv_obj_set_size(performance_network_chart, 182, 48);
    lv_obj_set_style_bg_color(performance_network_chart, COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(performance_network_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(performance_network_chart, 0, 0);
    lv_obj_set_style_radius(performance_network_chart, 0, 0);
    lv_obj_set_style_pad_all(performance_network_chart, 0, 0);
    lv_obj_set_style_line_width(performance_network_chart, 1, LV_PART_ITEMS);
    lv_obj_set_style_size(performance_network_chart, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_type(performance_network_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(performance_network_chart, 0, 0);
    lv_chart_set_point_count(performance_network_chart, 32);
    lv_chart_set_range(performance_network_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    performance_network_series = lv_chart_add_series(
        performance_network_chart, COLOR_BLACK, LV_CHART_AXIS_PRIMARY_Y);
    for(int i = 0; i < 32; ++i) {
        lv_chart_set_next_value(performance_network_chart,
                                performance_network_series, 0);
    }

    make_white_mask(screen, 354, 268, 36, 18);
    performance_battery_label = make_value_label(
        screen, &ui_font_11_regular, 354, 268, 36, 18);
    update_battery_labels();
    performance_wifi_status_image = lv_image_create(screen);
    lv_image_set_src(performance_wifi_status_image, wifi_asset_for(current_wifi_state));
    lv_obj_set_pos(performance_wifi_status_image, 46, 270);
    performance_pc_status_image = lv_image_create(screen);
    lv_image_set_src(performance_pc_status_image, pc_asset_for(current_pc_connected));
    lv_obj_set_pos(performance_pc_status_image, 236, 270);
    ui_update_performance(
        current_performance_state.cpu_percent,
        current_performance_state.memory_percent,
        current_performance_state.gpu_percent,
        current_performance_state.gpu_valid,
        current_performance_state.disk_percent,
        current_performance_state.cpu_temperature_c,
        current_performance_state.cpu_temperature_valid,
        current_performance_state.gpu_temperature_c,
        current_performance_state.gpu_temperature_valid,
        current_performance_state.latency_ms,
        current_performance_state.upload_mb_per_second,
        current_performance_state.download_mb_per_second,
        current_performance_state.connected);
    lv_screen_load(screen);
}

static void refresh_syna_conversation(void)
{
    if(syna_user_label != NULL && lv_obj_is_valid(syna_user_label)) {
        char text[192];
        snprintf(text, sizeof(text), "你\n%s", current_syna_user[0] ? current_syna_user : "…");
        lv_label_set_text(syna_user_label, text);
    }
    if(syna_assistant_label != NULL && lv_obj_is_valid(syna_assistant_label)) {
        char text[224];
        snprintf(text, sizeof(text), "Syna\n%s",
                 current_syna_assistant[0] ? current_syna_assistant : "…");
        lv_label_set_text(syna_assistant_label, text);
    }
}

static void refresh_syna_todos(void)
{
    int completed = 0;
    for(int index = 0; index < current_todo_count; ++index) {
        if(current_todos[index].completed) ++completed;
    }
    if(syna_todo_count_label != NULL && lv_obj_is_valid(syna_todo_count_label)) {
        char count[24];
        snprintf(count, sizeof(count), "%d/%d", completed, current_todo_count);
        lv_label_set_text(syna_todo_count_label, count);
    }
    for(int index = 0; index < UI_MAX_TODOS; ++index) {
        if(syna_todo_images[index] == NULL || syna_todo_labels[index] == NULL) continue;
        if(index < current_todo_count) {
            lv_image_set_src(syna_todo_images[index], current_todos[index].completed
                                 ? &ui_syna_todo_checked
                                 : &ui_syna_todo_unchecked);
            lv_label_set_text(syna_todo_labels[index], current_todos[index].text);
            lv_obj_remove_flag(syna_todo_images[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(syna_todo_labels[index], LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(syna_todo_images[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(syna_todo_labels[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_show_syna(void)
{
    current_page = PAGE_SYNA;
    sync_assistant_visibility();
    if(syna_screen != NULL) {
        clock_label = syna_clock_label;
        refresh_syna_conversation();
        refresh_syna_todos();
        lv_screen_load(syna_screen);
        ui_update_clock();
        return;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    syna_screen = screen;
    style_screen(screen);
    lv_obj_t *base = lv_image_create(screen);
    lv_image_set_src(base, &ui_syna_base);
    lv_obj_set_pos(base, 0, 0);

    clock_label = make_label(screen, "--:--", &lv_font_montserrat_42, 40, 9);
    syna_clock_label = clock_label;
    lv_obj_set_width(clock_label, 129);
    lv_obj_set_style_text_align(clock_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *wallet = lv_image_create(screen);
    lv_image_set_src(wallet, &ui_syna_wallet);
    lv_obj_set_pos(wallet, 207, 28);
    syna_api_provider_label = make_label(screen, current_api_provider,
                                         &ui_font_11_regular, 243, 33);
    lv_obj_set_size(syna_api_provider_label, 63, 17);
    lv_label_set_long_mode(syna_api_provider_label, LV_LABEL_LONG_CLIP);
    syna_api_balance_label = make_label(screen, current_api_balance,
                                        &ui_font_11_regular, 306, 33);
    lv_obj_set_size(syna_api_balance_label, 77, 17);
    lv_label_set_long_mode(syna_api_balance_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(syna_api_balance_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *speech = lv_image_create(screen);
    lv_image_set_src(speech, &ui_syna_speech);
    lv_obj_set_pos(speech, 17, 79);
    lv_obj_t *todo_header = lv_image_create(screen);
    lv_image_set_src(todo_header, &ui_syna_todo_checked);
    lv_obj_set_pos(todo_header, 220, 80);
    syna_todo_count_label = make_label(screen, "0/0", &ui_font_11_regular, 350, 81);
    lv_obj_set_size(syna_todo_count_label, 32, 17);
    lv_obj_set_style_text_align(syna_todo_count_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *user_bubble = make_panel(screen, 43, 111, 150, 48);
    lv_obj_set_style_radius(user_bubble, 7, 0);
    syna_user_label = make_label(user_bubble, "", &ui_font_14_cjk, 7, 3);
    lv_obj_set_size(syna_user_label, 136, 42);
    lv_label_set_long_mode(syna_user_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(syna_user_label, 0, 0);

    lv_obj_t *assistant_bubble = make_panel(screen, 17, 167, 166, 70);
    lv_obj_set_style_radius(assistant_bubble, 7, 0);
    syna_assistant_label = make_label(assistant_bubble, "", &ui_font_14_cjk, 7, 3);
    lv_obj_set_size(syna_assistant_label, 152, 62);
    lv_label_set_long_mode(syna_assistant_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(syna_assistant_label, 0, 0);

    for(int index = 0; index < UI_MAX_TODOS; ++index) {
        const int y = 114 + index * 31;
        syna_todo_images[index] = lv_image_create(screen);
        lv_image_set_src(syna_todo_images[index], &ui_syna_todo_unchecked);
        lv_obj_set_pos(syna_todo_images[index], 221, y);
        syna_todo_labels[index] = make_label(screen, "", &ui_font_14_cjk, 243, y - 2);
        lv_obj_set_size(syna_todo_labels[index], 138, 22);
        lv_label_set_long_mode(syna_todo_labels[index], LV_LABEL_LONG_CLIP);
    }

    make_white_mask(screen, 354, 268, 36, 18);
    syna_battery_label = make_value_label(screen, &ui_font_11_regular,
                                          354, 268, 36, 18);
    syna_wifi_status_image = lv_image_create(screen);
    lv_image_set_src(syna_wifi_status_image, wifi_asset_for(current_wifi_state));
    lv_obj_set_pos(syna_wifi_status_image, 46, 270);
    syna_pc_status_image = lv_image_create(screen);
    lv_image_set_src(syna_pc_status_image, pc_asset_for(current_pc_connected));
    lv_obj_set_pos(syna_pc_status_image, 236, 270);

    refresh_syna_conversation();
    refresh_syna_todos();
    update_battery_labels();
    ui_update_clock();
    lv_screen_load(screen);
}

/* Weather page: cached like the other primary pages. Data arrives from
 * WeatherService via ui_update_weather; the simulator feeds mock values.
 * Icons are 1-bit const arrays in flash, mapped from QWeather icon codes.
 * Wind arrows point downwind (a southwest wind blows toward northeast).
 * Bottom bar: device IP, update time and the battery gauge. */
static lv_obj_t *weather_screen;
static lv_obj_t *weather_city_label;
static lv_obj_t *weather_clock_label;
static lv_obj_t *weather_temp_label;
static lv_obj_t *weather_hero_icon;
static lv_obj_t *weather_text_label;
static lv_obj_t *weather_indoor_label;
static lv_obj_t *weather_indoor_icon;
static lv_obj_t *weather_feels_label;
static lv_obj_t *weather_humidity_label;
static lv_obj_t *weather_wind_label;
static lv_obj_t *weather_wind_icon;
static lv_obj_t *weather_uv_label;
static lv_obj_t *weather_param_icons[4];
static lv_obj_t *weather_daily_labels[UI_WEATHER_DAILY][5];
static lv_obj_t *weather_daily_icons[UI_WEATHER_DAILY];
static lv_obj_t *weather_daily_drops[UI_WEATHER_DAILY];
static lv_obj_t *weather_updated_label;
static lv_obj_t *weather_ip_label;
static lv_obj_t *weather_wifi_image;
static lv_obj_t *weather_battery_frame;
static lv_obj_t *weather_battery_fill;
static ui_weather_info_t current_weather;
static char current_weather_icon[8] = "";
static char current_daily_icons[UI_WEATHER_DAILY][8] = {""};
static int current_wind360 = -1;

/* Indoor readings come from the onboard environment service via
 * ui_update_environment; the weather page shows them for comparison. */
static bool weather_indoor_valid = false;
static float weather_indoor_temperature_c = 0.0f;
static float weather_indoor_humidity_percent = 0.0f;

typedef enum {
    WEATHER_KIND_SUN,
    WEATHER_KIND_PARTLY,
    WEATHER_KIND_CLOUDY,
    WEATHER_KIND_RAIN,
    WEATHER_KIND_SHOWER,
    WEATHER_KIND_THUNDER,
    WEATHER_KIND_SNOW,
    WEATHER_KIND_FOG,
    WEATHER_KIND_HAZE,
    WEATHER_KIND_WIND,
} weather_icon_kind_t;

static weather_icon_kind_t weather_icon_kind(const char *code)
{
    const int value = (code != NULL && code[0] != '\0') ? atoi(code) : 0;
    if(value == 100 || value == 150) return WEATHER_KIND_SUN;
    if(value == 101 || value == 102 || value == 151 || value == 152) {
        return WEATHER_KIND_PARTLY;
    }
    if((value >= 103 && value <= 104) || (value >= 153 && value <= 154)) {
        return WEATHER_KIND_CLOUDY;
    }
    if(value == 302 || value == 303) return WEATHER_KIND_THUNDER;
    if(value >= 300 && value < 400) return WEATHER_KIND_RAIN;
    if(value >= 400 && value < 500) return WEATHER_KIND_SNOW;
    if(value >= 500 && value <= 503) return WEATHER_KIND_FOG;
    if(value > 503 && value < 520) return WEATHER_KIND_HAZE;
    return WEATHER_KIND_WIND;
}

static const lv_image_dsc_t *const kWeatherHeroIcons[] = {
    &ui_weather_sun, &ui_weather_partly, &ui_weather_cloudy, &ui_weather_rain,
    &ui_weather_shower, &ui_weather_thunder, &ui_weather_snow, &ui_weather_fog,
    &ui_weather_haze, &ui_weather_wind,
};

static const lv_image_dsc_t *const kWeatherSmallIcons[] = {
    &ui_weather16_sun, &ui_weather16_partly, &ui_weather16_cloudy,
    &ui_weather16_rain, &ui_weather16_shower, &ui_weather16_thunder,
    &ui_weather16_snow, &ui_weather16_fog, &ui_weather16_haze,
    &ui_weather16_wind,
};

static const lv_image_dsc_t *weather_hero_icon_for(const char *code)
{
    return kWeatherHeroIcons[weather_icon_kind(code)];
}

static const lv_image_dsc_t *weather_small_icon_for(const char *code)
{
    return kWeatherSmallIcons[weather_icon_kind(code)];
}

/* 风从 wind360 吹来，箭头指向下风向（wind360 + 180），八方位取整。 */
static const lv_image_dsc_t *weather_wind_icon_for(int wind360)
{
    static const lv_image_dsc_t *const kArrows[8] = {
        &ui_weather_wind_n, &ui_weather_wind_ne, &ui_weather_wind_e,
        &ui_weather_wind_se, &ui_weather_wind_s, &ui_weather_wind_sw,
        &ui_weather_wind_w, &ui_weather_wind_nw,
    };
    if(wind360 < 0 || wind360 >= 360) return &ui_weather_wind;
    const int downwind = (wind360 + 180) % 360;
    return kArrows[((downwind + 22) / 45) % 8];
}

static void set_label_text(lv_obj_t *label, const char *text)
{
    if(label == NULL || !lv_obj_is_valid(label)) return;
    /* Skip invalidation on the monochrome panel when unchanged. */
    if(strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static void set_image_src(lv_obj_t *image, const lv_image_dsc_t *source)
{
    if(image == NULL || !lv_obj_is_valid(image)) return;
    if(lv_image_get_src(image) != source) lv_image_set_src(image, source);
}

static void refresh_weather_page(void)
{
    char line[64];
    if(current_weather.aqi_category[0] != '\0' &&
       strcmp(current_weather.aqi, "--") != 0) {
        snprintf(line, sizeof(line), "%s [%s] %s", current_weather.city,
                 current_weather.aqi_category, current_weather.aqi);
    } else {
        snprintf(line, sizeof(line), "%s", current_weather.city);
    }
    set_label_text(weather_city_label, line);
    set_label_text(weather_temp_label, current_weather.temp);
    /* 错误状态优先显示在现象位置（hero 卡片）。 */
    set_label_text(weather_text_label,
                   current_weather.status[0] ? current_weather.status
                                             : current_weather.text);
    if(strcmp(current_weather_icon, current_weather.icon) != 0) {
        snprintf(current_weather_icon, sizeof(current_weather_icon), "%s",
                 current_weather.icon);
        set_image_src(weather_hero_icon, weather_hero_icon_for(current_weather_icon));
    }
    if(weather_indoor_valid) {
        snprintf(line, sizeof(line), "室内 %.0f°C %.0f%%",
                 weather_indoor_temperature_c, weather_indoor_humidity_percent);
    } else {
        snprintf(line, sizeof(line), "室内 --");
    }
    set_label_text(weather_indoor_label, line);
    snprintf(line, sizeof(line), "体感 %s", current_weather.feels_like);
    set_label_text(weather_feels_label, line);
    snprintf(line, sizeof(line), "湿度 %s", current_weather.humidity);
    set_label_text(weather_humidity_label, line);
    set_label_text(weather_wind_label, current_weather.wind);
    if(current_wind360 != current_weather.wind360) {
        current_wind360 = current_weather.wind360;
        set_image_src(weather_wind_icon, weather_wind_icon_for(current_wind360));
    }
    snprintf(line, sizeof(line), "紫外线 %s", current_weather.uv_level);
    set_label_text(weather_uv_label, line);
    static const char *const day_names[UI_WEATHER_DAILY] = {
        "今天", "明天", "后天"};
    for(int index = 0; index < UI_WEATHER_DAILY; ++index) {
        const ui_weather_day_t *day = &current_weather.daily[index];
        set_label_text(weather_daily_labels[index][0], day_names[index]);
        set_label_text(weather_daily_labels[index][1], day->text_day);
        snprintf(line, sizeof(line), "%s/%s", day->high, day->low);
        set_label_text(weather_daily_labels[index][2], line);
        set_label_text(weather_daily_labels[index][3], day->precip);
        set_label_text(weather_daily_labels[index][4], day->lunar_day);
        if(strcmp(current_daily_icons[index], day->icon_day) != 0) {
            snprintf(current_daily_icons[index], sizeof(current_daily_icons[index]),
                     "%s", day->icon_day);
            set_image_src(weather_daily_icons[index],
                          weather_small_icon_for(current_daily_icons[index]));
        }
    }
    snprintf(line, sizeof(line), "%s 更新", current_weather.update_time);
    set_label_text(weather_updated_label, line);
    char ip[16];
    snprintf(line, sizeof(line), "%s",
             station_ip_text(ip, sizeof(ip)) ? ip : "--");
    set_label_text(weather_ip_label, line);
    set_image_src(weather_wifi_image,
                  current_wifi_state == UI_WIFI_CONNECTED ? &ui_weather_wifi
                                                          : &ui_weather_wifi_off);
    /* 图形电池：填充宽度按百分比。 */
    if(weather_battery_fill != NULL && lv_obj_is_valid(weather_battery_fill)) {
        int percent = current_battery_percent;
        if(!current_battery_valid || percent < 0) percent = 0;
        if(percent > 100) percent = 100;
        const int width = (20 * percent) / 100;
        if(lv_obj_get_width(weather_battery_fill) != width) {
            lv_obj_set_width(weather_battery_fill, width);
            if(percent > 0) lv_obj_remove_flag(weather_battery_fill, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(weather_battery_fill, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_show_weather(void)
{
    current_page = PAGE_WEATHER;
    sync_assistant_visibility();
    if(weather_screen != NULL) {
        clock_label = weather_clock_label;
        refresh_weather_page();
        lv_screen_load(weather_screen);
        ui_update_clock();
        return;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    weather_screen = screen;
    style_screen(screen);

    /* 顶栏：城市 + AQI 左，星期时钟右。 */
    weather_city_label = make_label(screen, "--", &ui_font_14_cjk, 18, 12);
    lv_obj_set_size(weather_city_label, 226, 20);
    lv_label_set_long_mode(weather_city_label, LV_LABEL_LONG_CLIP);

    weather_clock_label = make_label(screen, "--:--", &ui_font_14_cjk, 254, 12);
    lv_obj_set_size(weather_clock_label, 128, 20);
    lv_label_set_long_mode(weather_clock_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(weather_clock_label, LV_TEXT_ALIGN_RIGHT, 0);
    clock_label = weather_clock_label;

    /* 温度卡：40px 加粗温度 + 32px 天气图标 + 现象/状态 + 室内对比。 */
    lv_obj_t *hero = make_panel(screen, 16, 42, 172, 124);
    weather_temp_label = make_label(hero, "--", &ui_font_40_bold, 10, 6);
    lv_obj_set_size(weather_temp_label, 108, 46);
    lv_label_set_long_mode(weather_temp_label, LV_LABEL_LONG_CLIP);
    weather_hero_icon = lv_image_create(hero);
    lv_image_set_src(weather_hero_icon, &ui_weather_sun);
    lv_obj_set_pos(weather_hero_icon, 126, 8);
    weather_text_label = make_label(hero, "--", &ui_font_14_cjk, 10, 60);
    weather_indoor_icon = lv_image_create(hero);
    lv_image_set_src(weather_indoor_icon, &ui_weather_home);
    lv_obj_set_pos(weather_indoor_icon, 10, 92);
    weather_indoor_label = make_label(hero, "室内 --", &ui_font_14_cjk, 36, 94);

    /* 详情卡：体感/湿度/风/紫外线，四行带参数图标。 */
    lv_obj_t *detail = make_panel(screen, 204, 42, 180, 124);
    const lv_image_dsc_t *param_icons[] = {
        &ui_weather_thermometer, &ui_weather_drop, NULL, &ui_weather_uv};
    const int detail_y[] = {10, 41, 72, 103};
    for(int index = 0; index < 4; ++index) {
        if(param_icons[index] == NULL) continue;  /* 风行由风向箭头占位 */
        weather_param_icons[index] = lv_image_create(detail);
        lv_image_set_src(weather_param_icons[index], param_icons[index]);
        lv_obj_set_pos(weather_param_icons[index], 10, detail_y[index] - 1);
    }
    weather_wind_icon = lv_image_create(detail);
    lv_image_set_src(weather_wind_icon, &ui_weather_wind);
    lv_obj_set_pos(weather_wind_icon, 10, detail_y[2] - 1);
    weather_feels_label =
        make_label(detail, "--", &ui_font_14_cjk, 36, detail_y[0]);
    weather_humidity_label =
        make_label(detail, "--", &ui_font_14_cjk, 36, detail_y[1]);
    weather_wind_label =
        make_label(detail, "--", &ui_font_14_cjk, 36, detail_y[2]);
    weather_uv_label =
        make_label(detail, "--", &ui_font_14_cjk, 36, detail_y[3]);

    /* 预报卡：日期、现象、高低温、降水量与农历。 */
    lv_obj_t *forecast = make_panel(screen, 16, 176, 368, 86);
    for(int index = 0; index < UI_WEATHER_DAILY; ++index) {
        const int row_y = 9 + index * 27;
        weather_daily_icons[index] = lv_image_create(forecast);
        lv_image_set_src(weather_daily_icons[index], &ui_weather16_sun);
        lv_obj_set_pos(weather_daily_icons[index], 14, row_y);
        weather_daily_labels[index][0] =
            make_label(forecast, "--", &ui_font_14_cjk, 34, row_y);
        lv_obj_set_size(weather_daily_labels[index][0], 78, 20);
        lv_label_set_long_mode(weather_daily_labels[index][0], LV_LABEL_LONG_CLIP);
        weather_daily_labels[index][1] =
            make_label(forecast, "--", &ui_font_14_cjk, 114, row_y);
        lv_obj_set_size(weather_daily_labels[index][1], 58, 20);
        lv_label_set_long_mode(weather_daily_labels[index][1], LV_LABEL_LONG_CLIP);
        weather_daily_labels[index][2] =
            make_label(forecast, "--", &ui_font_14_cjk, 174, row_y);
        lv_obj_set_size(weather_daily_labels[index][2], 64, 20);
        lv_label_set_long_mode(weather_daily_labels[index][2], LV_LABEL_LONG_CLIP);
        weather_daily_drops[index] = lv_image_create(forecast);
        lv_image_set_src(weather_daily_drops[index], &ui_weather12_drop);
        lv_obj_set_pos(weather_daily_drops[index], 244, row_y + 3);
        weather_daily_labels[index][3] =
            make_label(forecast, "--", &ui_font_11_regular, 258, row_y + 1);
        lv_obj_set_size(weather_daily_labels[index][3], 26, 16);
        lv_label_set_long_mode(weather_daily_labels[index][3], LV_LABEL_LONG_CLIP);
        weather_daily_labels[index][4] =
            make_label(forecast, "--", &ui_font_14_cjk, 288, row_y);
        lv_obj_set_size(weather_daily_labels[index][4], 66, 20);
        lv_label_set_long_mode(weather_daily_labels[index][4], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(weather_daily_labels[index][4],
                                    LV_TEXT_ALIGN_RIGHT, 0);
    }

    /* 底栏：设备 IP、更新时间、图形电池与百分比。 */
    weather_ip_label = make_label(screen, "--", &ui_font_11_regular, 22, 267);
    lv_obj_set_size(weather_ip_label, 104, 16);
    lv_label_set_long_mode(weather_ip_label, LV_LABEL_LONG_CLIP);
    weather_updated_label = make_label(screen, "-- 更新", &ui_font_14_cjk, 132, 266);
    lv_obj_set_size(weather_updated_label, 100, 18);
    lv_label_set_long_mode(weather_updated_label, LV_LABEL_LONG_CLIP);
    weather_battery_frame = make_panel(screen, 300, 266, 26, 14);
    lv_obj_set_style_radius(weather_battery_frame, 2, 0);
    weather_battery_fill = lv_obj_create(weather_battery_frame);
    lv_obj_remove_flag(weather_battery_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(weather_battery_fill, 2, 2);
    lv_obj_set_size(weather_battery_fill, 0, 6);
    lv_obj_set_style_radius(weather_battery_fill, 0, 0);
    lv_obj_set_style_border_width(weather_battery_fill, 0, 0);
    lv_obj_set_style_pad_all(weather_battery_fill, 0, 0);
    lv_obj_set_style_bg_color(weather_battery_fill, COLOR_BLACK, 0);
    lv_obj_set_style_bg_opa(weather_battery_fill, LV_OPA_COVER, 0);
    lv_obj_t *battery_cap = lv_obj_create(screen);
    lv_obj_remove_flag(battery_cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(battery_cap, 327, 270);
    lv_obj_set_size(battery_cap, 3, 6);
    lv_obj_set_style_radius(battery_cap, 0, 0);
    lv_obj_set_style_border_width(battery_cap, 0, 0);
    lv_obj_set_style_pad_all(battery_cap, 0, 0);
    lv_obj_set_style_bg_color(battery_cap, COLOR_BLACK, 0);
    lv_obj_set_style_bg_opa(battery_cap, LV_OPA_COVER, 0);
    make_white_mask(screen, 334, 266, 28, 16);
    weather_battery_label = make_value_label(screen, &ui_font_11_regular,
                                             334, 266, 28, 16);

    refresh_weather_page();
    update_battery_labels();
    ui_update_clock();
    lv_screen_load(screen);
}

void ui_update_weather(const ui_weather_info_t *info)
{
    if(info == NULL) return;
    current_weather = *info;
    /* Defensive termination, mirroring ui_update_todos. */
    current_weather.city[sizeof(current_weather.city) - 1] = '\0';
    current_weather.temp[sizeof(current_weather.temp) - 1] = '\0';
    current_weather.text[sizeof(current_weather.text) - 1] = '\0';
    current_weather.icon[sizeof(current_weather.icon) - 1] = '\0';
    current_weather.feels_like[sizeof(current_weather.feels_like) - 1] = '\0';
    current_weather.humidity[sizeof(current_weather.humidity) - 1] = '\0';
    current_weather.wind[sizeof(current_weather.wind) - 1] = '\0';
    current_weather.temp_max[sizeof(current_weather.temp_max) - 1] = '\0';
    current_weather.temp_min[sizeof(current_weather.temp_min) - 1] = '\0';
    current_weather.aqi[sizeof(current_weather.aqi) - 1] = '\0';
    current_weather.aqi_category[sizeof(current_weather.aqi_category) - 1] = '\0';
    current_weather.uv_level[sizeof(current_weather.uv_level) - 1] = '\0';
    current_weather.update_time[sizeof(current_weather.update_time) - 1] = '\0';
    current_weather.status[sizeof(current_weather.status) - 1] = '\0';
    for(int index = 0; index < UI_WEATHER_DAILY; ++index) {
        current_weather.daily[index].date[
            sizeof(current_weather.daily[index].date) - 1] = '\0';
        current_weather.daily[index].text_day[
            sizeof(current_weather.daily[index].text_day) - 1] = '\0';
        current_weather.daily[index].icon_day[
            sizeof(current_weather.daily[index].icon_day) - 1] = '\0';
        current_weather.daily[index].precip[
            sizeof(current_weather.daily[index].precip) - 1] = '\0';
        current_weather.daily[index].lunar_day[
            sizeof(current_weather.daily[index].lunar_day) - 1] = '\0';
        current_weather.daily[index].high[
            sizeof(current_weather.daily[index].high) - 1] = '\0';
        current_weather.daily[index].low[
            sizeof(current_weather.daily[index].low) - 1] = '\0';
    }
    if(current_page == PAGE_WEATHER) refresh_weather_page();
}

void ui_show_computers(void)
{
    current_page = PAGE_COMPUTERS;
    sync_assistant_visibility();
    selected_computer = current_list_computer >= 0 ? current_list_computer : 0;
    clock_label = NULL;
    if(computers_screen != NULL) {
        update_computer_rows();
        lv_screen_load(computers_screen);
        return;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    computers_screen = screen;
    style_screen(screen);

    lv_obj_t *frame = make_panel(screen, 0, 0, 400, 300);
    lv_obj_set_style_radius(frame, 0, 0);

    lv_obj_t *header = make_panel(screen, 6, 7, 388, 44);
    make_label(header, "SELECT COMPUTER", &ui_font_18_regular, 14, 9);
    computer_found_label = make_label(header, "0 FOUND", &ui_font_11_regular,
                                      306, 15);
    lv_obj_set_width(computer_found_label, 68);
    lv_obj_set_style_text_align(computer_found_label, LV_TEXT_ALIGN_RIGHT, 0);

    for(int i = 0; i < COMPUTER_ROWS_VISIBLE; ++i) {
        int32_t row_y = 57 + i * 59;
        lv_obj_t *row = make_panel(screen, 6, row_y, 388, 53);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, computer_row_clicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        computer_rows[i] = row;
        /* Keep Latin at 18 px; use the existing native CJK glyphs for names. */
        static lv_font_t computer_name_font;
        computer_name_font = ui_font_18_regular;
        computer_name_font.fallback = &ui_font_14_cjk;
        computer_name_labels[i] = make_label(row, "",
                                             &computer_name_font, 14, 5);
        computer_state_labels[i] = make_label(row, "", &ui_font_11_regular, 14, 32);

        computer_current_labels[i] = make_label(row, "",
                                                &ui_font_11_regular, 314, 12);
    }

    lv_obj_t *detail_panel = make_panel(screen, 6, 234, 388, 60);
    make_label(detail_panel, "KEY  NEXT", &ui_font_11_regular, 14, 10);
    make_label(detail_panel, "BOOT CONNECT", &ui_font_11_regular, 145, 10);
    make_label(detail_panel, "HOLD KEY BACK", &ui_font_11_regular, 270, 10);

    lv_obj_t *remembered_label = make_label(
        detail_panel, "PAIRED PCS ARE REMEMBERED BY REPORTER ID",
        &ui_font_11_regular, 0, 38);
    lv_obj_set_width(remembered_label, 388);
    lv_obj_set_style_text_align(remembered_label, LV_TEXT_ALIGN_CENTER, 0);
    update_computer_rows();
    lv_screen_load(screen);
}

/* The STA IP is read straight from esp-netif, mirroring
 * ApiBalanceService::WifiReady(). Returns false when WiFi is down. */
#ifdef ESP_PLATFORM
static bool station_ip_text(char *buf, size_t len)
{
    wifi_ap_record_t ap_info = {0};
    if(esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) return false;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {0};
    if(netif == NULL || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK ||
       ip_info.ip.addr == 0) return false;
    snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    return true;
}
#else
/* Host simulator has no esp-netif; the About page shows a placeholder. */
static bool station_ip_text(char *buf, size_t len)
{
    (void)buf;
    (void)len;
    return false;
}
#endif

static lv_obj_t *about_ip_label;

static void refresh_about_ip(void)
{
    if(about_ip_label == NULL || !lv_obj_is_valid(about_ip_label)) return;
    char ip[16];
    char text[64];
    if(station_ip_text(ip, sizeof(ip))) {
        snprintf(text, sizeof(text), "IP 地址：%s（管理页 :8080）", ip);
    } else {
        snprintf(text, sizeof(text), "IP 地址：未连接");
    }
    /* Skip invalidation on the monochrome panel when unchanged. */
    if(strcmp(lv_label_get_text(about_ip_label), text) != 0) {
        lv_label_set_text(about_ip_label, text);
    }
}

/* This page is cached like the other primary pages. */
static lv_obj_t *about_screen;
static void ui_show_about(void)
{
    current_page = PAGE_ABOUT;
    if(about_screen == NULL) {
        about_screen = lv_obj_create(NULL);
        style_screen(about_screen);
        make_label(about_screen, "希娜 Syna  v1.0.0", &ui_font_14_cjk, 18, 16);
        make_label(about_screen, "关于作者：楠木", &ui_font_14_cjk, 18, 46);
        make_label(about_screen, "QQ/微信：157884200", &ui_font_14_cjk, 18, 70);
        make_label(about_screen, "github.com/nanmu-self", &ui_font_14_cjk, 18, 94);
        make_label(about_screen, "Waveshare_ESP32_RLCD", &ui_font_14_cjk, 18, 118);
        make_label(about_screen, "发布版本：仓库 Releases 页面", &ui_font_14_cjk, 18, 139);
        make_label(about_screen, "原创部分 MIT - 保留版权声明", &ui_font_14_cjk, 18, 163);
        make_label(about_screen, "基于 xiaozhi-esp32 二次开发", &ui_font_14_cjk, 18, 188);
        make_label(about_screen, "感谢小智、黑沐、LVGL及开源贡献者", &ui_font_14_cjk, 18, 213);
        about_ip_label = make_label(about_screen, "IP 地址：未连接",
                                    &ui_font_14_cjk, 18, 250);
        make_label(about_screen, "天气数据：和风天气", &ui_font_14_cjk, 18, 274);
    }
    refresh_about_ip();
    lv_screen_load(about_screen);
}

static void dismiss_signature(lv_timer_t *timer)
{
    lv_obj_t *panel = lv_timer_get_user_data(timer);
    lv_obj_delete(panel);
    lv_timer_delete(timer);
}

static void show_startup_signature(void)
{
    lv_obj_t *panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(panel, 400, 300);
    lv_obj_set_pos(panel, 0, 0);
    style_screen(panel);
    lv_obj_t *title = make_label(panel, "希娜 Syna · 楠木", &ui_font_28_brand, 0, 106);
    lv_obj_set_width(title, 400);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *version = make_label(panel, "v1.0.0", &ui_font_18_regular, 0, 157);
    lv_obj_set_width(version, 400);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, 0);
    lv_timer_create(dismiss_signature, 2000, panel);
}

void ui_toggle_page(void)
{
    if(current_page == PAGE_DASHBOARD) ui_show_performance();
    else if(current_page == PAGE_PERFORMANCE) ui_show_syna();
    else if(current_page == PAGE_SYNA) ui_show_weather();
    else if(current_page == PAGE_WEATHER) ui_show_about();
    else ui_show_dashboard();
}

void ui_select_computer(int direction)
{
    if(current_page != PAGE_COMPUTERS) {
        ui_show_computers();
        return;
    }

    if(computer_list_count <= 0) return;
    selected_computer =
        (selected_computer + direction + computer_list_count) % computer_list_count;
    if(selected_computer < computer_list_offset) computer_list_offset = selected_computer;
    if(selected_computer >= computer_list_offset + COMPUTER_ROWS_VISIBLE) {
        computer_list_offset = selected_computer - COMPUTER_ROWS_VISIBLE + 1;
    }
    update_computer_rows();
}

int ui_confirm_computer(void)
{
    if(current_page != PAGE_COMPUTERS || computer_list_count <= 0) return -1;

    current_list_computer = selected_computer;
    if(selected_computer < MOCK_COMPUTER_COUNT) current_computer = selected_computer;
    /* Dashboard values depend on the selected computer. Recreate this one
     * screen only when that selection actually changes. */
    lv_obj_t *old_dashboard = dashboard_screen;
    dashboard_screen = NULL;
    dashboard_clock_label = NULL;
    ui_show_dashboard();
    if(old_dashboard != NULL && old_dashboard != lv_screen_active()) {
        lv_obj_delete(old_dashboard);
    }
    return current_list_computer;
}

void ui_update_computer_list(const ui_computer_info_t *items, int count,
                             int current_index)
{
    if(count < 0) count = 0;
    if(count > UI_MAX_COMPUTERS) count = UI_MAX_COMPUTERS;
    computer_list_count = count;
    for(int i = 0; i < count; ++i) computer_list[i] = items[i];

    if(current_index >= 0 && current_index < count) current_list_computer = current_index;
    else if(count == 0) current_list_computer = -1;
    if(selected_computer < 0 || selected_computer >= count) {
        selected_computer = current_list_computer >= 0 ? current_list_computer : 0;
    }
    if(selected_computer < computer_list_offset) computer_list_offset = selected_computer;
    if(selected_computer >= computer_list_offset + COMPUTER_ROWS_VISIBLE) {
        computer_list_offset = selected_computer - COMPUTER_ROWS_VISIBLE + 1;
    }
    if(computer_list_count <= COMPUTER_ROWS_VISIBLE) computer_list_offset = 0;
    if(computers_screen != NULL) update_computer_rows();
}

void ui_update_clock(void)
{
    /* Keep the About page IP line fresh while the page is on screen. */
    if(current_page == PAGE_ABOUT) refresh_about_ip();

    if(clock_label == NULL || !lv_obj_is_valid(clock_label)) return;

    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if(local == NULL) return;

    char time_text[24];
    if(current_page == PAGE_WEATHER) {
        snprintf(time_text, sizeof(time_text), "%s %02d:%02d",
                 kWeekdayNames[local->tm_wday], local->tm_hour, local->tm_min);
    } else {
        strftime(time_text, sizeof(time_text), "%H:%M", local);
    }
    /* The screen only shows hours and minutes. Avoid invalidating a full-screen
     * monochrome render once per second when the visible text did not change. */
    if(strcmp(lv_label_get_text(clock_label), time_text) != 0) {
        lv_label_set_text(clock_label, time_text);
    }
}

void ui_update_environment(float temperature_c, float humidity_percent,
                           int battery_percent, bool environment_valid,
                           bool battery_valid)
{
    current_battery_percent = battery_percent;
    current_battery_valid = battery_valid;
    weather_indoor_valid = environment_valid;
    weather_indoor_temperature_c = temperature_c;
    weather_indoor_humidity_percent = humidity_percent;
    if(current_page == PAGE_WEATHER) refresh_weather_page();
    if(dashboard_temperature_label != NULL &&
       lv_obj_is_valid(dashboard_temperature_label)) {
        char value[12];
        if(environment_valid) snprintf(value, sizeof(value), "%.0f\xC2\xB0" "C", temperature_c);
        else snprintf(value, sizeof(value), "--");
        if(strcmp(lv_label_get_text(dashboard_temperature_label), value) != 0) {
            lv_label_set_text(dashboard_temperature_label, value);
        }
    }

    if(dashboard_humidity_label != NULL && lv_obj_is_valid(dashboard_humidity_label)) {
        char value[8];
        if(environment_valid) snprintf(value, sizeof(value), "%.0f%%", humidity_percent);
        else snprintf(value, sizeof(value), "--");
        if(strcmp(lv_label_get_text(dashboard_humidity_label), value) != 0) {
            lv_label_set_text(dashboard_humidity_label, value);
        }
    }

    update_battery_labels();
}

void ui_update_wifi_state(ui_wifi_state_t state)
{
    /* UpdateStatusBar runs once per second. Reassigning the same image source
     * invalidates all three page trees even when nothing changed. On the RLCD
     * this eventually leaves the main task spending long enough in
     * lv_inv_area to trip the watchdog, freezing the clock and buttons first.
     * Page constructors already use current_wifi_state, so unchanged states
     * require no LVGL work here. */
    if(current_wifi_state == state) return;
    current_wifi_state = state;
    const lv_image_dsc_t *asset = wifi_asset_for(state);
    if(dashboard_wifi_status_image != NULL &&
       lv_obj_is_valid(dashboard_wifi_status_image)) {
        lv_image_set_src(dashboard_wifi_status_image, asset);
    }
    if(performance_wifi_status_image != NULL &&
       lv_obj_is_valid(performance_wifi_status_image)) {
        lv_image_set_src(performance_wifi_status_image, asset);
    }
    if(syna_wifi_status_image != NULL && lv_obj_is_valid(syna_wifi_status_image)) {
        lv_image_set_src(syna_wifi_status_image, asset);
    }
}

void ui_update_pc_connected(bool connected)
{
    /* Reporter snapshots arrive continuously. Reassigning the same source to
     * three page trees invalidates them all and can eventually wedge LVGL. */
    if(current_pc_connected == connected) return;
    current_pc_connected = connected;
    const lv_image_dsc_t *asset = pc_asset_for(connected);
    if(dashboard_pc_status_image != NULL && lv_obj_is_valid(dashboard_pc_status_image)) {
        lv_image_set_src(dashboard_pc_status_image, asset);
    }
    if(performance_pc_status_image != NULL && lv_obj_is_valid(performance_pc_status_image)) {
        lv_image_set_src(performance_pc_status_image, asset);
    }
    if(syna_pc_status_image != NULL && lv_obj_is_valid(syna_pc_status_image)) {
        lv_image_set_src(syna_pc_status_image, asset);
    }
}

void ui_update_syna_conversation(const char *user_text,
                                 const char *assistant_reply)
{
    if(user_text != NULL) {
        snprintf(current_syna_user, sizeof(current_syna_user), "%s", user_text);
    }
    if(assistant_reply != NULL) {
        snprintf(current_syna_assistant, sizeof(current_syna_assistant), "%s",
                 assistant_reply);
    }
    refresh_syna_conversation();
}

void ui_update_api_balance(const char *provider, const char *balance)
{
    snprintf(current_api_provider, sizeof(current_api_provider), "%s",
             provider != NULL && provider[0] ? provider : "API");
    snprintf(current_api_balance, sizeof(current_api_balance), "%s",
             balance != NULL && balance[0] ? balance : "--");
    if(syna_api_provider_label != NULL && lv_obj_is_valid(syna_api_provider_label)) {
        lv_label_set_text(syna_api_provider_label, current_api_provider);
    }
    if(syna_api_balance_label != NULL && lv_obj_is_valid(syna_api_balance_label)) {
        lv_label_set_text(syna_api_balance_label, current_api_balance);
    }
}

void ui_update_todos(const ui_todo_item_t *items, int count)
{
    if(count < 0) count = 0;
    if(count > UI_MAX_TODOS) count = UI_MAX_TODOS;
    if(items == NULL && count > 0) return;
    current_todo_count = count;
    for(int index = 0; index < count; ++index) {
        current_todos[index] = items[index];
        current_todos[index].text[sizeof(current_todos[index].text) - 1] = '\0';
    }
    refresh_syna_todos();
}

void ui_update_agent_state(const char *state)
{
    if(state == NULL || state[0] == '\0') state = "OFFLINE";
    const bool state_changed = strcmp(current_agent_state, state) != 0;
    if(!state_changed) return;
    snprintf(current_agent_state, sizeof(current_agent_state), "%s", state);
    if(dashboard_agent_status_label != NULL &&
       lv_obj_is_valid(dashboard_agent_status_label)) {
        char status_text[40];
        snprintf(status_text, sizeof(status_text), "● %s",
                 agent_state_text(current_agent_state));
        lv_label_set_text(dashboard_agent_status_label, status_text);
        if(strcmp(current_agent_state, "DONE") != 0) {
            lv_obj_remove_flag(dashboard_agent_status_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(agent_done_blink_timer != NULL && state_changed) {
        if(strcmp(current_agent_state, "DONE") == 0) {
            lv_timer_reset(agent_done_blink_timer);
            lv_timer_resume(agent_done_blink_timer);
        }
        else {
            lv_timer_pause(agent_done_blink_timer);
            if(dashboard_agent_status_label != NULL &&
               lv_obj_is_valid(dashboard_agent_status_label)) {
                lv_obj_remove_flag(dashboard_agent_status_label, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void ui_update_quota_provider(const char *name)
{
    if(dashboard_quota_source_value == NULL ||
       !lv_obj_is_valid(dashboard_quota_source_value)) return;
    lv_label_set_text(dashboard_quota_source_value,
                      (name != NULL && name[0] != '\0') ? name : "未知");
}

// 额度重置倒计时（相对秒数，来自 Reporter，不依赖板子时钟基准）：
// “31m” / “9h” / “2d9h”；未知为空。
static void format_quota_countdown(char *buffer, size_t size, int32_t resets_in)
{
    buffer[0] = '\0';
    if(resets_in <= 0) return;
    int64_t diff = resets_in;
    if(diff < 60) {
        snprintf(buffer, size, "<1m");
        return;
    }
    if(diff < 60 * 60) {
        snprintf(buffer, size, "%dm", (int)(diff / 60));
        return;
    }
    int64_t days = diff / (24 * 60 * 60);
    if(days >= 10) {
        snprintf(buffer, size, "%dd", (int)days);
        return;
    }
    int hours = (int)((diff % (24 * 60 * 60)) / (60 * 60));
    if(days >= 1) {
        snprintf(buffer, size, "%dd%dh", (int)days, hours);
        return;
    }
    snprintf(buffer, size, "%dh", (int)(diff / (60 * 60)));
}

void ui_update_codex_quota(int short_remaining_percent, int week_remaining_percent,
                           int month_remaining_percent, bool connected, bool stale,
                           int32_t short_resets_in, int32_t week_resets_in,
                           int32_t month_resets_in)
{
    const int values[3] = {short_remaining_percent, week_remaining_percent,
                           month_remaining_percent};
    const int32_t resets[3] = {short_resets_in, week_resets_in, month_resets_in};
    for(int index = 0; index < 3; ++index) {
        const bool valid = connected && values[index] >= 0 && values[index] <= 100;
        char text[8];
        if(valid) snprintf(text, sizeof(text), "%s%d%%", stale ? "~" : "", values[index]);
        else snprintf(text, sizeof(text), "--");
        if(dashboard_quota_percent[index] != NULL &&
           lv_obj_is_valid(dashboard_quota_percent[index])) {
            lv_label_set_text(dashboard_quota_percent[index], text);
        }
        if(dashboard_quota_fill[index] != NULL &&
           lv_obj_is_valid(dashboard_quota_fill[index])) {
            if(valid && values[index] > 0) {
                lv_obj_set_width(dashboard_quota_fill[index], (38 * values[index]) / 100);
                lv_obj_remove_flag(dashboard_quota_fill[index], LV_OBJ_FLAG_HIDDEN);
            }
            else {
                lv_obj_add_flag(dashboard_quota_fill[index], LV_OBJ_FLAG_HIDDEN);
            }
        }
        char countdown[40];
        format_quota_countdown(countdown, sizeof(countdown),
                               connected ? resets[index] : -1);
        if(dashboard_quota_countdown[index] != NULL &&
           lv_obj_is_valid(dashboard_quota_countdown[index])) {
            if(countdown[0] != '\0') {
                lv_label_set_text(dashboard_quota_countdown[index], countdown);
                lv_obj_remove_flag(dashboard_quota_countdown[index], LV_OBJ_FLAG_HIDDEN);
            }
            else {
                lv_obj_add_flag(dashboard_quota_countdown[index], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

static void format_media_time(char *buffer, size_t size, int seconds, bool valid)
{
    if(!valid || seconds < 0) {
        snprintf(buffer, size, "--:--");
        return;
    }
    snprintf(buffer, size, "%02d:%02d", seconds / 60, seconds % 60);
}

void ui_update_media(bool available, const char *status, const char *title,
                     const char *artist, int position_seconds,
                     int duration_seconds, const char *lyric)
{
    const bool playing = available && status != NULL && strcmp(status, "playing") == 0;
    const bool paused = available && status != NULL && strcmp(status, "paused") == 0;
    if(dashboard_media_status_label != NULL) {
        lv_label_set_text(dashboard_media_status_label,
                          playing ? "正在播放" : (paused ? "已暂停" : "未播放"));
    }
    if(dashboard_media_title_label != NULL) {
        lv_label_set_text(dashboard_media_title_label,
                          available && title != NULL && title[0] ? title : "网易云音乐");
    }
    if(dashboard_media_artist_label != NULL) {
        lv_label_set_text(dashboard_media_artist_label,
                          available && artist != NULL && artist[0] ? artist : "--");
    }
    if(dashboard_media_lyric_label != NULL) {
        lv_label_set_text(dashboard_media_lyric_label,
                          available && lyric != NULL && lyric[0] ? lyric : "暂无歌词");
    }
    char position_text[12];
    char duration_text[12];
    const bool timeline_valid = available && duration_seconds > 0;
    format_media_time(position_text, sizeof(position_text), position_seconds, timeline_valid);
    format_media_time(duration_text, sizeof(duration_text), duration_seconds, timeline_valid);
    if(dashboard_media_position_label != NULL) {
        lv_label_set_text(dashboard_media_position_label, position_text);
    }
    if(dashboard_media_duration_label != NULL) {
        lv_label_set_text(dashboard_media_duration_label, duration_text);
    }
    if(dashboard_media_progress_knob != NULL) {
        int progress = timeline_valid ? (position_seconds * 182) / duration_seconds : 0;
        if(progress < 0) progress = 0;
        if(progress > 182) progress = 182;
        lv_obj_set_x(dashboard_media_progress_knob, 191 + progress);
    }
}

void ui_update_performance(float cpu_percent, float memory_percent,
                           float gpu_percent, bool gpu_valid,
                           float disk_percent, float cpu_temperature_c,
                           bool cpu_temperature_valid,
                           float gpu_temperature_c,
                           bool gpu_temperature_valid,
                           int latency_ms, float upload_mb_per_second,
                           float download_mb_per_second, bool connected)
{
    current_performance_state.cpu_percent = cpu_percent;
    current_performance_state.memory_percent = memory_percent;
    current_performance_state.gpu_percent = gpu_percent;
    current_performance_state.gpu_valid = gpu_valid;
    current_performance_state.disk_percent = disk_percent;
    current_performance_state.cpu_temperature_c = cpu_temperature_c;
    current_performance_state.cpu_temperature_valid = cpu_temperature_valid;
    current_performance_state.gpu_temperature_c = gpu_temperature_c;
    current_performance_state.gpu_temperature_valid = gpu_temperature_valid;
    current_performance_state.latency_ms = latency_ms;
    current_performance_state.upload_mb_per_second = upload_mb_per_second;
    current_performance_state.download_mb_per_second = download_mb_per_second;
    current_performance_state.connected = connected;
    if(performance_screen == NULL || !lv_obj_is_valid(performance_screen)) return;

    const float values[4] = {cpu_percent, memory_percent, gpu_percent, disk_percent};
    for(int index = 0; index < 4; ++index) {
        bool valid = connected && (index != 2 || gpu_valid);
        int percent = valid ? (int)(values[index] + 0.5f) : 0;
        if(percent < 0) percent = 0;
        if(percent > 100) percent = 100;
        char text[8];
        if(valid) snprintf(text, sizeof(text), "%d%%", percent);
        else snprintf(text, sizeof(text), "--");
        lv_label_set_text(performance_usage_labels[index], text);
        lv_obj_set_width(performance_usage_fills[index], (150 * percent) / 100);
    }

    char text[20];
    if(connected && cpu_temperature_valid)
        snprintf(text, sizeof(text), "%.0f\xC2\xB0" "C", cpu_temperature_c);
    else snprintf(text, sizeof(text), "--");
    lv_label_set_text(performance_temperature_labels[0], text);

    if(connected && gpu_temperature_valid)
        snprintf(text, sizeof(text), "%.0f\xC2\xB0" "C", gpu_temperature_c);
    else snprintf(text, sizeof(text), "--");
    lv_label_set_text(performance_temperature_labels[1], text);

    if(connected) snprintf(text, sizeof(text), "%d ms", latency_ms);
    else snprintf(text, sizeof(text), "--");
    lv_label_set_text(performance_latency_label, text);

    if(connected) snprintf(text, sizeof(text), "%.1f MB/s", upload_mb_per_second);
    else snprintf(text, sizeof(text), "--");
    lv_label_set_text(performance_upload_label, text);

    if(connected) snprintf(text, sizeof(text), "%.1f MB/s", download_mb_per_second);
    else snprintf(text, sizeof(text), "--");
    lv_label_set_text(performance_download_label, text);

    if(performance_network_chart != NULL && performance_network_series != NULL &&
       lv_obj_is_valid(performance_network_chart)) {
        lv_chart_set_next_value(
            performance_network_chart,
            performance_network_series,
            connected ? network_chart_value(upload_mb_per_second,
                                              download_mb_per_second) : 0);
    }
    ui_update_pc_connected(connected);
}

bool ui_is_computer_page(void)
{
    return current_page == PAGE_COMPUTERS;
}

bool ui_is_syna_page(void)
{
    return current_page == PAGE_SYNA;
}

void ui_init(void)
{
    ui_show_dashboard();
    show_startup_signature();
    agent_done_blink_timer = lv_timer_create(agent_done_blink_cb, 500, NULL);
    lv_timer_pause(agent_done_blink_timer);
}

static void update_computer_rows(void)
{
    if(computer_found_label != NULL) {
        char found[20];
        snprintf(found, sizeof(found), "%d FOUND", computer_list_count);
        lv_label_set_text(computer_found_label, found);
    }
    for(int i = 0; i < COMPUTER_ROWS_VISIBLE; ++i) {
        const int item_index = computer_list_offset + i;
        const bool visible = item_index < computer_list_count;
        if(!visible) {
            lv_obj_add_flag(computer_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(computer_rows[i], LV_OBJ_FLAG_HIDDEN);
        const bool selected = item_index == selected_computer;
        const ui_computer_info_t *item = &computer_list[item_index];
        char state[48];
        snprintf(state, sizeof(state), "%s  AGENT %s",
                 item->online ? "ONLINE" : "OFFLINE", item->agent_state);
        lv_label_set_text(computer_name_labels[i], item->name);
        lv_label_set_text(computer_state_labels[i], state);
        lv_label_set_text(computer_current_labels[i],
                          item_index == current_list_computer ? "CURRENT" : "");

        /* White-on-black antialiased glyphs lose strokes after 1-bit
         * quantization. Keep the same rounded-card language as the dashboard,
         * and show selection with a bold black outline instead. */
        lv_obj_set_style_bg_color(computer_rows[i], COLOR_WHITE, 0);
        lv_obj_set_style_border_width(computer_rows[i], selected ? 3 : 1, 0);
        lv_obj_set_style_text_color(computer_rows[i], COLOR_BLACK, 0);
        lv_obj_set_style_text_color(computer_name_labels[i], COLOR_BLACK, 0);
        lv_obj_set_style_text_color(computer_state_labels[i], COLOR_BLACK, 0);
        lv_obj_set_style_text_color(computer_current_labels[i], COLOR_BLACK, 0);
    }
}

static void computer_row_clicked(lv_event_t *event)
{
    selected_computer = computer_list_offset +
                        (int)(uintptr_t)lv_event_get_user_data(event);
    update_computer_rows();
}
