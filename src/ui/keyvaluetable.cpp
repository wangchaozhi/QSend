#include "keyvaluetable.h"
#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

KeyValueTable::KeyValueTable(QWidget *parent) : QWidget(parent), table_(new QTableWidget(this)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({QStringLiteral("启用"), QStringLiteral("KEY"), QStringLiteral("VALUE")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_->setColumnWidth(0, 52);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(36);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    layout->addWidget(table_);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("＋ 添加一行"));
    add->setObjectName("addRow");
    auto *remove = new QPushButton(QStringLiteral("删除选中"));
    remove->setObjectName("removeRow");
    auto *hint = new QLabel(QStringLiteral("双击编辑 · 支持 {{变量名}}"));
    hint->setObjectName("muted");
    actions->addWidget(add);
    actions->addWidget(remove);
    actions->addStretch();
    actions->addWidget(hint);
    layout->addLayout(actions);
    connect(table_, &QTableWidget::itemChanged, this, &KeyValueTable::changed);
    connect(add, &QPushButton::clicked, this, [this] {
        addRow({true, {}, {}});
        table_->setCurrentCell(table_->rowCount() - 1, 1);
        table_->editItem(table_->item(table_->rowCount() - 1, 1));
        emit changed();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        auto rows = table_->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });
        for (const auto &row : rows) table_->removeRow(row.row());
        if (!rows.isEmpty()) emit changed();
    });
}

void KeyValueTable::addRow(const qsend::KeyValue &value) {
    const QSignalBlocker blocker(table_);
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto *check = new QTableWidgetItem;
    check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    check->setCheckState(value.enabled ? Qt::Checked : Qt::Unchecked);
    table_->setItem(row, 0, check);
    table_->setItem(row, 1, new QTableWidgetItem(value.key));
    table_->setItem(row, 2, new QTableWidgetItem(value.value));
}

QList<qsend::KeyValue> KeyValueTable::values() const {
    QList<qsend::KeyValue> result;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QString key = table_->item(row, 1)->text().trimmed();
        if (!key.isEmpty()) result.append({table_->item(row, 0)->checkState() == Qt::Checked, key, table_->item(row, 2)->text()});
    }
    return result;
}

void KeyValueTable::setValues(const QList<qsend::KeyValue> &values) {
    const QSignalBlocker blocker(table_);
    table_->setRowCount(0);
    for (const auto &value : values) addRow(value);
    if (values.isEmpty()) addRow({true, {}, {}});
}
