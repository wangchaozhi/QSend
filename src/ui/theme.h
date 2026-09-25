#pragma once
#include <QString>

inline QString appStyle() {
    return QStringLiteral(R"qss(
* { font-family: "Microsoft YaHei UI", "Noto Sans", "Segoe UI"; font-size: 13px; }
QMainWindow, QDialog { background: #10151c; color: #dce4ee; }
QWidget { color: #dce4ee; }
QWidget#central { background: #10151c; }
QFrame#sidebar { background: #141b24; border-right: 1px solid #293341; }
QFrame#topbar { background: #141b24; border-bottom: 1px solid #293341; }
QLabel#brand { color: #f2f7fc; font-size: 24px; font-weight: 700; }
QLabel#brandMark { color: #12231f; background: #70e1b4; border-radius: 10px; font-size: 23px; font-weight: 800; }
QLabel#muted { color: #7f90a6; font-size: 12px; }
QLabel#sectionTitle { color: #edf3fb; font-size: 16px; font-weight: 600; }
QLabel#eyebrow { color: #73869d; font-size: 11px; font-weight: 600; }
QLabel#responseBadge { color: #74deb0; background: #1b322b; border-radius: 5px; padding: 5px 10px; }
QLabel#inlineMessage { color: #f0bc77; padding: 7px 10px; background: #342b20; border-radius: 5px; }
QLineEdit, QComboBox, QSpinBox { background: #1b2430; border: 1px solid #344252; border-radius: 6px; padding: 7px 10px; min-height: 18px; selection-background-color: #245a4d; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus { border: 1px solid #60c99d; }
QLineEdit#requestUrl { font-family: "Cascadia Code", "Consolas", monospace; font-size: 14px; padding: 12px; }
QComboBox#method { color: #70e1b4; font-weight: 700; font-size: 14px; padding: 12px 8px; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #202b38; color: #dce4ee; selection-background-color: #2b554b; outline: none; }
QPushButton { background: #202c39; color: #cbd7e5; border: 1px solid #354454; border-radius: 6px; padding: 7px 12px; min-height: 18px; }
QPushButton:hover { background: #2c3d4d; border-color: #526577; }
QPushButton:pressed { background: #18232e; }
QPushButton:disabled { color: #617184; background: #18202a; border-color: #26313e; }
QPushButton#sendButton { background: #70e1b4; color: #102a20; border: none; font-weight: 700; font-size: 14px; padding: 13px 24px; }
QPushButton#sendButton:hover { background: #95efcb; }
QPushButton#sendButton:disabled { background: #3b6f5e; color: #19382d; }
QPushButton#newRequest { background: #223b34; border-color: #35574a; color: #8eebc7; text-align: left; padding: 10px 14px; }
QPushButton#saveRequest { background: #25463c; color: #9cedce; border-color: #3b6b59; }
QTabWidget::pane { border: none; border-top: 1px solid #2b3745; background: transparent; }
QTabBar::tab { color: #8799ad; background: transparent; padding: 11px 16px; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: #96ebc9; border-bottom: 2px solid #70e1b4; }
QTabBar::tab:hover { color: #dde8f5; }
QTableWidget { background: #141c25; alternate-background-color: #17212c; color: #c9d6e6; border: 1px solid #2b3745; border-radius: 4px; gridline-color: #273342; outline: none; }
QTableWidget::item { padding: 5px 8px; border-bottom: 1px solid #202d3a; }
QTableWidget::item:selected { background: #28493f; color: #e8faf4; }
QTableWidget QLineEdit { padding: 0px; border-radius: 0px; }
QHeaderView::section { background: #1c2733; color: #8da1b7; font-size: 11px; font-weight: 600; border: none; padding: 9px; text-align: left; }
QTableCornerButton::section { background: #1c2733; border: none; }
QListWidget { background: transparent; color: #bbc8d8; border: none; outline: none; }
QListWidget::item { padding: 10px 8px; border-radius: 6px; margin: 2px 0; }
QListWidget::item:selected { background: #283d38; color: #baf6df; }
QListWidget::item:hover { background: #202e3b; }
QPlainTextEdit { background: #131b24; color: #ccdaeb; border: 1px solid #2b3745; border-radius: 5px; padding: 12px; font-family: "Cascadia Code", "Consolas", monospace; font-size: 13px; selection-background-color: #2d594c; }
QSplitter::handle { background: #273340; height: 1px; width: 1px; }
QSplitter::handle:hover { background: #70e1b4; }
QScrollBar:vertical { background: transparent; width: 9px; margin: 0; }
QScrollBar::handle:vertical { background: #3b4c5e; min-height: 30px; border-radius: 4px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 0; }
QScrollBar::handle:horizontal { background: #3b4c5e; min-width: 30px; border-radius: 4px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QCheckBox { spacing: 8px; color: #b4c4d7; }
QStatusBar { color: #798da5; background: #131b24; border-top: 1px solid #293542; font-size: 11px; }
QToolTip { background: #253443; color: #e0eaf6; padding: 6px; border: 1px solid #496074; }
QMenu { background: #202c39; color: #dce4ee; border: 1px solid #3a4b5b; padding: 4px; }
QMenu::item { padding: 7px 22px; }
QMenu::item:selected { background: #305348; }
QDialogButtonBox QPushButton { min-width: 64px; }
)qss");
}
