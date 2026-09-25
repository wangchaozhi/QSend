#pragma once
#include <QSyntaxHighlighter>
class JsonHighlighter : public QSyntaxHighlighter {
public:
    explicit JsonHighlighter(QTextDocument *parent) : QSyntaxHighlighter(parent) {}
protected:
    void highlightBlock(const QString &text) override;
};
