#pragma once

#include <atomic>
#include <mutex>

#include <esp_http_server.h>

class SettingsPortalService {
public:
    static SettingsPortalService& GetInstance();

    // 依据服务器实例区分请求来源（局域网门户 / 热点门户）
    static bool IsLanRequest(httpd_req_t* request);

    // 创建后台任务等待 STA 联网；拿到 IP 后自动拉起局域网管理服务器（端口 8080）。
    // 不能依赖 esp_event：默认事件循环在联网初始化时才创建，板构造阶段注册会失败。
    void Start();

    // 热点配置模式的自定义首页（经 apply_ui_overlay.py 的 weak hook 挂入上游）。
    esp_err_t ServeRoot(httpd_req_t* request);

    // 热点配置模式服务器（192.168.4.1:80）追加的面板接口。
    void RegisterHandlers(httpd_handle_t server);

    float GetBatteryScale();

private:
    SettingsPortalService() = default;

    static esp_err_t HandleRoot(httpd_req_t* request);    // LAN：登录分流
    static esp_err_t HandleLogin(httpd_req_t* request);   // LAN：登录 / 首次设密码
    static esp_err_t HandleLogout(httpd_req_t* request);  // LAN：退出登录
    static esp_err_t HandleWifi(httpd_req_t* request);    // LAN：保存并切换 Wi-Fi
    static esp_err_t HandleScan(httpd_req_t* request);    // LAN：扫描周围热点
    static esp_err_t HandleConfig(httpd_req_t* request);  // 两端共用
    static esp_err_t HandleSave(httpd_req_t* request);    // 两端共用

    void StartLanServer();
    static void PortalWatchTask(void* arg);

    std::atomic<int> battery_scale_milli_{0};
    std::mutex lan_mutex_;
    httpd_handle_t lan_server_ = nullptr;
    std::atomic<bool> watch_started_{false};
};
