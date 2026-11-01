// SPDX-License-Identifier: MPL-2.0
#include "mini.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <shlobj.h>
#include <stdexcept>
#include <tlhelp32.h>
namespace mini {
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string utf8(const std::wstring &w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring error_text(DWORD code) {
    wchar_t *b = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&b, 0, nullptr);
    std::wstring s = b ? b : L"Windows error " + std::to_wstring(code);
    if (b)
        LocalFree(b);
    return s;
}
fs::path data_dir() {
    auto r = exe_dir() / L"data";
    fs::create_directories(r);
    return r;
}
fs::path exe_path() {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    return fs::path(std::wstring(path, n));
}
fs::path exe_dir() {
    return exe_path().parent_path();
}
bool official_running() {
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h == INVALID_HANDLE_VALUE)
        return true;
    PROCESSENTRY32W p{};
    p.dwSize = sizeof(p);
    bool found = false;
    if (Process32FirstW(h, &p))
        do {
            if (!_wcsicmp(p.szExeFile, L"MythCool.exe")) {
                found = true;
                break;
            }
        } while (Process32NextW(h, &p));
    CloseHandle(h);
    return found;
}
static std::string lower(std::string s) {
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}
Metric identify(const std::string &o, const std::string &i) {
    auto a = lower(o), b = lower(i);
    if (a.find("cpu") != a.npos) {
        if (b == "max cpu/thread usage")
            return Metric::CpuMax;
        if (b == "total cpu usage")
            return Metric::CpuTotal;
        if (b == "total cpu utility")
            return Metric::CpuUtility;
        if (b == "cpu package power")
            return Metric::CpuPower;
        if (b == "cpu (tctl/tdie)")
            return Metric::CpuTemp;
    }
    if (a.find("gpu") != a.npos && a.find("nvidia") != a.npos) {
        if (b == "gpu core load")
            return Metric::GpuLoad;
        if (b == "gpu power")
            return Metric::GpuPower;
        if (b == "gpu temperature")
            return Metric::GpuTemp;
    }
    return Metric::Unknown;
}
static uint32_t color(const Json &j, const char *k) {
    if (!j.contains(k))
        return 0xffffff;
    auto v = j[k];
    if (v.is_number_unsigned() || v.is_number_integer())
        return v.get<uint32_t>() & 0xffffff;
    auto s = v.get<std::string>();
    if (!s.empty() && s[0] == '#')
        s.erase(0, 1);
    if (s.size() == 3)
        s = {s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6 || s.find_first_not_of("0123456789abcdefABCDEF") != s.npos)
        throw std::runtime_error("Invalid sensor colour");
    return (uint32_t)std::stoul(s, nullptr, 16);
}
std::vector<Slot> parse_slots(const Json &j) {
    if (!j.is_array() || j.size() > 6)
        throw std::runtime_error("N360 requires at most six sensor slots");
    std::vector<Slot> out;
    for (auto &v : j) {
        Slot s;
        s.enabled = v.value("enable", 1) != 0;
        s.align = v.value("align", 1);
        s.x = v.value("x", 0);
        s.y = v.value("y", 0);
        s.title = v.value("title", std::string{});
        s.unit = v.value("unit", std::string{});
        s.unit_type = v.value("unitType", 0);
        s.title_rgb = color(v, "titleRGB");
        s.param_rgb = color(v, "paramRGB");
        s.unit_rgb = color(v, "unitRGB");
        auto a = v.value("sensor_about", Json::object());
        s.outer = a.value("OutKey", std::string{});
        s.inner = a.value("InnerKey", std::string{});
        s.metric = identify(s.outer, s.inner);
        if (s.x < 0 || s.x > 65535 || s.y < 0 || s.y > 65535 || s.align < 0 || s.align > 3 ||
            s.title.size() > 9 || s.unit.size() > 5 || s.unit_type < 0 || s.unit_type > 13)
            throw std::runtime_error("Sensor layout exceeds the official protocol limits");
        out.push_back(s);
    }
    return out;
}
Json slots_json(const std::vector<Slot> &s) {
    Json a = Json::array();
    for (auto &v : s)
        a.push_back({{"enable", v.enabled ? 1 : 0},
                     {"align", v.align},
                     {"x", v.x},
                     {"y", v.y},
                     {"title", v.title},
                     {"unit", v.unit},
                     {"unitType", v.unit_type},
                     {"titleRGB", v.title_rgb},
                     {"paramRGB", v.param_rgb},
                     {"unitRGB", v.unit_rgb},
                     {"sensor_about", {{"OutKey", v.outer}, {"InnerKey", v.inner}}}});
    return a;
}
static void u16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static void text(uint8_t *p, size_t capacity, const std::string &s) {
    std::memcpy(p, s.data(), std::min(s.size(), capacity - 1));
}
Packet ui_packet(const std::vector<Slot> &s) {
    if (s.size() > 6)
        throw std::runtime_error("Too many slots");
    Packet b{};
    b[0] = 0xa5;
    b[1] = 0x5a;
    b[2] = 0x1a;
    b[3] = 0xf1;
    b[4] = 0xff;
    const std::array<std::string, 14> units = {"",
                                               "\xc2\xb0"
                                               "C",
                                               "\xc2\xb0"
                                               "F",
                                               "%",
                                               "MHz",
                                               "RPM",
                                               "W",
                                               "V",
                                               "A",
                                               "MB",
                                               "GB",
                                               "KB/s",
                                               "MB/s",
                                               "GB/s"};
    for (size_t i = 0; i < s.size(); ++i) {
        auto &v = s[i];
        auto *p = b.data() + 5 + 40 * i;
        p[0] = v.enabled ? 1 : 0;
        p[1] = (uint8_t)v.align;
        u16(p + 2, v.x);
        u16(p + 4, v.y);
        u32(p + 6, v.title_rgb);
        text(p + 10, 10, v.title);
        u32(p + 20, v.param_rgb);
        text(p + 24, 6, v.value);
        u32(p + 30, v.unit_rgb);
        text(p + 34, 6, v.unit_type > 0 ? units.at(v.unit_type) : v.unit);
    }
    b[245] = 0x16;
    return b;
}
Packet brightness_packet(int n) {
    if (n < 0 || n > 100)
        throw std::runtime_error("Brightness must be 0..100");
    Packet b{};
    b[0] = 0xa5;
    b[1] = 0x5a;
    b[2] = 0x18;
    b[3] = 3;
    b[4] = 0;
    u16(b.data() + 5, n);
    b[7] = 0x16;
    return b;
}
Packet settings_packet() {
    Packet b{};
    b[0] = 0xa5;
    b[1] = 0x5a;
    b[2] = 0x18;
    b[3] = 0;
    b[4] = 0x16;
    return b;
}
bool valid_reply(const std::vector<uint8_t> &r, uint8_t c) {
    return r.size() >= 5 && r[0] == 0xa5 && r[1] == 0x5a && r[2] == c && size_t(r[3]) + 4 < r.size() &&
           r[size_t(r[3]) + 4] == 0x16;
}
} // namespace mini
