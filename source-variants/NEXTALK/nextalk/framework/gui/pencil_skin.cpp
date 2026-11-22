#include "pencil_skin.hpp"
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QVariant>
#include <algorithm>
#include <cmath>

namespace ccfw::gui::pencil {
namespace {
QVariant inherited(const QWidget* widget,const char* key) {
    for(auto* current=widget;current;current=current->parentWidget()) if(current->property(key).isValid())return current->property(key);
    return {};
}
const QBrush& grain(const QColor& base) {
    static QHash<QRgb,QBrush> cache;
    auto found=cache.constFind(base.rgb());if(found!=cache.cend())return found.value();
    QImage tile(128,128,QImage::Format_RGB32);
    for(int y=0;y<128;++y)for(int x=0;x<128;++x) {
        unsigned value=static_cast<unsigned>(x+y*128+731);value^=value>>16;value*=0x7feb352dU;value^=value>>15;
        const int delta=static_cast<int>(value%5)-2;
        tile.setPixelColor(x,y,QColor(std::clamp(base.red()+delta,0,255),std::clamp(base.green()+delta,0,255),std::clamp(base.blue()+delta,0,255)));
    }
    cache.insert(base.rgb(),QBrush(tile));return cache.constFind(base.rgb()).value();
}
void star(QPainter& painter,QPointF center,double radius,const QColor& color) {
    QPainterPath shape;
    for(int i=0;i<10;++i) {const double angle=-1.57079632679+i*3.14159265359/5;const double r=i%2?radius*.43:radius;const QPointF p(center.x()+std::cos(angle)*r,center.y()+std::sin(angle)*r);if(!i)shape.moveTo(p);else shape.lineTo(p);}
    shape.closeSubpath();painter.setPen(QPen(color,.85));painter.setBrush(Qt::NoBrush);painter.drawPath(shape);
}
void doodles(QPainter& painter,const QRect& bounds,bool night) {
    painter.setRenderHint(QPainter::Antialiasing);star(painter,{bounds.right()-17.0,16},5,night?QColor("#b79d69"):QColor("#dec179"));
    if(bounds.height()<100||bounds.width()<100)return;
    painter.setPen(QPen(night?QColor("#6d9b9e"):QColor("#b8d7d4"),.9));painter.setBrush(Qt::NoBrush);
    const QPointF p(14,bounds.bottom()-15);QPainterPath cloud;cloud.moveTo(p+QPointF(0,3));cloud.cubicTo(p+QPointF(-6,3),p+QPointF(-6,-4),p+QPointF(0,-5));cloud.cubicTo(p+QPointF(-1,-13),p+QPointF(10,-14),p+QPointF(11,-6));cloud.cubicTo(p+QPointF(21,-8),p+QPointF(23,4),p+QPointF(14,4));cloud.lineTo(p+QPointF(0,3));painter.drawPath(cloud);
    const QPointF flower(bounds.right()-16,bounds.bottom()-17);painter.setPen(QPen(night?QColor("#a48baf"):QColor("#cbb4d3"),.8));
    for(int i=0;i<5;++i){painter.save();painter.translate(flower);painter.rotate(i*72.0);painter.drawEllipse(QRectF(-2,-9,4,6));painter.restore();}painter.drawEllipse(flower,1.8,1.8);
}
QPainterPath cloudBorder(const QRectF& r) {
    const double corner=std::min(11.0,r.height()/4);QPainterPath path;path.moveTo(r.left()+corner,r.top());
    auto edge=[&](QPointF from,QPointF to,QPointF outward){const double distance=std::hypot(to.x()-from.x(),to.y()-from.y());const int count=std::max(1,static_cast<int>(distance/13));for(int i=0;i<count;++i){const auto a=from+(to-from)*(static_cast<double>(i)/count);const auto b=from+(to-from)*(static_cast<double>(i+1)/count);path.quadTo((a+b)/2+outward,b);}};
    edge({r.left()+corner,r.top()},{r.right()-corner,r.top()},{0,-2.7});path.quadTo(r.topRight(),{r.right(),r.top()+corner});
    edge({r.right(),r.top()+corner},{r.right(),r.bottom()-corner},{2.4,0});path.quadTo(r.bottomRight(),{r.right()-corner,r.bottom()});
    edge({r.right()-corner,r.bottom()},{r.left()+corner,r.bottom()},{0,2.7});path.quadTo(r.bottomLeft(),{r.left(),r.bottom()-corner});
    edge({r.left(),r.bottom()-corner},{r.left(),r.top()+corner},{-2.4,0});path.quadTo(r.topLeft(),{r.left()+corner,r.top()});path.closeSubpath();return path;
}
}
bool enabled(const QWidget* widget){return inherited(widget,"uiSkin").toString()=="pencil";}
bool dark(const QWidget* widget){return inherited(widget,"appearanceMode").toString()=="dark";}
QColor accent(bool night){return QColor(night?"#c7a5d0":"#896b98");}
QPalette palette(QPalette result,bool night) {
    const QColor paper(night?"#29252b":"#fffdf2"),base(night?"#302b32":"#fffef8"),ink(night?"#eadfea":"#65576e");
    for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled}) {
        result.setColor(group,QPalette::Window,paper);result.setColor(group,QPalette::Base,base);result.setColor(group,QPalette::AlternateBase,paper);result.setColor(group,QPalette::Button,base);
        for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText})result.setColor(group,role,group==QPalette::Disabled?QColor(night?"#9a8c9d":"#a79aa6"):ink);
        result.setColor(group,QPalette::PlaceholderText,QColor(night?"#b59fb8":"#9c879f"));result.setColor(group,QPalette::Highlight,QColor(night?"#62465e":"#eed6e6"));result.setColor(group,QPalette::HighlightedText,ink);
        result.setColor(group,QPalette::ToolTipBase,base);result.setColor(group,QPalette::ToolTipText,ink);
    }return result;
}
QString stylesheet(bool night) {
    const QString paper=night?"#29252b":"#fffdf2",base=night?"#302b32":"#fffef8",ink=night?"#eadfea":"#65576e",muted=night?"#bea7c2":"#907e94",line=night?"#8e7195":"#c7b4ce",pink=night?"#594253":"#f0d9e6",gold=night?"#65533a":"#fae3ad";
    return QStringLiteral(R"QSS(
* { font-family:"KaiTi","Microsoft YaHei UI"; font-size:15px; }
QWidget { color:%3; }
QMainWindow,QDialog,QWidget#appRoot,QStackedWidget { background:%1; }
QWidget#messengerRoot,QWidget#messengerNav,QWidget#recentPanel,QWidget#contactPanel,QWidget#conversationPanel,QWidget#fileQueuePane { background:transparent; border:0; }
QLabel { background:transparent; }
QLineEdit,QPlainTextEdit,QComboBox,QSpinBox { background:%2; color:%3; border:1px solid %5; border-radius:9px; padding:8px 9px; selection-background-color:%6; selection-color:%3; }
QLineEdit:focus,QPlainTextEdit:focus,QComboBox:focus,QSpinBox:focus { border-color:%8; }
QLineEdit#chatPeer,QLineEdit#chatPeer:focus { background:transparent; border:0; color:%8; font-family:"KaiTi","Microsoft YaHei UI"; font-size:23px; padding:0; }
QPushButton { background:%2; color:%8; border:1px solid %5; border-radius:9px; padding:7px 12px; }
QPushButton:hover { background:%6; border-color:%8; }
QPushButton:pressed { background:%6; }
QPushButton:focus { border:1px dashed %8; }
QPushButton:disabled { background:%1; color:%4; border-color:%5; }
QPushButton[primary="true"]:enabled,QPushButton[primary="true"]:enabled:hover,QPushButton[primary="true"]:enabled:pressed { background:%6; color:%3; border:1px solid %5; }
QWidget#messengerNav QPushButton { background:transparent; color:%8; padding:9px 1px; border:1px solid transparent; border-radius:7px; font-size:13px; }
QWidget#messengerNav QPushButton:checked { background:%7; color:%3; border-color:%5; }
QPushButton#accountMenu { border:0; padding:0; }
QWidget#contactSections QPushButton { font-size:13px; padding:7px 3px; }
QWidget#contactSections QPushButton:checked { background:%6; color:%3; border-color:%5; }
QListWidget,QListWidget::viewport,QScrollArea,QScrollArea>QWidget,QScrollArea>QWidget>QWidget { background:transparent; border:0; }
QListWidget::item { border-radius:8px; margin:3px 4px; padding:3px; color:%3; }
QListWidget::item:hover { background:%7; }
QListWidget::item:selected { background:%6; border:1px solid %5; color:%3; }
QLabel#recentPreview,QLabel#contactPresence,QLabel#panelCaption,QLabel#fieldCaption,QLabel#queueFileInfo,QLabel#queueConversationLabel,QLabel#fileQueueSummary { color:%4; font-size:12px; }
QLabel#recentTime { color:%4; font-size:11px; }
QLabel#chatRemark { color:%4; font-size:12px; }
QLabel#unreadBadge { background:%6; color:%3; border:1px solid %5; }
QLabel#dateSeparator { background:%1; color:%4; border:1px dashed %5; border-radius:8px; padding:3px 9px; font-size:12px; }
QLabel#messageName,QLabel#messageTime,QLabel#messageDelivery { color:%4; font-size:11px; }
QPushButton#sendMessage,QPushButton#groupSend { background:%6; color:%8; font-weight:500; }
QPlainTextEdit#chatInput,QPlainTextEdit#groupInput { background:%2; border:1px solid %5; border-radius:14px; }
QLabel#groupTitle { color:%8; font-size:23px; }
QLabel#accountStatus[phase] { background:%7; color:%3; border:1px dashed %5; }
QWidget#fileQueueRow { background:%2; border:1px dashed %5; border-radius:10px; }
QLabel#queueFileName { color:%3; font-size:15px; }
QLabel#queueFileState { color:%8; }
QPushButton[queueAction="true"] { font-size:13px; padding:5px 9px; }
QProgressBar { background:%1; border:1px solid %5; border-radius:5px; color:%3; }
QProgressBar::chunk { background:%6; }
QScrollBar:vertical { background:%1; width:9px; }
QScrollBar:horizontal { background:%1; height:9px; }
QScrollBar::handle { background:%5; border-radius:4px; min-height:20px; min-width:20px; }
QScrollBar::add-line,QScrollBar::sub-line { width:0; height:0; }
QScrollBar::add-page,QScrollBar::sub-page { background:transparent; }
QWidget#loginCard,QWidget#registrationCard { background:transparent; border:0; }
QLabel#loginTitle,QLabel#registrationTitle { color:%8; font-family:"KaiTi","Microsoft YaHei UI"; font-size:26px; }
QLabel#loginSubtitle,QLabel#registrationSubtitle { color:%4; font-size:13px; }
QTabWidget::pane,QTabWidget>QStackedWidget,QGroupBox { background:%1; border-color:%5; }
QTabWidget#identityCardTabs::pane,QTabWidget#identityCardTabs>QStackedWidget { background:%1; border:0; }
QTabBar::tab { background:%2; color:%8; border:1px solid %5; border-radius:5px; padding:6px 9px; }
QTabBar::tab:selected { background:%6; color:%3; }
QMenu { background:%2; color:%3; border:1px solid %5; }
QMenu::item { color:%3; }
QMenu::item:selected { background:%6; color:%3; }
QMenu::item:disabled { color:%4; }
QMenu::separator { background:%5; }
QComboBox QAbstractItemView { background:%2; color:%3; border-color:%5; selection-background-color:%6; selection-color:%3; }
QCheckBox,QRadioButton { color:%3; }
QCheckBox::indicator,QRadioButton::indicator { border:1px solid %5; background:%2; }
QCheckBox::indicator:checked { background:%6; border-color:%8; }
QLabel#identityUid { color:%8; font-family:Consolas; font-size:28px; }
)QSS").arg(paper,base,ink,muted,line,pink,gold,accent(night).name());
}
QString bubbleStylesheet(bool night) {
    return QStringLiteral("QWidget#chatBubble,QWidget#fileCard{background:transparent;border:0;} QLabel#messageText,QLabel#fileName{color:%1;font-family:\"KaiTi\",\"Microsoft YaHei UI\";font-size:15px;} QLabel#messageDelivery,QLabel#messageTime,QLabel#messageName,QLabel#fileSize,QLabel#fileTime{color:%2;font-size:11px;background:transparent;}").arg(night?"#eadfea":"#65576e",night?"#baa5bf":"#8e7596");
}
Paper::Paper(Tone tone,bool decorations,QWidget* parent):QWidget(parent),tone_(tone),decorations_(decorations){setProperty("pencilPaper",true);setAttribute(Qt::WA_StyledBackground,true);}
void Paper::paintEvent(QPaintEvent* event) {
    if(!enabled(this)){QWidget::paintEvent(event);return;}
    const bool night=dark(this);const auto color=tone_==Tone::Rose?QColor(night?"#30262d":"#faf1ee"):tone_==Tone::Cream?QColor(night?"#302b28":"#fff9e9"):QColor(night?"#29252b":"#fffdf2");
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
    if(tone_==Tone::Card){QPainterPath clip;clip.addRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1),17,17);painter.setClipPath(clip);}
    painter.fillRect(rect(),grain(color));
    if(tone_==Tone::Card){painter.setPen(QPen(QColor(night?"#8e7195":"#c7b4ce"),1));painter.setBrush(Qt::NoBrush);painter.drawRoundedRect(QRectF(rect()).adjusted(1.5,1.5,-1.5,-1.5),17,17);}
    else if(tone_==Tone::Rose||tone_==Tone::Cream){painter.setPen(QPen(QColor(night?"#62505e":"#cdb4c4"),1));painter.drawLine(QPointF(width()-1,0),QPointF(width()-1,height()));painter.setOpacity(.25);painter.drawLine(QPointF(width()-2.4,0),QPointF(width()-1.8,height()));painter.setOpacity(1);}
    if(decorations_)doodles(painter,rect(),night);
}
void Bubble::paintEvent(QPaintEvent* event) {
    if(!enabled(this)){QWidget::paintEvent(event);return;}
    const bool own=property("ownMessage").toBool(),night=dark(this);const QColor fill(own?(night?"#493344":"#f8e6ef"):(night?"#2d4049":"#e5f2f9"));
    const QColor outline(own?(night?"#c499b9":"#bc99b8"):(night?"#8bb7c9":"#95b8cb"));
    const auto border=cloudBorder(QRectF(rect()).adjusted(4,4,-4,-4));QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(property("locatedFile").toBool()?accent(night):outline,property("locatedFile").toBool()?2:1.15));painter.setBrush(grain(fill));painter.drawPath(border);
    auto faint=outline;faint.setAlpha(65);painter.translate(.5,.4);painter.setPen(QPen(faint,.5));painter.setBrush(Qt::NoBrush);painter.drawPath(border);
}
void Button::paintEvent(QPaintEvent* event) {
    QPushButton::paintEvent(event);if(!enabled(this)||objectName()=="accountMenu"||width()<28||height()<20)return;
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);QColor ink=accent(dark(this));ink.setAlpha(isEnabled()?75:30);painter.setPen(QPen(ink,.7));painter.setBrush(Qt::NoBrush);
    const QRectF r=QRectF(rect()).adjusted(2.5,2.5,-2.5,-2.5);QPainterPath line;line.moveTo(r.left()+8,r.top()+.2);line.cubicTo(r.center().x(),r.top()-.2,r.center().x(),r.top()+.7,r.right()-8,r.top());line.quadTo(r.right()+.3,r.top(),r.right(),r.top()+8);line.lineTo(r.right()-.4,r.bottom()-8);line.quadTo(r.right(),r.bottom(),r.right()-8,r.bottom());line.cubicTo(r.center().x(),r.bottom()+.3,r.center().x(),r.bottom()-.3,r.left()+8,r.bottom());line.quadTo(r.left(),r.bottom(),r.left(),r.bottom()-8);line.lineTo(r.left()+.5,r.top()+8);line.quadTo(r.left(),r.top(),r.left()+8,r.top());painter.drawPath(line);
}
void Avatar::paintEvent(QPaintEvent* event) {
    QLabel::paintEvent(event);if(!enabled(this)||width()<24)return;QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);const QColor gold(dark(this)?"#d2b477":"#d3b580");painter.setPen(QPen(gold,.9));painter.setBrush(Qt::NoBrush);painter.drawEllipse(QRectF(rect()).adjusted(1.8,1.8,-1.8,-1.8));star(painter,{width()-4.0,4.0},2.8,gold);
}
}
