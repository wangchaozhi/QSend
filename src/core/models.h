#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace qsend {

struct KeyValue {
    bool enabled = true;
    QString key;
    QString value;
};

struct RequestData {
    QString id;
    QString name = QStringLiteral("新建请求");
    QString method = QStringLiteral("GET");
    QString url;
    QList<KeyValue> params;
    QList<KeyValue> headers;
    QString bodyType = QStringLiteral("none"); // none, json, text, form
    QString body;
    QString authType = QStringLiteral("none"); // none, bearer, basic
    QString token;
    QString username;
    QString password;
    int timeoutMs = 30000;
    bool followRedirects = true;
};

struct ResponseData {
    int statusCode = 0;
    QString reason;
    QByteArray body;
    QList<KeyValue> headers;
    qint64 elapsedMs = 0;
    qint64 sizeBytes = 0;
    QString error;
    QString finalUrl;
    bool cancelled = false;
    bool truncated = false;
};

struct HistoryEntry {
    RequestData request;
    QDateTime time;
    int statusCode = 0;
    qint64 elapsedMs = 0;
};

struct WorkspaceData {
    QList<RequestData> requests;
    QList<HistoryEntry> history;
    QMap<QString, QList<KeyValue>> environments;
    QString activeEnvironment = QStringLiteral("本地开发");
};

QJsonObject requestToJson(const RequestData &request);
RequestData requestFromJson(const QJsonObject &object);
QString resolveVariables(const QString &text, const QList<KeyValue> &variables,
                         QStringList *missing = nullptr);

} // namespace qsend

Q_DECLARE_METATYPE(qsend::ResponseData)
