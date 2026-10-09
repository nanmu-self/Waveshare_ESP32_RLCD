"""Exercise the actual firmware quota renderer with a small LVGL host stub."""
from pathlib import Path
import re
import subprocess
import tempfile
import os
from check_panel_buttons import method, BOARD, ROOT

support = r'''
#include <cstdio>
#include <cassert>
#include <string>
#include <ctime>
struct lv_obj_t { std::string text; int width=0; bool hidden=false; };
lv_obj_t objs[15];
lv_obj_t *dashboard_quota_caption[3] = {&objs[0], &objs[1], &objs[2]};
lv_obj_t *dashboard_quota_countdown[3] = {&objs[3], &objs[4], &objs[5]};
lv_obj_t *dashboard_quota_percent[3] = {&objs[6], &objs[7], &objs[8]};
lv_obj_t *dashboard_quota_track[3] = {&objs[9], &objs[10], &objs[11]};
lv_obj_t *dashboard_quota_fill[3] = {&objs[12], &objs[13], &objs[14]};
const int LV_OBJ_FLAG_HIDDEN=1;
bool lv_obj_is_valid(lv_obj_t *p) {return p != nullptr;}
void lv_label_set_text(lv_obj_t *p,const char *s) {p->text=s;}
void lv_obj_set_width(lv_obj_t *p,int n) {p->width=n;}
void lv_obj_remove_flag(lv_obj_t *p,int) {p->hidden=false;}
void lv_obj_add_flag(lv_obj_t *p,int) {p->hidden=true;}
'''
scenarios = r'''
int main() {
 ui_update_codex_quota(-1,64,-1,true,true, -1,-1,-1);
 assert(objs[6].text=="--" && objs[7].text=="~64%" && objs[8].text=="--");
 assert(objs[3].hidden && objs[4].hidden && objs[5].hidden);
 assert(objs[12].hidden && !objs[13].hidden && objs[14].hidden);
 assert(objs[13].width == 24);
 ui_update_codex_quota(0,100,50,true,true, 31*60, (2*24+9)*3600, (1*24+9)*3600);
 assert(objs[6].text=="~0%" && objs[7].text=="~100%" && objs[8].text=="~50%");
 assert(objs[12].hidden && !objs[13].hidden && !objs[14].hidden);
 assert(objs[13].width == 38 && objs[14].width == 19);
 assert(objs[3].text=="31m");
 assert(objs[4].text=="2d9h");
 assert(objs[5].text=="1d9h");
 ui_update_codex_quota(10,60,70,true,false, 0,0,0);
 assert(objs[6].text=="10%" && objs[7].text=="60%" && objs[8].text=="70%");
 assert(objs[3].hidden && objs[4].hidden && objs[5].hidden);
 ui_update_codex_quota(-1,-1,-1,true,false, -1,-1,-1);
 assert(objs[6].text=="--" && objs[7].text=="--" && objs[8].text=="--");
 ui_update_codex_quota(10,60,70,false,false, -1,-1,-1);
 assert(objs[6].text=="--" && objs[7].text=="--" && objs[8].text=="--");
 assert(objs[12].hidden && objs[13].hidden && objs[14].hidden);
 assert(objs[3].hidden && objs[4].hidden && objs[5].hidden);
 puts("PASS: stale, recovery, missing, monthly, countdown, zero, full, disconnected");
}
'''

def main():
    font = (BOARD / 'ui_font_14_regular.c').read_text(encoding='utf-8')
    offsets = [int(x) for x in re.findall(r'\d+', re.search(r'unicode_list\[\]\s*=\s*\{(.*?)\}', font, re.S)[1])]
    advances = [int(x) / 16 for x in re.findall(r'\.adv_w=(\d+)', font)]
    width = sum(advances[offsets.index(ord(c)-32)+1] for c in '~100%')
    assert width <= 42, f'Cached quota text exceeds label width: {width}'
    print('Maximum cached quota advance:', width)
    with tempfile.TemporaryDirectory(prefix='syna-quota-', ignore_cleanup_errors=True) as tmp:
        directory = Path(tmp)
        source = directory / 'test.cpp'
        source.write_text(support
                          + method(BOARD/'ui.c', 'static void format_quota_countdown(')
                          + method(BOARD/'ui.c', 'void ui_update_codex_quota(')
                          + scenarios, encoding='utf-8')
        exe = directory / 'test.exe'
        command = ['cl.exe', '/nologo', '/EHsc', '/std:c++17', '/utf-8', str(source), '/Fe:'+str(exe), '/Fo:'+str(directory/'test.obj')]
        batch = directory/'build.cmd'
        vcvars = Path(os.environ.get('SYNA_VCVARS', r'D:\vs\VC\Auxiliary\Build\vcvars64.bat'))
        batch.write_text('@echo off\nchcp 65001 >nul\nset VSCMD_SKIP_SENDTELEMETRY=1\ncall "'+str(vcvars)+'" >nul\n'+subprocess.list2cmdline(command)+'\nexit /b %errorlevel%\n', encoding='utf-8')
        subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],check=True)

if __name__ == '__main__':
    main()
