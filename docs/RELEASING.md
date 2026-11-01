# 维护与发布

主分支 main。源码、原创图形、文档、对应第三方源码与许可证纳入 Git。build、dist、releases、out、third_party、n360-analysis、个人 data 不纳入 Git。厂商二进制、固件和个人视频不公开。

生成发行版：

```powershell
.\tools\package.ps1
```

脚本在 build/release-build 独立构建、执行 CTest，核对 CMake 与 EXE 版本，然后按清单打包到 releases：

- MythCool-mini-版本-windows-x64.zip：程序、说明、许可证、对应源码。
- MythCool-mini-版本-source.zip：项目源码、固定依赖清单和构建工具。
- 两个 ZIP 各自的 .sha256。
- release-notes.md。

打包不读取个人 data，不覆盖运行中的 dist。使用者应从 Releases 下载 windows-x64 应用包；GitHub 的 Source code ZIP 仅是源码。

后续版本：修改 CMakeLists.txt、src/resources.rc 与 src/app.manifest 的版本，同时更新诊断版本、CHANGELOG.md、docs/RELEASE-NOTES.md 和便携说明。运行 package.ps1，完成必要的真机验证，再提交 main。

推送 main / PR 会构建、测试和保存 14 天的 Actions 构建产物。准备好公开发行时推送版本标签，例如：

```powershell
git tag -a v0.2.0 -m "Release v0.2.0"
git push origin v0.2.0
```

标签必须与源码版本一致。标签构建成功后，工作流自动公开发布对应 Release、ZIP、SHA256 和发行说明；它不会创建草稿。发布前应先在 main 检查构建结果。已发布版本有修改时使用新版本号，勿移动已发布标签。

构建作业只读仓库；只有标签发行作业获得当前仓库 contents 写入权限，使用 GitHub 自动提供的 GITHUB_TOKEN，无需额外个人令牌。Actions 均固定官方提交。手动备用方式：Releases → Draft a new release，选择版本标签，粘贴本地 release-notes.md，上传 ZIP 和 SHA256 后发布。

GitHub 新仓库应为空仓库，不预生成 README 或许可证；已有本地提交推送 main 后再发标签。当前远程为 https://github.com/Axiomerator/MythCool-mini 。
