# 验证记录

验证日期：2026-09-26；Windows 11 x64；Qt 6.8.3；MSVC 2022 19.38；Release 构建。

- 主工程 CMake / Ninja 构建成功。
- `scripts/build-windows.ps1` 在新的构建目录独立编译成功。
- CTest：2 / 2 测试套件通过。
- 核心测试：15 个功能测试通过，另有初始化和清理各 1 项。
- 界面测试：3 个功能测试通过，另有初始化和清理各 1 项。
- 合计：18 个功能测试通过；Qt Test 日志合计 22 项通过，0 失败。

核心测试覆盖真实 TCP 请求中的 JSON、表单、查询参数编码、启停条目、Bearer / Basic 认证、HEAD 响应、HTTP 4xx、超时后重用、取消、非法 URL、缺失变量、同源 / 跨源 / 手动重定向、10 MiB 上限、本地持久化、损坏工作区保护、Postman 嵌套导入与两种往返导出（包含和移除 QSend 扩展）。

界面测试通过 Qt Test 操作发送、编辑、格式化和保存控件，确认真实 HTTP 201 响应能显示、历史记录会保存、重启后请求可恢复、认证字段切换正确、非法请求显示错误、参数行可添加和删除。

额外手动运行验证：

- 使用打包后的 `QSend.exe`，独立于 Qt 的 PATH 环境，向附带本地服务发送 GET，获得 HTTP 200。
- 使用打包程序向 `https://httpbin.org/get` 发送 HTTPS 请求，获得 HTTP 200；Windows Schannel TLS 插件可用。
- 使用应用自己的窗口渲染生成截图并检查中文、表格、响应 JSON、按钮和布局。截图展示本地服务返回的真实数据。

## 三端 CI 发布验证

2026-09-26 的 [三端验证运行](https://github.com/wangchaozhi/QSend/actions/runs/36215789191) 全部通过，源码提交 `8bed70c`。各端均使用 Qt 6.8.3，运行相同的 18 个功能测试，再打包并暂时隐藏 Qt SDK，验证独立启动、应用版本、TLS 后端、本地 HTTP 200 和窗口截图。

| 平台 | 构建与测试 | 部署包验证 |
| --- | --- | --- |
| Windows x64 / MSVC | CTest 2/2 | ZIP，Schannel TLS |
| macOS Universal / clang | CTest 2/2，原生 arm64 运行 | ZIP，arm64/x86_64 架构和 ad-hoc 签名校验 |
| Linux x64 / GCC | CTest 2/2 | tar.gz，OpenSSL TLS |

这次运行还修复了测试 HTTP 服务销毁连接时的回调生命周期问题。本地 Windows 核心、界面套件各连续运行 10 次，全部通过。Windows 打包测试改用原生 `windows` 平台插件，避免 offscreen 后端找不到系统字体导致诊断截图出现方框；本地原生截图已确认中文和 JSON 正常显示。

标签发布仍会对对应提交重新执行全部门禁。最新证据见 [Actions](https://github.com/wangchaozhi/QSend/actions/workflows/ci-release.yml)，正式下载与校验和见 [Releases](https://github.com/wangchaozhi/QSend/releases)。平台依赖、签名限制和复现步骤见 [发布说明](docs/RELEASING.md)。

当前验证不等于全部系统版本、HTTPS 证书/代理组合、完整键盘交互、安装升级或 Postman 全部能力均已覆盖。Windows 尚无 Authenticode 签名，macOS 尚无 Developer ID 与 Apple 公证。
