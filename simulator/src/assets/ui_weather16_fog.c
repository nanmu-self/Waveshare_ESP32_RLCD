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

#ifndef LV_ATTRIBUTE_ui_weather16_fog
#define LV_ATTRIBUTE_ui_weather16_fog
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_ui_weather16_fog
uint8_t ui_weather16_fog_map[] = {
    0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x3f, 0xfc, 0x1f,
    0xe0, 0x07, 0xc0, 0x03, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x03, 0xe0, 0x07,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x01, 0xff, 0xff,
    0xff, 0xff, 0xf0, 0x07,
};

const lv_image_dsc_t ui_weather16_fog = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_I1,
    .flags = 0,
    .w = 16,
    .h = 16,
    .stride = 2,
    .reserved_2 = 0,
  },
  .data_size = sizeof(ui_weather16_fog_map),
  .data = ui_weather16_fog_map,
  .reserved = NULL,
};
