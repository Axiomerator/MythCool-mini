// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "json.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
namespace mini {
namespace fs = std::filesystem;
using Json = nlohmann::json;
std::wstring wide(const std::string &);
std::string utf8(const std::wstring &);
std::wstring error_text(DWORD);
fs::path data_dir();
fs::path exe_dir();
fs::path exe_path();
bool is_elevated();
struct StartupState {
    bool registered = false, enabled = false, matches = false;
    std::wstring task_path, executable, error;
};
StartupState startup_state();
StartupState set_startup(bool enabled);
std::wstring startup_task_xml(const fs::path &, const std::wstring &sid);
bool official_running();
enum class Metric { Unknown, CpuMax, CpuTotal, CpuUtility, CpuPower, CpuTemp, GpuLoad, GpuPower, GpuTemp };
struct Slot {
    bool enabled = true;
    int align = 1, x = 0, y = 0;
    uint32_t title_rgb = 0xffffff, param_rgb = 0xffffff, unit_rgb = 0xffffff;
    std::string title, unit, outer, inner, value = "--";
    int unit_type = 0;
    Metric metric = Metric::Unknown;
};
Metric identify(const std::string &outer, const std::string &inner);
std::vector<Slot> parse_slots(const Json &);
Json slots_json(const std::vector<Slot> &);
struct Imported {
    std::vector<Slot> slots;
    fs::path source;
};
Imported import_official();
Imported import_profile(const fs::path &partitions, const fs::path &scratch);
std::vector<Slot> load_cached();
void save_cached(const Imported &);
using Packet = std::array<uint8_t, 512>;
Packet ui_packet(const std::vector<Slot> &);
Packet brightness_packet(int);
Packet settings_packet();
bool valid_reply(const std::vector<uint8_t> &, uint8_t command);
struct DeviceInfo {
    std::wstring path;
    USHORT input = 0, output = 0, usage_page = 0, usage = 0;
};
std::vector<DeviceInfo> enumerate_devices();
class Screen {
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DeviceInfo info_;
    bool transfer(bool write, void *, DWORD, DWORD &);

  public:
    ~Screen() {
        close();
    }
    bool open();
    void close();
    bool exchange(const Packet &, std::vector<uint8_t> &);
    bool settings(int &brightness, int &mode);
    bool brightness(int);
    bool update(const std::vector<Slot> &);
    std::wstring error;
};
struct SensorValues {
    std::optional<double> cpu_max, cpu_total, cpu_utility, cpu_power, cpu_temp, gpu_load, gpu_power, gpu_temp;
};
class Sensors {
    struct Impl;
    Impl *impl_ = nullptr;

  public:
    Sensors() = default;
    ~Sensors();
    Sensors(const Sensors &) = delete;
    Sensors &operator=(const Sensors &) = delete;
    void initialize(const std::vector<Slot> &);
    void close();
    SensorValues sample();
    void apply(std::vector<Slot> &, const SensorValues &);
    std::wstring cpu_status, gpu_status;
};
struct Snapshot {
    std::vector<Slot> slots;
    std::wstring status = L"正在读取原版设置", cpu_status, gpu_status, source, config_status;
    int brightness = -1;
    bool paused = false, official = false;
    uint64_t updates = 0;
};
class Engine {
    std::mutex mutex_;
    Snapshot view_;
    std::thread thread_;
    HANDLE wake_ = nullptr;
    std::atomic<bool> stop_ = false, reload_ = false, paused_ = false;
    std::atomic<int> light_ = -1;
    void run();

  public:
    Engine();
    ~Engine();
    void reload();
    void pause(bool);
    void light(int);
    Snapshot snapshot();
};
Json diagnostics(bool read_sensors, bool probe);
Json bounded_run(int seconds);
Json test_brightness();
} // namespace mini
