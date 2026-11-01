# 管理员登录自启

0.2.0 默认通过应用清单 `requireAdministrator` 申请管理员身份，手动双击按 Windows UAC 提示启动。状态页显示当前身份和可用监控项数，例如“管理员 · 6/6 项可用”。

本项目参考了 Peripheral Battery Checker 的开关行为：使用当前用户身份、读回核验实际系统注册、关闭移除、显示系统错误，并在激活窗口或打开托盘菜单时重新读取状态。其启动文件夹快捷方式适用于无需提权的程序；mini 改用 Windows Task Scheduler 原生 COM 接口，以支持管理员身份的登录自启。

**操作**

窗口右下角和托盘右键菜单都提供“开机自启（管理员）”。默认关闭。开启时写入 `\MythCool-mini\<当前用户 SID>`；关闭时删除该用户的这条任务。关闭自启开关后，当前监控进程继续工作。

任务只在当前用户登录桌面后运行，延迟 10 秒，采用 `InteractiveToken` 和 `HighestAvailable`；目标为实际 EXE，参数 `--tray`，工作目录为 EXE 所在文件夹。无需保存密码。注册时需管理员身份，本机用户已具备并使用该权限。任务不设置常驻运行时限，也不因电池状态停止。计划任务的系统注册元数据由 Windows 保存，软件和布局缓存仍在工程目录内。

重新打开窗口或托盘菜单，会读取任务的实际启用状态、目标路径、参数、工作目录、运行用户及权限；操作失败会给出系统错误。任务管理器的“启动应用”不一定列出计划任务，应在“任务计划程序 → 任务计划程序库 → MythCool-mini”中核对。移动 EXE 后重新开启开关可更新目标。

CLI 同样可用：

```powershell
$exe='D:\Apps\MythCool-mini\MythCool-mini.exe'
Start-Process $exe -Wait -ArgumentList '--autostart on --output startup.json'
Start-Process $exe -Wait -ArgumentList '--autostart status --output startup.json'
Start-Process $exe -Wait -ArgumentList '--autostart off --output startup.json'
```

查询返回 0 表示有效启用，3 表示未启用或目标不匹配；操作错误返回 1，参数错误返回 2。普通启动会由清单请求本次管理员权限。注册完成后，登录时由已授权的最高权限任务启动托盘进程。

**已验证**

本机原生 COM 注册、读回、中文及带空格/`&` 路径、任务外部禁用、重新开启、重复关闭均通过。使用注册的任务执行实际启动，得到单个常驻进程，管理员权限和六项数据均正常；用户也确认界面能正常开启和关闭。实机回归测试会恢复自启关闭；日常启用状态由用户选择。

2026-11-01 用户已完成注销后重新登录验证：程序自动启动，冷头显示正常。开发验证脚本 `tools/verify-startup.ps1` 在管理员 PowerShell 中运行，会临时注册并移除真实当前用户任务，要求开始时自启关闭，结束后保持关闭。普通 CTest 不改变真实自启状态。

平台依据：[Microsoft 任务安全上下文](https://learn.microsoft.com/en-us/windows/win32/taskschd/security-contexts-for-running-tasks)、[Microsoft C++ 登录触发示例](https://learn.microsoft.com/en-us/windows/win32/taskschd/logon-trigger-example--c---)。
