// SPDX-License-Identifier: MPL-2.0
#include "mini.h"
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <cstdio>
namespace mini {
namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t folder_name[] = L"MythCool-mini";
constexpr wchar_t folder_path[] = LR"(\MythCool-mini)";
class Bstr {
    BSTR value_ = nullptr;

  public:
    Bstr() = default;
    explicit Bstr(const std::wstring &s) : value_(SysAllocStringLen(s.data(), UINT(s.size()))) {
        if (!value_)
            throw std::bad_alloc();
    }
    explicit Bstr(const wchar_t *s) : Bstr(std::wstring(s)) {}
    ~Bstr() {
        SysFreeString(value_);
    }
    Bstr(const Bstr &) = delete;
    Bstr &operator=(const Bstr &) = delete;
    operator BSTR() const {
        return value_;
    }
    BSTR *put() {
        SysFreeString(value_);
        value_ = nullptr;
        return &value_;
    }
    std::wstring str() const {
        return value_ ? std::wstring(value_, SysStringLen(value_)) : std::wstring();
    }
};
struct Apartment {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Apartment() {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};
void check(HRESULT hr, const char *operation) {
    if (FAILED(hr)) {
        char code[32];
        std::snprintf(code, sizeof(code), " (0x%08lX)", unsigned long(hr));
        throw std::runtime_error(std::string(operation) + code + ": " + utf8(error_text(DWORD(hr))));
    }
}
bool missing(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}
std::wstring user_sid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        check(HRESULT_FROM_WIN32(GetLastError()), "Open process token");
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    BOOL ok = GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes);
    DWORD code = ok ? 0 : GetLastError();
    CloseHandle(token);
    check(HRESULT_FROM_WIN32(code), "Read user SID");
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &text))
        check(HRESULT_FROM_WIN32(GetLastError()), "Convert user SID");
    std::wstring sid = text;
    LocalFree(text);
    return sid;
}
bool same_user(const std::wstring &value, const std::wstring &sid) {
    if (value == sid)
        return true;
    DWORD bytes = 0, chars = 0;
    SID_NAME_USE type;
    LookupAccountNameW(nullptr, value.c_str(), nullptr, &bytes, nullptr, &chars, &type);
    if (!bytes)
        return false;
    std::vector<BYTE> data(bytes);
    std::wstring domain(chars, 0);
    if (!LookupAccountNameW(nullptr, value.c_str(), data.data(), &bytes, domain.data(), &chars, &type))
        return false;
    LPWSTR actual = nullptr;
    if (!ConvertSidToStringSidW(data.data(), &actual))
        return false;
    bool equal = sid == actual;
    LocalFree(actual);
    return equal;
}
std::wstring xml(const std::wstring &s) {
    std::wstring out;
    for (auto c : s)
        switch (c) {
        case L'&':
            out += L"&amp;";
            break;
        case L'<':
            out += L"&lt;";
            break;
        case L'>':
            out += L"&gt;";
            break;
        case L'\"':
            out += L"&quot;";
            break;
        case L'\'':
            out += L"&apos;";
            break;
        default:
            out += c;
        }
    return out;
}
bool same_path(const std::wstring &a, const fs::path &b) {
    auto left = fs::path(a).lexically_normal().wstring(), right = b.lexically_normal().wstring();
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
struct Scheduler {
    Apartment apartment;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    std::wstring sid = user_sid();
    Scheduler(bool create) {
        if (apartment.hr != RPC_E_CHANGED_MODE)
            check(apartment.hr, "Initialize COM");
        check(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(service.GetAddressOf())),
              "Open Task Scheduler");
        VARIANT empty{};
        check(service->Connect(empty, empty, empty, empty), "Connect Task Scheduler");
        HRESULT hr = service->GetFolder(Bstr(folder_path), folder.GetAddressOf());
        if (create && missing(hr)) {
            ComPtr<ITaskFolder> root;
            check(service->GetFolder(Bstr(L"\\"), root.GetAddressOf()), "Open task root");
            hr = root->CreateFolder(Bstr(folder_name), empty, folder.GetAddressOf());
        }
        if (!missing(hr))
            check(hr, "Open mini task folder");
    }
};
StartupState read(Scheduler &s) {
    StartupState result;
    result.task_path = std::wstring(folder_path) + L"\\" + s.sid;
    if (!s.folder)
        return result;
    ComPtr<IRegisteredTask> task;
    HRESULT hr = s.folder->GetTask(Bstr(s.sid), task.GetAddressOf());
    if (missing(hr))
        return result;
    check(hr, "Read mini startup task");
    result.registered = true;
    VARIANT_BOOL enabled = VARIANT_FALSE;
    check(task->get_Enabled(&enabled), "Read task state");
    result.enabled = enabled != VARIANT_FALSE;
    ComPtr<ITaskDefinition> def;
    check(task->get_Definition(def.GetAddressOf()), "Read task definition");
    ComPtr<IPrincipal> principal;
    check(def->get_Principal(principal.GetAddressOf()), "Read principal");
    TASK_RUNLEVEL_TYPE level;
    TASK_LOGON_TYPE logon;
    Bstr user;
    check(principal->get_RunLevel(&level), "Read task run level");
    check(principal->get_LogonType(&logon), "Read task logon type");
    check(principal->get_UserId(user.put()), "Read task user");
    ComPtr<IActionCollection> actions;
    check(def->get_Actions(actions.GetAddressOf()), "Read task actions");
    LONG count = 0;
    check(actions->get_Count(&count), "Count task actions");
    if (count != 1)
        return result;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    check(actions->get_Item(1, action.GetAddressOf()), "Read task action");
    if (FAILED(action.As(&exec)))
        return result;
    Bstr path, args, working;
    check(exec->get_Path(path.put()), "Read executable");
    check(exec->get_Arguments(args.put()), "Read arguments");
    check(exec->get_WorkingDirectory(working.put()), "Read working directory");
    result.executable = path.str();
    ComPtr<ITriggerCollection> triggers;
    check(def->get_Triggers(triggers.GetAddressOf()), "Read task triggers");
    check(triggers->get_Count(&count), "Count task triggers");
    if (count != 1)
        return result;
    ComPtr<ITrigger> trigger;
    ComPtr<ILogonTrigger> login;
    check(triggers->get_Item(1, trigger.GetAddressOf()), "Read task trigger");
    if (FAILED(trigger.As(&login)))
        return result;
    Bstr login_user;
    VARIANT_BOOL trigger_on = VARIANT_FALSE;
    check(login->get_UserId(login_user.put()), "Read trigger user");
    check(login->get_Enabled(&trigger_on), "Read trigger state");
    result.enabled = result.enabled && trigger_on != VARIANT_FALSE;
    result.matches = same_path(path.str(), exe_path()) && args.str() == L"--tray" &&
                     same_path(working.str(), exe_dir()) && same_user(user.str(), s.sid) &&
                     same_user(login_user.str(), s.sid) && level == TASK_RUNLEVEL_HIGHEST &&
                     logon == TASK_LOGON_INTERACTIVE_TOKEN;
    return result;
}
} // namespace
bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}
std::wstring startup_task_xml(const fs::path &executable, const std::wstring &sid) {
    return LR"(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
 <RegistrationInfo><Author>MythCool-mini</Author><Description>N360 monitoring, current user logon, elevated tray.</Description></RegistrationInfo>
 <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>)" +
           xml(sid) + LR"(</UserId><Delay>PT10S</Delay></LogonTrigger></Triggers>
 <Principals><Principal id="CurrentUser"><UserId>)" +
           xml(sid) +
           LR"(</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>
 <Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><StartWhenAvailable>true</StartWhenAvailable><AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled><ExecutionTimeLimit>PT0S</ExecutionTimeLimit></Settings>
 <Actions Context="CurrentUser"><Exec><Command>)" +
           xml(executable.wstring()) + LR"(</Command><Arguments>--tray</Arguments><WorkingDirectory>)" +
           xml(executable.parent_path().wstring()) + LR"(</WorkingDirectory></Exec></Actions>
</Task>)";
}
StartupState startup_state() {
    try {
        Scheduler s(false);
        return read(s);
    } catch (const std::exception &e) {
        StartupState s;
        s.error = wide(e.what());
        return s;
    }
}
StartupState set_startup(bool enabled) {
    try {
        Scheduler s(enabled);
        if (enabled) {
            Bstr document(startup_task_xml(exe_path(), s.sid));
            Bstr sid(s.sid);
            VARIANT user{}, empty{};
            user.vt = VT_BSTR;
            user.bstrVal = sid;
            ComPtr<IRegisteredTask> task;
            check(s.folder->RegisterTask(Bstr(s.sid), document, TASK_CREATE_OR_UPDATE, user, empty,
                                         TASK_LOGON_INTERACTIVE_TOKEN, empty, task.GetAddressOf()),
                  "Register startup task");
        } else if (s.folder) {
            HRESULT hr = s.folder->DeleteTask(Bstr(s.sid), 0);
            if (!missing(hr))
                check(hr, "Remove startup task");
        }
        auto state = read(s);
        if ((enabled && (!state.enabled || !state.matches)) || (!enabled && state.registered))
            throw std::runtime_error("Startup read-back verification failed");
        return state;
    } catch (const std::exception &e) {
        StartupState s;
        s.error = wide(e.what());
        return s;
    }
}
} // namespace mini
