#include "workspacestore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace qsend {
namespace {
constexpr qint64 MaximumFileBytes = 20 * 1024 * 1024;
constexpr qsizetype MaximumHistoryEntries = 100;

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

QJsonArray pairsToJson(const QList<KeyValue> &pairs, bool postman = false)
{
    QJsonArray result;
    for (const auto &pair : pairs) {
        QJsonObject object{{QStringLiteral("key"), pair.key}, {QStringLiteral("value"), pair.value}};
        object.insert(postman ? QStringLiteral("disabled") : QStringLiteral("enabled"), postman ? !pair.enabled : pair.enabled);
        if (postman) object.insert(QStringLiteral("type"), QStringLiteral("text"));
        result.append(object);
    }
    return result;
}

QString jsonString(const QJsonValue &value)
{
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 16);
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return {};
}

QList<KeyValue> pairsFromJson(const QJsonArray &array, bool postman = false)
{
    QList<KeyValue> result;
    for (const auto &value : array) {
        const auto object = value.toObject();
        result.append({postman ? !object.value(QStringLiteral("disabled")).toBool()
                               : object.value(QStringLiteral("enabled")).toBool(true),
                       object.value(QStringLiteral("key")).toString(),
                       jsonString(object.value(QStringLiteral("value")))});
    }
    return result;
}

bool validPairs(const QJsonValue &value)
{
    if (!value.isArray()) return false;
    for (const auto &entry : value.toArray()) {
        if (!entry.isObject()) return false;
        const auto pair = entry.toObject();
        if (!pair.value(QStringLiteral("key")).isString() || !pair.value(QStringLiteral("value")).isString()
            || (pair.contains(QStringLiteral("enabled")) && !pair.value(QStringLiteral("enabled")).isBool())) return false;
    }
    return true;
}

bool validNativeRequest(const QJsonValue &value)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    if (!object.value(QStringLiteral("url")).isString()) return false;
    const QStringList textFields{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("method"),
                                  QStringLiteral("bodyType"), QStringLiteral("body"), QStringLiteral("authType"),
                                  QStringLiteral("token"), QStringLiteral("username"), QStringLiteral("password")};
    for (const auto &field : textFields)
        if (object.contains(field) && !object.value(field).isString()) return false;
    for (const auto &field : {QStringLiteral("params"), QStringLiteral("headers")})
        if (object.contains(field) && !validPairs(object.value(field))) return false;
    if (object.contains(QStringLiteral("timeoutMs")) && !object.value(QStringLiteral("timeoutMs")).isDouble()) return false;
    if (object.contains(QStringLiteral("followRedirects")) && !object.value(QStringLiteral("followRedirects")).isBool()) return false;
    return true;
}

bool readJson(const QString &path, QJsonDocument *document, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("无法读取文件：%1").arg(file.errorString()));
    if (file.size() > MaximumFileBytes) return fail(error, QStringLiteral("文件超过 20 MiB，请拆分后重试。"));
    const auto data = file.read(MaximumFileBytes + 1);
    if (data.size() > MaximumFileBytes) return fail(error, QStringLiteral("文件超过 20 MiB，请拆分后重试。"));
    if (file.error() != QFile::NoError) return fail(error, QStringLiteral("读取文件失败：%1").arg(file.errorString()));
    QJsonParseError parseError;
    *document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("JSON 格式错误（位置 %1）：%2。原文件未被修改。")
                    .arg(parseError.offset).arg(parseError.errorString()));
    return true;
}

bool writeJson(const QString &path, const QJsonObject &object, QString *error)
{
    const auto directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) return fail(error, QStringLiteral("无法创建保存目录：%1").arg(directory));
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaximumFileBytes) return fail(error, QStringLiteral("数据超过 20 MiB，请减少请求或历史记录后保存。"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, QStringLiteral("无法打开保存文件：%1").arg(file.errorString()));
    if (file.write(bytes) != bytes.size()) return fail(error, QStringLiteral("文件写入失败：%1").arg(file.errorString()));
    if (!file.commit()) return fail(error, QStringLiteral("文件保存失败：%1").arg(file.errorString()));
    return true;
}

bool applyPostmanAuth(const QJsonObject &auth, RequestData &request, QString *error)
{
    const auto type = auth.value(QStringLiteral("type")).toString(QStringLiteral("noauth"));
    if (type == QStringLiteral("noauth") || type.isEmpty()) { request.authType = QStringLiteral("none"); return true; }
    QMap<QString, QString> fields;
    for (const auto &value : auth.value(type).toArray()) {
        const auto object = value.toObject();
        fields.insert(object.value(QStringLiteral("key")).toString(), jsonString(object.value(QStringLiteral("value"))));
    }
    if (type == QStringLiteral("bearer")) {
        request.authType = type;
        request.token = fields.value(QStringLiteral("token"));
    } else if (type == QStringLiteral("basic")) {
        request.authType = type;
        request.username = fields.value(QStringLiteral("username"));
        request.password = fields.value(QStringLiteral("password"));
    } else if (type == QStringLiteral("apikey")) {
        KeyValue pair{true, fields.value(QStringLiteral("key")), fields.value(QStringLiteral("value"))};
        if (fields.value(QStringLiteral("in")) == QStringLiteral("query")) request.params.append(pair);
        else request.headers.append(pair);
    } else {
        return fail(error, QStringLiteral("请求“%1”使用暂不支持的认证 %2；请先改为 Basic、Bearer 或 API Key。").arg(request.name, type));
    }
    return true;
}

QString joinUrlPart(const QJsonValue &value, const QString &separator)
{
    if (value.isString()) return value.toString();
    QStringList parts;
    for (const auto &part : value.toArray()) parts.append(part.toString());
    return parts.join(separator);
}

bool importItems(const QJsonArray &items, const QString &prefix, const QJsonObject &inheritedAuth,
                 QList<RequestData> *result, QString *error, int depth = 0)
{
    if (depth > 32) return fail(error, QStringLiteral("集合目录嵌套超过 32 层。"));
    for (const auto &itemValue : items) {
        if (!itemValue.isObject()) return fail(error, QStringLiteral("集合中包含无效的请求项目。"));
        const auto item = itemValue.toObject();
        const auto name = item.value(QStringLiteral("name")).toString(QStringLiteral("未命名请求"));
        const auto fullName = prefix.isEmpty() ? name : prefix + QStringLiteral(" / ") + name;
        const auto auth = item.value(QStringLiteral("auth")).isObject() ? item.value(QStringLiteral("auth")).toObject() : inheritedAuth;
        if (item.value(QStringLiteral("item")).isArray()) {
            if (!importItems(item.value(QStringLiteral("item")).toArray(), fullName, auth, result, error, depth + 1)) return false;
            continue;
        }
        if (!item.contains(QStringLiteral("request"))) continue;
        if (result->size() >= 10000) return fail(error, QStringLiteral("单次最多导入 10000 个请求。"));
        RequestData request;
        request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.name = fullName;
        const auto requestValue = item.value(QStringLiteral("request"));
        if (requestValue.isString()) {
            request.url = requestValue.toString();
            if (!applyPostmanAuth(auth, request, error)) return false;
            result->append(request);
            continue;
        }
        if (!requestValue.isObject()) return fail(error, QStringLiteral("请求“%1”的内容无效。").arg(fullName));
        const auto object = requestValue.toObject();
        if (item.value(QStringLiteral("_qsend")).isObject()) {
            if (!validNativeRequest(item.value(QStringLiteral("_qsend"))))
                return fail(error, QStringLiteral("请求“%1”的 QSend 扩展数据损坏。").arg(fullName));
            request = requestFromJson(item.value(QStringLiteral("_qsend")).toObject());
            request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            request.name = fullName;
            result->append(request);
            continue;
        }
        request.method = object.value(QStringLiteral("method")).toString(QStringLiteral("GET")).toUpper();
        request.headers = pairsFromJson(object.value(QStringLiteral("header")).toArray(), true);
        const auto url = object.value(QStringLiteral("url"));
        if (url.isString()) request.url = url.toString();
        else if (url.isObject()) {
            const auto urlObject = url.toObject();
            request.url = urlObject.value(QStringLiteral("raw")).toString();
            if (request.url.isEmpty()) {
                auto protocol = urlObject.value(QStringLiteral("protocol")).toString(QStringLiteral("https"));
                request.url = protocol + QStringLiteral("://") + joinUrlPart(urlObject.value(QStringLiteral("host")), QStringLiteral("."));
                const auto port = jsonString(urlObject.value(QStringLiteral("port")));
                if (!port.isEmpty()) request.url += QLatin1Char(':') + port;
                const auto path = joinUrlPart(urlObject.value(QStringLiteral("path")), QStringLiteral("/"));
                if (!path.isEmpty()) request.url += QLatin1Char('/') + path;
            }
            if (urlObject.value(QStringLiteral("query")).isArray()) {
                request.params = pairsFromJson(urlObject.value(QStringLiteral("query")).toArray(), true);
                // Postman's structured query is authoritative; do not duplicate raw URL query entries.
                const auto queryStart = request.url.indexOf(QLatin1Char('?'));
                if (queryStart >= 0) {
                    const auto fragmentStart = request.url.indexOf(QLatin1Char('#'), queryStart);
                    request.url = request.url.left(queryStart) + (fragmentStart >= 0 ? request.url.mid(fragmentStart) : QString());
                }
            }
        }
        const auto body = object.value(QStringLiteral("body")).toObject();
        const auto mode = body.value(QStringLiteral("mode")).toString();
        if (mode == QStringLiteral("raw")) {
            request.body = body.value(QStringLiteral("raw")).toString();
            const auto language = body.value(QStringLiteral("options")).toObject().value(QStringLiteral("raw")).toObject().value(QStringLiteral("language")).toString();
            bool json = language == QStringLiteral("json");
            for (const auto &header : request.headers)
                if (header.enabled && header.key.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0
                    && header.value.contains(QStringLiteral("json"), Qt::CaseInsensitive)) json = true;
            request.bodyType = json ? QStringLiteral("json") : QStringLiteral("text");
        } else if (mode == QStringLiteral("urlencoded")) {
            request.bodyType = QStringLiteral("form");
            QStringList lines;
            for (const auto &pair : pairsFromJson(body.value(QStringLiteral("urlencoded")).toArray(), true)) {
                if (!pair.enabled) continue;
                if (pair.key.contains(QLatin1Char('=')) || pair.key.contains(QLatin1Char('\n')) || pair.value.contains(QLatin1Char('\n'))
                    || pair.key.contains(QLatin1Char('\r')) || pair.value.contains(QLatin1Char('\r')))
                    return fail(error, QStringLiteral("请求“%1”的表单包含换行或键名中的等号，当前逐行编辑器无法无损表示。").arg(fullName));
                lines.append(pair.key + QLatin1Char('=') + pair.value);
            }
            request.body = lines.join(QLatin1Char('\n'));
        } else if (!mode.isEmpty()) {
            return fail(error, QStringLiteral("请求“%1”的请求体类型 %2 暂不支持；请使用 raw 或 urlencoded。").arg(fullName, mode));
        }
        const auto requestAuth = object.value(QStringLiteral("auth")).isObject() ? object.value(QStringLiteral("auth")).toObject() : auth;
        if (!applyPostmanAuth(requestAuth, request, error)) return false;
        result->append(request);
    }
    return true;
}

QJsonObject postmanRequest(const RequestData &request)
{
    QJsonObject object{{QStringLiteral("method"), request.method},
                       {QStringLiteral("header"), pairsToJson(request.headers, true)}};
    QString raw = request.url;
    const auto fragmentStart = raw.indexOf(QLatin1Char('#'));
    if (fragmentStart >= 0) raw.truncate(fragmentStart);
    QList<KeyValue> params;
    const auto queryStart = raw.indexOf(QLatin1Char('?'));
    if (queryStart >= 0) {
        const auto query = QUrlQuery(raw.mid(queryStart + 1));
        for (const auto &pair : query.queryItems(QUrl::FullyDecoded)) params.append({true, pair.first, pair.second});
    }
    params.append(request.params);
    for (const auto &param : request.params) {
        if (!param.enabled || param.key.isEmpty()) continue;
        raw += raw.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?');
        raw += QString::fromLatin1(QUrl::toPercentEncoding(param.key, "{}")) + QLatin1Char('=')
            + QString::fromLatin1(QUrl::toPercentEncoding(param.value, "{}"));
    }
    object.insert(QStringLiteral("url"), QJsonObject{{QStringLiteral("raw"), raw}, {QStringLiteral("query"), pairsToJson(params, true)}});
    const auto authField = [](const QString &key, const QString &value) {
        return QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("value"), value},
                           {QStringLiteral("type"), QStringLiteral("string")}};
    };
    if (request.authType == QStringLiteral("bearer")) {
        object.insert(QStringLiteral("auth"), QJsonObject{{QStringLiteral("type"), QStringLiteral("bearer")},
            {QStringLiteral("bearer"), QJsonArray{authField(QStringLiteral("token"), request.token)}}});
    } else if (request.authType == QStringLiteral("basic")) {
        object.insert(QStringLiteral("auth"), QJsonObject{{QStringLiteral("type"), QStringLiteral("basic")},
            {QStringLiteral("basic"), QJsonArray{authField(QStringLiteral("username"), request.username),
                                                authField(QStringLiteral("password"), request.password)}}});
    } else object.insert(QStringLiteral("auth"), QJsonObject{{QStringLiteral("type"), QStringLiteral("noauth")}});
    if (request.bodyType == QStringLiteral("json") || request.bodyType == QStringLiteral("text")) {
        const QJsonObject language{{QStringLiteral("language"), request.bodyType}};
        const QJsonObject options{{QStringLiteral("raw"), language}};
        object.insert(QStringLiteral("body"), QJsonObject{{QStringLiteral("mode"), QStringLiteral("raw")}, {QStringLiteral("raw"), request.body},
            {QStringLiteral("options"), options}});
    } else if (request.bodyType == QStringLiteral("form")) {
        QList<KeyValue> pairs;
        for (auto line : request.body.split(QLatin1Char('\n'))) {
            if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
            if (line.trimmed().isEmpty()) continue;
            const auto separator = line.indexOf(QLatin1Char('='));
            pairs.append({true, separator < 0 ? line : line.left(separator), separator < 0 ? QString() : line.mid(separator + 1)});
        }
        object.insert(QStringLiteral("body"), QJsonObject{{QStringLiteral("mode"), QStringLiteral("urlencoded")}, {QStringLiteral("urlencoded"), pairsToJson(pairs, true)}});
    }
    return object;
}
} // namespace

WorkspaceStore::WorkspaceStore(const QString &filePath)
    : filePath_(filePath.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("workspace.json")) : filePath)
{}

QString WorkspaceStore::filePath() const { return filePath_; }

WorkspaceData WorkspaceStore::defaults()
{
    WorkspaceData workspace;
    workspace.environments.insert(QStringLiteral("本地开发"), {{true, QStringLiteral("baseUrl"), QStringLiteral("http://localhost:3000")},
                                                             {true, QStringLiteral("token"), QString()}});
    workspace.environments.insert(QStringLiteral("测试环境"), {{true, QStringLiteral("baseUrl"), QStringLiteral("https://httpbin.org")},
                                                             {true, QStringLiteral("token"), QStringLiteral("demo-token")}});
    RequestData get;
    get.id = QStringLiteral("example-get");
    get.name = QStringLiteral("查询参数 · GET");
    get.url = QStringLiteral("https://httpbin.org/get");
    get.params = {{true, QStringLiteral("hello"), QStringLiteral("QSend")}};
    workspace.requests.append(get);
    RequestData post;
    post.id = QStringLiteral("example-post");
    post.name = QStringLiteral("JSON 请求 · POST");
    post.method = QStringLiteral("POST");
    post.url = QStringLiteral("https://httpbin.org/post");
    post.bodyType = QStringLiteral("json");
    post.body = QStringLiteral("{\n  \"message\": \"Hello from QSend\",\n  \"ready\": true\n}");
    workspace.requests.append(post);
    RequestData auth;
    auth.id = QStringLiteral("example-auth");
    auth.name = QStringLiteral("Bearer 认证 · GET");
    auth.url = QStringLiteral("https://httpbin.org/bearer");
    auth.authType = QStringLiteral("bearer");
    auth.token = QStringLiteral("demo-token");
    workspace.requests.append(auth);
    return workspace;
}

WorkspaceData WorkspaceStore::load(QString *error) const
{
    if (error) error->clear();
    if (!QFileInfo::exists(filePath_)) return defaults();
    QJsonDocument document;
    if (!readJson(filePath_, &document, error)) return defaults();
    const auto root = document.object();
    if (!document.isObject() || !root.value(QStringLiteral("requests")).isArray()
        || (root.contains(QStringLiteral("version")) && root.value(QStringLiteral("version")).toInt() != 1)
        || (root.contains(QStringLiteral("environments")) && !root.value(QStringLiteral("environments")).isObject())
        || (root.contains(QStringLiteral("history")) && !root.value(QStringLiteral("history")).isArray())) {
        fail(error, QStringLiteral("工作区格式或版本不受支持。原文件未被修改。"));
        return defaults();
    }
    WorkspaceData workspace;
    for (const auto &value : root.value(QStringLiteral("requests")).toArray()) {
        if (!validNativeRequest(value)) {
            fail(error, QStringLiteral("工作区请求格式损坏。原文件未被修改。"));
            return defaults();
        }
        workspace.requests.append(requestFromJson(value.toObject()));
    }
    const auto environments = root.value(QStringLiteral("environments")).toObject();
    for (auto it = environments.begin(); it != environments.end(); ++it) {
        if (!validPairs(it.value())) {
            fail(error, QStringLiteral("工作区环境变量格式损坏。原文件未被修改。"));
            return defaults();
        }
        workspace.environments.insert(it.key(), pairsFromJson(it.value().toArray()));
    }
    if (workspace.environments.isEmpty()) workspace.environments = defaults().environments;
    workspace.activeEnvironment = root.value(QStringLiteral("activeEnvironment")).toString(QStringLiteral("本地开发"));
    if (!workspace.environments.contains(workspace.activeEnvironment)) workspace.activeEnvironment = workspace.environments.firstKey();
    for (const auto &value : root.value(QStringLiteral("history")).toArray()) {
        if (workspace.history.size() >= MaximumHistoryEntries) break;
        const auto object = value.toObject();
        if (!validNativeRequest(object.value(QStringLiteral("request")))) {
            fail(error, QStringLiteral("工作区历史记录格式损坏。原文件未被修改。"));
            return defaults();
        }
        workspace.history.append({requestFromJson(object.value(QStringLiteral("request")).toObject()),
                                  QDateTime::fromString(object.value(QStringLiteral("time")).toString(), Qt::ISODateWithMs),
                                  object.value(QStringLiteral("statusCode")).toInt(),
                                  static_cast<qint64>(object.value(QStringLiteral("elapsedMs")).toDouble())});
    }
    return workspace;
}

bool WorkspaceStore::save(const WorkspaceData &workspace, QString *error) const
{
    if (error) error->clear();
    QJsonArray requests;
    for (const auto &request : workspace.requests) requests.append(requestToJson(request));
    QJsonArray history;
    for (qsizetype i = 0; i < qMin(workspace.history.size(), MaximumHistoryEntries); ++i) {
        const auto &entry = workspace.history.at(i);
        history.append(QJsonObject{{QStringLiteral("request"), requestToJson(entry.request)},
                                   {QStringLiteral("time"), entry.time.toString(Qt::ISODateWithMs)},
                                   {QStringLiteral("statusCode"), entry.statusCode},
                                   {QStringLiteral("elapsedMs"), static_cast<double>(entry.elapsedMs)}});
    }
    QJsonObject environments;
    for (auto it = workspace.environments.begin(); it != workspace.environments.end(); ++it)
        environments.insert(it.key(), pairsToJson(it.value()));
    return writeJson(filePath_, {{QStringLiteral("format"), QStringLiteral("qsend-workspace")},
                                {QStringLiteral("version"), 1}, {QStringLiteral("requests"), requests},
                                {QStringLiteral("history"), history}, {QStringLiteral("environments"), environments},
                                {QStringLiteral("activeEnvironment"), workspace.activeEnvironment}}, error);
}

bool WorkspaceStore::importCollection(const QString &filePath, QList<RequestData> *requests, QString *error)
{
    if (error) error->clear();
    if (!requests) return fail(error, QStringLiteral("未提供导入目标。"));
    QJsonDocument document;
    if (!readJson(filePath, &document, error)) return false;
    if (!document.isObject()) return fail(error, QStringLiteral("集合根节点必须是 JSON 对象。"));
    const auto root = document.object();
    QList<RequestData> imported;
    if (root.value(QStringLiteral("requests")).isArray()) {
        if (root.contains(QStringLiteral("version")) && root.value(QStringLiteral("version")).toInt() != 1)
            return fail(error, QStringLiteral("此 QSend 集合版本暂不支持。"));
        for (const auto &value : root.value(QStringLiteral("requests")).toArray()) {
            if (!validNativeRequest(value)) return fail(error, QStringLiteral("集合包含格式错误的请求。"));
            if (imported.size() >= 10000) return fail(error, QStringLiteral("单次最多导入 10000 个请求。"));
            auto request = requestFromJson(value.toObject());
            request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            imported.append(request);
        }
    } else if (root.value(QStringLiteral("item")).isArray()) {
        if (!importItems(root.value(QStringLiteral("item")).toArray(), {}, root.value(QStringLiteral("auth")).toObject(), &imported, error)) return false;
    } else return fail(error, QStringLiteral("不是 QSend 或 Postman v2.1 集合文件。"));
    if (imported.isEmpty()) return fail(error, QStringLiteral("集合中没有可导入的请求。"));
    *requests = imported;
    return true;
}

bool WorkspaceStore::exportCollection(const QString &filePath, const QList<RequestData> &requests, QString *error)
{
    if (error) error->clear();
    QJsonArray items;
    for (const auto &request : requests)
        items.append(QJsonObject{{QStringLiteral("name"), request.name}, {QStringLiteral("request"), postmanRequest(request)},
                                 {QStringLiteral("_qsend"), requestToJson(request)}});
    const QJsonObject info{{QStringLiteral("name"), QStringLiteral("QSend Collection")},
                           {QStringLiteral("schema"), QStringLiteral("https://schema.getpostman.com/json/collection/v2.1.0/collection.json")},
                           {QStringLiteral("description"), QStringLiteral("Exported by QSend. Basic/Bearer credentials are stored as plain text.")}};
    return writeJson(filePath, {{QStringLiteral("info"), info}, {QStringLiteral("item"), items}}, error);
}
} // namespace qsend
