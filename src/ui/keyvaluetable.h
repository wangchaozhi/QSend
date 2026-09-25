#pragma once
#include "core/models.h"
#include <QWidget>
class QTableWidget;

class KeyValueTable : public QWidget {
    Q_OBJECT
public:
    explicit KeyValueTable(QWidget *parent = nullptr);
    QList<qsend::KeyValue> values() const;
    void setValues(const QList<qsend::KeyValue> &values);
    QTableWidget *table() const { return table_; }
signals:
    void changed();
private:
    void addRow(const qsend::KeyValue &value);
    QTableWidget *table_;
};
