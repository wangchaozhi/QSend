#include "ui/mainwindow.h"
#include "ui/theme.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QSslSocket>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QSend");
    QCoreApplication::setApplicationName("QSend");
    QCoreApplication::setApplicationVersion(QSEND_VERSION);
    app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#10151c"));
    palette.setColor(QPalette::WindowText, QColor("#dce4ee"));
    palette.setColor(QPalette::Base, QColor("#141c25"));
    palette.setColor(QPalette::Text, QColor("#dce4ee"));
    palette.setColor(QPalette::Button, QColor("#202c39"));
    palette.setColor(QPalette::ButtonText, QColor("#dce4ee"));
    palette.setColor(QPalette::Highlight, QColor("#346853"));
    palette.setColor(QPalette::HighlightedText, QColor("#effff7"));
    app.setPalette(palette);
    app.setStyleSheet(appStyle());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QSend — 本地 API 调试工作台"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"workspace", "Workspace JSON path", "path"});
    parser.addOption({"url", "Initial request URL", "url"});
    parser.addOption({"send", "Send initial request immediately"});
    parser.addOption({"screenshot", "Save a window screenshot and exit (verification)", "path"});
    parser.addOption({"capture-delay", "Screenshot delay in milliseconds", "ms", "1500"});
    parser.addOption({"runtime-info", "Write version and TLS runtime diagnostics then exit", "path"});
    parser.process(app);
    if (parser.isSet("runtime-info")) {
        const QJsonObject info{{"version", QCoreApplication::applicationVersion()},
            {"qtVersion", qVersion()}, {"sslSupported", QSslSocket::supportsSsl()},
            {"sslBackend", QSslSocket::activeBackend()},
            {"sslBackends", QJsonArray::fromStringList(QSslSocket::availableBackends())}};
        const QByteArray data = QJsonDocument(info).toJson();
        QFile file(parser.value("runtime-info"));
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() ? 0 : 2;
    }
    MainWindow window(parser.value("workspace"));
    if (parser.isSet("url")) window.openUrl(parser.value("url"));
    window.show();
    if (parser.isSet("send")) QTimer::singleShot(100, &window, &MainWindow::sendRequest);
    if (parser.isSet("screenshot")) {
        QTimer::singleShot(qBound(300, parser.value("capture-delay").toInt(), 60000), &app, [&] {
            const bool saved = window.grab().save(parser.value("screenshot"));
            app.exit(saved ? 0 : 2);
        });
    }
    return app.exec();
}
