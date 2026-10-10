#ifndef AI_PANEL_REPORTER_SERVICE_H
#define AI_PANEL_REPORTER_SERVICE_H

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

constexpr int kPanelMaxReporters = 8;

struct PanelReporterMetrics {
    bool connected = false;
    bool cpu_temperature_valid = false;
    bool gpu_valid = false;
    bool gpu_temperature_valid = false;
    float cpu_percent = 0;
    float memory_percent = 0;
    float gpu_percent = 0;
    float disk_percent = 0;
    float cpu_temperature_c = 0;
    float gpu_temperature_c = 0;
    float upload_bytes_per_second = 0;
    float download_bytes_per_second = 0;
    int16_t codex_short_remaining = -1;
    int16_t codex_week_remaining = -1;
    int16_t codex_month_remaining = -1;
    int32_t codex_short_resets_in = -1;
    int32_t codex_week_resets_in = -1;
    int32_t codex_month_resets_in = -1;
    bool codex_quota_stale = false;
    char quota_provider_title[24] = "Codex";
    uint32_t latency_ms = 0;
    char reporter_id[40] = {};
    char computer_name[40] = {};
    char agent_state[16] = "OFFLINE";
};

struct PanelReporterDevice {
    bool online = false;
    char reporter_id[40] = {};
    char computer_name[40] = {};
    char agent_state[16] = "OFFLINE";
};

struct PanelReporterSnapshot {
    PanelReporterMetrics metrics;
    PanelReporterDevice devices[kPanelMaxReporters];
    uint8_t device_count = 0;
    int8_t current_index = -1;
    uint32_t generation = 0;
};

class ReporterService {
public:
    static ReporterService& GetInstance();

    bool Start();
    bool GetSnapshot(PanelReporterSnapshot& snapshot);
    void RequestSelection(int index);
    void RequestReload();

private:
    struct Endpoint {
        bool online = false;
        sockaddr_in address = {};
        uint16_t http_port = 0;
        int64_t last_seen_ms = 0;
        char reporter_id[40] = {};
        char pairing_hash[65] = {};
        char computer_name[40] = {};
        char agent_state[16] = "OFFLINE";
        int16_t codex_short_remaining = -1;
        int16_t codex_week_remaining = -1;
        int16_t codex_month_remaining = -1;
        int32_t codex_short_resets_in = -1;
        int32_t codex_week_resets_in = -1;
        int32_t codex_month_resets_in = -1;
        bool codex_quota_stale = false;
        char quota_provider_title[24] = "Codex";
    };

    ReporterService();
    ~ReporterService() = default;
    ReporterService(const ReporterService&) = delete;
    ReporterService& operator=(const ReporterService&) = delete;

    static void TaskEntry(void* argument);
    void Run();
    bool WifiReady() const;
    bool OpenSocket();
    void CloseSocket();
    void SendDiscovery(int64_t now_ms);
    void ReceiveOne();
    void ParseDiscovery(const char* payload, size_t length,
                        const sockaddr_in& source);
    void ExpireEndpoints(int64_t now_ms);
    int FindEndpoint(const char* reporter_id) const;
    void SelectEndpoint(int index, bool persist);
    void PublishSnapshot();
    void ClearSelectedMetrics();
    void LoadSettings();

    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    int socket_ = -1;
    Endpoint endpoints_[kPanelMaxReporters];
    uint8_t endpoint_count_ = 0;
    int8_t selected_index_ = -1;
    int requested_index_ = -1;
    bool reload_requested_ = false;
    int64_t last_discovery_ms_ = 0;
    char selected_id_[40] = {};
    char expected_pairing_hash_[65] = {};
    char board_id_[20] = {};
    PanelReporterMetrics metrics_;
    PanelReporterSnapshot snapshot_;
};

#endif
