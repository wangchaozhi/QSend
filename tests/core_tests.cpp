#include "core_tests.h"
#include "core/models.h"
#include "core/requestengine.h"
#include "core/workspacestore.h"

#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

using namespace qsend;

namespace {

struct ReceivedRequest {
    QByteArray method;
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

// A real local HTTP peer: assertions inspect bytes sent over TCP, rather than
// reproducing RequestEngine's URL/body construction in the test.
class LocalHttpServer : public QObject {
public:
    LocalHttpServer()
    {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = server_.nextPendingConnection()) {
                buffers_.insert(socket, {});
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    receive(socket);
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    buffers_.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    ~LocalHttpServer() override
    {
        // QTcpServer owns accepted sockets. Disconnect callbacks before member
        // destruction: closing a socket can emit disconnected after buffers_
        // has already been destroyed (notably when a request is cancelled).
        const auto sockets = server_.findChildren<QTcpSocket *>();
        for (auto *socket : sockets)
            QObject::disconnect(socket, nullptr, this, nullptr);
    }

    bool listen() { return server_.listen(QHostAddress::LocalHost, 0); }
    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort());
    }

    bool respond = true;
    int status = 200;
    QByteArray reason = "OK";
    QByteArray responseBody = R"({"ok":true})";
    QHash<QByteArray, QByteArray> redirects;
    QList<ReceivedRequest> received;

private:
    void receive(QTcpSocket *socket)
    {
        auto found = buffers_.find(socket);
        if (found == buffers_.end())
            return;
        *found += socket->readAll();
        const int headerEnd = found->indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        const auto lines = found->left(headerEnd).split('\n');
        const auto requestLine = lines.value(0).trimmed().split(' ');
        if (requestLine.size() < 2)
            return;

        ReceivedRequest request;
        request.method = requestLine.at(0);
        request.target = requestLine.at(1);
        for (int i = 1; i < lines.size(); ++i) {
            const auto line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon > 0)
                request.headers.insert(line.left(colon).toLower(), line.mid(colon + 1).trimmed());
        }
        const qint64 contentLength = request.headers.value("content-length", "0").toLongLong();
        if (found->size() - headerEnd - 4 < contentLength)
            return;
        request.body = found->mid(headerEnd + 4, contentLength);
        received.append(request);
        buffers_.erase(found);

        if (respond) {
            const bool redirect = redirects.contains(request.target);
            QByteArray response = redirect ? QByteArray("HTTP/1.1 302 Found\r\n")
                                           : "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
            if (redirect)
                response += "Location: " + redirects.value(request.target) + "\r\n";
            response += "Content-Type: application/json; charset=utf-8\r\n";
            response += "X-Test-Server: local\r\n";
            response += "Content-Length: " + QByteArray::number(responseBody.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            if (request.method != "HEAD") response += responseBody;
            socket->write(response);
            socket->disconnectFromHost();
        }
    }

    QTcpServer server_;
    QHash<QTcpSocket *, QByteArray> buffers_;
};

ResponseData responseFrom(const QSignalSpy &spy)
{
    return qvariant_cast<ResponseData>(spy.at(0).at(0));
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

void CoreTests::headReadsHeadersWithoutWaitingForBody()
{
    LocalHttpServer server;
    QVERIFY(server.listen());
    RequestEngine engine;
    QSignalSpy finished(&engine, &RequestEngine::finished);
    RequestData request;
    request.method = "HEAD";
    request.url = server.baseUrl() + "/head";
    request.timeoutMs = 1500;
    QString error;
    QVERIFY2(engine.send(request, {}, &error), qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
    const auto response = responseFrom(finished);
    QCOMPARE(response.statusCode, 200);
    QVERIFY(response.body.isEmpty());
    QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
    QVERIFY(RequestEngine::curlCommand(request, {}).contains("--head"));
    request.bodyType = "json";
    request.body = "{}";
    QVERIFY(!engine.send(request, {}, &error));
    QVERIFY(!error.isEmpty());
}

void CoreTests::initTestCase()
    {
        qRegisterMetaType<ResponseData>();
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }

void CoreTests::variablesRespectDisabledEntriesAndReportMissing()
    {
        const QList<KeyValue> variables = {
            {true, "host", "old.example"},
            {false, "disabled", "do-not-use"},
            {true, "host", "api.example"},
            {true, "empty", ""},
        };
        QStringList missing;
        const auto resolved = resolveVariables(
            "https://{{host}}/{{empty}}/{{disabled}}/{{missing}}/{{missing}}", variables, &missing);
        QCOMPARE(resolved, QStringLiteral("https://api.example//{{disabled}}/{{missing}}/{{missing}}"));
        QCOMPARE(missing, QStringList({"disabled", "missing"}));
    }

void CoreTests::postJsonQueryHeadersAndBearerReachTheServer()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.method = "POST";
        request.url = "{{base_url}}/echo?existing=1";
        request.params = {{true, "search", "{{search}}"}, {false, "hidden", "secret"}};
        request.headers = {{true, "X-Environment", "{{environment}}"}, {false, "X-Hidden", "secret"}};
        request.bodyType = "json";
        request.body = QStringLiteral("{\"message\":\"{{message}}\"}");
        request.authType = "bearer";
        request.token = "{{token}}";
        const QList<KeyValue> variables = {
            {true, "base_url", server.baseUrl()},
            {true, "search", QStringLiteral("a b&中文%")},
            {true, "environment", "local-test"},
            {true, "message", QStringLiteral("你好")},
            {true, "token", "demo-token"},
        };
        QString error;
        QVERIFY2(engine.send(request, variables, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(server.received.size(), 1);
        const auto &received = server.received.first();
        QCOMPARE(received.method, QByteArray("POST"));
        const QUrl requestUrl = QUrl::fromEncoded(received.target);
        QCOMPARE(requestUrl.path(), QStringLiteral("/echo"));
        const QUrlQuery query(requestUrl);
        QCOMPARE(query.queryItemValue("existing"), QStringLiteral("1"));
        QCOMPARE(query.queryItemValue("search", QUrl::FullyDecoded), QStringLiteral("a b&中文%"));
        QVERIFY(!query.hasQueryItem("hidden"));
        QVERIFY(!received.target.contains(' '));
        QCOMPARE(received.headers.value("x-environment"), QByteArray("local-test"));
        QVERIFY(!received.headers.contains("x-hidden"));
        QCOMPARE(received.headers.value("authorization"), QByteArray("Bearer demo-token"));
        QVERIFY(received.headers.value("content-type").startsWith("application/json"));
        QCOMPARE(received.body, QStringLiteral("{\"message\":\"你好\"}").toUtf8());
        const auto response = responseFrom(finished);
        QCOMPARE(response.statusCode, 200);
        QCOMPARE(response.body, server.responseBody);
        QCOMPARE(response.sizeBytes, qint64(server.responseBody.size()));
        QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
        QVERIFY(!engine.isBusy());
    }

void CoreTests::basicAuthAndFormEncodingReachTheServer()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.method = "POST";
        request.url = server.baseUrl() + "/form";
        request.authType = "basic";
        request.username = "{{user}}";
        request.password = "p@ss:word";
        request.bodyType = "form";
        request.body = QStringLiteral("message=hello world&more\nname=中文\nempty=\nequals=a=b");
        QString error;
        QVERIFY2(engine.send(request, {{true, "user", "alice"}}, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(server.received.size(), 1);
        const auto &received = server.received.first();
        QCOMPARE(received.headers.value("authorization"), QByteArray("Basic YWxpY2U6cEBzczp3b3Jk"));
        QVERIFY(received.headers.value("content-type").startsWith("application/x-www-form-urlencoded"));
        // QUrlQuery leaves '+' intact, so normalize form spaces before decoding.
        QByteArray encodedBody = received.body;
        encodedBody.replace('+', ' ');
        const QUrlQuery fields(QString::fromUtf8(encodedBody));
        QCOMPARE(fields.queryItems(QUrl::FullyDecoded).size(), 4);
        QCOMPARE(fields.queryItemValue("message", QUrl::FullyDecoded), QStringLiteral("hello world&more"));
        QCOMPARE(fields.queryItemValue("name", QUrl::FullyDecoded), QStringLiteral("中文"));
        QVERIFY(fields.hasQueryItem("empty"));
        QCOMPARE(fields.queryItemValue("empty"), QString());
        QCOMPARE(fields.queryItemValue("equals", QUrl::FullyDecoded), QStringLiteral("a=b"));
    }

void CoreTests::httpErrorKeepsStatusBodyAndHeaders()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        server.status = 422;
        server.reason = "Unprocessable Entity";
        server.responseBody = R"({"error":"validation_failed","field":"email"})";
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.url = server.baseUrl() + "/invalid";
        QVERIFY(engine.send(request, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        const auto response = responseFrom(finished);
        QCOMPARE(response.statusCode, 422);
        QCOMPARE(response.body, server.responseBody);
        QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
        bool foundTestHeader = false;
        for (const auto &header : response.headers)
            foundTestHeader |= header.key.compare("X-Test-Server", Qt::CaseInsensitive) == 0
                && header.value == "local";
        QVERIFY(foundTestHeader);
    }

void CoreTests::timeoutFinishesAndAllowsAnotherRequest()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        server.respond = false;
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.url = server.baseUrl() + "/slow";
        request.timeoutMs = 100;
        QVERIFY(engine.send(request, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        const auto response = responseFrom(finished);
        QVERIFY(response.error.contains(QStringLiteral("超时")));
        QVERIFY(!response.cancelled);
        QVERIFY(!engine.isBusy());

        server.respond = true;
        request.timeoutMs = 2000;
        QVERIFY(engine.send(request, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 3000);
        QCOMPARE(qvariant_cast<ResponseData>(finished.at(1).at(0)).statusCode, 200);
    }

void CoreTests::cancelFinishesOnceAndReleasesBusyState()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        server.respond = false;
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.url = server.baseUrl() + "/wait";
        request.timeoutMs = 2000;
        QVERIFY(engine.send(request, {}));
        QVERIFY(engine.isBusy());
        QTRY_COMPARE_WITH_TIMEOUT(server.received.size(), 1, 1000);
        QString busyError;
        QVERIFY(!engine.send(request, {}, &busyError));
        QVERIFY(!busyError.isEmpty());
        engine.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
        const auto response = responseFrom(finished);
        QVERIFY(response.cancelled);
        QVERIFY(response.error.contains(QStringLiteral("取消")));
        QVERIFY(!engine.isBusy());
        engine.cancel();
        QTest::qWait(30);
        QCOMPARE(finished.count(), 1);
    }

void CoreTests::invalidSchemeDoesNotStartNetworkOperation()
    {
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.url = "file:///etc/passwd";
        QString error;
        QVERIFY(!engine.send(request, {}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!engine.isBusy());
        QCOMPARE(finished.count(), 0);
    }

void CoreTests::missingVariablesRejectTheRequestButDisabledFieldsDoNot()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.url = server.baseUrl() + "/variables";
        request.headers = {{true, "X-Token", "{{missing_token}}"}};
        QString error;
        QVERIFY(!engine.send(request, {}, &error));
        QVERIFY(error.contains("missing_token"));
        QCOMPARE(finished.count(), 0);
        QVERIFY(!engine.isBusy());
        request.headers[0].enabled = false;
        QVERIFY2(engine.send(request, {}, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(server.received.size(), 1);
        QVERIFY(!server.received.first().headers.contains("x-token"));
    }

void CoreTests::redirectsFollowOnlyWithinTheSameOrigin()
    {
        LocalHttpServer server;
        LocalHttpServer otherOrigin;
        QVERIFY(server.listen());
        QVERIFY(otherOrigin.listen());
        server.redirects.insert("/redirect", (server.baseUrl() + "/final").toUtf8());
        server.redirects.insert("/cross-origin", (otherOrigin.baseUrl() + "/unexpected").toUtf8());
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.url = server.baseUrl() + "/redirect";
        request.headers = {{true, "X-Api-Key", "private-token"}};
        request.followRedirects = true;
        QVERIFY(engine.send(request, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(server.received.size(), 2);
        QCOMPARE(server.received.last().target, QByteArray("/final"));
        QCOMPARE(responseFrom(finished).statusCode, 200);
        QCOMPARE(responseFrom(finished).finalUrl, server.baseUrl() + "/final");

        finished.clear();
        request.url = server.baseUrl() + "/cross-origin";
        QVERIFY(engine.send(request, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(otherOrigin.received.size(), 0);
        QCOMPARE(responseFrom(finished).statusCode, 302);
        QVERIFY(!responseFrom(finished).error.isEmpty());
    }

void CoreTests::manualRedirectReturnsTheOriginalResponse()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        server.redirects.insert("/redirect", (server.baseUrl() + "/final").toUtf8());
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.url = server.baseUrl() + "/redirect";
        request.followRedirects = false;
        QVERIFY(engine.send(request, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(server.received.size(), 1);
        QCOMPARE(responseFrom(finished).statusCode, 302);
        QCOMPARE(responseFrom(finished).body, server.responseBody);
        QVERIFY(responseFrom(finished).error.isEmpty());
    }

void CoreTests::oversizedResponseIsExplicitlyTruncated()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        server.responseBody = QByteArray(10 * 1024 * 1024 + 17, 'x');
        RequestEngine engine;
        QSignalSpy finished(&engine, &RequestEngine::finished);
        RequestData request;
        request.url = server.baseUrl() + "/large";
        QVERIFY(engine.send(request, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        const auto response = responseFrom(finished);
        QCOMPARE(response.body.size(), 10 * 1024 * 1024);
        QVERIFY(response.truncated);
        QVERIFY(!response.error.isEmpty());
        QVERIFY(!response.cancelled);
        QVERIFY(!engine.isBusy());
    }

void CoreTests::workspacePersistsRequestHistoryAndEnvironment()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("nested/workspace.json");
        WorkspaceStore store(path);
        RequestData request;
        request.id = "persisted-request";
        request.name = QStringLiteral("保存测试");
        request.method = "PATCH";
        request.url = "{{base_url}}/items/7";
        request.params = {{true, "page", "2"}, {false, "debug", "true"}};
        request.headers = {{true, "X-Custom", "value"}, {false, "X-Disabled", "keep-me"}};
        request.bodyType = "json";
        request.body = "{\"enabled\":true}";
        request.authType = "basic";
        request.username = "user";
        request.password = "secret";
        request.token = "saved-token";
        request.timeoutMs = 12345;
        request.followRedirects = false;
        WorkspaceData workspace;
        workspace.requests = {request};
        workspace.activeEnvironment = "staging";
        workspace.environments.insert("staging", {{true, "base_url", "https://api.example"},
                                                   {false, "token", "disabled-secret"}});
        const auto timestamp = QDateTime::fromString("2026-01-02T03:04:05.123Z", Qt::ISODateWithMs);
        workspace.history.append({request, timestamp, 201, 42});

        QString error;
        QVERIFY2(store.save(workspace, &error), qPrintable(error));
        const auto loaded = WorkspaceStore(path).load(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.requests.size(), 1);
        const auto &saved = loaded.requests.first();
        QCOMPARE(saved.id, request.id);
        QCOMPARE(saved.name, request.name);
        QCOMPARE(saved.method, request.method);
        QCOMPARE(saved.url, request.url);
        QCOMPARE(saved.bodyType, request.bodyType);
        QCOMPARE(saved.body, request.body);
        QCOMPARE(saved.authType, request.authType);
        QCOMPARE(saved.username, request.username);
        QCOMPARE(saved.password, request.password);
        QCOMPARE(saved.token, request.token);
        QCOMPARE(saved.timeoutMs, request.timeoutMs);
        QCOMPARE(saved.followRedirects, false);
        QCOMPARE(saved.params.size(), 2);
        QCOMPARE(saved.params.at(0).value, QStringLiteral("2"));
        QVERIFY(!saved.params.at(1).enabled);
        QCOMPARE(saved.headers.size(), 2);
        QVERIFY(!saved.headers.at(1).enabled);
        QCOMPARE(saved.headers.at(1).value, QStringLiteral("keep-me"));
        QCOMPARE(loaded.activeEnvironment, QStringLiteral("staging"));
        QCOMPARE(loaded.environments.value("staging").size(), 2);
        QVERIFY(!loaded.environments.value("staging").at(1).enabled);
        QCOMPARE(loaded.environments.value("staging").at(1).value, QStringLiteral("disabled-secret"));
        QCOMPARE(loaded.history.size(), 1);
        QCOMPARE(loaded.history.first().request.id, request.id);
        QCOMPARE(loaded.history.first().time, timestamp);
        QCOMPARE(loaded.history.first().statusCode, 201);
        QCOMPARE(loaded.history.first().elapsedMs, qint64(42));
    }

void CoreTests::corruptWorkspaceIsReportedAndNotOverwritten()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("workspace.json");
        const QByteArray broken = "{this is not JSON";
        QVERIFY(writeFile(path, broken));
        QString error;
        WorkspaceStore(path).load(&error);
        QVERIFY(!error.isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), broken);
    }

void CoreTests::nestedPostmanCollectionImportsAndRoundTrips()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString input = directory.filePath("postman.json");
        const QByteArray collection = R"JSON({
          "info": {"name": "Sample", "schema": "https://schema.getpostman.com/json/collection/v2.1.0/collection.json"},
          "item": [{"name": "Users", "item": [
            {"name": "Create", "request": {
              "method": "POST",
              "url": {"raw": "{{base_url}}/users", "query": [
                {"key": "name", "value": "a b"},
                {"key": "debug", "value": "1", "disabled": true}]},
              "header": [{"key": "X-Trace", "value": "yes"},
                         {"key": "X-Optional", "value": "no", "disabled": true}],
              "auth": {"type": "bearer", "bearer": [{"key": "token", "value": "{{token}}", "type": "string"}]},
              "body": {"mode": "raw", "raw": "{\"name\":\"Ada\"}", "options": {"raw": {"language": "json"}}}
            }},
            {"name": "Admin", "item": [{"name": "Update", "request": {
              "method": "PUT", "url": "https://api.example/users/1",
              "auth": {"type": "basic", "basic": [{"key": "username", "value": "alice"}, {"key": "password", "value": "secret"}]},
              "body": {"mode": "urlencoded", "urlencoded": [{"key": "name", "value": "Ada Lovelace"}, {"key": "ignored", "value": "no", "disabled": true}]}
            }}]}
          ]}]
        })JSON";
        QVERIFY(writeFile(input, collection));
        QList<RequestData> requests;
        QString error;
        QVERIFY2(WorkspaceStore::importCollection(input, &requests, &error), qPrintable(error));
        QCOMPARE(requests.size(), 2);
        const auto &create = requests.at(0);
        QCOMPARE(create.name, QStringLiteral("Users / Create"));
        QCOMPARE(create.method, QStringLiteral("POST"));
        QCOMPARE(create.url, QStringLiteral("{{base_url}}/users"));
        QCOMPARE(create.params.size(), 2);
        QCOMPARE(create.params.at(0).value, QStringLiteral("a b"));
        QVERIFY(!create.params.at(1).enabled);
        QCOMPARE(create.headers.size(), 2);
        QVERIFY(!create.headers.at(1).enabled);
        QCOMPARE(create.authType, QStringLiteral("bearer"));
        QCOMPARE(create.token, QStringLiteral("{{token}}"));
        QCOMPARE(create.bodyType, QStringLiteral("json"));
        QCOMPARE(create.body, QStringLiteral("{\"name\":\"Ada\"}"));
        const auto &update = requests.at(1);
        QCOMPARE(update.name, QStringLiteral("Users / Admin / Update"));
        QCOMPARE(update.authType, QStringLiteral("basic"));
        QCOMPARE(update.username, QStringLiteral("alice"));
        QCOMPARE(update.password, QStringLiteral("secret"));
        QCOMPARE(update.bodyType, QStringLiteral("form"));
        QCOMPARE(update.body, QStringLiteral("name=Ada Lovelace"));

        const QString output = directory.filePath("exported.postman_collection.json");
        QVERIFY2(WorkspaceStore::exportCollection(output, requests, &error), qPrintable(error));
        QFile exported(output);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        const auto document = QJsonDocument::fromJson(exported.readAll());
        QVERIFY(document.isObject());
        QVERIFY(document.object().value("info").toObject().value("schema").toString().contains("v2.1.0"));
        const auto standardRequest = document.object().value("item").toArray().first().toObject().value("request").toObject();
        QCOMPARE(standardRequest.value("auth").toObject().value("type").toString(), QStringLiteral("bearer"));
        QCOMPARE(standardRequest.value("body").toObject().value("mode").toString(), QStringLiteral("raw"));
        QCOMPARE(standardRequest.value("header").toArray().at(1).toObject().value("disabled").toBool(), true);
        QList<RequestData> roundTrip;
        QVERIFY2(WorkspaceStore::importCollection(output, &roundTrip, &error), qPrintable(error));
        QCOMPARE(roundTrip.size(), 2);
        for (int i = 0; i < requests.size(); ++i) {
            const auto &expected = requests.at(i);
            const auto &actual = roundTrip.at(i);
            QCOMPARE(actual.name, expected.name);
            QCOMPARE(actual.method, expected.method);
            QCOMPARE(actual.url, expected.url);
            QCOMPARE(actual.bodyType, expected.bodyType);
            QCOMPARE(actual.body, expected.body);
            QCOMPARE(actual.authType, expected.authType);
            QCOMPARE(actual.token, expected.token);
            QCOMPARE(actual.username, expected.username);
            QCOMPARE(actual.password, expected.password);
            QCOMPARE(actual.params.size(), expected.params.size());
            for (int row = 0; row < expected.params.size(); ++row) {
                QCOMPARE(actual.params.at(row).enabled, expected.params.at(row).enabled);
                QCOMPARE(actual.params.at(row).key, expected.params.at(row).key);
                QCOMPARE(actual.params.at(row).value, expected.params.at(row).value);
            }
            QCOMPARE(actual.headers.size(), expected.headers.size());
            for (int row = 0; row < expected.headers.size(); ++row) {
                QCOMPARE(actual.headers.at(row).enabled, expected.headers.at(row).enabled);
                QCOMPARE(actual.headers.at(row).key, expected.headers.at(row).key);
                QCOMPARE(actual.headers.at(row).value, expected.headers.at(row).value);
            }
        }

        // Validate the standard Postman fields independently of QSend's private
        // preservation extension, which could otherwise hide a broken exporter.
        QJsonObject portable = document.object();
        QJsonArray portableItems;
        for (const auto &value : portable.value("item").toArray()) {
            auto item = value.toObject();
            item.remove("_qsend");
            portableItems.append(item);
        }
        portable.insert("item", portableItems);
        const QString portablePath = directory.filePath("portable.postman_collection.json");
        QVERIFY(writeFile(portablePath, QJsonDocument(portable).toJson()));
        QList<RequestData> portableRequests;
        QVERIFY2(WorkspaceStore::importCollection(portablePath, &portableRequests, &error), qPrintable(error));
        QCOMPARE(portableRequests.size(), 2);
        QCOMPARE(portableRequests.at(0).url, create.url);
        QCOMPARE(portableRequests.at(0).params.size(), 2);
        QCOMPARE(portableRequests.at(0).params.at(0).value, QStringLiteral("a b"));
        QVERIFY(!portableRequests.at(0).params.at(1).enabled);
        QCOMPARE(portableRequests.at(0).bodyType, QStringLiteral("json"));
        QCOMPARE(portableRequests.at(0).body, create.body);
        QCOMPARE(portableRequests.at(0).token, create.token);
        QCOMPARE(portableRequests.at(1).bodyType, QStringLiteral("form"));
        QCOMPARE(portableRequests.at(1).body, update.body);
        QCOMPARE(portableRequests.at(1).username, update.username);
        QCOMPARE(portableRequests.at(1).password, update.password);
    }
QTEST_GUILESS_MAIN(CoreTests)
