# 构建与开发

环境：Windows x64，Visual Studio 2022 C++ 桌面工具和 Windows SDK，CMake ≥3.25、Git、PowerShell。

```powershell
.\tools\bootstrap.ps1
.\tools\build.ps1
```

输出 `build/Release/MythCool-mini.exe`、`PawnIOLib.dll` 和 `AMDFamily17.bin`。程序默认要求管理员身份。依赖和下载 SHA256 在 dependencies.lock.json；bootstrap 只准备依赖，不安装驱动。

`tools/build.ps1 -BuildDirectory 指定目录` 可选择开发构建位置。`tools/package.ps1` 自动准备固定依赖，在 build/release-build 独立构建并执行 CTest；不覆盖日常 dist。更新原创图标使用 Python 标准库运行 tools/generate_icon.py，普通编译无需 Python。

代码：core.cpp 处理槽与报文；config.cpp 从原版 LevelDB 的副本导入；sensors.cpp 按需采集；device.cpp 使用 Windows HID；engine.cpp 串行调度；autostart.cpp 操作当前用户最高权限任务；main.cpp 提供原生窗口和托盘。

CTest 覆盖协议、字段边界、指标语义、压缩 SSTable / WAL 配置读取及原始测试库保全，以及任务 XML 属性。不在 CI 安装驱动或发送真实硬件命令。

诊断 CLI：

- --diagnose --output result.json：读配置和传感器，报告管理员身份。
- --import-only --output layout.json：导入检查。
- --probe-screen --output screen.json：原版退出后查询亮度和模式。
- --run-for 15 --output run.json：有界真实参数续刷；先退出其他 mini 实例和原版。
- --test-brightness --output light.json：短暂息屏再恢复并读回；先退出其他控制程序。
- --autostart on / off / status：与窗口相同的系统注册实现。

Win32 子系统命令请用 Start-Process -Wait 等待。诊断中可能包含本机路径，仅发布脱敏材料。tools/verify-startup.ps1 是手动管理员实机测试，会临时注册并移除本程序真实任务，要求开始时自启关闭；它不属于普通 CTest。

资源采样使用 Python 标准库：

```powershell
python tools/measure_resources.py --pid 实际进程ID --seconds 60 --output out/resources.json
```

这是只读采样，不操作设备。进程 I/O 包含 USB 与驱动流量，不等于磁盘传输量。发版流程见 RELEASING.md。
