# Third-party components

QSend source code uses the MIT License; see `LICENSE`. Binary packages also contain dependencies with their own licenses.

## Qt 6.8.3

The application dynamically links Qt Core, Gui, Widgets and Network and ships the necessary platform, image and TLS plugins. Qt is copyright The Qt Company Ltd. and other contributors. Compatible Qt libraries remain replaceable.

Release packages contain the upstream license texts in `licenses/Qt-6.8.3/` and the installed Qt package's SPDX inventories in `licenses/qt-sbom/` when supplied by Qt. These include the notices for third-party components embedded in Qt.

- [Corresponding Qt Base source](https://github.com/qt/qtbase/tree/v6.8.3)
- [Qt Base 6.8.3 source archive](https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz)
- [Qt licensing information](https://www.qt.io/licensing/)

## Platform runtime dependencies

Windows packages include Microsoft Visual C++ redistributable runtime DLLs and may contain the Direct3D compiler deployed by Qt. These are copyright Microsoft Corporation and follow the applicable Microsoft terms, not QSend's MIT license.

Linux uses the host operating system's glibc, graphics/font libraries and OpenSSL 3. Their distribution-provided copyright inventories are copied into the package when present. Qt's deployment tool includes Qt and non-system dependencies from the Qt SDK; the system dynamic loader and glibc are not bundled.

macOS uses Apple's system frameworks. Frameworks inside the application bundle are deployed Qt dependencies; system frameworks are not redistributed.
