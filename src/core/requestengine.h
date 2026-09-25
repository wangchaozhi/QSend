#pragma once

#include "models.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>

class QNetworkReply;

namespace qsend {
class RequestEngine : public QObject {
    Q_OBJECT
public:
    explicit RequestEngine(QObject *parent = nullptr);
    bool send(const RequestData &request, const QList<KeyValue> &variables, QString *error = nullptr);
    bool isBusy() const;
    void cancel();
    static QString curlCommand(const RequestData &request, const QList<KeyValue> &variables, QString *error = nullptr);
signals:
    void finished(const qsend::ResponseData &response);
    void progress(qint64 received, qint64 total);
private:
    QNetworkAccessManager manager_;
    QPointer<QNetworkReply> reply_;
    QElapsedTimer elapsed_;
    QTimer deadline_;
    QByteArray buffer_;
    bool cancelled_ = false;
    bool timedOut_ = false;
    bool truncated_ = false;
};
}
