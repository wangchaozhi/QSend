#include "models.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QUuid>

namespace qsend {
namespace {
QJsonArray pairsToJson(const QList<KeyValue> &pairs)
{
    QJsonArray result;
    for (const auto &pair : pairs)
        result.append(QJsonObject{{QStringLiteral("enabled"), pair.enabled},
                                  {QStringLiteral("key"), pair.key},
                                  {QStringLiteral("value"), pair.value}});
    return result;
}

QList<KeyValue> pairsFromJson(const QJsonArray &array)
{
    QList<KeyValue> result;
    for (const auto &value : array) {
        const auto object = value.toObject();
        result.append({object.value(QStringLiteral("enabled")).toBool(true),
                       object.value(QStringLiteral("key")).toString(),
                       object.value(QStringLiteral("value")).toString()});
    }
    return result;
}
} // namespace

QJsonObject requestToJson(const RequestData &r)
{
    return {{QStringLiteral("id"), r.id},
            {QStringLiteral("name"), r.name},
            {QStringLiteral("method"), r.method},
            {QStringLiteral("url"), r.url},
            {QStringLiteral("params"), pairsToJson(r.params)},
            {QStringLiteral("headers"), pairsToJson(r.headers)},
            {QStringLiteral("bodyType"), r.bodyType},
            {QStringLiteral("body"), r.body},
            {QStringLiteral("authType"), r.authType},
            {QStringLiteral("token"), r.token},
            {QStringLiteral("username"), r.username},
            {QStringLiteral("password"), r.password},
            {QStringLiteral("timeoutMs"), r.timeoutMs},
            {QStringLiteral("followRedirects"), r.followRedirects}};
}

RequestData requestFromJson(const QJsonObject &o)
{
    RequestData r;
    r.id = o.value(QStringLiteral("id")).toString();
    if (r.id.isEmpty()) r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.name = o.value(QStringLiteral("name")).toString(r.name);
    r.method = o.value(QStringLiteral("method")).toString(r.method).toUpper();
    r.url = o.value(QStringLiteral("url")).toString();
    r.params = pairsFromJson(o.value(QStringLiteral("params")).toArray());
    r.headers = pairsFromJson(o.value(QStringLiteral("headers")).toArray());
    r.bodyType = o.value(QStringLiteral("bodyType")).toString(r.bodyType);
    r.body = o.value(QStringLiteral("body")).toString();
    r.authType = o.value(QStringLiteral("authType")).toString(r.authType);
    r.token = o.value(QStringLiteral("token")).toString();
    r.username = o.value(QStringLiteral("username")).toString();
    r.password = o.value(QStringLiteral("password")).toString();
    r.timeoutMs = o.value(QStringLiteral("timeoutMs")).toInt(r.timeoutMs);
    r.followRedirects = o.value(QStringLiteral("followRedirects")).toBool(true);
    return r;
}

QString resolveVariables(const QString &text, const QList<KeyValue> &variables, QStringList *missing)
{
    QMap<QString, QString> values;
    for (const auto &variable : variables)
        if (variable.enabled && !variable.key.trimmed().isEmpty())
            values.insert(variable.key.trimmed(), variable.value);

    static const QRegularExpression pattern(QStringLiteral(R"(\{\{\s*([^{}]+?)\s*\}\})"));
    QString result;
    qsizetype offset = 0;
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += text.mid(offset, match.capturedStart() - offset);
        const auto name = match.captured(1).trimmed();
        if (values.contains(name)) result += values.value(name);
        else {
            result += match.captured();
            if (missing && !missing->contains(name)) missing->append(name);
        }
        offset = match.capturedEnd();
    }
    result += text.mid(offset);
    return result;
}
} // namespace qsend
