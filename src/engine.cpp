// SPDX-License-Identifier: MPL-2.0
#include "mini.h"
#include <fstream>
#include <psapi.h>
namespace mini {
Engine::Engine() {
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = std::thread([this] { run(); });
}
Engine::~Engine() {
    stop_ = true;
    SetEvent(wake_);
    if (thread_.joinable())
        thread_.join();
    CloseHandle(wake_);
}
void Engine::reload() {
    reload_ = true;
    SetEvent(wake_);
}
void Engine::pause(bool b) {
    paused_ = b;
    SetEvent(wake_);
}
void Engine::light(int n) {
    if (n >= 0 && n <= 100) {
        light_ = n;
        SetEvent(wake_);
    }
}
Snapshot Engine::snapshot() {
    std::lock_guard lock(mutex_);
    return view_;
}
void Engine::run() {
    Snapshot s;
    Screen screen;
    Sensors sensors;
    bool initialized = false, was_official = true;
    auto retry = std::chrono::steady_clock::now();
    auto import = [&] {
        try {
            auto i = import_official();
            save_cached(i);
            s.slots = i.slots;
            s.source = i.source.wstring();
            s.status = L"已读取原版设置";
            s.config_status.clear();
            initialized = false;
            sensors.close();
        } catch (const std::exception &e) {
            s.config_status = L"读取原版设置失败：" + wide(e.what());
            if (s.slots.empty())
                try {
                    s.slots = load_cached();
                    s.source = L"本地上次成功导入的设置";
                } catch (...) {
                }
        }
    };
    import();
    while (!stop_) {
        bool official = official_running();
        s.official = official;
        s.paused = paused_;
        if (reload_.exchange(false) || (!official && was_official)) {
            import();
            retry = std::chrono::steady_clock::now();
        }
        was_official = official;
        if (official) {
            screen.close();
            sensors.close();
            initialized = false;
            s.brightness = -1;
            light_ = -1;
            s.cpu_status = L"CPU 采集待机";
            s.gpu_status = L"GPU 采集待机";
            s.status = L"原版正在运行，mini 待机；退出原版后自动接管";
        } else if (s.slots.empty()) {
            screen.close();
            s.status = L"没有屏幕监控项目，请先用原版设置后重新读取";
        } else {
            if (s.paused) {
                sensors.close();
                initialized = false;
                s.cpu_status = L"CPU 采集已暂停";
                s.gpu_status = L"GPU 采集已暂停";
            } else {
                if (!initialized) {
                    sensors.initialize(s.slots);
                    initialized = true;
                }
                s.cpu_status = sensors.cpu_status;
                s.gpu_status = sensors.gpu_status;
                sensors.apply(s.slots, sensors.sample());
            }
            if (std::chrono::steady_clock::now() >= retry) {
                bool ok = screen.open();
                if (ok && s.brightness < 0) {
                    int mode = 0;
                    ok = screen.settings(s.brightness, mode);
                }
                if (ok) {
                    int requested = light_.exchange(-1);
                    if (requested >= 0) {
                        ok = screen.brightness(requested);
                        if (ok)
                            s.brightness = requested;
                    }
                    if (ok && !s.paused) {
                        ok = screen.update(s.slots);
                        if (ok)
                            ++s.updates;
                    }
                }
                if (ok)
                    s.status =
                        s.paused ? L"已暂停参数刷新；亮度菜单仍可使用" : L"正在续刷 N360 参数（1 秒/次）";
                else {
                    s.status = L"N360 通信失败：" + screen.error;
                    screen.close();
                    s.brightness = -1;
                    retry = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                }
            }
        }
        {
            std::lock_guard lock(mutex_);
            view_ = s;
            if (!s.config_status.empty())
                view_.status += L"；" + s.config_status;
        }
        WaitForSingleObject(wake_, 1000);
    }
}
Json diagnostics(bool read_sensors, bool probe) {
    Json j;
    j["version"] = "0.2.0";
    j["administrator"] = is_elevated();
    j["official_running"] = official_running();
    std::vector<Slot> slots;
    try {
        auto i = import_official();
        slots = i.slots;
        j["profile_source"] = utf8(i.source.wstring());
        j["slots"] = slots_json(slots);
    } catch (const std::exception &e) {
        j["config_error"] = e.what();
    }
    Json devices = Json::array();
    for (auto &i : enumerate_devices())
        devices.push_back({{"path", utf8(i.path)},
                           {"input_report_bytes", i.input},
                           {"output_report_bytes", i.output},
                           {"usage_page", i.usage_page},
                           {"usage", i.usage}});
    j["hid"] = devices;
    Sensors sensor_session;
    if (read_sensors && !slots.empty()) {
        auto &s = sensor_session;
        s.initialize(slots);
        s.sample();
        Sleep(1100);
        s.apply(slots, s.sample());
        j["cpu_backend"] = utf8(s.cpu_status);
        j["gpu_backend"] = utf8(s.gpu_status);
        Json vals = Json::array();
        for (auto &i : slots)
            vals.push_back({{"title", i.title},
                            {"sensor", i.inner},
                            {"value", i.value},
                            {"supported", i.metric != Metric::Unknown}});
        j["values"] = vals;
    }
    if (probe) {
        if (j["official_running"].get<bool>())
            j["probe_error"] = "Official software is running; no device command was sent";
        else {
            Screen d;
            int b, m;
            if (d.settings(b, m))
                j["screen"] = {{"brightness", b}, {"play_mode", m}};
            else
                j["probe_error"] = utf8(d.error);
        }
    }
    PROCESS_MEMORY_COUNTERS_EX p{};
    p.cb = sizeof(p);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&p, sizeof(p)))
        j["memory"] = {{"private_commit_bytes", p.PrivateUsage}, {"working_set_bytes", p.WorkingSetSize}};
    return j;
}
Json bounded_run(int seconds) {
    if (seconds < 2 || seconds > 3600)
        throw std::runtime_error("--run-for must be 2..3600 seconds");
    FILETIME created{}, exited{}, kernel_start{}, user_start{}, kernel_end{}, user_end{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel_start, &user_start);
    auto wall_start = std::chrono::steady_clock::now();
    Engine engine;
    Json samples = Json::array();
    for (int n = 0; n < seconds; ++n) {
        Sleep(1000);
        auto s = engine.snapshot();
        Json values = Json::array();
        for (auto &i : s.slots)
            values.push_back({{"title", i.title}, {"value", i.value}});
        samples.push_back(
            {{"second", n + 1}, {"updates", s.updates}, {"status", utf8(s.status)}, {"values", values}});
    }
    auto s = engine.snapshot();
    PROCESS_MEMORY_COUNTERS_EX p{};
    p.cb = sizeof(p);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&p, sizeof(p));
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel_end, &user_end);
    auto ft = [](FILETIME f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    double cpu_seconds = double(ft(kernel_end) + ft(user_end) - ft(kernel_start) - ft(user_start)) / 1e7;
    double wall_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();
    return {{"administrator", is_elevated()},
            {"cpu_seconds", cpu_seconds},
            {"wall_seconds", wall_seconds},
            {"cpu_percent_all_processors",
             100. * cpu_seconds / wall_seconds / GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)},
            {"cpu_backend", utf8(s.cpu_status)},
            {"gpu_backend", utf8(s.gpu_status)},
            {"brightness", s.brightness},
            {"updates", s.updates},
            {"samples", samples},
            {"memory", {{"working_set_bytes", p.WorkingSetSize}, {"private_commit_bytes", p.PrivateUsage}}}};
}
Json test_brightness() {
    if (official_running())
        throw std::runtime_error("Exit the official software before brightness testing");
    Screen d;
    int before = -1, mode = 0;
    if (!d.settings(before, mode))
        throw std::runtime_error(utf8(d.error));
    int dark = -1, darkmode = -1, after = -1, aftermode = -1;
    bool off_ok = d.brightness(0) && d.settings(dark, darkmode) && dark == 0;
    Sleep(250);
    bool restore_ok = d.brightness(before) && d.settings(after, aftermode) && after == before;
    return {{"initial_brightness", before},
            {"screen_off_verified", off_ok},
            {"restored_brightness", after},
            {"restore_verified", restore_ok},
            {"play_mode_unchanged", mode == darkmode && mode == aftermode},
            {"error", utf8(d.error)}};
}

} // namespace mini
