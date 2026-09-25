#include "jsonhighlighter.h"
#include <QRegularExpression>
#include <QTextCharFormat>

void JsonHighlighter::highlightBlock(const QString &text) {
    static const QRegularExpression tokens(QStringLiteral(R"re("(?:\\.|[^"\\])*"\s*(?=:)|"(?:\\.|[^"\\])*"|\b(?:true|false|null)\b|-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)re"));
    auto matches = tokens.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString token = match.captured().trimmed();
        QTextCharFormat format;
        if (token.startsWith('"')) {
            const auto rest = text.mid(match.capturedEnd()).trimmed();
            format.setForeground(rest.startsWith(':') ? QColor("#79b8ff") : QColor("#86d9b3"));
        } else if (token == "true" || token == "false" || token == "null") {
            format.setForeground(QColor("#c7a4ef"));
        } else {
            format.setForeground(QColor("#edbd7b"));
        }
        setFormat(static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()), format);
    }
}
