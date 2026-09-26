Qt 6 + C++ 中文 API 调试工作台的首个公开版本。

### 下载与运行

- **Windows x64**：下载 `windows-x64.zip`，解压后启动 `QSend.exe`。
- **macOS Universal**：下载 `macos-universal.zip`，将 `QSend.app` 放到应用目录，支持 Apple Silicon 和 Intel。应用使用 ad-hoc 签名，未配置 Developer ID 或 Apple 公证，系统可能拦截；可从源码构建，不需要关闭系统安全保护。
- **Linux x64**：下载 `linux-x64.tar.gz`，解压后运行 `./run-qsend.sh`。构建基线 Ubuntu 22.04/glibc 2.35，需要桌面图形基础库与 OpenSSL 3；无需预装 Qt。详见仓库的 `docs/RELEASING.md`。

三端包均在 CI 中经过构建、核心/界面测试、隔离 Qt SDK 的启动与 HTTP 请求测试，并检查 TLS 后端可用性。`SHA256SUMS` 用于校验下载，`build-manifest.json` 记录各平台构建信息及源代码提交。

### 当前能力

HTTP/HTTPS、参数与请求头、JSON/文本/表单、Basic/Bearer、环境变量、集合、历史、响应查看、Postman v2.1 基础导入导出及 cURL 导出。

这是 MVP；暂不包含 WebSocket/gRPC、脚本断言、OAuth 授权流程或云同步。请求和环境变量仍在本机明文保存。开发规划见仓库 `ROADMAP.md`。
