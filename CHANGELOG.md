# Changelog

## 0.1.0

首个公开版本：Qt 6 + C++ 中文桌面 API 调试工具。

- HTTP/HTTPS、参数与请求头、JSON/文本/表单、Basic/Bearer 认证。
- 环境变量、本地集合与历史、Postman v2.1 基础导入导出、cURL 导出。
- 响应格式化、原始响应、响应头、超时与取消。
- Windows x64、macOS Universal、Linux x64 标签发布工作流；测试和部署验证通过后才上传 Release。
- 发布包包含 Qt 运行依赖、许可、构建记录及 SHA-256 校验文件。

限制：这是 MVP；未包含脚本断言、WebSocket/gRPC、OAuth 流程和云同步。认证信息与环境变量目前以本地明文存储。平台包未配置付费代码签名证书；macOS 使用 ad-hoc 签名，未经过 Apple 公证。
