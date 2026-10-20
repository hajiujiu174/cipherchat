#include "theme.h"
#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>
namespace Messager {
Theme& Theme::instance() {
    static Theme t;
    return t;
}
Theme::Theme() {
    QSettings s;
    m_mode = Mode(qBound(0, s.value("appearance/theme", 0).toInt(), 2));
    m_fontSize = qBound(13, s.value("appearance/font", 14).toInt(), 17);
    m_skin = s.value("appearance/skin", "default").toString()=="pencil"?PencilSkin:DefaultSkin;
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_mode == System)
            apply();
    });
}
void Theme::setMode(Mode mode) {
    m_mode = mode;
    QSettings().setValue("appearance/theme", int(mode));
    apply();
}
void Theme::setFontSize(int pixels) {
    m_fontSize = qBound(13, pixels, 17);
    QSettings().setValue("appearance/font", m_fontSize);
    apply();
}
void Theme::setSkin(Skin skin) {
    m_skin=skin==PencilSkin?PencilSkin:DefaultSkin;
    QSettings().setValue("appearance/skin",pencil()?"pencil":"default");
    apply();
}
void Theme::apply() {
    m_dark =
        m_mode == Dark || (m_mode == System && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    if (m_dark)
        m_colors = {"#151820", "#1b1e28", "#11141c", "#242835", "#303545", "#edf0f7", "#a5adc2",
                    "#a99aff", "#32304c", "#2b3040", "#7460d7", "#171423", "#7ec7b1", "#f5a2ac"};
    else
        m_colors = {"#f6f7fb", "#ffffff", "#f0f1f8", "#f2f4f9", "#e7eaf2", "#242938", "#788196",
                    "#6954d6", "#eeebff", "#eef1f7", "#6b57d6", "#ffffff", "#368270", "#bc4e62"};
    if(pencil()) {
        if(m_dark)m_colors={"#29252b","#302b32","#30262d","#39323b","#806889","#eadfea","#bfaec4","#d0afd9","#594253","#2d4049","#493344","#29252b","#94c8b1","#f0a8b2"};
        else m_colors={"#fffdf2","#fffef8","#faf1ee","#fff7e9","#c7b4ce","#65576e","#907e94","#896b98","#f0d9e6","#e5f2f9","#f8e6ef","#ffffff","#477b69","#a5425d"};
    }
    const auto& c = m_colors;
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.surface);
    p.setColor(QPalette::AlternateBase, c.subtle);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.surface);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.onAccent);
    p.setColor(QPalette::PlaceholderText, c.muted);
    p.setColor(QPalette::ToolTipBase, c.surface);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::Disabled, QPalette::Text, c.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
    qApp->setPalette(p);
    QFont f;
    f.setFamilies({"Microsoft YaHei", "Segoe UI"});
    if(pencil())f.setFamilies({"KaiTi","Microsoft YaHei","Segoe UI"});
#ifndef Q_OS_WIN
    f.setFamily("Noto Sans CJK SC");
#endif
    f.setPixelSize(m_fontSize);
    qApp->setFont(f);
    // QSS is limited to named surfaces and standard control states. Tokens live above.
    auto style=QString(R"(
      QWidget { color:%9; }
      QLabel { background:transparent; }
      QLabel[bubbleOutgoing="true"] { color:white; }
      QDialog { background:%13; color:%9; }
      QMenu { background:%1; color:%9; border:1px solid %3; padding:6px; }
      QMenu::item { padding:7px 16px; }
      QMenu::item:selected { background:%4; color:%9; }
      QMenu::item:disabled { color:%5; }
      QCheckBox, QRadioButton { color:%9; }
      QPlainTextEdit { background:%1; color:%9; border:1px solid %3; border-radius:9px; selection-background-color:%6; selection-color:%11; }
      QFrame[surface="white"], QWidget[surface="white"] { background:%1; }
      QFrame[surface="rail"] { background:%2; border-right:1px solid %3; }
      QFrame[card="true"] { background:%1; border:1px solid %3; border-radius:14px; }
      QFrame[role="banner"] { background:%4; border-radius:9px; }
      QFrame[role="composer"] { background:%1; border:1px solid %3; border-radius:14px; }
      QFrame[role="header"] { background:%1; border-bottom:1px solid %3; }
      QFrame[role="detail"] { background:%1; border-left:1px solid %3; }
      QLabel[role="muted"] { color:%5; }
      QLabel[role="accent"] { color:%6; }
      QLabel[role="success"] { color:%7; }
      QLabel[role="danger"] { color:%8; }
      QLabel[role="eyebrow"] { color:%5; font-size:11px; font-weight:600; }
      QPushButton { padding:8px 14px; border:1px solid %3; border-radius:8px; background:%1; color:%9; }
      QPushButton:hover { background:%10; }
      QPushButton:focus { border:1px solid %6; }
      QPushButton:disabled { color:%5; background:%10; }
      QPushButton[variant="primary"] { background:%6; color:%11; border:1px solid %6; font-weight:600; }
      QPushButton[variant="primary"]:hover { background:%12; }
      QPushButton[variant="ghost"], QPushButton[variant="chip"] { background:transparent; border:1px solid transparent; }
      QPushButton[variant="ghost"]:hover, QPushButton[variant="chip"]:hover { background:%10; }
      QPushButton[variant="chip"]:checked { color:%6; background:%4; }
      QLineEdit { padding:10px 12px; background:%10; color:%9; border:1px solid transparent; border-radius:9px; selection-background-color:%6; }
      QLineEdit:focus { border:1px solid %6; }
      QPushButton#groupMembers { text-align:left; padding:10px 30px 10px 12px; background:%10; }
      QMenu#groupMembersPopup { background:%1; color:%9; border:1px solid %3; border-radius:9px; padding:8px; }
      QMenu#groupMembersPopup QCheckBox { padding:8px; spacing:10px; border-radius:6px; }
      QMenu#groupMembersPopup QCheckBox:hover { background:%10; }
      QMenu#groupMembersPopup QCheckBox::indicator { width:18px; height:18px; }
      QPlainTextEdit#messageInput { background:transparent; border:0; padding:3px; }
      QListView#conversationList { background:%1; border:0; outline:0; }
      QScrollArea { border:0; background:transparent; }
      QScrollBar:vertical { width:7px; background:transparent; margin:2px; }
      QScrollBar::handle:vertical { background:%3; border-radius:3px; min-height:30px; }
      QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
      QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
      QSplitter::handle { background:%3; width:1px; }
      QComboBox { background:%10; border:1px solid %3; border-radius:8px; padding:8px 12px; min-width:120px; }
      QComboBox QAbstractItemView { background:%1; color:%9; selection-background-color:%4; selection-color:%6; padding:6px; }
      QProgressBar { border:0; border-radius:3px; background:%10; height:6px; text-align:center; }
      QProgressBar::chunk { background:%6; border-radius:3px; }
      QProgressBar[status="completed"]::chunk { background:%7; }
      QProgressBar[status="failed"]::chunk { background:%8; }
      QToolTip { padding:6px; border:1px solid %3; border-radius:4px; }
    )")
                            .arg(c.surface.name(), c.rail.name(), c.border.name(), c.accentSoft.name(),
                                 c.muted.name(), c.accent.name(), c.success.name(), c.danger.name(),
                                 c.text.name(), c.subtle.name(), c.onAccent.name(),
                                 pencil()?c.accentSoft.name():(m_dark ? "#bcafff" : "#5945c2"),c.window.name());
    if(pencil())style+=QString(R"(
      QPushButton[variant="primary"], QPushButton[variant="primary"]:hover { background:%1; color:%2; border-color:%3; }
      QLineEdit { border:1px solid %3; background:%4; }
      QComboBox { color:%2; }
      QPlainTextEdit#messageInput { background:transparent; border:0; }
      QLabel[bubbleOutgoing="true"] { color:%2; }
    )").arg(c.accentSoft.name(),c.text.name(),c.border.name(),c.surface.name());
    qApp->setStyleSheet(style);
    emit changed();
}
} // namespace Messager
