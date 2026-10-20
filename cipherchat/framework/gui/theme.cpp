#include "theme.hpp"
#include "accent_theme.hpp"
#include <QDate>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
namespace ccfw::gui::theme {
namespace {
// 设计令牌。修改颜色只需要改这里；样式表通过 literal 拼接引用。
constexpr const char* Ink = "#22384A";
constexpr const char* InkSoft = "#5B7186";
constexpr const char* Muted = "#8CA0B3";
constexpr const char* Line = "#E1E8EE";
constexpr const char* Field = "#D3DCE4";
constexpr const char* Canvas = "#F3F6F9";
constexpr const char* Primary = "#1F7A6D";
constexpr const char* PrimaryHover = "#26897B";
constexpr const char* PrimaryPress = "#1A655A";
constexpr const char* PrimaryTint = "#E3F2EF";
constexpr const char* SidebarBg = "#17352F";
constexpr const char* SidebarItem = "#2C5D53";
constexpr const char* SidebarItemHover = "#22463F";
constexpr const char* SidebarText = "#9DB8B2";
constexpr const char* SidebarIcon = "#A9C6C0";
constexpr const char* BubbleOwn = "#D7EFE8";
constexpr const char* BubbleOwnInk = "#17453F";
constexpr const char* CodeBg = "#142E29";
constexpr const char* CodeInk = "#9FE8D9";

QPixmap canvas(int size) {
    QPixmap pm(size * 2, size * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    return pm;
}
// 肩身 ellipse 底部超出画布被裁剪，形成半身人形。
void person(QPainterPath& path, qreal u, qreal cx, qreal top, qreal scale) {
    path.addEllipse(QRectF(cx - 4.4 * u * scale, top, 8.8 * u * scale, 8.8 * u * scale));
    path.addEllipse(QRectF(cx - 7.2 * u * scale, top + 9.6 * u * scale, 14.4 * u * scale, 14.4 * u * scale));
}
void bubbleShape(QPainterPath& path, qreal u, qreal x, qreal y, qreal w, qreal h) {
    path.addRoundedRect(QRectF(x, y, w, h), 4.6 * u, 4.6 * u);
    QPolygonF tail { QPointF(x + 3.2 * u, y + h - 0.4 * u), QPointF(x + 2.6 * u, y + h + 3.6 * u), QPointF(x + 8.0 * u, y + h - 0.2 * u) };
    path.addPolygon(tail);
}
}

QPixmap glyph(Glyph kind, const QColor& color, int size) {
    auto pm = canvas(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal u = size / 24.0;
    QPainterPath path;
    switch (kind) {
        case Glyph::Account: {
            person(path, u, 12, 3.2, 1.0);
            p.fillPath(path, color);
            break;
        }
        case Glyph::Chat: {
            bubbleShape(path, u, 2.6, 3.4, 18.8, 13.2);
            p.fillPath(path, color);
            break;
        }
        case Glyph::Group: {
            QColor back = color; back.setAlpha(110);
            QPainterPath rear;
            bubbleShape(rear, u, 8.4, 2.6, 13.0, 10.6);
            p.fillPath(rear, back);
            QPainterPath front;
            bubbleShape(front, u, 2.6, 8.2, 14.6, 10.6);
            p.fillPath(front, color);
            break;
        }
        case Glyph::Lock: {
            QPen pen(color, 2.1 * u);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(8.2 * u, 3.0 * u, 7.6 * u, 9.4 * u), 0, 180 * 16);
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            path.addRoundedRect(QRectF(5.4 * u, 10.6 * u, 13.2 * u, 9.8 * u), 2.4 * u, 2.4 * u);
            p.drawPath(path);
            break;
        }
        case Glyph::Attach: {
            QPen pen(color, 1.9 * u);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(4.0 * u, 4.0 * u, 16.0 * u, 16.0 * u), 4.6 * u, 4.6 * u);
            p.drawLine(QPointF(12 * u, 8.6 * u), QPointF(12 * u, 15.4 * u));
            p.drawLine(QPointF(8.6 * u, 12 * u), QPointF(15.4 * u, 12 * u));
            break;
        }
        case Glyph::Send: {
            QPolygonF plane { QPointF(21.2 * u, 3.2 * u), QPointF(3.0 * u, 10.8 * u),
                QPointF(10.6 * u, 13.8 * u), QPointF(13.6 * u, 21.0 * u) };
            path.addPolygon(plane);
            path.closeSubpath();
            p.fillPath(path, color);
            break;
        }
        case Glyph::Document: {
            path.addRoundedRect(QRectF(5.0 * u, 3.0 * u, 14.0 * u, 18.0 * u), 2.6 * u, 2.6 * u);
            p.fillPath(path, color);
            // 文本行用镂空表现，任何底色都成立。
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            QPen pen(QColor(0, 0, 0), 1.7 * u);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            for (int i = 0; i < 3; ++i) {
                const qreal y = (9.0 + i * 3.4) * u;
                p.drawLine(QPointF(8.2 * u, y), QPointF(i == 2 ? 13.6 * u : 15.8 * u, y));
            }
            break;
        }
    }
    return pm;
}

QIcon icon(Glyph kind, const QColor& color) {
    QIcon result;
    result.addPixmap(glyph(kind, color, 16));
    result.addPixmap(glyph(kind, color, 20));
    result.addPixmap(glyph(kind, color, 24));
    return result;
}

QPixmap avatar(const QString& name, int size) {
    static const char* palette[] = {"#2E7D6E", "#3E6FA6", "#8A63A8", "#B0654F", "#5B8C3E", "#A8743D", "#4F7D8C", "#9D5A78"};
    auto pm = canvas(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QString key = name.isEmpty() ? QStringLiteral("?") : name;
    p.setBrush(QColor(palette[qHash(key) % 8]));
    p.drawEllipse(0, 0, size, size);
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(static_cast<int>(size * 0.42));
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, size, size - 1), Qt::AlignCenter, key.left(1).toUpper());
    return pm;
}

QPixmap brand(int size) {
    auto pm = canvas(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(Primary));
    p.drawEllipse(0, 0, size, size);
    const auto mark = glyph(Glyph::Lock, Qt::white, static_cast<int>(size * 0.56));
    p.drawPixmap(QPointF((size - mark.width() / mark.devicePixelRatio()) / 2, (size - mark.height() / mark.devicePixelRatio()) / 2), mark);
    return pm;
}

QPixmap fileChip(int size, const QColor& accent) {
    auto pm = canvas(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(accent.isValid() ? accent : QColor(Primary));
    p.drawRoundedRect(0, 0, size, size, size * 0.26, size * 0.26);
    const auto doc = glyph(Glyph::Document, accent.isValid() ? accentInk(accent) : QColor(Qt::white), static_cast<int>(size * 0.64));
    p.drawPixmap(QPointF((size - doc.width() / doc.devicePixelRatio()) / 2, (size - doc.height() / doc.devicePixelRatio()) / 2), doc);
    return pm;
}

QString stylesheet() {
    // 色值与上方设计令牌一一对应；此处集中书写，便于整体审阅视觉规则。
    return QStringLiteral(
        "QWidget{font-family:'Microsoft YaHei UI';font-size:10pt;color:#22384A;}"
        "QMainWindow{background:#F3F6F9;} QDialog{background:#F3F6F9;}"
        "QToolTip{background:#22384A;color:#F4F8FB;border:0;padding:6px 8px;}"
        // 顶栏
        "#brandTitle{font-size:17px;font-weight:700;color:#14352F;}"
        "#brandSubtitle{font-size:11px;color:#6C8291;}"
        "#accountStatus{background:#E7EDF3;color:#4A6172;border-radius:11px;padding:5px 12px;font-weight:600;}"
        "#accountStatus[phase=\"busy\"]{background:#FCF1DC;color:#8A6215;}"
        "#accountStatus[phase=\"online\"]{background:#DFF2EC;color:#1F6156;}"
        // 侧边导航
        "#sideNav{background:#17352F;}"
        "#sideNavButton{background:transparent;color:#9DB8B2;border:0;border-radius:10px;padding:13px 14px;"
        "text-align:left;font-weight:600;}"
        "#sideNavButton:hover{background:#22463F;}"
        "#sideNavButton:checked{background:#2C5D53;color:#FFFFFF;}"
        "#sideNavFooter{color:#7FA39C;font-size:10px;font-weight:600;}"
        "QTabWidget#mainTabs::pane{border:0;}"
        // 输入控件
        "QLineEdit,QPlainTextEdit,QComboBox,QSpinBox{background:#FFFFFF;border:1px solid #D3DCE4;border-radius:8px;"
        "padding:7px 10px;selection-background-color:#BFE3DC;selection-color:#14352F;}"
        "QLineEdit:focus,QPlainTextEdit:focus,QComboBox:focus,QSpinBox:focus{border:1px solid #26897B;}"
        "QLineEdit:disabled,QPlainTextEdit:disabled,QComboBox:disabled,QSpinBox:disabled{background:#F1F4F7;color:#9AA9B5;}"
        "QLineEdit#chatPeer{border:0;background:transparent;padding:1px 0;font-size:14px;font-weight:700;}"
        "QLineEdit#chatPeer:focus{border-bottom:1px solid #26897B;border-radius:0;}"
        // 按钮
        "QPushButton{background:#E7ECF2;color:#2A4356;border:0;border-radius:8px;padding:8px 16px;font-weight:600;}"
        "QPushButton:hover{background:#DCE4EC;} QPushButton:pressed{background:#D0DBE5;}"
        "QPushButton:disabled{background:#EDF0F3;color:#A4ACB4;font-weight:400;}"
        "QPushButton[primary=\"true\"]{background:#1F7A6D;color:#FFFFFF;}"
        "QPushButton[primary=\"true\"]:hover{background:#26897B;} QPushButton[primary=\"true\"]:pressed{background:#1A655A;}"
        "QPushButton[primary=\"true\"]:disabled{background:#A9CFC8;color:#F2F8F6;}"
        "QPushButton[ghost=\"true\"]{background:transparent;color:#5B7186;}"
        "QPushButton[ghost=\"true\"]:hover{background:#E9EEF3;} QPushButton[ghost=\"true\"]:pressed{background:#DDE5EC;}"
        "QPushButton#fileTransferToggle{background:transparent;color:#40607A;padding:7px 10px;}"
        "QPushButton#fileTransferToggle:hover{background:#E9EEF3;}"
        "QPushButton#fileTransferToggle:checked{background:#E3F2EF;color:#1F6156;}"
        "QPushButton#advancedSettingsToggle{background:transparent;color:#1F6156;padding:4px 2px;text-align:left;}"
        "QPushButton#advancedSettingsToggle:hover{background:transparent;color:#26897B;}"
        "QPushButton#advancedSettingsToggle:checked{font-weight:700;}"
        // 复选框
        "QCheckBox{spacing:8px;}"
        "QCheckBox::indicator{width:17px;height:17px;border:1px solid #B9C6D0;border-radius:5px;background:#FFFFFF;}"
        "QCheckBox::indicator:hover{border-color:#26897B;}"
        "QCheckBox::indicator:checked{background:#1F7A6D;border-color:#1F7A6D;}"
        "QCheckBox::indicator:disabled{background:#EDF0F3;border-color:#D3DCE4;}"
        // 列表与滚动：滚动区域必须不透明，避免圆角裁剪出未合成的黑色区域。
        "QListWidget{background:#FFFFFF;border:0;}"
        "QListWidget::item{border-radius:10px;margin:2px 4px;}"
        "QListWidget::item:hover{background:#EEF3F6;}"
        "QListWidget::item:selected{background:#E3F2EF;}"
        "QScrollArea{border:0;background:#FFFFFF;}"
        "QScrollBar:vertical{background:transparent;width:10px;margin:2px;}"
        "QScrollBar::handle:vertical{background:#C7D3DC;border-radius:4px;min-height:32px;}"
        "QScrollBar::handle:vertical:hover{background:#AEBDC9;}"
        "QScrollBar:horizontal{background:transparent;height:10px;margin:2px;}"
        "QScrollBar::handle:horizontal{background:#C7D3DC;border-radius:4px;min-width:32px;}"
        "QScrollBar::handle:horizontal:hover{background:#AEBDC9;}"
        "QScrollBar::add-line,QScrollBar::sub-line{width:0;height:0;}"
        "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}"
        // 时间线与气泡
        "QScrollArea#chatMessages,QScrollArea#groupMessages{background:#FFFFFF;border:1px solid #E1E8EE;border-radius:12px;}"
        "QLabel#dateSeparator{background:#E7EDF3;color:#5B7186;border-radius:9px;padding:3px 12px;font-size:10px;font-weight:600;}"
        "QLabel#emptyState{color:#8CA0B3;font-size:11pt;background:transparent;}"
        "QLabel#messageName{font-size:10px;font-weight:600;}"
        "QLabel#messageName{color:#5B7186;} #contactPresence{font-size:10px;background:transparent;color:#8CA0B3;} #contactPresence[online=\"true\"]{color:#2E8B5F;}"
        "QLabel#messageDelivery{font-size:10px;}"
        "QLabel#fileName{font-weight:600;}"
        "QLabel#fileSize{font-size:10px;color:#5B7186;}"
        "QLabel#fileTime{font-size:10px;}"
        // 卡片与面板
        "QGroupBox{background:#FFFFFF;border:1px solid #E1E8EE;border-radius:12px;margin-top:12px;padding:14px 14px 12px 14px;font-weight:600;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:0;padding:0 4px;color:#40607A;}"
        "#loginCard{background:#FFFFFF;border:1px solid #E1E8EE;border-radius:14px;}"
        "#conversationFiles{background:#FFFFFF;border:1px solid #E1E8EE;border-radius:12px;}"
        "#contactPanel{background:#FFFFFF;border:1px solid #E1E8EE;border-radius:12px;}"
        "#panelCaption{color:#8CA0B3;font-size:11px;font-weight:700;}"
        "#fieldCaption{color:#5B7186;font-size:10px;font-weight:600;}"
        "#typingStatus{color:#61798A;font-size:11px;}"
        "#encryptionBadge{background:#E3F2EF;border-radius:9px;}"
        "#encryptionBadge QLabel{font-size:10.5px;font-weight:600;color:#1F6156;}"
        "#memberPill{background:#EEF3F6;border-radius:9px;padding:6px 12px;color:#4A6172;}"
        "#loginTitle{font-size:20px;font-weight:700;color:#14352F;}"
        "#loginSubtitle{font-size:11px;color:#6C8291;}"
        "#dialogTitle{font-size:15px;font-weight:700;color:#14352F;}"
        "#hintNote{color:#8CA0B3;font-size:10px;}"
        "#notification{border-radius:10px;padding:9px 14px;font-weight:600;}"
        // 指纹安全块
        "QLabel#ownFingerprint,QLabel#peerFingerprint{font-family:'Consolas';font-size:13px;background:#142E29;color:#9FE8D9;"
        "border-radius:8px;padding:12px;}"
        // 进度与表格
        "QProgressBar{background:#E4EBEF;border:0;border-radius:4px;text-align:center;color:#33544E;"
        "font-size:9pt;min-height:15px;max-height:15px;}"
        "QProgressBar::chunk{background:#2E8577;border-radius:4px;}"
        "QTableWidget{background:#FFFFFF;border:1px solid #E1E8EE;border-radius:10px;gridline-color:transparent;"
        "alternate-background-color:#F7FAFC;}"
        "QTableWidget::item{padding:6px;border:0;}"
        "QHeaderView::section{background:#F2F6F9;color:#5B7186;border:0;padding:8px;font-weight:600;}"
        "QComboBox::drop-down{border:0;width:24px;}"
        "QComboBox::down-arrow{width:0;height:0;border-left:4px solid transparent;border-right:4px solid transparent;"
        "border-top:5px solid #5B7186;margin-right:8px;}"
        "QComboBox QAbstractItemView{background:#FFFFFF;border:1px solid #D3DCE4;border-radius:8px;outline:0;"
        "selection-background-color:#E3F2EF;selection-color:#1D5754;}");
}
}
