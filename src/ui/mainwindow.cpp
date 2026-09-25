#include "mainwindow.h"
#include "jsonhighlighter.h"
#include "keyvaluetable.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

using namespace qsend;

namespace {
QLabel *label(const QString &text, const char *name = nullptr) {
    auto *result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    if (name) result->setObjectName(QString::fromLatin1(name));
    return result;
}
QPushButton *button(const QString &text, const char *name = nullptr) {
    auto *result = new QPushButton(text);
    result->setCursor(Qt::PointingHandCursor);
    if (name) result->setObjectName(QString::fromLatin1(name));
    return result;
}
QString byteText(qint64 bytes) {
    return bytes >= 1024 * 1024 ? QString::number(bytes / 1048576.0, 'f', 2) + " MB"
         : bytes >= 1024 ? QString::number(bytes / 1024.0, 'f', 1) + " KB"
         : QString::number(bytes) + " B";
}
}

MainWindow::MainWindow(const QString &workspacePath, QWidget *parent)
    : QMainWindow(parent), store_(workspacePath), engine_(this) {
    QString error;
    workspace_ = store_.load(&error);
    storageHealthy_ = error.isEmpty();
    buildUi();
    bindUi();
    refreshEnvironments();
    refreshSidebar();
    if (!workspace_.requests.isEmpty()) setRequest(workspace_.requests.first());
    else setRequest(RequestData{});
    if (!error.isEmpty()) setMessage(QStringLiteral("工作区读取失败，已停用自动写入以保留原文件：") + error);
    statusBar()->showMessage(QStringLiteral("就绪  ·  Ctrl+Enter 发送  ·  Ctrl+S 保存  ·  Ctrl+N 新建"));
    setMinimumSize(1000, 760);
    resize(1380, 920);
}

void MainWindow::buildUi() {
    auto *central = new QWidget;
    central->setObjectName("central");
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    auto *top = new QFrame;
    top->setObjectName("topbar");
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(20, 13, 24, 13);
    topLayout->setSpacing(12);
    auto *mark = label(QStringLiteral("↗"), "brandMark");
    mark->setFixedSize(40, 40);
    mark->setAlignment(Qt::AlignCenter);
    topLayout->addWidget(mark);
    topLayout->addWidget(label("QSend", "brand"));
    topLayout->addSpacing(14);
    topLayout->addWidget(label(QStringLiteral("API 调试工作台"), "muted"));
    topLayout->addStretch();
    topLayout->addWidget(label(QStringLiteral("当前环境"), "muted"));
    environment_ = new QComboBox;
    environment_->setObjectName("environment");
    environment_->setMinimumWidth(154);
    topLayout->addWidget(environment_);
    envEdit_ = button(QStringLiteral("管理变量"));
    topLayout->addWidget(envEdit_);
    root->addWidget(top);

    auto *horizontal = new QSplitter(Qt::Horizontal);
    horizontal->setChildrenCollapsible(false);
    root->addWidget(horizontal, 1);
    auto *sidebar = new QFrame;
    sidebar->setObjectName("sidebar");
    sidebar->setMinimumWidth(230);
    sidebar->setMaximumWidth(420);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(16, 22, 16, 16);
    side->setSpacing(12);
    side->addWidget(label("WORKSPACE", "eyebrow"));
    auto *workspaceTitle = new QHBoxLayout;
    workspaceTitle->addWidget(label(QStringLiteral("我的工作区"), "sectionTitle"));
    workspaceTitle->addStretch();
    workspaceTitle->addWidget(label(QStringLiteral("本地"), "muted"));
    side->addLayout(workspaceTitle);
    new_ = button(QStringLiteral("＋  新建请求"), "newRequest");
    side->addWidget(new_);
    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("搜索请求名称或 URL"));
    search_->setClearButtonEnabled(true);
    side->addWidget(search_);
    auto *sideTabs = new QTabWidget;
    sideTabs->setDocumentMode(true);
    collection_ = new QListWidget;
    collection_->setObjectName("collectionList");
    history_ = new QListWidget;
    history_->setObjectName("historyList");
    sideTabs->addTab(collection_, QStringLiteral("请求集合"));
    sideTabs->addTab(history_, QStringLiteral("历史记录"));
    side->addWidget(sideTabs, 1);
    delete_ = button(QStringLiteral("删除选中请求"));
    side->addWidget(delete_);
    auto *transfer = new QHBoxLayout;
    import_ = button(QStringLiteral("导入"));
    export_ = button(QStringLiteral("导出集合"));
    import_->setToolTip(QStringLiteral("导入 Postman v2.1 或 QSend JSON 集合"));
    transfer->addWidget(import_);
    transfer->addWidget(export_);
    side->addLayout(transfer);
    auto *local = label(QStringLiteral("● 本地存储 · 无需登录"), "muted");
    local->setToolTip(store_.filePath());
    side->addWidget(local);
    horizontal->addWidget(sidebar);

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(26, 20, 26, 20);
    contentLayout->setSpacing(13);
    auto *titleLine = new QHBoxLayout;
    titleLine->addWidget(label(QStringLiteral("集合  /"), "muted"));
    requestName_ = new QLineEdit;
    requestName_->setObjectName("requestName");
    requestName_->setPlaceholderText(QStringLiteral("为请求起个名字"));
    requestName_->setMaximumWidth(350);
    titleLine->addWidget(requestName_);
    titleLine->addStretch();
    curl_ = button(QStringLiteral("复制 cURL"));
    curl_->setToolTip(QStringLiteral("复制 POSIX shell 格式的命令（可用于 Git Bash / WSL / macOS / Linux）"));
    save_ = button(QStringLiteral("保存请求"), "saveRequest");
    titleLine->addWidget(curl_);
    titleLine->addWidget(save_);
    contentLayout->addLayout(titleLine);

    auto *requestLine = new QHBoxLayout;
    requestLine->setSpacing(9);
    method_ = new QComboBox;
    method_->setObjectName("method");
    method_->addItems({"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"});
    method_->setEditable(true);
    method_->setFixedWidth(116);
    url_ = new QLineEdit;
    url_->setObjectName("requestUrl");
    url_->setPlaceholderText("https://api.example.com/v1/users");
    url_->setClearButtonEnabled(true);
    send_ = button(QStringLiteral("发送  ↗"), "sendButton");
    cancel_ = button(QStringLiteral("取消"), "cancelButton");
    cancel_->hide();
    requestLine->addWidget(method_);
    requestLine->addWidget(url_, 1);
    requestLine->addWidget(send_);
    requestLine->addWidget(cancel_);
    contentLayout->addLayout(requestLine);
    message_ = label({}, "inlineMessage");
    message_->setWordWrap(true);
    message_->hide();
    contentLayout->addWidget(message_);

    auto *vertical = new QSplitter(Qt::Vertical);
    vertical->setChildrenCollapsible(false);
    contentLayout->addWidget(vertical, 1);
    editor_ = new QWidget;
    auto *editLayout = new QVBoxLayout(editor_);
    editLayout->setContentsMargins(0, 0, 0, 14);
    requestTabs_ = new QTabWidget;
    requestTabs_->setObjectName("requestTabs");
    requestTabs_->setDocumentMode(true);
    params_ = new KeyValueTable;
    params_->setObjectName("paramsEditor");
    headers_ = new KeyValueTable;
    headers_->setObjectName("headersEditor");
    requestTabs_->addTab(params_, QStringLiteral("参数 Params"));
    requestTabs_->addTab(headers_, QStringLiteral("请求头 Headers"));

    auto *bodyPage = new QWidget;
    auto *bodyLayout = new QVBoxLayout(bodyPage);
    bodyLayout->setContentsMargins(0, 10, 0, 0);
    auto *bodyBar = new QHBoxLayout;
    bodyType_ = new QComboBox;
    bodyType_->setObjectName("bodyType");
    bodyType_->addItem(QStringLiteral("无请求体"), "none");
    bodyType_->addItem("JSON", "json");
    bodyType_->addItem(QStringLiteral("纯文本"), "text");
    bodyType_->addItem("x-www-form-urlencoded", "form");
    bodyBar->addWidget(bodyType_);
    bodyHint_ = label(QStringLiteral("支持 {{变量名}}"), "muted");
    bodyBar->addWidget(bodyHint_);
    bodyBar->addStretch();
    auto *format = button(QStringLiteral("格式化 JSON"), "formatJson");
    bodyBar->addWidget(format);
    bodyLayout->addLayout(bodyBar);
    body_ = new QPlainTextEdit;
    body_->setObjectName("requestBody");
    body_->setPlaceholderText(QStringLiteral("在此输入请求体…"));
    new JsonHighlighter(body_->document());
    bodyLayout->addWidget(body_, 1);
    requestTabs_->addTab(bodyPage, QStringLiteral("请求体 Body"));
    connect(format, &QPushButton::clicked, this, &MainWindow::formatBody);

    auto *authPage = new QWidget;
    auto *authLayout = new QVBoxLayout(authPage);
    authLayout->setContentsMargins(4, 18, 4, 10);
    auto *authBar = new QHBoxLayout;
    authBar->addWidget(label(QStringLiteral("认证方式")));
    authType_ = new QComboBox;
    authType_->setObjectName("authType");
    authType_->addItem(QStringLiteral("无需认证"), "none");
    authType_->addItem("Bearer Token", "bearer");
    authType_->addItem("Basic Auth", "basic");
    authBar->addWidget(authType_);
    authBar->addStretch();
    authLayout->addLayout(authBar);
    tokenRow_ = new QWidget;
    auto *tokenLayout = new QFormLayout(tokenRow_);
    tokenLayout->setContentsMargins(0, 10, 0, 0);
    token_ = new QLineEdit;
    token_->setObjectName("token");
    token_->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    token_->setPlaceholderText(QStringLiteral("输入 Token 或 {{token}}"));
    tokenLayout->addRow("Token", token_);
    authLayout->addWidget(tokenRow_);
    basicRow_ = new QWidget;
    auto *basicLayout = new QFormLayout(basicRow_);
    basicLayout->setContentsMargins(0, 10, 0, 0);
    username_ = new QLineEdit;
    username_->setObjectName("username");
    password_ = new QLineEdit;
    password_->setObjectName("password");
    password_->setEchoMode(QLineEdit::Password);
    basicLayout->addRow(QStringLiteral("用户名"), username_);
    basicLayout->addRow(QStringLiteral("密码"), password_);
    authLayout->addWidget(basicRow_);
    authLayout->addStretch();
    authLayout->addWidget(label(QStringLiteral("建议使用环境变量管理凭据。保存的请求和历史记录存放在本机。"), "muted"));
    requestTabs_->addTab(authPage, QStringLiteral("认证 Auth"));

    auto *settingsPage = new QWidget;
    auto *settings = new QFormLayout(settingsPage);
    settings->setContentsMargins(4, 24, 4, 12);
    timeout_ = new QSpinBox;
    timeout_->setObjectName("timeout");
    timeout_->setRange(1, 600);
    timeout_->setSuffix(QStringLiteral(" 秒"));
    timeout_->setMaximumWidth(140);
    settings->addRow(QStringLiteral("请求超时"), timeout_);
    followRedirects_ = new QCheckBox(QStringLiteral("自动跟随同源重定向"));
    followRedirects_->setToolTip(QStringLiteral("仅自动跳转到相同协议、主机及端口"));
    settings->addRow(QString{}, followRedirects_);
    settings->addRow(QString{}, label(QStringLiteral("HTTPS 使用系统证书校验；单次响应最多读取 10 MB。"), "muted"));
    requestTabs_->addTab(settingsPage, QStringLiteral("设置"));
    editLayout->addWidget(requestTabs_);
    editor_->setMinimumHeight(225);
    vertical->addWidget(editor_);

    auto *responsePage = new QWidget;
    responsePage->setMinimumHeight(220);
    auto *responseLayout = new QVBoxLayout(responsePage);
    responseLayout->setContentsMargins(0, 16, 0, 0);
    responseLayout->setSpacing(10);
    auto *responseBar = new QHBoxLayout;
    responseBar->addWidget(label(QStringLiteral("响应"), "sectionTitle"));
    responseBar->addSpacing(10);
    statusBadge_ = label(QStringLiteral("待发送"), "responseBadge");
    statusBadge_->setObjectName("responseBadge");
    timing_ = label(QStringLiteral("— ms"), "muted");
    size_ = label(QStringLiteral("— B"), "muted");
    responseBar->addWidget(statusBadge_);
    responseBar->addSpacing(8);
    responseBar->addWidget(timing_);
    responseBar->addSpacing(8);
    responseBar->addWidget(size_);
    responseBar->addStretch();
    responseCopy_ = button(QStringLiteral("复制"));
    responseSave_ = button(QStringLiteral("保存响应"));
    responseCopy_->setEnabled(false);
    responseSave_->setEnabled(false);
    responseBar->addWidget(responseCopy_);
    responseBar->addWidget(responseSave_);
    responseLayout->addLayout(responseBar);
    responseTabs_ = new QTabWidget;
    responseTabs_->setDocumentMode(true);
    pretty_ = new QPlainTextEdit;
    pretty_->setObjectName("responsePretty");
    pretty_->setReadOnly(true);
    pretty_->setPlaceholderText(QStringLiteral("准备好发起第一个请求了吗？\n\n输入 URL，或从左侧选择一个示例，然后点击「发送」。\n响应内容、耗时和状态码会显示在这里。"));
    new JsonHighlighter(pretty_->document());
    raw_ = new QPlainTextEdit;
    raw_->setObjectName("responseRaw");
    raw_->setReadOnly(true);
    responseHeaders_ = new QTableWidget(0, 2);
    responseHeaders_->setHorizontalHeaderLabels({"KEY", "VALUE"});
    responseHeaders_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    responseHeaders_->verticalHeader()->hide();
    responseHeaders_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    responseHeaders_->setShowGrid(false);
    responseTabs_->addTab(pretty_, "Pretty");
    responseTabs_->addTab(raw_, "Raw");
    responseTabs_->addTab(responseHeaders_, QStringLiteral("响应头"));
    responseLayout->addWidget(responseTabs_, 1);
    responseHint_ = label(QStringLiteral("响应将在此显示 · JSON 自动格式化"), "muted");
    responseHint_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    responseHint_->setWordWrap(true);
    responseLayout->addWidget(responseHint_);
    vertical->addWidget(responsePage);
    vertical->setSizes({340, 365});
    horizontal->addWidget(content);
    horizontal->setSizes({270, 1110});
    horizontal->setStretchFactor(1, 1);

    auto shortcut = [this](const QKeySequence &key, auto fn) {
        auto *action = new QAction(this);
        action->setShortcut(key);
        addAction(action);
        connect(action, &QAction::triggered, this, fn);
    };
    shortcut(QKeySequence("Ctrl+Return"), [this] { sendRequest(); });
    shortcut(QKeySequence::Save, [this] { saveRequest(); });
    shortcut(QKeySequence::New, [this] { new_->click(); });
    auto *version = label("QSend 0.1  ·  Qt 6", "muted");
    statusBar()->addPermanentWidget(version);
}

void MainWindow::bindUi() {
    for (auto *field : {requestName_, url_, token_, username_, password_})
        connect(field, &QLineEdit::textChanged, this, &MainWindow::markDirty);
    for (auto *combo : {method_, bodyType_, authType_})
        connect(combo, &QComboBox::currentTextChanged, this, &MainWindow::markDirty);
    connect(body_, &QPlainTextEdit::textChanged, this, &MainWindow::markDirty);
    connect(params_, &KeyValueTable::changed, this, &MainWindow::markDirty);
    connect(headers_, &KeyValueTable::changed, this, &MainWindow::markDirty);
    connect(timeout_, &QSpinBox::valueChanged, this, &MainWindow::markDirty);
    connect(followRedirects_, &QCheckBox::toggled, this, &MainWindow::markDirty);
    connect(authType_, &QComboBox::currentIndexChanged, this, &MainWindow::updateAuth);
    connect(bodyType_, &QComboBox::currentIndexChanged, this, [this] {
        body_->setEnabled(bodyType_->currentData().toString() != "none");
        bodyHint_->setText(bodyType_->currentData().toString() == "form"
            ? QStringLiteral("每行 key=value，自动编码") : QStringLiteral("支持 {{变量名}}"));
    });
    connect(send_, &QPushButton::clicked, this, &MainWindow::sendRequest);
    connect(cancel_, &QPushButton::clicked, &engine_, &RequestEngine::cancel);
    connect(save_, &QPushButton::clicked, this, [this] { saveRequest(); });
    connect(new_, &QPushButton::clicked, this, [this] {
        if (!guardUnsaved()) return;
        RequestData request;
        request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        setRequest(request);
        requestName_->setFocus();
        requestName_->selectAll();
    });
    connect(collection_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString id = item->data(Qt::UserRole).toString();
        if (id == active_.id) return;
        if (!guardUnsaved()) { refreshSidebar(); return; }
        for (const auto &request : workspace_.requests) if (request.id == id) { setRequest(request); break; }
    });
    connect(history_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int index = item->data(Qt::UserRole).toInt();
        if (index < 0 || index >= workspace_.history.size() || !guardUnsaved()) return;
        auto request = workspace_.history.at(index).request;
        request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        setRequest(request);
        markDirty();
        setMessage(QStringLiteral("已从历史记录载入。保存后将作为一个新请求加入集合。"));
    });
    connect(search_, &QLineEdit::textChanged, this, &MainWindow::refreshSidebar);
    connect(environment_, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (loading_ || text.isEmpty()) return;
        workspace_.activeEnvironment = text;
        persist();
        statusBar()->showMessage(QStringLiteral("已切换环境：") + text, 4000);
    });
    connect(envEdit_, &QPushButton::clicked, this, &MainWindow::editEnvironments);
    connect(import_, &QPushButton::clicked, this, &MainWindow::importCollection);
    connect(export_, &QPushButton::clicked, this, &MainWindow::exportCollection);
    connect(delete_, &QPushButton::clicked, this, [this] {
        auto *item = collection_->currentItem();
        if (!item) return;
        const auto id = item->data(Qt::UserRole).toString();
        if (QMessageBox::question(this, QStringLiteral("删除请求"), QStringLiteral("从集合中删除这个请求？")) != QMessageBox::Yes) return;
        const auto previous = workspace_.requests;
        for (qsizetype i = 0; i < workspace_.requests.size(); ++i) if (workspace_.requests[i].id == id) { workspace_.requests.removeAt(i); break; }
        if (!persist()) { workspace_.requests = previous; return; }
        if (active_.id == id) setRequest(RequestData{});
        refreshSidebar();
    });
    connect(curl_, &QPushButton::clicked, this, [this] {
        QString error;
        const auto command = RequestEngine::curlCommand(currentRequest(), variables(), &error);
        if (!error.isEmpty()) { setMessage(error); return; }
        QApplication::clipboard()->setText(command);
        statusBar()->showMessage(QStringLiteral("cURL 命令已复制，可在 Git Bash / WSL 等 POSIX shell 中使用"), 5000);
    });
    connect(responseCopy_, &QPushButton::clicked, this, [this] {
        QString text;
        if (responseTabs_->currentIndex() == 2) {
            for (const auto &header : response_.headers) text += header.key + ": " + header.value + '\n';
        } else text = responseTabs_->currentIndex() == 0 ? pretty_->toPlainText() : raw_->toPlainText();
        QApplication::clipboard()->setText(text);
        statusBar()->showMessage(QStringLiteral("响应内容已复制"), 3000);
    });
    connect(responseSave_, &QPushButton::clicked, this, [this] {
        if (!haveResponse_) return;
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存原始响应"), "response.json", "All files (*)");
        if (path.isEmpty()) return;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(response_.body) != response_.body.size() || !file.commit()) {
            setMessage(QStringLiteral("保存失败：") + file.errorString());
            return;
        }
        statusBar()->showMessage(QStringLiteral("响应已保存至：") + path, 5000);
    });
    connect(&engine_, &RequestEngine::progress, this, [this](qint64 received, qint64) {
        statusBar()->showMessage(QStringLiteral("正在接收响应：") + byteText(received));
    });
    connect(&engine_, &RequestEngine::finished, this, &MainWindow::showResponse);
}

void MainWindow::setRequest(const RequestData &request) {
    loading_ = true;
    active_ = request;
    if (active_.id.isEmpty()) active_.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    requestName_->setText(active_.name);
    if (method_->findText(active_.method) < 0) method_->addItem(active_.method);
    method_->setCurrentText(active_.method);
    url_->setText(active_.url);
    params_->setValues(active_.params);
    headers_->setValues(active_.headers);
    bodyType_->setCurrentIndex(qMax(0, bodyType_->findData(active_.bodyType)));
    body_->setPlainText(active_.body);
    body_->setEnabled(active_.bodyType != "none");
    authType_->setCurrentIndex(qMax(0, authType_->findData(active_.authType)));
    token_->setText(active_.token);
    username_->setText(active_.username);
    password_->setText(active_.password);
    timeout_->setValue(qMax(1, active_.timeoutMs / 1000));
    followRedirects_->setChecked(active_.followRedirects);
    updateAuth();
    loading_ = false;
    dirty_ = false;
    save_->setText(QStringLiteral("保存请求"));
    setWindowTitle(active_.name + " — QSend");
    setMessage({});
    pretty_->clear();
    raw_->clear();
    responseHeaders_->setRowCount(0);
    statusBadge_->setText(QStringLiteral("待发送"));
    timing_->setText(QStringLiteral("— ms"));
    size_->setText(QStringLiteral("— B"));
    responseHint_->setText(QStringLiteral("响应将在此显示 · JSON 自动格式化"));
    haveResponse_ = false;
    responseCopy_->setEnabled(false);
    responseSave_->setEnabled(false);
    refreshSidebar();
}

RequestData MainWindow::currentRequest() const {
    RequestData request = active_;
    request.name = requestName_->text().trimmed();
    if (request.name.isEmpty()) request.name = QStringLiteral("未命名请求");
    request.method = method_->currentText();
    request.url = url_->text().trimmed();
    request.params = params_->values();
    request.headers = headers_->values();
    request.bodyType = bodyType_->currentData().toString();
    request.body = body_->toPlainText();
    request.authType = authType_->currentData().toString();
    request.token = token_->text();
    request.username = username_->text();
    request.password = password_->text();
    request.timeoutMs = timeout_->value() * 1000;
    request.followRedirects = followRedirects_->isChecked();
    return request;
}

QList<KeyValue> MainWindow::variables() const {
    return workspace_.environments.value(workspace_.activeEnvironment);
}

void MainWindow::markDirty() {
    if (loading_) return;
    dirty_ = true;
    save_->setText(QStringLiteral("保存请求 •"));
    setWindowTitle(requestName_->text() + " * — QSend");
}

bool MainWindow::guardUnsaved() {
    if (!dirty_) return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("未保存的修改"), QStringLiteral("保存当前请求的修改吗？"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) return saveRequest();
    return answer == QMessageBox::Discard;
}

bool MainWindow::saveRequest() {
    const auto request = currentRequest();
    const auto previous = workspace_.requests;
    bool replaced = false;
    for (auto &saved : workspace_.requests) if (saved.id == request.id) { saved = request; replaced = true; break; }
    if (!replaced) workspace_.requests.append(request);
    if (!persist()) { workspace_.requests = previous; return false; }
    active_ = request;
    dirty_ = false;
    save_->setText(QStringLiteral("保存请求"));
    setWindowTitle(request.name + " — QSend");
    refreshSidebar();
    statusBar()->showMessage(QStringLiteral("已保存：") + request.name, 4000);
    return true;
}

bool MainWindow::persist() {
    if (!storageHealthy_) {
        setMessage(QStringLiteral("工作区文件无法读取，暂不覆盖原文件。请导出集合备份，并检查：") + store_.filePath());
        return false;
    }
    QString error;
    if (!store_.save(workspace_, &error)) { setMessage(QStringLiteral("保存工作区失败：") + error); return false; }
    return true;
}

void MainWindow::refreshSidebar() {
    const QSignalBlocker a(collection_), b(history_);
    collection_->clear();
    history_->clear();
    const QString filter = search_->text().trimmed();
    for (const auto &request : workspace_.requests) {
        if (!filter.isEmpty() && !(request.name + request.url).contains(filter, Qt::CaseInsensitive)) continue;
        auto *item = new QListWidgetItem(QString("%1   %2").arg(request.method, request.name), collection_);
        item->setData(Qt::UserRole, request.id);
        item->setToolTip(request.method + " " + request.url);
        if (request.id == active_.id) collection_->setCurrentItem(item);
    }
    for (qsizetype i = 0; i < workspace_.history.size(); ++i) {
        const auto &entry = workspace_.history.at(i);
        if (!filter.isEmpty() && !(entry.request.name + entry.request.url).contains(filter, Qt::CaseInsensitive)) continue;
        auto *item = new QListWidgetItem(QString("%1   %2\n%3  ·  %4  ·  %5 ms")
            .arg(entry.request.method, entry.request.name, entry.time.toLocalTime().toString("MM-dd HH:mm:ss"))
            .arg(entry.statusCode == 0 ? QStringLiteral("无响应") : QString::number(entry.statusCode)).arg(entry.elapsedMs), history_);
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setToolTip(entry.request.url);
    }
}

void MainWindow::refreshEnvironments() {
    const QSignalBlocker blocker(environment_);
    environment_->clear();
    if (workspace_.environments.isEmpty()) workspace_.environments.insert(QStringLiteral("本地开发"), {});
    if (!workspace_.environments.contains(workspace_.activeEnvironment)) workspace_.activeEnvironment = workspace_.environments.firstKey();
    environment_->addItems(workspace_.environments.keys());
    environment_->setCurrentText(workspace_.activeEnvironment);
}

void MainWindow::openUrl(const QString &url) {
    url_->setText(url);
}

void MainWindow::sendRequest() {
    if (engine_.isBusy()) return;
    QString error;
    sentRequest_ = currentRequest();
    if (!engine_.send(sentRequest_, variables(), &error)) { setMessage(error); return; }
    setMessage({});
    send_->setEnabled(false);
    send_->setText(QStringLiteral("请求中…"));
    cancel_->show();
    for (QWidget *widget : QList<QWidget *>{editor_, requestName_, url_, method_, environment_, envEdit_, new_, collection_, history_, delete_, import_})
        widget->setEnabled(false);
    statusBadge_->setText(QStringLiteral("发送中"));
    statusBar()->showMessage(QStringLiteral("正在发送请求…"));
}

void MainWindow::showResponse(const ResponseData &response) {
    response_ = response;
    haveResponse_ = true;
    send_->setEnabled(true);
    send_->setText(QStringLiteral("发送  ↗"));
    cancel_->hide();
    for (QWidget *widget : QList<QWidget *>{editor_, requestName_, url_, method_, environment_, envEdit_, new_, collection_, history_, delete_, import_})
        widget->setEnabled(true);
    responseCopy_->setEnabled(true);
    responseSave_->setEnabled(true);
    const QString statusText = response.statusCode > 0 ? QString::number(response.statusCode) + " " + response.reason
        : response.cancelled ? QStringLiteral("已取消") : QStringLiteral("请求失败");
    statusBadge_->setText(statusText);
    const bool success = response.statusCode >= 200 && response.statusCode < 400 && response.error.isEmpty();
    statusBadge_->setStyleSheet(success ? "color:#8ce6bd; background:#1d392e;" : "color:#f1bc85; background:#3a2d22;");
    timing_->setText(QString::number(response.elapsedMs) + " ms");
    size_->setText(byteText(response.sizeBytes));
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(response.body, &parse);
    // Keep the entire payload for Save; bound text presentation for large responses.
    constexpr int displayLimit = 1024 * 1024;
    const QByteArray display = response.body.left(displayLimit);
    raw_->setPlainText(QString::fromUtf8(display));
    if (response.body.size() <= displayLimit && parse.error == QJsonParseError::NoError && !json.isNull())
        pretty_->setPlainText(QString::fromUtf8(json.toJson(QJsonDocument::Indented)));
    else pretty_->setPlainText(QString::fromUtf8(display));
    responseHeaders_->setRowCount(0);
    for (const auto &header : response.headers) {
        const int row = responseHeaders_->rowCount();
        responseHeaders_->insertRow(row);
        responseHeaders_->setItem(row, 0, new QTableWidgetItem(header.key));
        responseHeaders_->setItem(row, 1, new QTableWidgetItem(header.value));
    }
    QString hint = response.finalUrl;
    if (response.body.size() > displayLimit) hint += QStringLiteral("  ·  仅预览前 1 MB，保存响应可获取已接收数据");
    if (response.truncated) hint += QStringLiteral("  ·  响应超过读取上限，内容已截断");
    if (response.body.isEmpty()) hint += QStringLiteral("  ·  响应体为空");
    responseHint_->setText(hint);
    setMessage(response.error);
    workspace_.history.prepend({sentRequest_, QDateTime::currentDateTimeUtc(), response.statusCode, response.elapsedMs});
    while (workspace_.history.size() > 100) workspace_.history.removeLast();
    persist();
    refreshSidebar();
    statusBar()->showMessage(QStringLiteral("请求完成  ·  ") + statusText, 7000);
}

void MainWindow::setMessage(const QString &message) {
    message_->setText(message);
    message_->setVisible(!message.isEmpty());
}

void MainWindow::formatBody() {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(body_->toPlainText().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || doc.isNull()) {
        setMessage(QStringLiteral("JSON 格式错误：") + error.errorString() + QStringLiteral("，位置 ") + QString::number(error.offset));
        return;
    }
    body_->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
    setMessage({});
}

void MainWindow::updateAuth() {
    tokenRow_->setVisible(authType_->currentData().toString() == "bearer");
    basicRow_->setVisible(authType_->currentData().toString() == "basic");
}

void MainWindow::editEnvironments() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("环境变量"));
    dialog.resize(780, 480);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(label(QStringLiteral("环境变量"), "sectionTitle"));
    layout->addWidget(label(QStringLiteral("在 URL、参数、请求头、认证和请求体中使用 {{变量名}}。"), "muted"));
    auto environments = workspace_.environments;
    QString selected = workspace_.activeEnvironment;
    auto *row = new QHBoxLayout;
    auto *picker = new QComboBox;
    picker->addItems(environments.keys());
    picker->setCurrentText(selected);
    auto *add = button(QStringLiteral("新增环境"));
    auto *remove = button(QStringLiteral("删除环境"));
    row->addWidget(picker, 1);
    row->addWidget(add);
    row->addWidget(remove);
    layout->addLayout(row);
    auto *table = new KeyValueTable;
    table->setValues(environments.value(selected));
    layout->addWidget(table, 1);
    layout->addWidget(label(QStringLiteral("变量保存在本机，未加密。导出请求集合不会包含环境变量。"), "muted"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(picker, &QComboBox::currentTextChanged, &dialog, [&](const QString &name) {
        if (name.isEmpty()) return;
        environments[selected] = table->values();
        selected = name;
        table->setValues(environments.value(selected));
    });
    connect(add, &QPushButton::clicked, &dialog, [&] {
        bool ok = false;
        const QString name = QInputDialog::getText(&dialog, QStringLiteral("新增环境"), QStringLiteral("环境名称"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (environments.contains(name)) { QMessageBox::information(&dialog, QStringLiteral("环境已存在"), QStringLiteral("请使用不同的名称。")); return; }
        environments.insert(name, {});
        picker->addItem(name);
        picker->setCurrentText(name);
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (environments.size() <= 1) return;
        const QSignalBlocker blocker(picker);
        environments.remove(selected);
        picker->removeItem(picker->currentIndex());
        selected = picker->currentText();
        table->setValues(environments.value(selected));
    });
    if (dialog.exec() != QDialog::Accepted) return;
    environments[selected] = table->values();
    const auto previous = workspace_;
    workspace_.environments = environments;
    workspace_.activeEnvironment = selected;
    if (!persist()) { workspace_ = previous; return; }
    refreshEnvironments();
    statusBar()->showMessage(QStringLiteral("环境变量已保存"), 4000);
}

void MainWindow::importCollection() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入集合"), {}, "JSON (*.json)");
    if (path.isEmpty()) return;
    QList<RequestData> imported;
    QString error;
    if (!WorkspaceStore::importCollection(path, &imported, &error)) { setMessage(error); return; }
    const auto previous = workspace_.requests;
    for (auto &request : imported) {
        request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        workspace_.requests.append(request);
    }
    if (!persist()) { workspace_.requests = previous; return; }
    refreshSidebar();
    statusBar()->showMessage(QStringLiteral("已导入 %1 个请求").arg(imported.size()), 5000);
}

void MainWindow::exportCollection() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出集合"), "QSend.postman_collection.json", "JSON (*.json)");
    if (path.isEmpty()) return;
    QString error;
    if (!WorkspaceStore::exportCollection(path, workspace_.requests, &error)) { setMessage(error); return; }
    statusBar()->showMessage(QStringLiteral("已导出 Postman v2.1 集合：") + path, 6000);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!guardUnsaved()) { event->ignore(); return; }
    if (engine_.isBusy()) engine_.cancel();
    event->accept();
}
