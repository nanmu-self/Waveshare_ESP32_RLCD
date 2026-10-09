#include "settings_portal_service.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <psa/crypto.h>

#include "api_balance_service.h"
#include "reporter_service.h"
#include "weather_service.h"
#include "settings.h"
#include "ssid_manager.h"
#include "wifi_manager.h"

namespace {

constexpr char kTag[] = "PanelSettings";
constexpr size_t kMaximumBodyBytes = 4096;

constexpr char kPortalSaltKey[] = "portal-salt";   // 32 个 hex 字符 = 16 字节盐
constexpr char kPortalHashKey[] = "portal-hash";   // 64 个 hex 字符 = SHA-256(salt+密码)
constexpr char kSessionCookie[] = "syna_session";
constexpr int64_t kSessionTtlMicros = static_cast<int64_t>(24) * 3600 * 1000000;
constexpr int64_t kLoginLockoutMicros = static_cast<int64_t>(60) * 1000000;
constexpr int kMaxLoginFailures = 5;

const char kSettingsPage[] = R"HTML(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>召唤Syna-sama</title><style>
*{box-sizing:border-box}body{margin:0;background:#f1f1ef;color:#111;font-family:system-ui,"Microsoft YaHei",sans-serif}.card{max-width:620px;margin:20px auto;padding:24px;background:#fff;border:2px solid #111;border-radius:18px;box-shadow:6px 6px 0 #111}h1{font-size:25px;margin:0 0 7px}.sub,.tip{color:#555;line-height:1.55}.sub{margin:0 0 18px}.tip{font-size:13px;margin:7px 0 0}fieldset{margin:16px 0;padding:15px;border:2px solid #111;border-radius:12px}legend{padding:0 7px;font-weight:800}label{display:block;font-weight:700;margin:12px 0 6px}input,select,textarea{width:100%;padding:11px;border:2px solid #111;border-radius:8px;font-size:16px;background:#fff}textarea{min-height:76px;resize:vertical}button{width:100%;margin-top:15px;padding:13px;border:2px solid #111;border-radius:8px;background:#111;color:#fff;font-size:17px;font-weight:800}.notice{display:none;padding:11px;margin:0 0 14px;border:1px solid #111;border-radius:8px;background:#eee}.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}#lanbar{display:none}#lanbar a{color:#111}@media(max-width:520px){.card{margin:0;border-radius:0;box-shadow:none}.row{grid-template-columns:1fr}}</style></head><body><main class="card"><h1>召唤Syna-sama</h1><p class="sub">网络、Reporter、API 余额、天气、syna语音和电池校准统一设置。密码与令牌不会回显，留空即保持原值。设备联网后，电脑浏览器直接打开 http://板子IP:8080 也能进入本页（无需连接热点，需管理密码）。</p><div id="notice" class="notice"></div><form id="form">
<fieldset><legend>网络与时间</legend><label>Wi-Fi / 手机热点名称</label><input name="ssid" list="networks" maxlength="32" autocomplete="off"><datalist id="networks"></datalist><label>Wi-Fi 密码</label><input name="password" type="password" maxlength="64" autocomplete="new-password" placeholder="留空保持已保存密码"><p class="tip">若不修改网络，两项都留空。设备会联网自动校时，时区固定为中国标准时间 UTC+8。</p></fieldset>
<fieldset><legend>电脑 Reporter</legend><label>配对令牌</label><input name="reporter_token" type="password" maxlength="96" autocomplete="off" placeholder="留空保持原令牌"><p class="tip">应与电脑 Reporter 中的令牌一致。</p></fieldset>
<fieldset><legend>API 余额</legend><label>提供商显示名称</label><input name="api_provider" maxlength="40" placeholder="DeepSeek / OpenRouter / 自建平台"><label>API 基础地址</label><input name="api_base" maxlength="200" placeholder="https://api.example.com"><label>API Key</label><input name="api_key" type="password" maxlength="160" autocomplete="off" placeholder="留空保持原 Key"><label>余额获取方式</label><select name="balance_adapter"><option value="disabled">不查询余额</option><option value="deepseek">DeepSeek 官方预设</option><option value="custom">自定义 HTTP 接口</option></select><section id="custom"><label>余额接口完整地址</label><input name="balance_url" maxlength="240"><div class="row"><div><label>请求方法</label><select name="balance_method"><option>GET</option><option>POST</option></select></div><div><label>鉴权方式</label><select name="balance_auth"><option value="bearer">Bearer</option><option value="header">自定义请求头</option><option value="query">URL 参数</option><option value="none">无鉴权</option></select></div></div><label>请求头名 / 参数名</label><input name="balance_auth_name" maxlength="64" placeholder="Authorization"><label>余额 JSON 路径</label><input name="balance_value_path" maxlength="160" placeholder="data.balance"><div class="row"><div><label>固定单位</label><input name="balance_unit" maxlength="16" placeholder="CNY"></div><div><label>数值倍率</label><input name="balance_scale" type="number" step="0.000001"></div></div><label>单位 JSON 路径（可选）</label><input name="balance_unit_path" maxlength="160"><label>POST JSON（可选）</label><textarea name="balance_body" maxlength="512" placeholder="留空保持原内容；可使用 {{API_KEY}}"></textarea></section></fieldset>
<fieldset><legend>天气</legend><label>和风天气 API Key</label><input name="weather_key" type="password" maxlength="64" autocomplete="off" placeholder="留空保持原 Key"><label>城市位置</label><select name="weather_mode"><option value="auto">自动 IP 定位</option><option value="manual">手动填写 LocationID</option></select><section id="wxmanual"><label>城市 LocationID</label><input name="weather_location" maxlength="12" placeholder="如 101280101（广州）"><p class="tip">城市 ID 可在和风天气控制台查询；自动定位无需填写。</p></section></fieldset><fieldset><legend>syna语音</legend><p class="tip">当前小智云能力由设备激活信息管理，无需与余额 API 共用 Key。</p><label>OTA / 配置服务地址（高级，可选）</label><input name="ota_url" maxlength="240"><p class="tip">留空使用固件默认官方地址。填写错误会导致下次启动无法取得小智云配置。</p></fieldset>
<fieldset><legend>电池校准</legend><label>电压倍率</label><input name="battery_scale" type="number" min="2.5" max="3.5" step="0.001"><p class="tip">默认 3.000。新倍率 = 当前倍率 × 万用表实测电压 ÷ 屏幕显示电压。</p></fieldset>
<fieldset><legend>管理密码（局域网访问）</legend><label>管理密码</label><input name="portal_password" type="password" maxlength="64" autocomplete="new-password" placeholder="留空保持不变"><p class="tip">设备联网后，电脑浏览器打开 http://板子IP:8080 需输入此密码。首次可在这里设置，或首次打开局域网页面时按提示设置；忘记密码时在热点页面重新设置即可。</p></fieldset><p class="tip" id="lanbar">当前通过局域网访问 · <a href="#" id="logout">退出登录</a></p><button type="submit">保存设置</button></form><p class="tip">以后先按住 BOOT，再同时长按 KEY 约 3 秒即可再次进入热点配置模式；已保存的 Wi-Fi 不会被删除。</p></main><script>
const f=document.getElementById('form'),n=document.getElementById('notice'),custom=document.getElementById('custom'),wxman=document.getElementById('wxmanual');let portal='ap';const show=m=>{n.textContent=m;n.style.display='block';scrollTo(0,0)};const sync=()=>{custom.style.display=f.balance_adapter.value==='custom'?'block':'none';wxman.style.display=f.weather_mode.value==='manual'?'block':'none'};f.balance_adapter.onchange=sync;f.weather_mode.onchange=sync;
fetch('/panel/config').then(r=>r.json()).then(c=>{if(c.portal)portal=c.portal;document.getElementById('lanbar').style.display=portal==='lan'?'block':'none';if(c.has_password)f.portal_password.placeholder='留空保持现有密码';for(const[k,v]of Object.entries(c)){if(f.elements[k]&&v!==null)f.elements[k].value=v}sync()}).catch(()=>show('读取设置失败，请刷新页面'));
fetch('/scan').then(r=>r.json()).then(x=>{const d=document.getElementById('networks');for(const a of(x.aps||[])){const o=document.createElement('option');o.value=String(a.ssid);d.appendChild(o)}}).catch(()=>{});
document.getElementById('logout').onclick=async e=>{e.preventDefault();await fetch('/panel/logout',{method:'POST'});location.reload()};
f.onsubmit=async e=>{e.preventDefault();const o=Object.fromEntries(new FormData(f));show('正在保存…');try{let r=await fetch('/panel/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}),j=await r.json();if(!j.success)throw Error(j.error||'保存失败');if(portal==='lan'){if(o.ssid){r=await fetch('/panel/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:o.ssid,password:o.password})});j=await r.json();if(!j.success)throw Error(j.error||'Wi-Fi 切换失败');show('设置已保存。设备正在切换 Wi-Fi，若板子地址变化，请用新地址重新打开本页。')}else{show('设置已保存。')}}else{if(o.ssid){r=await fetch('/submit',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:o.ssid,password:o.password})});j=await r.json();if(!j.success)throw Error(j.error||'Wi-Fi 连接失败')}else{r=await fetch('/exit',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});j=await r.json();if(!j.success)throw Error(j.error||'退出设置模式失败')}show('设置已保存。设备正在恢复联网，Syna-0721 将自动关闭。')}}catch(x){show(x.message)}};sync();
</script></body></html>)HTML";

const char kLoginPage[] = R"HTML(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Syna 管理登录</title><style>
*{box-sizing:border-box}body{margin:0;background:#f1f1ef;color:#111;font-family:system-ui,"Microsoft YaHei",sans-serif}.card{max-width:420px;margin:60px auto 20px;padding:24px;background:#fff;border:2px solid #111;border-radius:18px;box-shadow:6px 6px 0 #111}h1{font-size:22px;margin:0 0 7px}.sub,.tip{color:#555;line-height:1.55}.sub{margin:0 0 18px}.tip{font-size:13px;margin:14px 0 0}label{display:block;font-weight:700;margin:12px 0 6px}input{width:100%;padding:11px;border:2px solid #111;border-radius:8px;font-size:16px;background:#fff}button{width:100%;margin-top:15px;padding:13px;border:2px solid #111;border-radius:8px;background:#111;color:#fff;font-size:17px;font-weight:800}.notice{display:none;padding:11px;margin:0 0 14px;border:1px solid #111;border-radius:8px;background:#eee}
</style></head><body><main class="card"><h1>希娜 Syna 管理页</h1><p class="sub">__SUB__</p><div id="notice" class="notice"></div><form id="form">__CONFIRM__<label>管理密码</label><input name="password" type="password" maxlength="64" autocomplete="__AUTOCOMPLETE__"><button type="submit">__BUTTON__</button></form><p class="tip">忘记密码？在设备上先按住 BOOT，再同时长按 KEY 约 3 秒进入热点配置模式，在"管理密码"栏重新设置。设备屏幕的"关于"页可查看本页地址。</p></main><script>
const f=document.getElementById('form'),n=document.getElementById('notice');const show=m=>{n.textContent=m;n.style.display='block'};f.onsubmit=async e=>{e.preventDefault();const o=Object.fromEntries(new FormData(f));if(f.elements.confirm&&o.password!==o.confirm){show('两次输入的密码不一致');return}show('正在验证…');try{const r=await fetch('/panel/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}),j=await r.json();if(!j.success)throw Error(j.error||'操作失败');location.reload()}catch(x){show(x.message)}};
</script></body></html>)HTML";

std::string ReplaceAll(std::string text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string BuildLoginPage(bool first_setup) {
    const char* sub = first_setup
        ? "首次使用：请设置一个管理密码（至少 4 位），之后从局域网访问本页时需输入。"
        : "输入管理密码以进入管理页。";
    const char* confirm = first_setup
        ? "<label>再次输入密码</label><input name=\"confirm\" type=\"password\" maxlength=\"64\" autocomplete=\"new-password\">"
        : "";
    const char* button = first_setup ? "保存并进入" : "进入管理页";
    const char* autocomplete = first_setup ? "new-password" : "current-password";
    std::string page = ReplaceAll(kLoginPage, "__SUB__", sub);
    page = ReplaceAll(page, "__CONFIRM__", confirm);
    page = ReplaceAll(page, "__BUTTON__", button);
    return ReplaceAll(page, "__AUTOCOMPLETE__", autocomplete);
}

void AddString(cJSON* root, const char* name, const std::string& value) {
    cJSON_AddStringToObject(root, name, value.c_str());
}

bool ReadFloat(const char* ns, const char* key, float& value) {
    nvs_handle_t handle = 0;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = sizeof(value);
    const esp_err_t result = nvs_get_blob(handle, key, &value, &size);
    nvs_close(handle);
    return result == ESP_OK;
}

void WriteFloat(const char* ns, const char* key, float value) {
    nvs_handle_t handle = 0;
    if (nvs_open(ns, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_blob(handle, key, &value, sizeof(value)) == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
}

void SetIfPresent(Settings& settings, const cJSON* root, const char* json_name,
                  const char* key, bool allow_empty = true) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, json_name);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) return;
    if (allow_empty || item->valuestring[0] != '\0') settings.SetString(key, item->valuestring);
}

std::string BytesToHex(const uint8_t* data, size_t length) {
    static const char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        hex.push_back(kDigits[data[i] >> 4]);
        hex.push_back(kDigits[data[i] & 0x0F]);
    }
    return hex;
}

bool HexToBytes(const char* hex, uint8_t* out, size_t out_length) {
    if (strlen(hex) != out_length * 2) return false;
    for (size_t i = 0; i < out_length; ++i) {
        unsigned value = 0;
        for (int half = 0; half < 2; ++half) {
            const char ch = hex[i * 2 + half];
            unsigned digit;
            if (ch >= '0' && ch <= '9') digit = ch - '0';
            else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
            else return false;
            value = value * 16 + digit;
        }
        out[i] = static_cast<uint8_t>(value);
    }
    return true;
}

bool ConstantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

// ---------- 管理密码（SHA-256 + 盐，仅存哈希） ----------

bool HashPassword(const std::string& salt_hex, const std::string& password, std::string& hash_hex) {
    static std::once_flag psa_once;
    std::call_once(psa_once, []() {
        if (psa_crypto_init() != PSA_SUCCESS) ESP_LOGE(kTag, "psa_crypto_init failed");
    });
    uint8_t salt[16];
    if (!HexToBytes(salt_hex.c_str(), salt, sizeof(salt))) return false;
    std::string input(reinterpret_cast<const char*>(salt), sizeof(salt));
    input += password;
    uint8_t digest[32];
    size_t digest_length = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(input.data()),
                         input.size(), digest, sizeof(digest), &digest_length) != PSA_SUCCESS ||
        digest_length != sizeof(digest)) {
        return false;
    }
    hash_hex = BytesToHex(digest, sizeof(digest));
    return true;
}

bool ReadPortalSecret(std::string& salt_hex, std::string& hash_hex) {
    Settings panel("wifi-config");
    salt_hex = panel.GetString(kPortalSaltKey, "");
    hash_hex = panel.GetString(kPortalHashKey, "");
    return salt_hex.size() == 32 && hash_hex.size() == 64;
}

void WritePortalSecret(const std::string& password) {
    uint8_t salt[16];
    esp_fill_random(salt, sizeof(salt));
    const std::string salt_hex = BytesToHex(salt, sizeof(salt));
    std::string hash_hex;
    if (!HashPassword(salt_hex, password, hash_hex)) {
        ESP_LOGE(kTag, "Failed to hash portal password");
        return;
    }
    Settings panel("wifi-config", true);
    panel.SetString(kPortalSaltKey, salt_hex);
    panel.SetString(kPortalHashKey, hash_hex);
}

// ---------- 登录会话（单用户，RAM 内令牌，重启即失效） ----------

std::mutex s_session_mutex;
std::string s_session_token;
// Set-Cookie 的缓冲必须活到响应发送完成，用静态缓冲配合互斥锁。
char s_session_cookie_buffer[128];
int64_t s_session_expiry_us = 0;

std::mutex s_login_mutex;
int s_login_failures = 0;
int64_t s_login_lockout_until_us = 0;

bool LoginLocked(std::string& message) {
    std::lock_guard<std::mutex> lock(s_login_mutex);
    if (esp_timer_get_time() < s_login_lockout_until_us) {
        message = "失败次数过多，请一分钟后再试";
        return true;
    }
    return false;
}

void NoteLoginFailure() {
    std::lock_guard<std::mutex> lock(s_login_mutex);
    if (++s_login_failures >= kMaxLoginFailures) {
        s_login_lockout_until_us = esp_timer_get_time() + kLoginLockoutMicros;
        s_login_failures = 0;
    }
}

void NoteLoginSuccess() {
    std::lock_guard<std::mutex> lock(s_login_mutex);
    s_login_failures = 0;
    s_login_lockout_until_us = 0;
}

bool HasSession(httpd_req_t* request) {
    std::lock_guard<std::mutex> lock(s_session_mutex);
    if (s_session_token.empty() || esp_timer_get_time() >= s_session_expiry_us) return false;
    char cookie[80];
    size_t cookie_length = sizeof(cookie);
    if (httpd_req_get_cookie_val(request, kSessionCookie, cookie, &cookie_length) != ESP_OK) {
        return false;
    }
    // 恒定时间比较，避免令牌逐字节试探
    const size_t a = s_session_token.size(), b = strlen(cookie);
    const size_t common = a < b ? a : b;
    uint8_t diff = a == b ? 0 : 1;
    for (size_t i = 0; i < common; ++i) diff |= static_cast<uint8_t>(s_session_token[i] ^ cookie[i]);
    return diff == 0;
}

void IssueSession(httpd_req_t* request) {
    std::lock_guard<std::mutex> lock(s_session_mutex);
    uint8_t random[32];
    esp_fill_random(random, sizeof(random));
    s_session_token = BytesToHex(random, sizeof(random));
    s_session_expiry_us = esp_timer_get_time() + kSessionTtlMicros;
    snprintf(s_session_cookie_buffer, sizeof(s_session_cookie_buffer),
             "%s=%s; Path=/; Max-Age=86400; HttpOnly; SameSite=Lax",
             kSessionCookie, s_session_token.c_str());
    httpd_resp_set_hdr(request, "Set-Cookie", s_session_cookie_buffer);
}

void ClearSession() {
    std::lock_guard<std::mutex> lock(s_session_mutex);
    s_session_token.clear();
    s_session_expiry_us = 0;
}

// ---------- 公共请求处理辅助 ----------

// 局域网门户需要会话；热点门户（物理按键才能进入）保持免登录。
bool RequireLanSession(httpd_req_t* request) {
    if (!SettingsPortalService::IsLanRequest(request) || HasSession(request)) return true;
    httpd_resp_set_status(request, "401 Unauthorized");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, "{\"success\":false,\"error\":\"请先登录\"}");
    return false;
}

bool ReadJsonBody(httpd_req_t* request, cJSON** root_out) {
    if (request->content_len == 0 || request->content_len > kMaximumBodyBytes) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid payload size");
        return false;
    }
    std::vector<char> body(request->content_len + 1);
    size_t received = 0;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request, body.data() + received,
                                         request->content_len - received);
        if (count <= 0) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Failed to receive payload");
            return false;
        }
        received += count;
    }
    body[received] = '\0';
    *root_out = cJSON_Parse(body.data());
    if (*root_out == nullptr) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return false;
    }
    return true;
}

const char* JsonString(const cJSON* root, const char* name) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

esp_err_t RespondJson(httpd_req_t* request, const std::string& json) {
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json.c_str(), json.size());
}

}  // namespace

bool SettingsPortalService::IsLanRequest(httpd_req_t* request) {
    return request->handle == GetInstance().lan_server_;
}

SettingsPortalService& SettingsPortalService::GetInstance() {
    static SettingsPortalService instance;
    return instance;
}

void SettingsPortalService::Start() {
    if (watch_started_.exchange(true)) return;
    // 板构造阶段默认事件循环尚未创建，不能注册 IP 事件；
    // 改为后台任务轮询 STA IP，联网后自动拉起管理页。
    if (xTaskCreate(PortalWatchTask, "portal-watch", 4096, this, 1, nullptr) != pdPASS) {
        watch_started_.store(false);
        ESP_LOGE(kTag, "Failed to create portal watch task");
    }
}

void SettingsPortalService::PortalWatchTask(void* arg) {
    auto* service = static_cast<SettingsPortalService*>(arg);
    while (true) {
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t info = {};
        if (netif != nullptr && esp_netif_get_ip_info(netif, &info) == ESP_OK &&
            info.ip.addr != 0) {
            break;
        }
        // 未联网（含配置模式 STA 关闭期间）继续等待；轮询开销可忽略
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(kTag, "Station has IP; starting LAN portal");
    service->StartLanServer();
    vTaskDelete(nullptr);
}

void SettingsPortalService::StartLanServer() {
    std::lock_guard<std::mutex> lock(lan_mutex_);
    if (lan_server_ != nullptr) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // 80 端口留给热点配置门户，两者可同时存在
    config.server_port = 8080;
    config.ctrl_port = 32769;
    config.max_open_sockets = 4;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    if (httpd_start(&lan_server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start LAN portal server");
        lan_server_ = nullptr;
        return;
    }
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, HandleRoot, this},
        {"/panel/login", HTTP_POST, HandleLogin, this},
        {"/panel/logout", HTTP_POST, HandleLogout, this},
        {"/panel/config", HTTP_GET, HandleConfig, this},
        {"/panel/save", HTTP_POST, HandleSave, this},
        {"/panel/wifi", HTTP_POST, HandleWifi, this},
        {"/scan", HTTP_GET, HandleScan, this},
    };
    for (const auto& route : routes) {
        // 注册失败不致命：降级为缺路由并打日志，避免运行期 abort 重启
        if (httpd_register_uri_handler(lan_server_, &route) != ESP_OK) {
            ESP_LOGE(kTag, "Failed to register route %s", route.uri);
        }
    }
    ESP_LOGI(kTag, "LAN portal ready on port %d", config.server_port);
}

esp_err_t SettingsPortalService::ServeRoot(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, kSettingsPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t SettingsPortalService::HandleRoot(httpd_req_t* request) {
    std::string page;
    if (HasSession(request)) {
        page = kSettingsPage;
    } else {
        std::string salt, hash;
        page = BuildLoginPage(!ReadPortalSecret(salt, hash));
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, page.c_str(), page.size());
}

esp_err_t SettingsPortalService::HandleLogin(httpd_req_t* request) {
    cJSON* root = nullptr;
    if (!ReadJsonBody(request, &root)) return ESP_FAIL;
    const std::string password = JsonString(root, "password");
    const std::string confirm = JsonString(root, "confirm");
    cJSON_Delete(root);
    bool success = false;
    std::string error = "密码错误";
    std::string salt, hash;
    if (!ReadPortalSecret(salt, hash)) {
        // 首次使用：设置管理密码并直接登录
        if (password.size() < 4 || password.size() > 64) {
            error = "管理密码需 4-64 位";
        } else if (password != confirm) {
            error = "两次输入的密码不一致";
        } else {
            WritePortalSecret(password);
            success = true;
        }
    } else {
        std::string lock_message;
        if (LoginLocked(lock_message)) {
            error = lock_message;
        } else {
            std::string computed;
            if (!password.empty() && password.size() <= 128 &&
                HashPassword(salt, password, computed) && ConstantTimeEqual(computed, hash)) {
                success = true;
            } else {
                NoteLoginFailure();
            }
        }
    }
    if (!success) return RespondJson(request, "{\"success\":false,\"error\":\"" + error + "\"}");
    NoteLoginSuccess();
    IssueSession(request);
    return RespondJson(request, "{\"success\":true}");
}

esp_err_t SettingsPortalService::HandleLogout(httpd_req_t* request) {
    ClearSession();
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Set-Cookie",
                       "syna_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
    return httpd_resp_sendstr(request, "{\"success\":true}");
}

void SettingsPortalService::RegisterHandlers(httpd_handle_t server) {
    const httpd_uri_t config = {
        .uri = "/panel/config", .method = HTTP_GET,
        .handler = HandleConfig, .user_ctx = this};
    const httpd_uri_t save = {
        .uri = "/panel/save", .method = HTTP_POST,
        .handler = HandleSave, .user_ctx = this};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &config));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &save));
}

float SettingsPortalService::GetBatteryScale() {
    const int cached = battery_scale_milli_.load();
    if (cached != 0) return cached / 1000.0f;
    float scale = 3.0f;
    if (!ReadFloat("wifi-config", "bat-scale", scale) || !std::isfinite(scale) ||
        scale < 2.5f || scale > 3.5f) scale = 3.0f;
    battery_scale_milli_.store(static_cast<int>(std::lround(scale * 1000.0f)));
    return scale;
}

esp_err_t SettingsPortalService::HandleConfig(httpd_req_t* request) {
    if (!RequireLanSession(request)) return ESP_OK;
    std::string salt, hash;
    Settings panel("wifi-config");
    Settings wifi("wifi");
    cJSON* root = cJSON_CreateObject();
    AddString(root, "portal", SettingsPortalService::IsLanRequest(request) ? "lan" : "ap");
    cJSON_AddBoolToObject(root, "has_password", ReadPortalSecret(salt, hash));
    AddString(root, "api_provider", panel.GetString("api-provider", "API"));
    AddString(root, "api_base", panel.GetString("api-base", ""));
    AddString(root, "weather_mode", panel.GetString("wx-mode", "auto"));
    AddString(root, "weather_location", panel.GetString("wx-loc", ""));
    AddString(root, "balance_adapter", panel.GetString("bal-adapter", "disabled"));
    AddString(root, "balance_url", panel.GetString("bal-url", ""));
    AddString(root, "balance_method", panel.GetString("bal-method", "GET"));
    AddString(root, "balance_auth", panel.GetString("bal-auth", "bearer"));
    AddString(root, "balance_auth_name", panel.GetString("bal-hname", "Authorization"));
    AddString(root, "balance_value_path", panel.GetString("bal-path", "data.balance"));
    AddString(root, "balance_unit", panel.GetString("bal-unit", ""));
    AddString(root, "balance_unit_path", panel.GetString("bal-upath", ""));
    cJSON_AddNumberToObject(root, "balance_scale", [&]() { float v = 1.0f; ReadFloat("wifi-config", "bal-scale", v); return v; }());
    cJSON_AddNumberToObject(root, "battery_scale", SettingsPortalService::GetInstance().GetBatteryScale());
    AddString(root, "ota_url", wifi.GetString("ota_url", ""));
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    const esp_err_t result = RespondJson(request, json);
    cJSON_free(json);
    return result;
}

esp_err_t SettingsPortalService::HandleSave(httpd_req_t* request) {
    if (!RequireLanSession(request)) return ESP_OK;
    cJSON* root = nullptr;
    if (!ReadJsonBody(request, &root)) return ESP_FAIL;
    // 管理密码先校验后写库，避免无效密码与其他设置一起落盘
    const char* portal_password = JsonString(root, "portal_password");
    if (portal_password[0] != '\0' &&
        (strlen(portal_password) < 4 || strlen(portal_password) > 64)) {
        cJSON_Delete(root);
        return RespondJson(request,
                           "{\"success\":false,\"error\":\"管理密码需 4-64 位\"}");
    }
    {
        Settings panel("wifi-config", true);
        SetIfPresent(panel, root, "reporter_token", "reporter", false);
        SetIfPresent(panel, root, "api_provider", "api-provider");
        SetIfPresent(panel, root, "api_base", "api-base");
        SetIfPresent(panel, root, "api_key", "api-key", false);
        SetIfPresent(panel, root, "weather_mode", "wx-mode");
        SetIfPresent(panel, root, "weather_location", "wx-loc");
        SetIfPresent(panel, root, "weather_key", "wx-key", false);
        SetIfPresent(panel, root, "balance_adapter", "bal-adapter");
        SetIfPresent(panel, root, "balance_url", "bal-url");
        SetIfPresent(panel, root, "balance_method", "bal-method");
        SetIfPresent(panel, root, "balance_auth", "bal-auth");
        SetIfPresent(panel, root, "balance_auth_name", "bal-hname");
        SetIfPresent(panel, root, "balance_value_path", "bal-path");
        SetIfPresent(panel, root, "balance_unit", "bal-unit");
        SetIfPresent(panel, root, "balance_unit_path", "bal-upath");
        SetIfPresent(panel, root, "balance_body", "bal-body", false);
        if (portal_password[0] != '\0') WritePortalSecret(portal_password);
    }
    {
        Settings wifi("wifi", true);
        SetIfPresent(wifi, root, "ota_url", "ota_url");
    }
    const cJSON* balance_scale = cJSON_GetObjectItemCaseSensitive(root, "balance_scale");
    if (cJSON_IsString(balance_scale) && balance_scale->valuestring) {
        const float value = strtof(balance_scale->valuestring, nullptr);
        if (std::isfinite(value) && value != 0 && std::fabs(value) <= 1000000) {
            WriteFloat("wifi-config", "bal-scale", value);
        }
    }
    const cJSON* battery_scale = cJSON_GetObjectItemCaseSensitive(root, "battery_scale");
    if (cJSON_IsString(battery_scale) && battery_scale->valuestring) {
        const float value = strtof(battery_scale->valuestring, nullptr);
        if (std::isfinite(value) && value >= 2.5f && value <= 3.5f) {
            WriteFloat("wifi-config", "bat-scale", value);
            SettingsPortalService::GetInstance().battery_scale_milli_.store(
                static_cast<int>(std::lround(value * 1000.0f)));
        }
    }
    cJSON_Delete(root);
    ReporterService::GetInstance().RequestReload();
    ApiBalanceService::GetInstance().RequestRefresh();
    WeatherService::GetInstance().RequestRefresh();
    ESP_LOGI(kTag, "Settings saved; secret values were not echoed");
    return RespondJson(request, "{\"success\":true}");
}

esp_err_t SettingsPortalService::HandleWifi(httpd_req_t* request) {
    if (!RequireLanSession(request)) return ESP_OK;
    cJSON* root = nullptr;
    if (!ReadJsonBody(request, &root)) return ESP_FAIL;
    const std::string ssid = JsonString(root, "ssid");
    const std::string password = JsonString(root, "password");
    cJSON_Delete(root);
    if (ssid.empty() || ssid.size() > 32 || password.size() > 64) {
        return RespondJson(request,
                           "{\"success\":false,\"error\":\"Wi-Fi 名称或密码长度不合法\"}");
    }
    SsidManager::GetInstance().AddSsid(ssid, password);
    ESP_LOGI(kTag, "LAN portal: switching to Wi-Fi %s", ssid.c_str());
    // 先应答浏览器，再切换网络（STA 会重建，IP 可能变化）
    xTaskCreate([](void*) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        auto& manager = WifiManager::GetInstance();
        manager.StopStation();
        manager.StartStation();
        vTaskDelete(nullptr);
    }, "lan-wifi-switch", 4096, nullptr, 5, nullptr);
    return RespondJson(request, "{\"success\":true}");
}

esp_err_t SettingsPortalService::HandleScan(httpd_req_t* request) {
    if (!RequireLanSession(request)) return ESP_OK;
    // 阻塞扫描约 1-2 秒；若与后台定时扫描重叠则稍候重试一次
    esp_err_t err = esp_wifi_scan_start(nullptr, true);
    if (err != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(300));
        err = esp_wifi_scan_start(nullptr, true);
    }
    cJSON* root = cJSON_CreateObject();
    cJSON* aps = cJSON_AddArrayToObject(root, "aps");
    if (err == ESP_OK) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        if (ap_count > 20) ap_count = 20;
        auto* records = static_cast<wifi_ap_record_t*>(
            malloc(ap_count * sizeof(wifi_ap_record_t)));
        if (records != nullptr) {
            esp_wifi_scan_get_ap_records(&ap_count, records);
            for (uint16_t i = 0; i < ap_count; ++i) {
                cJSON* ap = cJSON_CreateObject();
                cJSON_AddStringToObject(ap, "ssid",
                                        reinterpret_cast<const char*>(records[i].ssid));
                cJSON_AddNumberToObject(ap, "rssi", records[i].rssi);
                cJSON_AddNumberToObject(ap, "authmode", records[i].authmode);
                cJSON_AddItemToArray(aps, ap);
            }
            free(records);
        }
    }
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    const esp_err_t result = RespondJson(request, json);
    cJSON_free(json);
    return result;
}

extern "C" esp_err_t wifi_config_custom_root(httpd_req_t* request) {
    return SettingsPortalService::GetInstance().ServeRoot(request);
}

extern "C" void wifi_config_register_custom_handlers(httpd_handle_t server) {
    SettingsPortalService::GetInstance().RegisterHandlers(server);
}

extern "C" const char* wifi_config_custom_ssid() {
    return "Syna-0721";
}
