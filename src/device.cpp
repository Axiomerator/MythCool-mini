// SPDX-License-Identifier: MPL-2.0
#include "mini.h"
#include <cstring>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
namespace mini {
std::vector<DeviceInfo> enumerate_devices() {
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    std::vector<DeviceInfo> out;
    if (set == INVALID_HANDLE_VALUE)
        return out;
    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA d{};
        d.cbSize = sizeof(d);
        if (!SetupDiEnumDeviceInterfaces(set, nullptr, &guid, i, &d))
            break;
        DWORD n = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &d, nullptr, 0, &n, nullptr);
        std::vector<uint8_t> buf(n);
        auto *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)buf.data();
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &d, detail, n, nullptr, nullptr))
            continue;
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        HIDD_ATTRIBUTES a{};
        a.Size = sizeof(a);
        if (HidD_GetAttributes(h, &a) && a.VendorID == 0x374a && a.ProductID == 0xa017) {
            PHIDP_PREPARSED_DATA p = nullptr;
            if (HidD_GetPreparsedData(h, &p)) {
                HIDP_CAPS c{};
                if (HidP_GetCaps(p, &c) == HIDP_STATUS_SUCCESS)
                    out.push_back({detail->DevicePath, c.InputReportByteLength, c.OutputReportByteLength,
                                   c.UsagePage, c.Usage});
                HidD_FreePreparsedData(p);
            }
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}
void Screen::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(handle_, nullptr);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}
bool Screen::open() {
    if (handle_ != INVALID_HANDLE_VALUE)
        return true;
    auto list = enumerate_devices();
    for (auto &i : list) {
        if (i.input != 513 || i.output != 513)
            continue;
        HANDLE h = CreateFileW(i.path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            handle_ = h;
            info_ = i;
            error.clear();
            return true;
        }
        error = L"N360 被占用或无访问权限：" + error_text(GetLastError());
    }
    if (error.empty())
        error = list.empty() ? L"未找到 N360 (374A:A017)" : L"N360 HID report 长度不匹配，停止发送";
    return false;
}
bool Screen::transfer(bool write, void *b, DWORD n, DWORD &done) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        error = error_text(GetLastError());
        return false;
    }
    BOOL ok = write ? WriteFile(handle_, b, n, &done, &ov) : ReadFile(handle_, b, n, &done, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        DWORD w = WaitForSingleObject(ov.hEvent, 350);
        if (w == WAIT_OBJECT_0)
            ok = GetOverlappedResult(handle_, &ov, &done, FALSE);
        else {
            CancelIoEx(handle_, &ov);
            GetOverlappedResult(handle_, &ov, &done, TRUE);
            SetLastError(ERROR_TIMEOUT);
            ok = FALSE;
        }
    }
    DWORD code = ok ? 0 : GetLastError();
    CloseHandle(ov.hEvent);
    if (!ok)
        error = error_text(code);
    return ok != FALSE;
}
bool Screen::exchange(const Packet &p, std::vector<uint8_t> &reply) {
    if (!open())
        return false;
    std::array<uint8_t, 513> send{}, recv{};
    std::memcpy(send.data() + 1, p.data(), 512);
    DWORD n = 0;
    if (!transfer(true, send.data(), 513, n) || n != 513) {
        close();
        return false;
    }
    if (!transfer(false, recv.data(), 513, n) || n < 6) {
        close();
        return false;
    }
    reply.assign(recv.begin() + 1, recv.begin() + n);
    if (!valid_reply(reply, p[2])) {
        error = L"N360 应答格式不匹配";
        close();
        return false;
    }
    error.clear();
    return true;
}
bool Screen::settings(int &b, int &m) {
    std::vector<uint8_t> r;
    if (!exchange(settings_packet(), r))
        return false;
    if (r[3] != 5 || r[4] != 0xff) {
        error = L"亮度查询应答不匹配";
        return false;
    }
    b = r[5] | (r[6] << 8);
    m = r[7] | (r[8] << 8);
    if (b < 0 || b > 100) {
        error = L"设备返回了无效的亮度值";
        return false;
    }
    return true;
}
bool Screen::brightness(int b) {
    std::vector<uint8_t> r;
    if (!exchange(brightness_packet(b), r))
        return false;
    if (r[3] != 2 || r[4] != 0 || r[5] != 0) {
        error = L"设备拒绝亮度设置";
        return false;
    }
    return true;
}
bool Screen::update(const std::vector<Slot> &s) {
    std::vector<uint8_t> r;
    if (!exchange(ui_packet(s), r))
        return false;
    if (r[3] != 2 || r[4] != 0xff || r[5] != 0) {
        error = L"设备拒绝参数更新";
        return false;
    }
    return true;
}
} // namespace mini
