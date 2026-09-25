#pragma once
#include "core/requestengine.h"
#include "core/workspacestore.h"
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class KeyValueTable;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &workspacePath = {}, QWidget *parent = nullptr);
    void openUrl(const QString &url);
    void sendRequest();
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void buildUi();
    void bindUi();
    void setRequest(const qsend::RequestData &request);
    qsend::RequestData currentRequest() const;
    QList<qsend::KeyValue> variables() const;
    void markDirty();
    bool guardUnsaved();
    bool saveRequest();
    bool persist();
    void refreshSidebar();
    void refreshEnvironments();
    void showResponse(const qsend::ResponseData &response);
    void editEnvironments();
    void importCollection();
    void exportCollection();
    void setMessage(const QString &message);
    void formatBody();
    void updateAuth();

    qsend::WorkspaceStore store_;
    qsend::WorkspaceData workspace_;
    qsend::RequestEngine engine_;
    qsend::RequestData active_;
    qsend::RequestData sentRequest_;
    qsend::ResponseData response_;
    bool loading_ = false;
    bool dirty_ = false;
    bool storageHealthy_ = true;
    bool haveResponse_ = false;

    QLineEdit *search_, *requestName_, *url_, *token_, *username_, *password_;
    QComboBox *method_, *environment_, *bodyType_, *authType_;
    QListWidget *collection_, *history_;
    QPushButton *send_, *cancel_, *save_, *new_, *import_, *export_, *envEdit_, *delete_, *curl_, *responseSave_, *responseCopy_;
    KeyValueTable *params_, *headers_;
    QPlainTextEdit *body_, *pretty_, *raw_;
    QTableWidget *responseHeaders_;
    QTabWidget *requestTabs_, *responseTabs_;
    QLabel *message_, *statusBadge_, *timing_, *size_, *responseHint_, *bodyHint_;
    QSpinBox *timeout_;
    QCheckBox *followRedirects_;
    QWidget *tokenRow_, *basicRow_, *editor_;
};
