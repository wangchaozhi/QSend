#include "ui/mainwindow.h"
#include "ui/keyvaluetable.h"
#include "ui/theme.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

class UiTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qApp->setStyle("Fusion");
        qApp->setStyleSheet(appStyle());
    }

    void sendResponseAndSaveReload() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray received;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                received += socket->readAll();
                if (!received.contains("\r\n\r\n")) return;
                const QByteArray body = "{\"message\":\"QSend works\",\"ok\":true}";
                socket->write("HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nX-Test: ui\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        const auto path = temp.filePath("workspace.json");
        {
            MainWindow window(path);
            window.show();
            window.findChild<QLineEdit *>("requestName")->setText(QStringLiteral("界面测试请求"));
            window.findChild<QLineEdit *>("requestUrl")->setText(QString("http://127.0.0.1:%1/hello").arg(server.serverPort()));
            window.findChild<KeyValueTable *>("paramsEditor")->setValues({{true, "source", "ui"}});
            auto *send = window.findChild<QPushButton *>("sendButton");
            auto *cancel = window.findChild<QPushButton *>("cancelButton");
            auto *engine = window.findChild<qsend::RequestEngine *>();
            QVERIFY(engine);
            QSignalSpy finished(engine, &qsend::RequestEngine::finished);
            QTest::mouseClick(send, Qt::LeftButton);
            QVERIFY(!send->isEnabled());
            QVERIFY(cancel->isVisible());
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 4000);
            QVERIFY(send->isEnabled());
            QVERIFY(!cancel->isVisible());
            QVERIFY2(received.startsWith("GET /hello?source=ui HTTP/1.1"), received.constData());
            QVERIFY(window.findChild<QLabel *>("responseBadge")->text().startsWith("201"));
            QVERIFY(window.findChild<QPlainTextEdit *>("responsePretty")->toPlainText().contains("QSend works"));
            QCOMPARE(window.findChild<QListWidget *>("historyList")->count(), 1);
            QTest::mouseClick(window.findChild<QPushButton *>("saveRequest"), Qt::LeftButton);
            QVERIFY(QFile::exists(path));
            QVERIFY(!window.windowTitle().contains('*'));
        }
        {
            MainWindow restored(path);
            QCOMPARE(restored.findChild<QLineEdit *>("requestName")->text(), QStringLiteral("界面测试请求"));
            QCOMPARE(restored.findChild<QListWidget *>("historyList")->count(), 1);
        }
    }

    void authAndBodyControls() {
        QTemporaryDir temp;
        MainWindow window(temp.filePath("workspace.json"));
        window.show();
        auto *tabs = window.findChild<QTabWidget *>("requestTabs");
        tabs->setCurrentIndex(3);
        auto *auth = window.findChild<QComboBox *>("authType");
        auth->setCurrentIndex(auth->findData("bearer"));
        QVERIFY(window.findChild<QLineEdit *>("token")->isVisible());
        QVERIFY(!window.findChild<QLineEdit *>("password")->isVisible());
        auth->setCurrentIndex(auth->findData("basic"));
        QVERIFY(!window.findChild<QLineEdit *>("token")->isVisible());
        QVERIFY(window.findChild<QLineEdit *>("password")->isVisible());
        tabs->setCurrentIndex(2);
        auto *type = window.findChild<QComboBox *>("bodyType");
        auto *body = window.findChild<QPlainTextEdit *>("requestBody");
        type->setCurrentIndex(type->findData("json"));
        QVERIFY(body->isEnabled());
        body->setPlainText("{\"x\":1}");
        QTest::mouseClick(window.findChild<QPushButton *>("formatJson"), Qt::LeftButton);
        QVERIFY(body->toPlainText().contains('\n'));
        body->setPlainText("{broken}");
        QTest::mouseClick(window.findChild<QPushButton *>("formatJson"), Qt::LeftButton);
        QVERIFY(window.findChild<QLabel *>("inlineMessage")->isVisible());
        type->setCurrentIndex(type->findData("none"));
        QVERIFY(!body->isEnabled());
    }

    void invalidRequestAndRowEditing() {
        QTemporaryDir temp;
        MainWindow window(temp.filePath("workspace.json"));
        window.show();
        window.openUrl("file:///example");
        QTest::mouseClick(window.findChild<QPushButton *>("sendButton"), Qt::LeftButton);
        QVERIFY(window.findChild<QPushButton *>("sendButton")->isEnabled());
        QVERIFY(window.findChild<QLabel *>("inlineMessage")->isVisible());
        auto *params = window.findChild<KeyValueTable *>("paramsEditor");
        params->setValues({{true, "q", QStringLiteral("中文&空格 test")}, {false, "skip", "x"}});
        QCOMPARE(params->values().size(), 2);
        QTest::mouseClick(params->findChild<QPushButton *>("addRow"), Qt::LeftButton);
        QCOMPARE(params->table()->rowCount(), 3);
        params->table()->selectRow(1);
        QTest::mouseClick(params->findChild<QPushButton *>("removeRow"), Qt::LeftButton);
        QCOMPARE(params->table()->rowCount(), 2);
        QCOMPARE(params->values().size(), 1);
    }
};

QTEST_MAIN(UiTests)
#include "ui_tests.moc"
