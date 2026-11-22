#pragma once
#include <QString>
#include <QPainter>
#include "theme.hpp"

namespace ccfw::gui::theme {
inline QPixmap codexMark(int size) {
    QPixmap result(size * 2, size * 2); result.setDevicePixelRatio(2); result.fill(Qt::transparent);
    QPainter painter(&result); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(QColor("#6151C6"));
    painter.drawRoundedRect(QRectF(0, 0, size, size), size * .29, size * .29);
    const int inner = size * .6;
    painter.drawPixmap((size - inner) / 2, (size - inner) / 2, glyph(Glyph::Lock, Qt::white, inner));
    return result;
}
// Codex「纸白工作台」：叠加于通用控件基线，GLM 样式独立保留。
inline QString codexStylesheet() {
    return QStringLiteral(R"QSS(
QMainWindow, QDialog, #appRoot { background:#F5F5F8; }
QTabWidget::pane,QTabWidget>QStackedWidget { background:#F5F5F8; border:0; }
#conversationPanel,#fileQueuePane { background:#FFFFFF; }
QTabBar::tab { background:#ECECF2; color:#515366; padding:6px 10px; }
QTabBar::tab:selected { background:#FFFFFF; color:#5146BD; }
QWidget { color:#242636; }
QMenu { background:#FFFFFF; color:#242636; border:1px solid #DFDFE9; padding:4px; }
QMenu::item { background:transparent; color:#242636; padding:6px 20px; }
QMenu::item:selected { background:#EEEAFE; color:#242636; }
QMenu::item:disabled { color:#6A6680; }
QMenu::separator { background:#E4E1EE; height:1px; margin:4px 8px; }
#topBar { background:#FFFFFF; border-bottom:1px solid #E4E5ED; }
#brandTitle { color:#27283E; font-size:18px; }
#brandSubtitle { color:#787B90; }
#sideNav { background:#FFFFFF; border-right:1px solid #E4E5ED; }
#sideNavButton { color:#646779; border-radius:7px; padding:14px 12px; font-weight:500; }
#sideNavButton:hover { background:#F4F3FC; }
#sideNavButton:checked { color:#5146BD; background:#EEEAFE; border-left:3px solid #6A59D1; font-weight:700; }
#sideNavFooter { color:#79768D; }
#workspaceCaption { color:#9793AB; font-size:10px; font-weight:700; padding:8px 12px; }
#designSwitch { background:#F5F4FA; border:1px solid #E5E2F0; border-radius:9px; }
QPushButton[designChoice="true"] { background:transparent; color:#77738A; border-radius:6px; padding:6px 13px; }
QPushButton[designChoice="true"]:checked { background:#FFFFFF; color:#5146BD; }
QPushButton[designChoice="true"]:hover { color:#5146BD; background:#EEEBFA; }
QLineEdit,QPlainTextEdit,QComboBox,QSpinBox { background:#FFFFFF; color:#242636; border:1px solid #DFDFE9; border-radius:6px; padding:8px 10px; selection-background-color:#DDD6FE; selection-color:#322961; }
QLineEdit:focus,QPlainTextEdit:focus,QComboBox:focus,QSpinBox:focus { border:1px solid #8070DD; }
QLineEdit#chatPeer:focus { border-bottom:1px solid #8070DD; }
QPushButton { background:#ECECF2; color:#515366; border-radius:6px; }
QPushButton:hover { background:#E2DFEF; }
QPushButton:pressed { background:#D9D4EC; }
QPushButton[primary="true"] { background:#6151C6; color:#FFFFFF; }
QPushButton[primary="true"]:hover { background:#7262D6; }
QPushButton[primary="true"]:pressed { background:#4D3DB0; }
QPushButton[primary="true"]:disabled { background:#C9C2E9; color:#F8F6FF; }
QPushButton[ghost="true"] { color:#6A6680; }
QPushButton[ghost="true"]:hover { background:#F1EEF9; }
QPushButton#advancedSettingsToggle { color:#6151C6; }
QPushButton#advancedSettingsToggle:hover { color:#8070DD; }
QPushButton#fileTransferToggle { color:#6A6680; }
QPushButton#fileTransferToggle:checked { color:#5146BD; background:#EEEAFE; }
QCheckBox::indicator:checked { background:#6151C6; border-color:#6151C6; }
QCheckBox::indicator:hover { border-color:#8070DD; }
#loginCard { border:1px solid #E3DFEF; border-radius:18px; background:#FFFFFF; }
#loginTitle { color:#302B49; font-size:24px; }
#loginSubtitle { color:#817B91; }
#dialogTitle { color:#302B49; }
#contactPanel,#conversationFiles,QGroupBox { background:#FFFFFF; border:1px solid #E4E1EE; border-radius:10px; }
QGroupBox::title { color:#6A6680; }
#panelCaption { color:#817B91; }
#fieldCaption { color:#6A6680; }
QListWidget::item { border-radius:7px; margin:3px 6px; }
QListWidget::item:hover { background:#F6F3FB; }
QListWidget::item:selected { background:#EEEAFE; }
QScrollArea#chatMessages,QScrollArea#groupMessages { background:#FAF9FD; border:1px solid #E4E1EE; border-radius:10px; }
#timelineBody { background:#FAF9FD; }
QSplitter::handle { background:#F5F5F8; }
#dateSeparator { color:#898295; background:#F0EDF5; font-weight:400; }
#encryptionBadge { background:#EFEAFB; }
#encryptionBadge QLabel { color:#6B5AA1; }
#ownFingerprint,#peerFingerprint { background:#302B49; color:#D5C7FF; }
QProgressBar { background:#EAE5F5; color:#514472; }
QProgressBar::chunk { background:#8070DD; }
QComboBox QAbstractItemView { background:#FFFFFF; color:#242636; selection-background-color:#EEEAFE; selection-color:#242636; }
QScrollBar::handle:vertical,QScrollBar::handle:horizontal { background:#D4CFDF; }
QScrollBar::handle:vertical:hover,QScrollBar::handle:horizontal:hover { background:#B7AFCB; }
)QSS");
}
inline QString codexBubble(bool own) {
    return own ? QStringLiteral(
        "#chatBubble,#fileCard{background:#EDE8FC;border:1px solid #E1D9F7;border-radius:12px;border-bottom-right-radius:3px;}"
        "#messageText{color:#352B58;} #messageDelivery{color:#85779F;}") : QStringLiteral(
        "#chatBubble,#fileCard{background:#FFFFFF;border:1px solid #E8E3F0;border-radius:12px;border-bottom-left-radius:3px;}"
        "#messageText{color:#302B49;} #messageDelivery{color:#898295;}");
}
}
