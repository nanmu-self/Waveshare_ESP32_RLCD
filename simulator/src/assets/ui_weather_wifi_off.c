/* Syna UI 天气图标（单色 I1），由 assets/generate_weather_icons.py 程序化生成。
 * 版权 (c) 2026 黑沐. MIT；图标为项目原创素材。
 */

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#elif defined(LV_LVGL_H_INCLUDE_SYSTEM)
#include <lvgl.h>
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_ui_weather_wifi_off
#define LV_ATTRIBUTE_ui_weather_wifi_off
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_ui_weather_wifi_off
uint8_t ui_weather_wifi_off_map[] = {
    0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0xf9, 0xf0, 0xff,
    0xff, 0xf0, 0xff, 0x07, 0xb0, 0xfe, 0x02, 0x10, 0xff, 0x74, 0x70, 0xff,
    0xf0, 0xf0, 0xff, 0xe3, 0xf0, 0xff, 0x87, 0xf0, 0xfe, 0x1f, 0xf0, 0xfc,
    0x0f, 0xf0, 0xf0, 0x07, 0xf0, 0xe3, 0x07, 0xf0, 0x87, 0x07, 0xf0, 0xdf,
    0x8f, 0xf0,
};

const lv_image_dsc_t ui_weather_wifi_off = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_I1,
    .flags = 0,
    .w = 20,
    .h = 14,
    .stride = 3,
    .reserved_2 = 0,
  },
  .data_size = sizeof(ui_weather_wifi_off_map),
  .data = ui_weather_wifi_off_map,
  .reserved = NULL,
};
