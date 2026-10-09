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

#ifndef LV_ATTRIBUTE_ui_weather_compass
#define LV_ATTRIBUTE_ui_weather_compass
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_ui_weather_compass
uint8_t ui_weather_compass_map[] = {
    0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xfc,
    0x0f, 0xc0, 0xf8, 0x07, 0xc0, 0xe1, 0xe1, 0xc0, 0xe7, 0xb9, 0xc0, 0xcf,
    0xbc, 0xc0, 0x8f, 0x1c, 0x40, 0x9f, 0x1e, 0x40, 0x9e, 0x0e, 0x40, 0x9e,
    0x0e, 0x40, 0x9c, 0xe6, 0x40, 0x8d, 0xf4, 0x40, 0xcf, 0xfc, 0xc0, 0xe7,
    0xf9, 0xc0, 0xe1, 0xe1, 0xc0, 0xf8, 0x07, 0xc0, 0xfc, 0x0f, 0xc0, 0xff,
    0xff, 0xc0,
};

const lv_image_dsc_t ui_weather_compass = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_I1,
    .flags = 0,
    .w = 18,
    .h = 18,
    .stride = 3,
    .reserved_2 = 0,
  },
  .data_size = sizeof(ui_weather_compass_map),
  .data = ui_weather_compass_map,
  .reserved = NULL,
};
