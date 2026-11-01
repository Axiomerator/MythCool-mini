// SPDX-License-Identifier: MPL-2.0
// AMD package-energy and Tctl/Tdie formulas follow LibreHardwareMonitor
// Hardware/Cpu/Amd17Cpu.cs (LibreHardwareMonitor contributors, MPL-2.0).
#include "mini.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <intrin.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <set>
namespace mini {
struct Sensors::Impl {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER usage = nullptr, utility = nullptr;
    struct CpuTimes {
        LARGE_INTEGER idle, kernel, user, reserved[2];
        ULONG reserved2;
    };
    static_assert(sizeof(CpuTimes) == 48);
    using CpuQuery = LONG(NTAPI *)(ULONG, PVOID, ULONG, PULONG);
    CpuQuery cpu_query = nullptr;
    std::vector<CpuTimes> previous;
    HMODULE nvml = nullptr, pawn = nullptr;
    HANDLE executor = nullptr, pci = nullptr;
    void *gpu = nullptr;
    std::set<Metric> needed;
    using NvInit = int (*)();
    using NvShutdown = int (*)();
    using NvCount = int (*)(unsigned *);
    using NvHandle = int (*)(unsigned, void **);
    using NvName = int (*)(void *, char *, unsigned);
    using NvTemp = int (*)(void *, unsigned, unsigned *);
    using NvPower = int (*)(void *, unsigned *);
    struct Util {
        unsigned gpu, memory;
    };
    using NvUtil = int (*)(void *, Util *);
    NvShutdown nv_shutdown = nullptr;
    NvTemp nv_temp = nullptr;
    NvPower nv_power = nullptr;
    NvUtil nv_util = nullptr;
    bool nv_started = false;
    using Open = HRESULT(WINAPI *)(HANDLE *);
    using Load = HRESULT(WINAPI *)(HANDLE, const unsigned char *, size_t);
    using Exec = HRESULT(WINAPI *)(HANDLE, const char *, const uint64_t *, size_t, uint64_t *, size_t,
                                   size_t *);
    using Close = HRESULT(WINAPI *)(HANDLE);
    Exec execute = nullptr;
    Close pawn_close = nullptr;
    bool energy_seeded = false;
    uint32_t energy = 0;
    double energy_unit = 0;
    std::chrono::steady_clock::time_point last;
    bool read(const char *fn, uint64_t in, uint64_t &out) {
        size_t count = 0;
        return executor && SUCCEEDED(execute(executor, fn, &in, 1, &out, 1, &count)) && count == 1;
    }
    bool needs(Metric m) {
        return needed.contains(m);
    }
    ~Impl() {
        if (query)
            PdhCloseQuery(query);
        if (executor && pawn_close)
            pawn_close(executor);
        if (pci)
            CloseHandle(pci);
        if (pawn)
            FreeLibrary(pawn);
        if (nv_started && nv_shutdown)
            nv_shutdown();
        if (nvml)
            FreeLibrary(nvml);
    }
};
Sensors::~Sensors() {
    close();
}
void Sensors::close() {
    delete impl_;
    impl_ = nullptr;
}
void Sensors::initialize(const std::vector<Slot> &slots) {
    close();
    impl_ = new Impl;
    auto &p = *impl_;
    cpu_status = L"无需采集 CPU";
    gpu_status = L"无需采集 GPU";
    for (auto &s : slots)
        if (s.enabled)
            p.needed.insert(s.metric);
    if (p.needs(Metric::CpuMax) || p.needs(Metric::CpuTotal)) {
        // NtQuerySystemInformation(8), documented by Microsoft. One entry per logical CPU.
        if (GetActiveProcessorGroupCount() == 1)
            p.cpu_query =
                (Impl::CpuQuery)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
        cpu_status = p.cpu_query ? L"CPU 占用：Windows 每线程忙碌时间" : L"CPU 占用：Windows 性能计数器";
    }
    if ((!p.cpu_query && (p.needs(Metric::CpuMax) || p.needs(Metric::CpuTotal))) ||
        p.needs(Metric::CpuUtility)) {
        if (PdhOpenQueryW(nullptr, 0, &p.query) == ERROR_SUCCESS) {
            if (!p.cpu_query && (p.needs(Metric::CpuMax) || p.needs(Metric::CpuTotal)))
                PdhAddEnglishCounterW(p.query, LR"(\Processor Information(*)\% Processor Time)", 0, &p.usage);
            if (p.needs(Metric::CpuUtility))
                PdhAddEnglishCounterW(p.query, LR"(\Processor Information(_Total)\% Processor Utility)", 0,
                                      &p.utility);
            PdhCollectQueryData(p.query);
        }
        if (!p.cpu_query)
            cpu_status = p.usage || p.utility ? L"CPU 占用：Windows 性能计数器" : L"CPU 性能计数器不可用";
    }
    if (p.needs(Metric::GpuLoad) || p.needs(Metric::GpuPower) || p.needs(Metric::GpuTemp)) {
        p.nvml = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (p.nvml) {
            auto init = (Impl::NvInit)GetProcAddress(p.nvml, "nvmlInit_v2");
            p.nv_shutdown = (Impl::NvShutdown)GetProcAddress(p.nvml, "nvmlShutdown");
            auto count = (Impl::NvCount)GetProcAddress(p.nvml, "nvmlDeviceGetCount_v2");
            auto handle = (Impl::NvHandle)GetProcAddress(p.nvml, "nvmlDeviceGetHandleByIndex_v2");
            auto name = (Impl::NvName)GetProcAddress(p.nvml, "nvmlDeviceGetName");
            p.nv_temp = (Impl::NvTemp)GetProcAddress(p.nvml, "nvmlDeviceGetTemperature");
            p.nv_power = (Impl::NvPower)GetProcAddress(p.nvml, "nvmlDeviceGetPowerUsage");
            p.nv_util = (Impl::NvUtil)GetProcAddress(p.nvml, "nvmlDeviceGetUtilizationRates");
            if (init && count && handle && name && p.nv_shutdown && init() == 0) {
                p.nv_started = true;
                unsigned n = 0;
                std::set<std::string> targets;
                for (auto &s : slots)
                    if (s.enabled && (s.metric == Metric::GpuLoad || s.metric == Metric::GpuPower ||
                                      s.metric == Metric::GpuTemp))
                        targets.insert(s.outer);
                unsigned matches = 0;
                if (count(&n) == 0)
                    for (unsigned i = 0; i < n; ++i) {
                        void *h = nullptr;
                        char model[128]{};
                        if (handle(i, &h) == 0 && name(h, model, sizeof(model)) == 0 && targets.size() == 1 &&
                            targets.begin()->find(model) != std::string::npos) {
                            p.gpu = h;
                            ++matches;
                        }
                    }
                if (matches != 1)
                    p.gpu = nullptr;
            }
        }
        gpu_status = p.gpu ? L"GPU：NVML，已匹配原版选择的显卡" : L"NVML 不可用或原版显卡无法唯一匹配";
    }
    if (p.needs(Metric::CpuPower) || p.needs(Metric::CpuTemp)) {
        int regs[4]{};
        __cpuid(regs, 0);
        char vendor[13]{};
        memcpy(vendor, &regs[1], 4);
        memcpy(vendor + 4, &regs[3], 4);
        memcpy(vendor + 8, &regs[2], 4);
        __cpuid(regs, 1);
        unsigned family = ((regs[0] >> 8) & 15) + ((regs[0] >> 20) & 255),
                 model = ((regs[0] >> 4) & 15) | ((regs[0] >> 12) & 240);
        if (std::string(vendor) != "AuthenticAMD" || family != 0x1a || model != 0x44) {
            cpu_status += L"；温度/功耗当前仅支持 Granite Ridge (9950X 系列)";
            return;
        }
        p.pawn = LoadLibraryExW((exe_dir() / L"PawnIOLib.dll").c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        HRESULT status = E_NOINTERFACE;
        if (p.pawn) {
            auto open = (Impl::Open)GetProcAddress(p.pawn, "pawnio_open");
            auto load = (Impl::Load)GetProcAddress(p.pawn, "pawnio_load");
            p.execute = (Impl::Exec)GetProcAddress(p.pawn, "pawnio_execute");
            p.pawn_close = (Impl::Close)GetProcAddress(p.pawn, "pawnio_close");
            if (open && load && p.execute && p.pawn_close) {
                status = open(&p.executor);
                if (SUCCEEDED(status)) {
                    std::ifstream f(exe_dir() / L"AMDFamily17.bin", std::ios::binary);
                    std::vector<unsigned char> module((std::istreambuf_iterator<char>(f)), {});
                    status = module.empty() ? E_FAIL : load(p.executor, module.data(), module.size());
                }
            }
        }
        if (FAILED(status)) {
            if (p.executor && p.pawn_close)
                p.pawn_close(p.executor);
            p.executor = nullptr;
            cpu_status += L"；CPU 温度/功耗未连接 PawnIO（请检查驱动并以管理员运行），HRESULT=" +
                          std::to_wstring((unsigned long)status);
            return;
        }
        p.pci = CreateMutexW(nullptr, FALSE, L"Global\\Access_PCI");
        uint64_t units = 0;
        if (p.read("ioctl_read_msr", 0xc0010299, units))
            p.energy_unit = std::ldexp(1., -int((units >> 8) & 31));
        cpu_status += L"；温度/封装功耗：PawnIO";
    }
}
SensorValues Sensors::sample() {
    SensorValues v;
    if (!impl_)
        return v;
    auto &p = *impl_;
    if (p.cpu_query) {
        std::vector<Impl::CpuTimes> current(GetActiveProcessorCount(0));
        ULONG length = 0;
        if (p.cpu_query(8, current.data(), ULONG(current.size() * sizeof(Impl::CpuTimes)), &length) >= 0 &&
            length == current.size() * sizeof(Impl::CpuTimes)) {
            if (p.previous.size() == current.size()) {
                double busiest = 0, all = 0, busy = 0;
                bool any = false;
                for (size_t i = 0; i < current.size(); ++i) {
                    auto &c = current[i];
                    auto &o = p.previous[i];
                    auto elapsed =
                        (c.kernel.QuadPart - o.kernel.QuadPart) + (c.user.QuadPart - o.user.QuadPart);
                    auto idle = c.idle.QuadPart - o.idle.QuadPart;
                    if (elapsed > 0 && idle >= 0 && idle <= elapsed) {
                        double used = double(elapsed - idle);
                        busiest = std::max(busiest, 100. * used / elapsed);
                        all += double(elapsed);
                        busy += used;
                        any = true;
                    }
                }
                if (any) {
                    v.cpu_max = busiest;
                    v.cpu_total = 100. * busy / all;
                }
            }
            p.previous = std::move(current);
        }
    }
    if (p.query && PdhCollectQueryData(p.query) == ERROR_SUCCESS) {
        if (p.usage) {
            DWORD bytes = 0, count = 0;
            auto status = PdhGetFormattedCounterArrayW(p.usage, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
            if (status == PDH_MORE_DATA) {
                std::vector<uint8_t> b(bytes);
                auto *a = (PDH_FMT_COUNTERVALUE_ITEM_W *)b.data();
                if (PdhGetFormattedCounterArrayW(p.usage, PDH_FMT_DOUBLE, &bytes, &count, a) ==
                    ERROR_SUCCESS) {
                    double max = 0;
                    bool any = false;
                    for (DWORD i = 0; i < count; ++i) {
                        if (a[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                            a[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
                            continue;
                        std::wstring name = a[i].szName;
                        double n = a[i].FmtValue.doubleValue;
                        if (name == L"_Total")
                            v.cpu_total = n;
                        else if (name.find(L"_Total") == name.npos) {
                            max = std::max(max, n);
                            any = true;
                        }
                    }
                    if (any)
                        v.cpu_max = max;
                }
            }
        }
        if (p.utility) {
            PDH_FMT_COUNTERVALUE n{};
            if (PdhGetFormattedCounterValue(p.utility, PDH_FMT_DOUBLE, nullptr, &n) == ERROR_SUCCESS &&
                (n.CStatus == 0 || n.CStatus == 1))
                v.cpu_utility = n.doubleValue;
        }
    }
    if (p.gpu) {
        unsigned n;
        if (p.needs(Metric::GpuTemp) && p.nv_temp && p.nv_temp(p.gpu, 0, &n) == 0)
            v.gpu_temp = n;
        if (p.needs(Metric::GpuPower) && p.nv_power && p.nv_power(p.gpu, &n) == 0)
            v.gpu_power = n / 1000.;
        Impl::Util u{};
        if (p.needs(Metric::GpuLoad) && p.nv_util && p.nv_util(p.gpu, &u) == 0)
            v.gpu_load = u.gpu;
    }
    if (p.executor) {
        GROUP_AFFINITY old{}, one{};
        one.Mask = 1;
        one.Group = 0;
        bool pinned = SetThreadGroupAffinity(GetCurrentThread(), &one, &old) != FALSE;
        uint64_t n = 0;
        if (pinned && p.needs(Metric::CpuPower) && p.energy_unit > 0 &&
            p.read("ioctl_read_msr", 0xc001029b, n)) {
            auto now = std::chrono::steady_clock::now();
            if (p.energy_seeded) {
                double sec = std::chrono::duration<double>(now - p.last).count();
                if (sec > 0)
                    v.cpu_power = uint32_t(uint32_t(n) - p.energy) * p.energy_unit / sec;
            }
            p.energy = (uint32_t)n;
            p.last = now;
            p.energy_seeded = true;
        }
        if (p.needs(Metric::CpuTemp) && p.pci) {
            DWORD w = WaitForSingleObject(p.pci, 10);
            if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) {
                if (p.read("ioctl_read_smn", 0x59800, n)) {
                    double t = ((uint32_t(n) >> 21) * 0.125);
                    if ((n & 0x80000) || ((n & 0x30000) == 0x30000))
                        t -= 49;
                    v.cpu_temp = t;
                }
                ReleaseMutex(p.pci);
            }
        }
        if (pinned)
            SetThreadGroupAffinity(GetCurrentThread(), &old, nullptr);
    }
    return v;
}
void Sensors::apply(std::vector<Slot> &s, const SensorValues &v) {
    for (auto &i : s) {
        std::optional<double> n;
        switch (i.metric) {
        case Metric::CpuMax:
            n = v.cpu_max;
            break;
        case Metric::CpuTotal:
            n = v.cpu_total;
            break;
        case Metric::CpuUtility:
            n = v.cpu_utility;
            break;
        case Metric::CpuPower:
            n = v.cpu_power;
            break;
        case Metric::CpuTemp:
            n = v.cpu_temp;
            break;
        case Metric::GpuLoad:
            n = v.gpu_load;
            break;
        case Metric::GpuPower:
            n = v.gpu_power;
            break;
        case Metric::GpuTemp:
            n = v.gpu_temp;
            break;
        default:
            break;
        }
        i.value = n && std::isfinite(*n) ? std::to_string(std::lround(*n)) : "--";
        if (i.value.size() > 5)
            i.value = "--";
    }
}
} // namespace mini
