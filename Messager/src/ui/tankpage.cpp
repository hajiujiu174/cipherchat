#include "tankpage.h"
#include "components.h"
#include "theme.h"
#include "services/chatservice.h"
#include "services/gomokuwire.h"
#include <QComboBox>
#include <cmath>
#include <QCloseEvent>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QShortcut>
#include <QVBoxLayout>
#include <QHBoxLayout>
namespace Messager {
TankBoard::TankBoard(TankController* controller,QWidget* parent):QWidget(parent),m_controller(controller) {
    setObjectName("tankBoard");setAccessibleName("坦克动荡迷宫战场");setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);setCursor(Qt::CrossCursor);
    setMinimumSize(360,250);
    m_current=controller->state();m_previous=m_current;m_blend.start();
    connect(controller,&TankController::changed,this,[this] {
        auto next=m_controller->state();
        if(next["frame"]!=m_current["frame"] || next["walls"]!=m_current["walls"]) {
            m_previous=m_current;m_current=next;
            if(m_previous["round"]!=next["round"])m_previous=next;
            m_blend.restart();
        }
        update();
    });
    m_animation.setInterval(20);
    connect(&m_animation,&QTimer::timeout,this,[this]{if(isVisible()&&m_controller->active()&&!m_controller->hosting())update();});
    m_animation.start();
    connect(&Theme::instance(),&Theme::changed,this,qOverload<>(&QWidget::update));
}
QRectF TankBoard::arenaRect() const {
    const double scale=qMin((width()-20)/660.0,(height()-20)/420.0);
    return QRectF((width()-660*scale)/2,(height()-420*scale)/2,660*scale,420*scale);
}
QPoint TankBoard::pointForArena(const QPointF& point) const {
    const auto rect=arenaRect();
    return (rect.topLeft()+QPointF(point.x()*rect.width()/660,point.y()*rect.height()/420)).toPoint();
}
void TankBoard::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    const auto& c=Theme::instance().colors();
    const double scale=qMin((width()-20)/660.0,(height()-20)/420.0);
    p.translate((width()-660*scale)/2,(height()-420*scale)/2);p.scale(scale,scale);
    p.setPen(QPen(hasFocus()?c.accent:c.border,3));p.setBrush(c.surface);p.drawRoundedRect(QRectF(-4,-4,668,428),8,8);
    auto state=m_controller->state();
    // Interpolate only the opponent. The local guest is rendered from bounded prediction.
    if(!m_controller->hosting()&&m_controller->active()) {
        auto tanks=state["tanks"].toArray(),old=m_previous["tanks"].toArray();
        const double blend=qMin(1.0,m_blend.elapsed()/double(m_controller->interpolationMs()));
        if(old.size()==2&&m_previous["round"]==state["round"]&&old[0].toArray()[3]==tanks[0].toArray()[3]) {
            auto a=tanks[0].toArray(),b=old[0].toArray();for(int j=0;j<2;++j)a[j]=b[j].toDouble()+(a[j].toDouble()-b[j].toDouble())*blend;
            double angle=std::remainder(a[2].toDouble()-b[2].toDouble(),360.0);a[2]=b[2].toDouble()+angle*blend;tanks[0]=a;
        }
        tanks[1]=m_controller->predictedState()["tanks"].toArray()[1];state["tanks"]=tanks;
    }
    p.setPen(QPen(c.text,4,Qt::SolidLine,Qt::RoundCap));
    for(auto value:state["walls"].toArray()) {auto a=value.toArray();p.drawLine(QPointF(a[0].toDouble(),a[1].toDouble()),QPointF(a[2].toDouble(),a[3].toDouble()));}
    const QColor colors[]={QColor("#a591ff"),QColor("#42bd98")};
    auto tanks=state["tanks"].toArray();
    for(int i=0;i<tanks.size();++i) {
        const auto a=tanks[i].toArray();
        if(!a[3].toBool()) {p.setPen(QPen(c.muted,3));p.drawLine(QPointF(a[0].toDouble()-9,a[1].toDouble()-9),QPointF(a[0].toDouble()+9,a[1].toDouble()+9));continue;}
        p.save();p.translate(a[0].toDouble(),a[1].toDouble());p.save();p.rotate(-a[2].toDouble());
        p.setPen(QPen(c.text,1.5));p.setBrush(colors[i].darker(160));
        // One filled rectangle exactly matches the solid movement/hit collider.
        const QRectF body(-TankEngine::BodyLength/2,-TankEngine::BodyWidth/2,TankEngine::BodyLength,TankEngine::BodyWidth);
        p.fillRect(body,colors[i]);p.drawRect(body.adjusted(.75,.75,-.75,-.75));p.restore();
        double aim=state.contains("aim")?state["aim"].toArray()[i].toDouble():a[2].toDouble();
        if(m_controller->pointerActive()&&i==(m_controller->hosting()?0:1)){
            const auto target=m_controller->pointerTarget();const double dx=target.x()-a[0].toDouble(),dy=target.y()-a[1].toDouble();
            if(std::hypot(dx,dy)>.01)aim=std::atan2(-dy,dx)*180/std::acos(-1.0);
            p.setBrush(Qt::NoBrush);p.setPen(QPen(c.accent,1,Qt::DashLine));p.drawEllipse(QPointF(0,0),TankEngine::AimOnlyRadius,TankEngine::AimOnlyRadius);
        }
        p.rotate(-aim);p.setPen(QPen(c.text,1));p.setBrush(colors[i].lighter(120));
        p.drawRect(QRectF(0,-2,TankEngine::BarrelLength,4));p.restore();
    }
    for(auto value:state["shots"].toArray()) {auto a=value.toArray();p.setPen(Qt::NoPen);p.setBrush(colors[a[3].toInt()]);p.drawEllipse(QPointF(a[0].toDouble(),a[1].toDouble()),3.5,3.5);}
    if(m_controller->active()&&m_controller->pointerActive()) {
        const auto target=m_controller->pointerTarget();p.setBrush(Qt::NoBrush);p.setPen(QPen(c.accent,1.5));
        p.drawEllipse(target,6,6);p.drawLine(target+QPointF(-9,0),target+QPointF(9,0));p.drawLine(target+QPointF(0,-9),target+QPointF(0,9));
    }
    if(!m_controller->active()) {p.fillRect(QRectF(150,174,360,72),c.surface);p.setPen(c.text);p.drawText(QRectF(155,175,350,70),Qt::AlignCenter,"坦克动荡 · 双人在线\n邀请好友加入后开始");}
}
void TankBoard::key(QKeyEvent* e,bool down) {
    int bit=0;
    switch(e->key()) {case Qt::Key_W:case Qt::Key_Up:bit=1;break;case Qt::Key_S:case Qt::Key_Down:bit=2;break;case Qt::Key_A:case Qt::Key_Left:bit=4;break;case Qt::Key_D:case Qt::Key_Right:bit=8;break;case Qt::Key_Space:bit=16;break;default:e->ignore();return;}
    if(!e->isAutoRepeat()) {if(down)m_buttons|=bit;else m_buttons&=~bit;m_controller->setButtons(m_buttons);}e->accept();
}
void TankBoard::keyPressEvent(QKeyEvent* e) {key(e,true);}
void TankBoard::keyReleaseEvent(QKeyEvent* e) {key(e,false);}
void TankBoard::stopInput() {m_buttons=0;m_controller->stopInput();update();}
void TankBoard::focusOutEvent(QFocusEvent* e) {stopInput();QWidget::focusOutEvent(e);}
void TankBoard::hideEvent(QHideEvent* e) {stopInput();QWidget::hideEvent(e);}
bool TankBoard::pointMouse(const QPointF& pos) {
    const auto rect=arenaRect();
    if(!m_controller->active()||!rect.contains(pos)) {stopInput();return false;}
    const auto local=pos-rect.topLeft();
    m_controller->setPointerTarget(QPointF(local.x()*660/rect.width(),local.y()*420/rect.height()));update();return true;
}
void TankBoard::mouseMoveEvent(QMouseEvent* e) {
    if(!window()->isActiveWindow()) {stopInput();e->ignore();return;}
    if(pointMouse(e->position()))setFocus(Qt::MouseFocusReason);
    e->accept();
}
void TankBoard::mousePressEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton) {QWidget::mousePressEvent(e);return;}
    setFocus(Qt::MouseFocusReason);
    if(pointMouse(e->position()))m_controller->setPointerFire(true);
    e->accept();
}
void TankBoard::mouseReleaseEvent(QMouseEvent* e) {
    if(e->button()==Qt::LeftButton) {m_controller->setPointerFire(false);e->accept();}
    else QWidget::mouseReleaseEvent(e);
}
void TankBoard::leaveEvent(QEvent* e) {stopInput();QWidget::leaveEvent(e);}
TankPage::TankPage(ChatService* service,QWidget* parent):QWidget(parent,Qt::Window),m_service(service) {
    setObjectName("tanktroublePage");m_controller=new TankController(service,this);
    setWindowTitle("坦克动荡 · NEXTALK");resize(1280,900);setMinimumSize(720,580);
    auto* root=new QVBoxLayout(this);root->setContentsMargins(20,16,20,16);
    auto* top=new QHBoxLayout;
    auto* back=button("返回小游戏","ghost");back->setObjectName("tankWindowBack");
    m_fullScreen=button("退出全屏（Esc）");m_fullScreen->setObjectName("tankFullScreen");
    top->addWidget(back);top->addWidget(label("坦克动荡",20));top->addStretch();top->addWidget(m_fullScreen);root->addLayout(top);
    connect(back,&QPushButton::clicked,this,[this]{close();});
    connect(m_fullScreen,&QPushButton::clicked,this,&TankPage::toggleFullScreen);
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);
    connect(escape,&QShortcut::activated,this,[this]{if(isFullScreen())toggleFullScreen();});
    auto* full=new QShortcut(QKeySequence(Qt::Key_F11),this);
    connect(full,&QShortcut::activated,this,&TankPage::toggleFullScreen);
    auto* row=new QHBoxLayout;m_friends=new QComboBox;m_friends->setObjectName("tankFriends");
    m_create=button("邀请好友开局","primary");m_create->setObjectName("tankCreate");
    m_leave=button("退出房间");m_leave->setObjectName("tankLeave");
    row->addWidget(m_friends,1);row->addWidget(m_create);row->addWidget(m_leave);root->addLayout(row);
    auto* inviteRow=new QHBoxLayout;m_rooms=new QComboBox;m_rooms->setObjectName("tankInvitations");
    m_join=button("加入邀请");m_join->setObjectName("tankJoin");inviteRow->addWidget(m_rooms,1);inviteRow->addWidget(m_join);root->addLayout(inviteRow);
    m_status=label({},13,"muted");m_status->setObjectName("tankStatus");m_status->setWordWrap(true);root->addWidget(m_status);
    m_score=label("紫方 0 : 0 绿方",18);m_score->setObjectName("tankScore");root->addWidget(m_score);
    m_board=new TankBoard(m_controller);root->addWidget(m_board,1);
    auto* help=label("炮管始终对准鼠标；鼠标在虚线圈内时只转向、不移动，圈外跟随鼠标，左键开火、按住连射。矩形车身整体挡墙，炮管不阻碍移动；最高移速约每秒 1.7 格。\n移出战场、切换窗口或页面时停止操作。每方最多 5 发炮弹，自己的反弹弹也会造成伤害；先得 5 分获胜。仍可使用方向键 / WASD 和空格。",12,"muted");help->setWordWrap(true);root->addWidget(help);
    auto* license=label("移植自 JustDoIt0910 / TankTrouble（MIT）· Qt 界面与好友联机适配",11,"muted");license->setWordWrap(true);root->addWidget(license);
    connect(m_create,&QPushButton::clicked,this,[this]{if(m_controller->create(m_friends->currentData().toString()))m_board->setFocus();});
    connect(m_leave,&QPushButton::clicked,m_controller,&TankController::leave);
    connect(m_join,&QPushButton::clicked,this,[this]{int i=m_rooms->currentIndex()<0?-1:m_rooms->currentData().toInt();if(i>=0&&i<m_invites.size())openInvitation(m_invites[i]);});
    connect(m_controller,&TankController::chatRequested,this,&TankPage::chatRequested);
    connect(m_controller,&TankController::changed,this,&TankPage::refresh);
    connect(service,&ChatService::contactsChanged,this,[this] {
        const auto selected=m_friends->currentData().toString();QSignalBlocker block(m_friends);m_friends->clear();
        for(const auto& c:m_service->contacts())if(c.friendContact&&c.online)m_friends->addItem(c.username,c.username);
        int at=m_friends->findData(selected);if(at>=0)m_friends->setCurrentIndex(at);refresh();
    });
    connect(service,&ChatService::messagesChanged,this,[this](const QString&){invitations();});
    connect(service,&ChatService::sessionChanged,this,[this]{invitations();});
    for(const auto& c:service->contacts())if(c.friendContact&&c.online)m_friends->addItem(c.username,c.username);
    invitations();refresh();
}
void TankPage::refresh() {
    auto s=m_controller->state();auto score=s["score"].toArray();
    m_score->setText(QString("紫方 %1 : %2 绿方 · 第 %3 局%4").arg(score[0].toInt()).arg(score[1].toInt()).arg(s["round"].toInt()).arg(s["wait"].toInt()>0?" · 下一局即将开始":""));
    if(!m_controller->networkText().isEmpty())m_score->setText(m_score->text()+"\n"+m_controller->networkText());
    const bool supported=m_service->gameRelaySupported();
    m_status->setText(!m_service->online()?"登录后可邀请好友进行双人实时对战。":!supported?"当前服务器尚未启用实时游戏；服务器升级后即可邀请好友联机。":m_controller->status());
    m_create->setEnabled(supported&&m_controller->room().isEmpty()&&m_friends->count()>0);
    m_leave->setEnabled(!m_controller->room().isEmpty());m_join->setEnabled(supported&&m_rooms->count()>0&&m_controller->room().isEmpty());
}
void TankPage::invitations() {
    QSignalBlocker block(m_rooms);m_rooms->clear();m_invites.clear();
    for(const auto& c:m_service->conversations())if(!c.group)for(const auto& m:m_service->messages(c.id)) {
        const auto o=GomokuWire::decode(m.text);
        if(!m.outgoing&&o["game"]=="tanktrouble"&&o["type"]=="invite"&&o["white"]==m_service->username()&&m_service->isFriend(m.sender)) {
            m_rooms->addItem(m.sender+" · "+o["room"].toString().left(8),m_invites.size());m_invites.append(m);
        }
    }
    refresh();
}
void TankPage::openInvitation(const Message& m) {if(m_controller->openInvitation(m))m_board->setFocus();}
void TankPage::openWindow() {
    m_controller->stopInput();showFullScreen();m_fullScreen->setText("退出全屏（Esc）");
    raise();activateWindow();m_board->setFocus();
}
void TankPage::toggleFullScreen() {
    m_controller->stopInput();
    if(isFullScreen()) {showNormal();m_fullScreen->setText("全屏（F11）");}
    else {showFullScreen();m_fullScreen->setText("退出全屏（Esc）");}
    m_board->setFocus();
}
void TankPage::closeEvent(QCloseEvent* e) {
    m_controller->stopInput();QWidget::closeEvent(e);emit returned();
}
}

