# 三端 CI 与标签发布

工作流：[`ci-release.yml`](../.github/workflows/ci-release.yml)。Qt 固定为 6.8.3，Python 3.12；外部 Actions 固定到提交 SHA。

## 触发规则

| 事件 | 执行行为 |
| --- | --- |
| 推送 `main` | 三端构建、CTest、部署、隔离 SDK 的运行测试与打包，保存 Actions artifacts |
| 向 `main` 提交 PR | 相同验证，不发布 Release |
| Actions 页面手动运行 | 相同验证，不发布 Release |
| 推送 `vX.Y.Z` 标签 | 相同验证；三端全部成功后上传产物，并将草稿发布为 GitHub Release |

标签必须与 `CMakeLists.txt` 的 `project(QSend VERSION X.Y.Z)` 一致。应用版本与 macOS bundle 版本均来自这个值。只有发布 job 有 `contents: write`，使用 GitHub 自带的 `GITHUB_TOKEN`，不需要额外 PAT。构建 job 没有仓库写权限。

## 平台与产物

| 平台 | Runner | 产物与启动方式 |
| --- | --- | --- |
| Windows x64 | `windows-2022`、MSVC x64 | `QSend-X.Y.Z-windows-x64.zip`，解压后运行 `QSend.exe` |
| macOS Universal | `macos-14`、clang | `QSend-X.Y.Z-macos-universal.zip`，运行 `QSend.app`，同时含 arm64/x86_64 |
| Linux x64 | `ubuntu-22.04`、GCC | `QSend-X.Y.Z-linux-x64.tar.gz`，解压后运行 `./run-qsend.sh` |

Windows 使用 windeployqt 并附带 MSVC 运行库；macOS/Linux 使用 Qt 官方 CMake 部署 API。包内附 Qt、平台/TLS 插件、许可证、示例和 `BUILD-INFO.json`。Linux 保留系统 glibc/图形库依赖，不捆绑动态加载器；基线为 Ubuntu 22.04/glibc 2.35，兼容发行版需要自行确认基础图形库与 OpenSSL 3。

Ubuntu 22.04 桌面环境如缺少基础库，可以安装：

```sh
sudo apt-get install libgl1 libegl1 libxkbcommon-x11-0 libxcb-cursor0 \
  libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 \
  libxcb-xinerama0 libxcb-randr0 libxcb-shape0 libxcb-xfixes0 \
  libfontconfig1 libfreetype6 libssl3
```

macOS 部署目标为 12.0，但 CI 实际运行环境为 macOS 14，不能等同于每个旧系统版本都已实机验证。Universal 文件的两个架构都检查存在，运行测试使用 runner 的原生架构。macOS 使用 ad-hoc 签名，未配置 Developer ID 和 Apple 公证；Windows 也未配置 Authenticode。下载后的系统拦截不能通过 CI 代替验证，可选择从源码构建，不建议关闭系统安全保护。

## 发布一个版本

1. 完成改动，修改 CMake 中的版本，更新 `CHANGELOG.md` 和 `packaging/RELEASE_NOTES.md`，提交到 `main`。
2. 等待该提交的三端 CI 全部通过。
3. 创建并推送对应标签，例如首个版本：

```sh
git switch main
git pull --ff-only
git tag -a v0.1.0 -m "QSend 0.1.0"
git push origin v0.1.0
```

标签运行会重新验证同一提交，利用 Qt 下载缓存减少耗时。发布 job 会校验三份 manifest 的平台、版本、提交和 SHA-256，再创建草稿、上传完整资产并公开。输出三端包、`SHA256SUMS`、`build-manifest.json`；GitHub 另提供标准源代码归档。

未通过构建/测试/打包时不会公开半成品版本。如果上传阶段暂时失败，会留下草稿，修复基础设施后重跑该 run 即可补齐；已公开 Release 的文件不会被重跑覆盖。

**不要移动已公开的标签。** 下载或 runner 的临时失败可重跑相同提交；若需要修改代码，增加版本并创建新标签。不要把修复提交推到 `main` 后误以为原标签会自动使用新代码。

## 验证内容与证据

- CTest 运行核心和界面两套测试；任何失败会停止对应平台发布。
- `scripts/ci-smoke.py` 暂时重命名 CI 的 Qt SDK 目录并清理 Qt 环境变量，确认部署包不依赖开发 SDK；无论成功失败都会恢复目录。
- 从部署包启动，检查版本、TLS 后端可用性，发送本地 HTTP 请求，断言 HTTP 200 并生成窗口截图。
- 这是 TLS 后端可用性检查，不是三端的全部 HTTPS 证书/代理场景覆盖；完整跨平台交互与安全验证仍按 `ROADMAP.md` 推进。
- 三端诊断日志、运行时 JSON 和截图作为 Actions artifacts 保存 14 天；正式发布包长期保存在 Release。
- macOS `codesign --verify --deep --strict` 和 `lipo` 检查应用完整性与双架构；不代表 Apple 公证。

下载后校验：

```sh
# Linux
sha256sum -c SHA256SUMS
# macOS
shasum -a 256 -c SHA256SUMS
```

在 Windows 使用 `Get-FileHash .\QSend-X.Y.Z-windows-x64.zip -Algorithm SHA256`，与 `SHA256SUMS` 对应条目比较。若只下载单个平台，校验工具可能同时报告未下载的其他两个文件缺失，应核对所下载文件的对应条目。

## 本地排查

```powershell
# Windows：自动初始化同一 MSVC 构建进程
.\scripts\build-windows.ps1 -QtPath C:\Qt\6.8.3\msvc2022_64 -DeployDir .\dist
```

Unix 部署需要配置 `-DQSEND_DEPLOY_RUNTIME=ON -DCMAKE_INSTALL_LIBDIR=lib`，然后调用相应打包脚本，参数依次为构建目录、全新 staging 目录、Qt 根目录。脚本不会递归删除已有 staging。

`scripts/release.py metadata` 可检查版本；`prepare` 复制许可/示例/元数据；`archive` 生成单个平台归档和 manifest；`collect` 校验三端产物并生成总校验文件。CI 的 `--isolate-qt` 仅允许在 `CI=true` 时使用，不应对日常 Qt 安装运行。
