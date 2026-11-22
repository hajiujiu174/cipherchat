#pragma once
#include <QColor>
#include <QPalette>
#include <QString>

namespace ccfw::gui::theme {
inline QColor blend(const QColor& a, const QColor& b, double weight) {
    return QColor::fromRgbF(a.redF() * (1-weight) + b.redF() * weight,
        a.greenF() * (1-weight) + b.greenF() * weight, a.blueF() * (1-weight) + b.blueF() * weight);
}
inline QPalette lightPalette(QPalette palette) {
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        palette.setColor(group, QPalette::Window, QColor("#F5F5F8"));
        palette.setColor(group, QPalette::Base, Qt::white);
        palette.setColor(group, QPalette::AlternateBase, QColor("#F2F1F8"));
        palette.setColor(group, QPalette::Button, QColor("#ECECF2"));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            palette.setColor(group, role, QColor(group == QPalette::Disabled ? "#9793A3" : "#242636"));
        palette.setColor(group, QPalette::PlaceholderText, QColor("#817B91"));
        palette.setColor(group, QPalette::Highlight, QColor("#DDD6FE"));
        palette.setColor(group, QPalette::HighlightedText, QColor("#322961"));
    }
    return palette;
}
inline QPalette darkPalette(QPalette palette) {
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        palette.setColor(group, QPalette::Window, QColor("#141820"));
        palette.setColor(group, QPalette::Base, QColor("#191E28"));
        palette.setColor(group, QPalette::AlternateBase, QColor("#222936"));
        palette.setColor(group, QPalette::Button, QColor("#2A3240"));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            palette.setColor(group, role, QColor(group == QPalette::Disabled ? "#778296" : "#E4EAF3"));
        palette.setColor(group, QPalette::PlaceholderText, QColor("#8D99AD"));
        palette.setColor(group, QPalette::Highlight, QColor("#414E65"));
        palette.setColor(group, QPalette::HighlightedText, Qt::white);
        palette.setColor(group, QPalette::ToolTipBase, QColor("#30394A"));
        palette.setColor(group, QPalette::ToolTipText, Qt::white);
    }
    return palette;
}
inline QString darkStylesheet(const QColor& accent) {
    const auto highlight = blend(accent, Qt::white, .60).name();
    const auto selected = blend(QColor("#222936"), accent, .20).name();
    return QStringLiteral(R"QSS(
QWidget { color:#E4EAF3; }
QMenu { background:#222936; color:#E4EAF3; border-color:#3A4558; }
QMenu::item { color:#E4EAF3; }
QMenu::item:selected { background:%2; color:#FFFFFF; }
QMenu::item:disabled { color:#9DAAC0; }
QMenu::separator { background:#3A4558; }
QMainWindow,QDialog,#appRoot { background:#141820; }
QTabWidget::pane,QTabWidget>QStackedWidget,#conversationPanel,#fileQueuePane { background:#191E28; border:0; }
QTabBar::tab { background:#2A3240; color:#DCE4F2; padding:6px 10px; }
QTabBar::tab:selected { background:#414E65; color:#FFFFFF; }
#topBar,#sideNav { background:#191E28; border-color:#303949; }
#brandTitle,#loginTitle,#dialogTitle { color:#F0F3FA; }
#brandSubtitle,#loginSubtitle,#sideNavFooter,#workspaceCaption,#panelCaption,#fieldCaption,#hintNote,#typingStatus { color:#9DAAC0; }
#sideNavButton { background:transparent; color:#B5C0D2; }
#sideNavButton:hover { background:#252D3B; }
#sideNavButton:checked { background:%2; color:%1; border-color:%1; }
#designSwitch { background:#141820; border-color:#303949; }
QPushButton[designChoice="true"] { color:#B5C0D2; background:transparent; }
QPushButton[designChoice="true"]:checked,QPushButton[designChoice="true"]:hover { color:%1; background:%2; }
QLineEdit,QPlainTextEdit,QComboBox,QSpinBox { background:#191E28; color:#E4EAF3; border-color:#3A4558; selection-background-color:%2; selection-color:#FFFFFF; }
QLineEdit:focus,QPlainTextEdit:focus,QComboBox:focus,QSpinBox:focus { border-color:%1; }
QLineEdit:disabled,QPlainTextEdit:disabled,QComboBox:disabled,QSpinBox:disabled { background:#202631; color:#778296; }
QLineEdit#chatPeer { color:#E4EAF3; background:transparent; }
QLineEdit#chatPeer:focus { border-color:%1; }
QComboBox QAbstractItemView { background:#222936; color:#E4EAF3; border-color:#3A4558; selection-background-color:%2; selection-color:#FFFFFF; }
QComboBox::down-arrow { border-top-color:#B5C0D2; }
QPushButton { background:#2A3240; color:#DCE4F2; }
QPushButton:hover { background:#364154; }
QPushButton:pressed { background:#414E65; }
QPushButton:disabled,QPushButton[primary="true"]:disabled { background:#29313D; color:#778296; border-color:#3A4558; }
QPushButton[ghost="true"],QPushButton#fileTransferToggle { background:transparent; color:#B5C0D2; }
QPushButton[ghost="true"]:hover,QPushButton#fileTransferToggle:hover { background:#252D3B; }
QPushButton#fileTransferToggle:checked { background:%2; color:%1; }
QPushButton#advancedSettingsToggle,QPushButton#advancedSettingsToggle:hover { color:%1; }
QCheckBox::indicator { background:#191E28; border-color:#647189; }
QCheckBox::indicator:disabled { background:#29313D; border-color:#3A4558; }
QGroupBox,#loginCard,#contactPanel,#conversationFiles { background:#191E28; border-color:#303949; }
QGroupBox::title { color:#B5C0D2; }
QListWidget,QScrollArea { background:#191E28; }
QListWidget::item:hover { background:#252D3B; }
QListWidget::item:selected { background:%2; }
QScrollArea#chatMessages,QScrollArea#groupMessages,#timelineBody { background:#141820; border-color:#303949; }
QSplitter::handle { background:#141820; }
QLabel#dateSeparator,#memberPill { background:#252D3B; color:#B5C0D2; }
QLabel#emptyState { color:#9DAAC0; }
QLabel#messageName,QLabel#fileSize,QLabel#fileTime { color:#B5C0D2; }
#contactPresence { color:#9DAAC0; font-size:10px; background:transparent; }
#contactPresence[online="true"] { color:#7CD5A3; }
#encryptionBadge { background:%2; }
#encryptionBadge QLabel { color:%1; }
QLabel#ownFingerprint,QLabel#peerFingerprint { background:#0E1219; color:%1; }
#accountStatus { background:#2A3240; color:#B5C0D2; }
#accountStatus[phase="online"] { background:#203C35; color:#8CE1B6; }
#accountStatus[phase="busy"] { background:#413724; color:#F0CB83; }
QProgressBar { background:#303949; color:#FFFFFF; }
QProgressBar::chunk { background:#4B596F; }
QTableWidget { background:#191E28; color:#E4EAF3; border-color:#303949; alternate-background-color:#222936; }
QHeaderView::section,QTableCornerButton::section { background:#252D3B; color:#B5C0D2; border:0; }
QScrollBar::handle:vertical,QScrollBar::handle:horizontal { background:#465268; }
QScrollBar::handle:vertical:hover,QScrollBar::handle:horizontal:hover { background:#60718B; }
QToolTip { background:#30394A; color:#F0F3FA; border:1px solid #52617A; }
)QSS").arg(highlight, selected);
}
inline QString darkBubble(bool own, const QColor& accent) {
    const auto background = own ? blend(QColor("#222936"), accent, .20).name() : QStringLiteral("#222936");
    return QStringLiteral("#chatBubble,#fileCard{background:%1;border:1px solid #3A4558;border-radius:12px;}"
        "QLabel#messageText,QLabel#fileName{color:#E4EAF3;} QLabel#messageDelivery,QLabel#fileTime,QLabel#messageName,QLabel#fileSize{color:#B5C0D2;}").arg(background);
}
}
