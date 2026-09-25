#include "requestengine.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

namespace qsend {
namespace {
constexpr qint64 MaximumResponseBytes = 10 * 1024 * 1024;

struct PreparedRequest {
    QUrl url;
    QByteArray method;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
    bool hasBody = false;
};

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool hasControl(const QString &text)
{
    for (const auto c : text)
        if (c.unicode() < 32 || c.unicode() == 127) return true;
    return false;
}

void setHeader(PreparedRequest &out, const QByteArray &key, const QByteArray &value)
{
    for (auto &header : out.headers) {
        if (header.first.compare(key, Qt::CaseInsensitive) == 0) {
            header.second = value;
            return;
        }
    }
    out.headers.append({key, value});
}

bool hasHeader(const PreparedRequest &out, const QByteArray &name)
{
    for (const auto &header : out.headers)
        if (header.first.compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}

QByteArray formEncode(const QString &text)
{
    return QUrl::toPercentEncoding(text).replace("%20", "+");
}

bool prepare(const RequestData &request, const QList<KeyValue> &variables,
             PreparedRequest &out, QString *error)
{
    if (error) error->clear();
    if (request.timeoutMs < 1 || request.timeoutMs > 600000)
        return fail(error, QStringLiteral("超时时间必须在 1–600000 毫秒之间。"));
    QStringList missing;
    const auto resolve = [&](const QString &value) { return resolveVariables(value, variables, &missing); };
    const auto urlText = resolve(request.url).trimmed();
    const auto method = request.method.trimmed().toUpper();
    static const QRegularExpression tokenPattern(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
    if (!tokenPattern.match(method).hasMatch())
        return fail(error, QStringLiteral("HTTP 方法不能为空或包含非法字符。"));
    out.method = method.toLatin1();
    out.url = QUrl(urlText, QUrl::TolerantMode);
    if (!missing.isEmpty())
        return fail(error, QStringLiteral("未定义的环境变量：%1").arg(missing.join(QStringLiteral("、"))));
    if (hasControl(urlText) || !out.url.isValid() || out.url.host().isEmpty()
        || (out.url.scheme() != QStringLiteral("http") && out.url.scheme() != QStringLiteral("https")))
        return fail(error, QStringLiteral("请输入完整的 http:// 或 https:// URL。"));
    if (!out.url.userInfo().isEmpty())
        return fail(error, QStringLiteral("请将 URL 中的账号密码移到 Auth 的 Basic 认证中。"));
    out.url.setFragment(QString());
    QUrlQuery query(out.url);
    for (const auto &param : request.params) {
        if (!param.enabled || param.key.isEmpty()) continue;
        // Values entered in the grid are literal Unicode, never pre-encoded URL text.
        const auto key = QString::fromLatin1(QUrl::toPercentEncoding(resolve(param.key)));
        const auto value = QString::fromLatin1(QUrl::toPercentEncoding(resolve(param.value)));
        query.addQueryItem(key, value);
    }
    out.url.setQuery(query);
    for (const auto &header : request.headers) {
        if (!header.enabled || header.key.trimmed().isEmpty()) continue;
        const auto key = resolve(header.key).trimmed();
        const auto value = resolve(header.value);
        if (!tokenPattern.match(key).hasMatch() || hasControl(value))
            return fail(error, QStringLiteral("请求头包含非法名称或控制字符（禁止 CR/LF）：%1").arg(key));
        // Qt must calculate framing. User-provided lengths can otherwise desynchronize a connection.
        if (key.compare(QStringLiteral("Content-Length"), Qt::CaseInsensitive) == 0
            || key.compare(QStringLiteral("Transfer-Encoding"), Qt::CaseInsensitive) == 0)
            return fail(error, QStringLiteral("%1 由网络引擎自动生成，请移除此请求头。").arg(key));
        setHeader(out, key.toLatin1(), value.toUtf8());
    }
    const auto auth = request.authType.toLower();
    if (auth == QStringLiteral("bearer")) {
        const auto token = resolve(request.token);
        if (hasControl(token)) return fail(error, QStringLiteral("Bearer Token 不能包含换行或控制字符。"));
        setHeader(out, "Authorization", "Bearer " + token.toUtf8());
    } else if (auth == QStringLiteral("basic")) {
        const auto username = resolve(request.username);
        const auto password = resolve(request.password);
        if (username.contains(QLatin1Char(':')) || hasControl(username) || hasControl(password))
            return fail(error, QStringLiteral("Basic 用户名不能含冒号，账号密码不能含控制字符。"));
        setHeader(out, "Authorization", "Basic " + (username + QLatin1Char(':') + password).toUtf8().toBase64());
    } else if (auth != QStringLiteral("none") && !auth.isEmpty()) {
        return fail(error, QStringLiteral("暂不支持此认证类型：%1").arg(request.authType));
    }

    const auto bodyType = request.bodyType.toLower();
    out.hasBody = bodyType != QStringLiteral("none") && !bodyType.isEmpty();
    if (out.method == "HEAD" && out.hasBody)
        return fail(error, QStringLiteral("HEAD 请求不支持请求体，请选择“无请求体”。"));
    if (out.hasBody) {
        const auto body = resolve(request.body);
        QByteArray contentType;
        if (bodyType == QStringLiteral("form")) {
            const auto lines = body.split(QLatin1Char('\n'));
            for (auto line : lines) {
                if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
                if (line.trimmed().isEmpty()) continue;
                const auto separator = line.indexOf(QLatin1Char('='));
                const auto key = separator < 0 ? line : line.left(separator);
                const auto value = separator < 0 ? QString() : line.mid(separator + 1);
                if (!out.body.isEmpty()) out.body += '&';
                out.body += formEncode(key) + '=' + formEncode(value);
            }
            contentType = "application/x-www-form-urlencoded";
        } else if (bodyType == QStringLiteral("json")) {
            out.body = body.toUtf8();
            contentType = "application/json; charset=utf-8";
        } else if (bodyType == QStringLiteral("text")) {
            out.body = body.toUtf8();
            contentType = "text/plain; charset=utf-8";
        } else {
            return fail(error, QStringLiteral("暂不支持此请求体类型：%1").arg(request.bodyType));
        }
        if (!hasHeader(out, "Content-Type")) setHeader(out, "Content-Type", contentType);
    }
    if (!missing.isEmpty())
        return fail(error, QStringLiteral("未定义的环境变量：%1").arg(missing.join(QStringLiteral("、"))));
    return true;
}

QString shellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}
} // namespace

RequestEngine::RequestEngine(QObject *parent) : QObject(parent), manager_(this)
{
    qRegisterMetaType<ResponseData>();
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (!reply_) return;
        timedOut_ = true;
        reply_->abort();
    });
}

bool RequestEngine::isBusy() const { return !reply_.isNull(); }

bool RequestEngine::send(const RequestData &request, const QList<KeyValue> &variables, QString *error)
{
    if (error) error->clear();
    if (isBusy()) return fail(error, QStringLiteral("已有请求正在发送，请等待完成或先取消。"));
    PreparedRequest prepared;
    if (!prepare(request, variables, prepared, error)) return false;
    QNetworkRequest networkRequest(prepared.url);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                request.followRedirects ? QNetworkRequest::SameOriginRedirectPolicy
                                                        : QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setMaximumRedirectsAllowed(10);
    networkRequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    networkRequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    networkRequest.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    for (const auto &header : prepared.headers) networkRequest.setRawHeader(header.first, header.second);
    buffer_.clear();
    cancelled_ = timedOut_ = truncated_ = false;
    elapsed_.start();
    auto *reply = prepared.method == "HEAD"
        ? manager_.head(networkRequest)
        : manager_.sendCustomRequest(networkRequest, prepared.method, prepared.body);
    reply_ = reply;
    reply->setReadBufferSize(256 * 1024);
    const auto consume = [this, reply] {
        const auto remaining = MaximumResponseBytes - buffer_.size();
        const auto data = reply->read(remaining + 1);
        if (data.size() > remaining) {
            buffer_.append(data.constData(), remaining);
            truncated_ = true;
            if (!reply->isFinished()) reply->abort();
        } else buffer_.append(data);
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    connect(reply, &QNetworkReply::downloadProgress, this, &RequestEngine::progress);
    connect(reply, &QNetworkReply::finished, this, [this, reply, consume] {
        deadline_.stop();
        if (!truncated_ && reply->bytesAvailable() > 0) consume();
        ResponseData result;
        result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        result.body = buffer_;
        result.sizeBytes = buffer_.size();
        result.elapsedMs = elapsed_.elapsed();
        result.finalUrl = reply->url().toString(QUrl::FullyEncoded);
        result.cancelled = cancelled_;
        result.truncated = truncated_;
        for (const auto &header : reply->rawHeaderPairs())
            result.headers.append({true, QString::fromLatin1(header.first), QString::fromUtf8(header.second)});
        if (truncated_) result.error = QStringLiteral("响应超过 10 MiB 上限，已停止接收，仅显示前 10 MiB。" );
        else if (timedOut_) result.error = QStringLiteral("请求超时，已终止连接。" );
        else if (cancelled_) result.error = QStringLiteral("请求已取消。" );
        else if (reply->error() == QNetworkReply::InsecureRedirectError)
            result.error = QStringLiteral("为保护认证信息，已阻止跨源重定向。请检查 Location 响应头。" );
        else if (reply->error() == QNetworkReply::TooManyRedirectsError)
            result.error = QStringLiteral("重定向超过 10 次，已停止请求。" );
        else if (reply->error() != QNetworkReply::NoError) {
            // HTTP errors are valid responses. Preserve their response body and status for inspection.
            const int code = static_cast<int>(reply->error());
            const bool httpError = result.statusCode >= 400 && ((code >= 201 && code <= 299) || (code >= 401 && code <= 499));
            if (!httpError) result.error = QStringLiteral("网络请求失败：%1").arg(reply->errorString());
        }
        reply_.clear();
        buffer_.clear();
        reply->deleteLater();
        emit finished(result);
    });
    deadline_.start(request.timeoutMs);
    return true;
}

void RequestEngine::cancel()
{
    if (!reply_) return;
    cancelled_ = true;
    reply_->abort();
}

QString RequestEngine::curlCommand(const RequestData &request, const QList<KeyValue> &variables, QString *error)
{
    PreparedRequest prepared;
    if (!prepare(request, variables, prepared, error)) return {};
    QStringList args{QStringLiteral("curl"), QStringLiteral("--globoff"), QStringLiteral("--request"),
                     shellQuote(QString::fromLatin1(prepared.method)), QStringLiteral("--url"),
                     shellQuote(prepared.url.toString(QUrl::FullyEncoded)), QStringLiteral("--max-time"),
                     QString::number(request.timeoutMs / 1000.0, 'f', 3)};
    if (prepared.method == "HEAD") args << QStringLiteral("--head");
    for (const auto &header : prepared.headers)
        args << QStringLiteral("--header") << shellQuote(QString::fromUtf8(header.first + ": " + header.second));
    if (prepared.hasBody) args << QStringLiteral("--data-binary") << shellQuote(QString::fromUtf8(prepared.body));
    QString command = args.join(QStringLiteral(" \\\n  "));
    if (request.followRedirects)
        command.prepend(QStringLiteral("# QSend 自动跟随同源重定向；curl 此处不自动跳转，避免跨源泄漏自定义凭据头。\n"));
    return command;
}
} // namespace qsend
