# 参与开发

主分支为 main。先阅读 README 支持范围、docs/DEVELOPMENT.md 和协议记录。

- 设备支持变更需提供型号、VID/PID、字段依据及实机结果。
- 原版安装目录保持只读，厂商二进制与分析副本留在本地 n360-analysis，不加入提交。
- 只采集屏幕启用的指标，避免增加无用常驻采样、日志或运行时。
- 改动后运行 tools/build.ps1；协议变更需有相应测试和真机验证记录。
- 公开诊断前删除个人路径、PID、用户 SID、序列号和账号信息；原始探测留在 out/。
- 更新依赖时同步 dependencies.lock.json、对应源码与许可证。

PR 请说明行为变化、验证硬件、测试结果和未验证范围。发版见 docs/RELEASING.md。
