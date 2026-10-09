#!/usr/bin/env python3
# Copyright (c) 2026 黑沐. Generator: MIT; generated icon data: Syna UI (MIT).
"""天气页 1-bit 图标生成器。

用 PIL 程序化绘制单色图标（黑形状/白底，无抗锯齿），生成与 LVGLImage.py
完全一致的 I1 C 数组（8 字节黑白调色板 + 1bpp 位图），双写模拟器与固件
overlay。图标是 const 数组，自然存放在 Flash，不占用 RAM。
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
OUTPUTS = [
    HERE.parent / 'src' / 'assets',
    HERE.parents[1] / 'xiaozhi' / 'overlay' / 'main' / 'boards' / 'waveshare' / 'esp32-s3-rlcd-4.2',
]
BLACK = 0
WHITE = 1


def canvas(size, height=None):
    image = Image.new('1', (size, height or size), WHITE)
    return image, ImageDraw.Draw(image)


def draw_sun(draw, cx, cy, radius, ray_start, ray_end):
    draw.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=BLACK)
    for index in range(8):
        angle = index * 45.0
        dx = math.cos(math.radians(angle))
        dy = math.sin(math.radians(angle))
        draw.line((cx + dx * ray_start, cy + dy * ray_start,
                   cx + dx * ray_end, cy + dy * ray_end), fill=BLACK, width=2)


def draw_cloud(draw, cx, bottom, scale=1.0):
    """实心云：三个重叠圆 + 底部矩形。cx 为中心，bottom 为云底 y。"""
    w = int(6 * scale)
    draw.ellipse((cx - 10 * scale, bottom - 9 * scale, cx - 2 * scale, bottom - 1 * scale), fill=BLACK)
    draw.ellipse((cx - 5 * scale, bottom - 13 * scale, cx + 5 * scale, bottom - 3 * scale), fill=BLACK)
    draw.ellipse((cx + 2 * scale, bottom - 9 * scale, cx + 10 * scale, bottom - 1 * scale), fill=BLACK)
    draw.rectangle((cx - 8 * scale, bottom - 6 * scale, cx + 8 * scale, bottom - 1 * scale), fill=BLACK)


def icon_sun():
    image, draw = canvas(32)
    draw_sun(draw, 16, 16, 6, 10, 14)
    return image


def icon_partly():
    image, draw = canvas(32)
    draw_sun(draw, 11, 11, 5, 8, 12)
    draw_cloud(draw, 18, 27, 1.0)
    return image


def icon_cloudy():
    image, draw = canvas(32)
    draw_cloud(draw, 11, 16, 0.8)
    draw_cloud(draw, 17, 28, 1.1)
    return image


def draw_rain(draw, drops):
    draw_cloud(draw, 16, 19, 1.1)
    for offset_x, offset_y in drops:
        draw.line((offset_x, offset_y, offset_x - 2, offset_y + 5), fill=BLACK, width=2)


def icon_rain():
    image, draw = canvas(32)
    draw_rain(draw, [(11, 23), (17, 25), (23, 23)])
    return image


def icon_shower():
    image, draw = canvas(32)
    draw_rain(draw, [(10, 23), (15, 26), (20, 23), (24, 26)])
    return image


def icon_thunder():
    image, draw = canvas(32)
    draw_cloud(draw, 16, 18, 1.1)
    draw.line((17, 20, 13, 25, 17, 25, 13, 31), fill=BLACK, width=2)
    return image


def icon_snow():
    image, draw = canvas(32)
    draw_cloud(draw, 16, 19, 1.1)
    for cx, cy in [(11, 24), (17, 27), (23, 24)]:
        draw.ellipse((cx - 1, cy - 1, cx + 1, cy + 1), fill=BLACK)
    return image


def icon_fog():
    image, draw = canvas(32)
    draw_cloud(draw, 16, 15, 1.0)
    draw.line((5, 21, 27, 21), fill=BLACK, width=2)
    draw.line((8, 26, 24, 26), fill=BLACK, width=2)
    return image


def icon_haze():
    image, draw = canvas(32)
    draw_sun(draw, 16, 12, 5, 8, 11)
    draw.line((4, 23, 28, 23), fill=BLACK, width=2)
    draw.line((8, 28, 24, 28), fill=BLACK, width=2)
    return image


def icon_wind():
    image, draw = canvas(32)
    draw.line((4, 11, 21, 11), fill=BLACK, width=2)
    draw.arc((18, 7, 26, 15), start=-90, end=90, fill=BLACK, width=2)
    draw.line((4, 18, 25, 18), fill=BLACK, width=2)
    draw.arc((22, 14, 30, 22), start=-90, end=90, fill=BLACK, width=2)
    draw.line((4, 25, 17, 25), fill=BLACK, width=2)
    draw.arc((14, 21, 22, 29), start=-90, end=90, fill=BLACK, width=2)
    return image


def icon_thermometer():
    image, draw = canvas(18)
    draw.rectangle((7, 1, 10, 10), fill=BLACK)
    draw.ellipse((5, 9, 12, 16), fill=BLACK)
    draw.ellipse((7, 11, 10, 14), fill=WHITE)
    return image


def icon_drop():
    image, draw = canvas(18)
    draw.polygon([(9, 1), (3, 10), (9, 16), (15, 10)], fill=BLACK)
    draw.ellipse((3, 6, 15, 16), fill=BLACK)
    return image


def icon_compass():
    image, draw = canvas(18)
    draw.ellipse((1, 1, 16, 16), outline=BLACK, width=2)
    draw.polygon([(9, 4), (6, 11), (9, 9), (12, 11)], fill=BLACK)
    return image


def icon_home():
    image, draw = canvas(18)
    draw.polygon([(9, 2), (1, 9), (3, 9), (3, 15), (15, 15), (15, 9), (17, 9)], fill=BLACK)
    draw.rectangle((7, 10, 11, 15), fill=WHITE)
    return image




# ---------- 16 px 预报行小图标（细节简化，保证小尺寸可辨认） ----------

def small16(painter):
    return painter()


def icon16_sun():
    image, draw = canvas(16)
    draw.ellipse((5, 5, 10, 10), fill=BLACK)
    for angle, (sx, sy, ex, ey) in {
        0: (11, 7, 14, 7), 180: (1, 7, 4, 7),
        90: (7, 1, 7, 4), 270: (7, 11, 7, 14)}.items():
        draw.line((sx, sy, ex, ey), fill=BLACK, width=1)
    return image


def icon16_partly():
    image, draw = canvas(16)
    draw.ellipse((2, 2, 8, 8), fill=BLACK)
    draw_cloud(draw, 9, 15, 0.55)
    return image


def icon16_cloudy():
    image, draw = canvas(16)
    draw_cloud(draw, 8, 14, 0.7)
    return image


def icon16_rain():
    image, draw = canvas(16)
    draw_cloud(draw, 8, 10, 0.7)
    draw.line((5, 12, 4, 15), fill=BLACK, width=1)
    draw.line((10, 12, 9, 15), fill=BLACK, width=1)
    return image


def icon16_shower():
    image, draw = canvas(16)
    draw_cloud(draw, 8, 10, 0.7)
    draw.line((4, 12, 3, 15), fill=BLACK, width=1)
    draw.line((8, 13, 7, 16), fill=BLACK, width=1)
    draw.line((11, 12, 10, 15), fill=BLACK, width=1)
    return image


def icon16_thunder():
    image, draw = canvas(16)
    draw_cloud(draw, 8, 10, 0.7)
    draw.line((9, 11, 7, 13, 9, 13, 7, 16), fill=BLACK, width=1)
    return image


def icon16_snow():
    image, draw = canvas(16)
    draw_cloud(draw, 8, 10, 0.7)
    draw.point((5, 13), fill=BLACK)
    draw.point((10, 14), fill=BLACK)
    return image


def icon16_fog():
    image, draw = canvas(16)
    draw_cloud(draw, 8, 8, 0.6)
    draw.line((2, 12, 14, 12), fill=BLACK, width=1)
    draw.line((4, 15, 12, 15), fill=BLACK, width=1)
    return image


def icon16_haze():
    image, draw = canvas(16)
    draw.ellipse((5, 2, 11, 8), fill=BLACK)
    draw.line((2, 12, 14, 12), fill=BLACK, width=1)
    draw.line((4, 15, 12, 15), fill=BLACK, width=1)
    return image


def icon16_wind():
    image, draw = canvas(16)
    draw.line((1, 5, 11, 5), fill=BLACK, width=1)
    draw.arc((9, 2, 14, 8), start=-90, end=90, fill=BLACK, width=1)
    draw.line((1, 10, 12, 10), fill=BLACK, width=1)
    draw.arc((10, 7, 15, 13), start=-90, end=90, fill=BLACK, width=1)
    return image


def icon_uv():
    """紫外线：太阳 + 长射线（辐射强度语义）。"""
    image, draw = canvas(18)
    draw.ellipse((6, 6, 11, 11), fill=BLACK)
    for index in range(8):
        angle = index * 45.0
        dx = math.cos(math.radians(angle))
        dy = math.sin(math.radians(angle))
        draw.line((8.5 + dx * 5, 8.5 + dy * 5, 8.5 + dx * 8, 8.5 + dy * 8),
                  fill=BLACK, width=1)
    return image


def icon12_drop():
    """预报行 12px 降水量水滴。"""
    image, draw = canvas(12)
    draw.polygon([(6, 0), (1, 7), (6, 11), (11, 7)], fill=BLACK)
    draw.ellipse((1, 4, 11, 11), fill=BLACK)
    return image

FORECAST_ICONS = {
    'weather16_sun': icon16_sun,
    'weather16_partly': icon16_partly,
    'weather16_cloudy': icon16_cloudy,
    'weather16_rain': icon16_rain,
    'weather16_shower': icon16_shower,
    'weather16_thunder': icon16_thunder,
    'weather16_snow': icon16_snow,
    'weather16_fog': icon16_fog,
    'weather16_haze': icon16_haze,
    'weather16_wind': icon16_wind,
}


# ---------- 8 方位风向箭头（风从该方向吹来，箭头指向下风向） ----------

def draw_arrow(draw, angle_deg, cx=9.0, cy=9.0, shaft=5.0, head=4.5, width=2):
    # angle：风的来向方位角（0=北风从北来，箭头向南）。屏幕坐标 y 向下。
    import math
    rad = math.radians(angle_deg)
    dx, dy = math.sin(rad), -math.cos(rad)  # 下风向单位向量
    sx, sy = cx - dx * shaft, cy - dy * shaft
    ex, ey = cx + dx * shaft, cy + dy * shaft
    draw.line((sx, sy, ex, ey), fill=BLACK, width=width)
    left = (ex - dx * head - dy * head * 0.6, ey - dy * head + dx * head * 0.6)
    right = (ex - dx * head + dy * head * 0.6, ey - dy * head - dx * head * 0.6)
    draw.polygon([(ex, ey), left, right], fill=BLACK)


def make_arrow_icon(angle_deg):
    def painter():
        image, draw = canvas(18)
        draw_arrow(draw, angle_deg)
        return image
    return painter


WIND_ARROWS = {
    'weather_wind_n': make_arrow_icon(0),
    'weather_wind_ne': make_arrow_icon(45),
    'weather_wind_e': make_arrow_icon(90),
    'weather_wind_se': make_arrow_icon(135),
    'weather_wind_s': make_arrow_icon(180),
    'weather_wind_sw': make_arrow_icon(225),
    'weather_wind_w': make_arrow_icon(270),
    'weather_wind_nw': make_arrow_icon(315),
}


# ---------- WiFi 信号（两态） ----------

def icon_wifi_on():
    image, draw = canvas(20, 14)
    draw.arc((2, -2, 18, 14), start=235, end=305, fill=BLACK, width=2)
    draw.arc((5, 2, 15, 12), start=235, end=305, fill=BLACK, width=2)
    draw.ellipse((8, 9, 12, 13), fill=BLACK)
    return image


def icon_wifi_off():
    image = icon_wifi_on()
    draw = ImageDraw.Draw(image)
    draw.line((2, 12, 18, 2), fill=BLACK, width=2)
    return image

WEATHER_ICONS = {
    'weather_sun': icon_sun,
    'weather_partly': icon_partly,
    'weather_cloudy': icon_cloudy,
    'weather_rain': icon_rain,
    'weather_shower': icon_shower,
    'weather_thunder': icon_thunder,
    'weather_snow': icon_snow,
    'weather_fog': icon_fog,
    'weather_haze': icon_haze,
    'weather_wind': icon_wind,
}

PARAM_ICONS = {
    'weather_thermometer': icon_thermometer,
    'weather_drop': icon_drop,
    'weather_compass': icon_compass,
    'weather_home': icon_home,
    'weather_uv': icon_uv,
}


HEADER = '''/* Syna UI 天气图标（单色 I1），由 assets/generate_weather_icons.py 程序化生成。
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

#ifndef LV_ATTRIBUTE_{symbol}
#define LV_ATTRIBUTE_{symbol}
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_{symbol}
uint8_t {symbol}_map[] = {{
'''

FOOTER = '''}};

const lv_image_dsc_t {symbol} = {{
  .header = {{
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_I1,
    .flags = 0,
    .w = {width},
    .h = {height},
    .stride = {stride},
    .reserved_2 = 0,
  }},
  .data_size = sizeof({symbol}_map),
  .data = {symbol}_map,
  .reserved = NULL,
}};
'''

# LVGL I1 调色板：索引 0 = 黑，索引 1 = 白（与 LVGLImage.py 输出一致）。
PALETTE_BYTES = [0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff]


def rows_of(values, width=12):
    return '\n'.join('    ' + ', '.join(values[i:i + width]) + ','
                     for i in range(0, len(values), width))


def to_c_array(name, image):
    """把 PIL 1-bit 图像转成 LVGL I1 C 数组文本（双端与现有素材格式一致）。"""
    width, height = image.size
    stride = (width + 7) // 8
    pixels = image.load()
    data = list(PALETTE_BYTES)
    for y in range(height):
        row = [0] * stride
        for x in range(width):
            # PIL：255 = 白。位值直接等于像素亮度（1 = 白 = palette[1]）。
            if pixels[x, y]:
                row[x // 8] |= 0x80 >> (x % 8)
        data.extend(row)
    body = HEADER.format(symbol=name).replace('\n', '\r\n')
    body += rows_of([f'0x{value:02x}' for value in data]) + '\n'
    body += FOOTER.format(symbol=name, width=width, height=height,
                          stride=stride).replace('\n', '\r\n')
    return body


def convert(name, image):
    text = to_c_array(f'ui_{name}', image)
    for output in OUTPUTS:
        output.mkdir(parents=True, exist_ok=True)
        (output / f'ui_{name}.c').write_text(text, encoding='utf-8', newline='')


def main():
    generated = []
    for name, painter in {**WEATHER_ICONS, **PARAM_ICONS,
                          **FORECAST_ICONS, **WIND_ARROWS,
                          'weather_wifi': icon_wifi_on,
                          'weather_wifi_off': icon_wifi_off,
                          'weather12_drop': icon12_drop}.items():
        image = painter()
        convert(name, image)
        generated.append(name)
        print(f'{name}: {image.size[0]}x{image.size[1]}')
    print(f'{len(generated)} weather icons generated to both asset directories')


if __name__ == '__main__':
    main()
