#pragma once

#include "models.h"

namespace qsend {
class WorkspaceStore {
public:
    explicit WorkspaceStore(const QString &filePath = {});
    QString filePath() const;
    WorkspaceData load(QString *error = nullptr) const;
    bool save(const WorkspaceData &workspace, QString *error = nullptr) const;
    static WorkspaceData defaults();
    static bool importCollection(const QString &filePath, QList<RequestData> *requests, QString *error = nullptr);
    static bool exportCollection(const QString &filePath, const QList<RequestData> &requests, QString *error = nullptr);
private:
    QString filePath_;
};
}
