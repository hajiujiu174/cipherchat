#pragma once
#include "appearance_theme.hpp"
#include "accent_theme.hpp"

namespace ccfw::gui::theme {
// Native adaptation of RhineLabUI/src/style.css. MIT (c) 2026 LBEILC.
// Palette and visual hierarchy are shared; controls remain CipherChat's.
inline QString rhineStylesheet(bool dark, const QColor& accent) {
    QString css = QStringLiteral(R"QSS(
QMainWindow,QDialog,#appRoot { background:@canvas; color:@ink; }
QWidget { color:@ink; }
#topBar { background:@canvas; border-bottom:1px solid @line; }
#brandTitle { color:@ink; font-size:19px; font-weight:800; }
#brandSubtitle { color:@muted; font-size:9px; }
#sideNav { background:@canvas; border-right:1px solid @line; }
#workspaceCaption { color:@muted; font-size:9px; padding:12px 4px; font-weight:500; }
#sideNavButton { background:transparent; color:@muted; border:0; border-bottom:1px solid @line; border-radius:0; padding:18px 6px; font-size:12px; text-align:left; }
#sideNavButton:hover { color:@ink; background:@surface; }
#sideNavButton:checked { color:@ink; background:@surface; border-left:3px solid @accent; font-weight:700; }
#sideNavFooter { color:@muted; font-size:9px; }
#softwareUpdate,#rhineAbout { background:transparent; color:@muted; text-align:left; padding:9px 4px; font-size:10px; }
#designSwitch { background:transparent; border:1px solid @line; border-radius:0; }
QPushButton[designChoice="true"] { color:@muted; background:transparent; border:0; border-radius:0; padding:6px 11px; }
QPushButton[designChoice="true"]:checked { background:@ink; color:@canvas; }
QPushButton[designChoice="true"]:hover { background:@tint; color:@ink; }
#rhineRoute { background:transparent; border-bottom:1px solid @line; }
#rhineRouteNumber { color:@accent; font-family:'Consolas'; font-size:25px; }
#rhineRouteTitle { color:@ink; font-size:23px; font-weight:650; }
#rhineRouteMeta { color:@muted; font-size:10px; }
#rhineEyebrow { color:@muted; font-size:10px; font-family:'Consolas'; }
#rhineHeroTitle { color:@ink; font-size:29px; font-weight:600; }
#rhineDescription { color:@muted; font-size:12px; }
#rhineArchiveCaption { color:@muted; border-top:1px solid @line; padding-top:12px; font-size:9px; font-family:'Consolas'; }
#rhineArchivePane QPushButton { background:transparent; padding:7px 5px; color:@muted; border-radius:0; font-size:10px; }
#rhineArchivePane QPushButton:hover { background:@surface; color:@ink; }
#rhineArchivePane QPushButton:checked { color:@accent; }
#loginCard { background:@surface; border:1px solid @line; border-radius:0; }
#loginTitle { color:@ink; font-size:25px; font-weight:600; }
#loginSubtitle { color:@muted; font-size:10px; }
#fieldCaption { color:@muted; font-size:11px; }
#panelCaption { color:@muted; font-size:10px; font-weight:600; padding-bottom:5px; }
#hintNote,#typingStatus,QLabel#emptyState { color:@muted; }
QScrollArea#advancedSettingsScroll { background:@surface; border:0; }
QLineEdit,QPlainTextEdit,QSpinBox,QComboBox { background:@field; color:@ink; border:1px solid @line; border-radius:0; padding:9px 10px; selection-background-color:@accent; selection-color:@accentInk; }
QLineEdit:focus,QPlainTextEdit:focus,QSpinBox:focus,QComboBox:focus { border:1px solid @accent; }
QLineEdit#chatPeer { color:@ink; border:0; border-bottom:1px solid @line; background:transparent; }
QLineEdit#chatPeer:focus { border-bottom:1px solid @accent; }
QPushButton { background:@surface; color:@ink; border:1px solid @line; border-radius:0; padding:8px 12px; }
QPushButton:hover { background:@tint; border-color:@accent; }
QPushButton:pressed { background:@tint; }
QPushButton:disabled { color:@muted; border-color:@line; background:@field; }
QPushButton[ghost="true"],QPushButton#advancedSettingsToggle { color:@muted; background:transparent; border:0; border-radius:0; }
QPushButton[ghost="true"]:hover,QPushButton#advancedSettingsToggle:hover { color:@ink; background:@tint; }
QPushButton#fileTransferToggle { color:@ink; }
QPushButton#fileTransferToggle:checked { color:@ink; background:@tint; border-color:@accent; }
QPushButton[primary="true"] { border-radius:0; }
QCheckBox { color:@ink; }
QCheckBox::indicator { background:@field; border:1px solid @line; border-radius:0; }
QCheckBox::indicator:checked { background:@accent; border-color:@accent; }
QGroupBox,#contactPanel,#conversationFiles { background:@surface; border:1px solid @line; border-radius:0; }
QGroupBox::title { color:@muted; }
QListWidget { background:transparent; border:0; }
QListWidget::item { background:transparent; border-radius:0; border-bottom:1px solid @line; margin:0 3px; }
QListWidget::item:hover { background:@field; }
QListWidget::item:selected { background:@tint; color:@ink; border-left:3px solid @accent; }
QScrollArea#chatMessages,QScrollArea#groupMessages { background:@surface; border:1px solid @line; border-radius:0; }
#timelineBody { background:@surface; }
#encryptionBadge { background:@tint; border-radius:0; }
#encryptionBadge QLabel { color:@ink; }
#dateSeparator { background:@canvas; color:@muted; border-radius:0; font-weight:400; }
#memberPill { background:@surface; color:@muted; border-radius:0; }
#messageName,#fileSize,#fileTime,#contactPresence,#strangerPresence { color:@muted; }
#contactPresence[online="true"] { color:@accent; }
QSplitter::handle { background:@canvas; }
#notification { border-radius:0; font-weight:400; }
#dialogTitle { color:@ink; }
#ownFingerprint,#peerFingerprint { background:@field; color:@ink; border:1px solid @line; border-radius:0; }
QProgressBar { background:@canvas; color:@ink; border-radius:0; }
QProgressBar::chunk { background:@accent; border-radius:0; }
QTableWidget { background:@surface; color:@ink; border:1px solid @line; border-radius:0; alternate-background-color:@field; }
QHeaderView::section { background:@canvas; color:@muted; border:0; border-bottom:1px solid @line; }
QComboBox::down-arrow { border-top-color:@muted; }
QComboBox QAbstractItemView { background:@surface; color:@ink; border:1px solid @line; selection-background-color:@tint; selection-color:@ink; border-radius:0; }
QScrollBar::handle:vertical,QScrollBar::handle:horizontal { background:@line; border-radius:0; }
QScrollBar::handle:vertical:hover,QScrollBar::handle:horizontal:hover { background:@muted; }
)QSS");
    return css.replace("@accentInk", accentInk(accent).name())
        .replace("@accent", accent.name())
        .replace("@canvas", dark ? "#222420" : "#E8E5E1")
        .replace("@surface", dark ? "#2A2D27" : "#F0EEEA")
        .replace("@field", dark ? "#242721" : "#F7F5F1")
        .replace("@ink", dark ? "#E8E5DC" : "#20231D")
        .replace("@muted", dark ? "#ADB2A4" : "#717367")
        .replace("@line", dark ? "#484D41" : "#C9C8BF")
        .replace("@tint", blend(accent, dark ? QColor("#2A2D27") : QColor("#F0EEEA"), dark ? .76 : .88).name());
}
inline QString rhineBubble(bool own, bool dark, const QColor& accent) {
    const auto surface = dark ? QColor("#30352B") : QColor("#F7F5F1");
    return QStringLiteral("#chatBubble,#fileCard{background:%1;border:1px solid %2;border-radius:0;border-left:3px solid %3;}#messageText{color:%4;}#messageDelivery{color:%5;}")
        .arg(own ? blend(accent, surface, .86).name() : surface.name(), dark ? "#4B5143" : "#D1CEC2", own ? accent.name() : (dark ? "#707762" : "#B3AF9F"), dark ? "#E8E5DC" : "#252820", dark ? "#B2B7A6" : "#727467");
}
}
