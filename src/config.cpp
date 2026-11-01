// SPDX-License-Identifier: MPL-2.0
#include "leveldb/cache.h"
#include "leveldb/db.h"
#include "mini.h"
#include <fstream>
#include <memory>
#include <shlobj.h>
namespace mini {
static fs::path profile_root() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
        throw std::runtime_error("Cannot locate official profile");
    fs::path r = p;
    CoTaskMemFree(p);
    return r / L"MythCool" / L"UserData" / L"Partitions";
}
static std::string decode(const std::string &v) {
    if (v.empty())
        throw std::runtime_error("Empty Chromium storage value");
    if (v[0] == 0) {
        if ((v.size() - 1) % 2)
            throw std::runtime_error("Invalid UTF-16 storage value");
        std::wstring w;
        for (size_t i = 1; i + 1 < v.size(); i += 2)
            w.push_back((wchar_t)((unsigned char)v[i] | ((unsigned char)v[i + 1] << 8)));
        return utf8(w);
    }
    if (v[0] == 1) {
        std::wstring w;
        for (size_t i = 1; i < v.size(); ++i)
            w.push_back((unsigned char)v[i]);
        return utf8(w);
    }
    throw std::runtime_error("Unknown Chromium storage encoding");
}
Imported import_official() {
    return import_profile(profile_root(), data_dir() / L"import");
}
Imported import_profile(const fs::path &root, const fs::path &scratch) {
    if (!fs::exists(root))
        throw std::runtime_error("Official Myth.Cool profile was not found");
    Imported result;
    bool found = false;
    fs::file_time_type newest = fs::file_time_type::min();
    for (auto &partition : fs::directory_iterator(root)) {
        if (!partition.is_directory())
            continue;
        auto src = partition.path() / L"Local Storage" / L"leveldb";
        if (!fs::exists(src / L"CURRENT"))
            continue;
        GUID guid;
        CoCreateGuid(&guid);
        wchar_t name[64];
        StringFromGUID2(guid, name, 64);
        auto tmp = scratch / name;
        fs::create_directories(tmp);
        try {
            for (auto &f : fs::directory_iterator(src)) {
                auto n = f.path().filename().wstring();
                if (!f.is_regular_file() || n == L"LOCK" || n == L"LOG" || n == L"LOG.old")
                    continue;
                fs::copy_file(f.path(), tmp / f.path().filename(), fs::copy_options::overwrite_existing);
            }
            leveldb::Options opt;
            opt.create_if_missing = false;
            opt.paranoid_checks = true;
            opt.max_open_files = 16;
            opt.write_buffer_size = 65536;
            std::unique_ptr<leveldb::Cache> cache(leveldb::NewLRUCache(256 * 1024));
            opt.block_cache = cache.get();
            leveldb::DB *raw = nullptr;
            auto status = leveldb::DB::Open(opt, utf8(tmp.wstring()), &raw);
            if (!status.ok())
                throw std::runtime_error(status.ToString());
            std::unique_ptr<leveldb::DB> db(raw);
            std::unique_ptr<leveldb::Iterator> it(db->NewIterator(leveldb::ReadOptions{}));
            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                auto key = it->key().ToString();
                const std::string suffix = "\1VK3DScreenSensorList";
                if (key.size() < suffix.size() ||
                    key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0 ||
                    key.find("_mythcool://") != 0)
                    continue;
                auto slots = parse_slots(Json::parse(decode(it->value().ToString())));
                auto time = fs::last_write_time(src / L"CURRENT");
                if (!found || time > newest) {
                    result = {slots, src};
                    newest = time;
                    found = true;
                }
            }
            if (!it->status().ok())
                throw std::runtime_error(it->status().ToString());
            it.reset();
            db.reset();
            cache.reset();
            fs::remove_all(tmp);
        } catch (...) {
            std::error_code ec;
            fs::remove_all(tmp, ec);
            throw;
        }
    }
    if (!found)
        throw std::runtime_error("VK3DScreenSensorList was not found in the official profile");
    return result;
}
std::vector<Slot> load_cached() {
    std::ifstream f(data_dir() / L"layout.json");
    if (!f)
        return {};
    Json j;
    f >> j;
    return parse_slots(j.at("slots"));
}
void save_cached(const Imported &i) {
    auto p = data_dir() / L"layout.json";
    auto temp = p;
    temp += L".tmp";
    {
        std::ofstream f(temp, std::ios::binary);
        f << Json{{"source", utf8(i.source.wstring())}, {"slots", slots_json(i.slots)}}.dump(2);
        if (!f)
            throw std::runtime_error("Cannot save imported layout");
    }
    if (!MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace layout cache");
}
} // namespace mini
