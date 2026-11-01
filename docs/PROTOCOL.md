# N360 协议与实现记录

本机 USB VID `0x374A`，PID `0xA017`，UsagePage `0xFF00`，Usage `1`。HID 输入、输出报告均为 513 字节：首字节 report ID 0，后续 512 字节是原版 USB 逻辑报文。Windows 自带 HidUsb 驱动即可通信。

下表按 **512 字节逻辑报文** 编址，空白字节补零。参数取自此前原版 USB helper 的反汇编；已在本机验证查询、文字更新应答及亮度设置。

| 操作 | 字节 |
| --- | --- |
| 读取设置 | `A5 5A 18 00 16` |
| 设置亮度 | `A5 5A 18 03 00 <brightness:u16le> 16` |
| 更新六个文字槽 | `A5 5A 1A F1 FF <6 × 40 bytes> 16` |

设置查询应答：`A5 5A 18 05 FF <brightness:u16le> <playMode:u16le> 16`。
亮度写入应答：`A5 5A 18 02 00 00 16`。
文字更新应答：`A5 5A 1A 02 FF 00 16`。

文字槽结构（每槽 40 字节）：

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 1 | enable |
| 1 | 1 | align |
| 2 | 2 | x，LE |
| 4 | 2 | y，LE |
| 6 | 4 | title RGB，LE |
| 10 | 10 | 标题，最多 9 字节并以零结束 |
| 20 | 4 | param RGB，LE |
| 24 | 6 | 参数文本，最多 5 字节并以零结束 |
| 30 | 4 | unit RGB，LE |
| 34 | 6 | 单位，最多 5 字节并以零结束 |

UTF-8 字符串按字节计算长度。单位编码 type 0 使用原版字面值；1～13 分别为 °C、°F、%、MHz、RPM、W、V、A、MB、GB、KB/s、MB/s、GB/s。原版视频、GUI 模式和时间同步命令不属于本版本续刷所需路径，程序只使用上述三种操作。

配置取自 `%LOCALAPPDATA%/MythCool/UserData/Partitions/*/Local Storage/leveldb` 的 `VK3DScreenSensorList`。Chromium 键以 `_mythcool://` 开头，以 `\x01VK3DScreenSensorList` 结尾，值首字节区分 UTF-16LE（0）和 Latin-1（1）。程序复制数据库文件（排除 LOCK 和文本 LOG）后才交给 LevelDB 打开，原版目录不创建锁、恢复日志或压缩表。

传感器、设备通信在单个工作线程运行；窗口线程只在可见时刷新控件。HID 使用 overlapped I/O，350ms 超时并取消、等待未完成 I/O；失败后关闭句柄，每 3 秒重试。只有通过设置查询后才开始发送文字更新。原版 `MythCool.exe` 存活时 mini 关闭设备和传感器句柄。型号和报告长度不匹配时不发送。

项目此前证据保存在 `n360-analysis/evidence`，包括 `hid-command-functions.asm.txt` 等。该目录仅供本地分析使用，不进入源码压缩包或应用发布包。

CPU 温度公式和 MSR 封装能量差分参考 LibreHardwareMonitor；SMN 访问遵守共享 `Global\Access_PCI` 互斥锁。线程读取封装能量时临时绑定逻辑 CPU 0，并恢复原亲和性。硬件层只调用已签名模块的 `ioctl_read_msr` 与 `ioctl_read_smn`。
