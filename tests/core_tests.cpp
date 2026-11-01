// SPDX-License-Identifier: MPL-2.0
#include "mini.h"
#include <fstream>
#include <iostream>
#include <leveldb/db.h>
#include <map>
#include <stdexcept>
using namespace mini;
static void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
int main() {
    try {
        Json j = Json::array(
            {{{"enable", 1},
              {"align", 3},
              {"x", 1620},
              {"y", 460},
              {"title", "GPU Power"},
              {"unit", "W"},
              {"titleRGB", "#123456"},
              {"paramRGB", "#abcdef"},
              {"unitRGB", "#FFFFFF"},
              {"sensor_about",
               {{"OutKey", "dGPU [#0]: NVIDIA GeForce RTX 3080 Ti"}, {"InnerKey", "GPU Power"}}}}});
        auto s = parse_slots(j);
        s[0].value = "325";
        auto b = ui_packet(s);
        check(b[0] == 0xa5 && b[1] == 0x5a && b[2] == 0x1a && b[3] == 241 && b[4] == 255 && b[245] == 22,
              "Official UI header and terminator");
        check(b[5] == 1 && b[6] == 3 && b[7] == 0x54 && b[8] == 6 && b[9] == 0xcc && b[10] == 1,
              "Preserve slot coordinates, alignment and enable flag");
        check(b[11] == 0x56 && b[12] == 0x34 && b[13] == 0x12 && b[15] == 'G' && b[24] == 0,
              "Colour byte order and 9-byte title boundary");
        check(b[29] == '3' && b[30] == '2' && b[31] == '5' && b[39] == 'W' && b[45] == 0,
              "Value/unit fields and unused slot disabled");
        check(parse_slots(slots_json(s))[0].outer == s[0].outer, "Import preserves hardware selection");
        check(identify("CPU [#0]", "Max CPU/Thread Usage") == Metric::CpuMax,
              "Do not substitute total usage for maximum thread usage");
        check(identify("GPU AMD Radeon", "GPU Power") == Metric::Unknown,
              "Do not query NVIDIA for AMD sensors");
        bool invalid = false;
        try {
            brightness_packet(101);
        } catch (...) {
            invalid = true;
        }
        check(invalid, "Reject invalid brightness");
        auto light = brightness_packet(50);
        check(light[2] == 24 && light[3] == 3 && light[4] == 0 && light[5] == 50 && light[6] == 0 &&
                  light[7] == 22,
              "Brightness golden packet");
        check(valid_reply({0xa5, 0x5a, 0x18, 5, 0xff, 50, 0, 0, 0, 0x16}, 0x18), "Read-settings response");
        check(!valid_reply({0xa5, 0x5a, 0x18, 255}, 0x18), "Reject truncated reply before indexing");
        j[0]["title"] = "a title longer than wire field";
        invalid = false;
        try {
            parse_slots(j);
        } catch (...) {
            invalid = true;
        }
        check(invalid, "Do not silently truncate original layout");

        auto task = startup_task_xml(fs::path(L"D:\\设备 & tools\\mini.exe"), L"S-1-5-21-123");
        check(task.find(L"设备 &amp; tools") != std::wstring::npos,
              "Escape XML-special characters in portable executable path");
        check(task.find(L"<RunLevel>HighestAvailable</RunLevel>") != std::wstring::npos &&
                  task.find(L"<LogonType>InteractiveToken</LogonType>") != std::wstring::npos,
              "Logon task uses current interactive user with full hardware access");
        check(task.find(L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>") != std::wstring::npos,
              "Resident task is not stopped after the scheduler default runtime limit");

        // Recover a pending WAL and compressed SSTables only in the copied DB.
        auto fixture = exe_dir() / (L"MythCool-mini-test-" + std::to_wstring(GetCurrentProcessId()));
        fs::create_directories(fixture / L"partitions" / L"profile" / L"Local Storage");
        auto dbpath = fixture / L"partitions" / L"profile" / L"Local Storage" / L"leveldb";
        auto readfiles = [](const fs::path &path) {
            std::map<std::wstring, std::string> result;
            for (auto &f : fs::directory_iterator(path))
                if (f.is_regular_file()) {
                    std::ifstream in(f.path(), std::ios::binary);
                    result[f.path().filename().wstring()] =
                        std::string(std::istreambuf_iterator<char>(in), {});
                }
            return result;
        };
        leveldb::Options opt;
        opt.create_if_missing = true;
        leveldb::DB *db = nullptr;
        auto st = leveldb::DB::Open(opt, utf8(dbpath.wstring()), &db);
        check(st.ok(), "Create profile fixture");
        std::string key = "_mythcool://fixture";
        key.push_back(0);
        key += "\1VK3DScreenSensorList";
        auto saved = slots_json(s);
        std::string value(1, 0);
        for (auto c : wide(saved.dump())) {
            value.push_back(char(c & 255));
            value.push_back(char(c >> 8));
        }
        check(db->Put(leveldb::WriteOptions{}, key, value).ok(), "Write UTF-16 profile");
        check(db->Put(leveldb::WriteOptions{}, "padding", std::string(65536, 'x')).ok(),
              "Write compressible fixture");
        db->CompactRange(nullptr, nullptr);
        delete db;
        db = nullptr;
        auto utf16 = import_profile(fixture / L"partitions", fixture / L"scratch");
        check(utf16.slots[0].x == 1620, "Read Chromium UTF-16 from compressed SSTable");
        check(leveldb::DB::Open(opt, utf8(dbpath.wstring()), &db).ok(), "Reopen fixture");
        saved[0]["x"] = 1500;
        value = std::string(1, 1) + saved.dump();
        check(db->Put(leveldb::WriteOptions{}, key, value).ok(), "Update layout in WAL");
        delete db;
        db = nullptr;
        auto before = readfiles(dbpath);
        auto imported = import_profile(fixture / L"partitions", fixture / L"scratch");
        check(imported.slots.size() == 1 && imported.slots[0].x == 1500,
              "Read latest WAL over compressed SSTable");
        check(before == readfiles(dbpath), "Original profile bytes and file set remain unchanged");
        delete db;
        fs::remove_all(fixture);
        std::cout << "All protocol and configuration checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
