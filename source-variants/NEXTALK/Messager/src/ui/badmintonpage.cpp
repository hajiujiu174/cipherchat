#include "badmintonpage.h"
#include "components.h"
#include "theme.h"
#include "services/chatservice.h"
#include "services/gomokuwire.h"
#include <QComboBox>
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
BadmintonBoard::BadmintonBoard(BadmintonController* controller,QWidget* parent):QWidget(parent),m_controller(controller) {
    setObjectName("badmintonBoard");setAccessibleName("火柴人羽毛球球场");setFocusPolicy(Qt::StrongFocus);setMinimumSize(360,250);
    m_current=controller->state();m_previous=m_current;m_blend.start();
    connect(controller,&BadmintonController::changed,this,[this]{
        const auto next=m_controller->state();
        if(next["frame"]!=m_current["frame"]) {m_previous=m_current;m_current=next;if(m_previous["round"]!=next["round"])m_previous=next;m_blend.restart();}update();
    });
    m_animation.setInterval(15);connect(&m_animation,&QTimer::timeout,this,[this]{if(isVisible())update();});m_animation.start();
}
QRectF BadmintonBoard::arenaRect() const {
    const double scale=qMin((width()-16)/990.0,(height()-16)/660.0);
    return QRectF((width()-990*scale)/2,(height()-660*scale)/2,990*scale,660*scale);
}
QPoint BadmintonBoard::pointForArena(const QPointF& point) const {
    const auto r=arenaRect();return (r.topLeft()+QPointF(point.x()*r.width()/990,point.y()*r.height()/660)).toPoint();
}
void BadmintonBoard::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const auto r=arenaRect();p.translate(r.topLeft());p.scale(r.width()/990,r.height()/660);
    QLinearGradient bg(0,0,0,660);bg.setColorAt(0,QColor("#102d45"));bg.setColorAt(1,QColor("#1b5e65"));p.fillRect(QRectF(0,0,990,660),bg);
    p.setPen(QColor("#34596b"));for(int y=90;y<440;y+=55)p.drawLine(20,y,970,y);
    p.setPen(QColor("#5a7b84"));p.setFont(QFont("Microsoft YaHei",22,QFont::Bold));p.drawText(QRectF(0,80,990,100),Qt::AlignCenter,"STICK FIGURE BADMINTON");
    p.fillRect(QRectF(0,619,990,41),QColor("#2b8a69"));p.setPen(QPen(QColor("#d9fff0"),3));p.drawLine(35,619,955,619);p.drawLine(53,627,937,627);
    auto state=m_controller->state();auto people=state["players"].toArray();
    // Interpolate the guest between two authoritative snapshots for smooth animation.
    if(!m_controller->hosting()&&m_controller->active()&&m_previous["round"]==state["round"]) {
        const double t=qMin(1.0,m_blend.elapsed()/double(m_controller->interpolationMs()));auto old=m_previous["players"].toArray();
        if(old.size()==2)for(int i=0;i<1;++i){auto a=people[i].toArray(),b=old[i].toArray();for(int j=0;j<2;++j)a[j]=b[j].toDouble()+(a[j].toDouble()-b[j].toDouble())*t;people[i]=a;}
    }
    if(!m_controller->hosting()&&m_controller->active()) {const auto predicted=m_controller->predictedState();people[1]=predicted["players"].toArray()[1];state["ball"]=predicted.value("ball");}
    const QColor colors[]={QColor("#b9a7ff"),QColor("#66e0b6")};
    for(int i=0;i<2;++i) {
        const auto a=people[i].toArray();const double x=a[0].toDouble(),y=a[1].toDouble(),side=i==0?1:-1;
        p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,55));p.drawEllipse(QRectF(x-43,610,86,12));
        p.save();p.translate(x,y);p.scale(side,1);p.setPen(QPen(colors[i],8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(0,-64),19,19);p.drawLine(QPointF(0,-44),QPointF(0,30));
        const double leg=std::sin(state["frame"].toInt()*0.15)*12;
        p.drawLine(QPointF(0,30),QPointF(-25-leg,97));p.drawLine(QPointF(0,30),QPointF(25+leg,97));p.drawLine(QPointF(0,-25),QPointF(-30,12));
        double angle=-0.65;if(a[3].toInt()>0)angle=-2.0+(18-a[3].toInt())*0.19;
        const QPointF hand(45*std::cos(angle),45*std::sin(angle));const QPointF head(118*std::cos(angle),118*std::sin(angle));
        p.drawLine(QPointF(0,-25),hand);p.setPen(QPen(QColor("#eef7ff"),4));p.drawLine(hand,head);
        p.save();p.translate(head);p.rotate(angle*180/3.141592653589793);p.drawEllipse(QRectF(-20,-15,40,30));p.setPen(QPen(QColor("#89b7bf"),1));for(int v=-10;v<=10;v+=5)p.drawLine(-15,v,15,v);p.restore();p.restore();
    }
    // Court net and posts.
    p.setPen(QPen(QColor("#bed4df"),2));for(int y=523;y<619;y+=12)p.drawLine(485,y,505,y);for(int x=485;x<=505;x+=5)p.drawLine(x,522,x,619);
    p.setPen(QPen(QColor("#ffffff"),5));p.drawLine(485,522,505,522);p.drawLine(495,522,495,619);
    const auto ball=state["ball"].toArray();p.save();p.translate(ball[0].toDouble(),ball[1].toDouble());p.rotate(std::atan2(ball[3].toDouble(),ball[2].toDouble())*180/3.141592653589793);
    p.setPen(QPen(QColor("#eaf6ff"),2));p.setBrush(QColor("#d6edff"));p.drawPolygon(QPolygonF{QPointF(-5,0),QPointF(-24,-10),QPointF(-24,10)});p.setBrush(QColor("#fff3ad"));p.drawEllipse(QPointF(0,0),5,5);p.restore();
    QString text;
    if(!m_controller->active())text="火柴人羽毛球\n开始单人练习，或邀请在线好友";
    else if(state["finished"].toBool())text=state["score"].toArray()[0].toInt()==7?"紫方获胜！":"绿方获胜！";
    else if(state["wait"].toInt()>0)text="得分！准备下一球";
    else if(state["serving"].toBool())text=QString("%1发球 · 按 J / 空格挥拍").arg(state["server"].toInt()==0?"紫方":"绿方");
    if(!text.isEmpty()){p.fillRect(QRectF(175,215,640,95),QColor(8,25,40,200));p.setPen(Qt::white);p.setFont(QFont("Microsoft YaHei",18,QFont::Bold));p.drawText(QRectF(175,215,640,95),Qt::AlignCenter,text);}
}
void BadmintonBoard::key(QKeyEvent* e,bool down) {
    int bit=0;switch(e->key()){case Qt::Key_A:case Qt::Key_Left:bit=1;break;case Qt::Key_D:case Qt::Key_Right:bit=2;break;case Qt::Key_W:case Qt::Key_Up:bit=4;break;case Qt::Key_S:case Qt::Key_Down:case Qt::Key_J:case Qt::Key_Space:bit=8;break;default:e->ignore();return;}
    if(!e->isAutoRepeat()){if(down)m_buttons|=bit;else m_buttons&=~bit;m_controller->setButtons(m_buttons);}e->accept();
}
void BadmintonBoard::keyPressEvent(QKeyEvent* e){key(e,true);}
void BadmintonBoard::keyReleaseEvent(QKeyEvent* e){key(e,false);}
void BadmintonBoard::stopInput(){m_buttons=0;m_controller->stopInput();}
void BadmintonBoard::focusOutEvent(QFocusEvent* e){stopInput();QWidget::focusOutEvent(e);}
void BadmintonBoard::hideEvent(QHideEvent* e){stopInput();QWidget::hideEvent(e);}
BadmintonPage::BadmintonPage(ChatService* service,QWidget* parent):QWidget(parent,Qt::Window),m_service(service) {
    setObjectName("badmintonPage");m_controller=new BadmintonController(service,this);
    setWindowTitle("火柴人羽毛球 · NEXTALK");resize(1280,900);setMinimumSize(720,580);
    auto* root=new QVBoxLayout(this);root->setContentsMargins(20,16,20,16);
    auto* top=new QHBoxLayout;
    auto* back=button("返回小游戏","ghost");back->setObjectName("badmintonWindowBack");
    m_fullScreen=button("退出全屏（Esc）");m_fullScreen->setObjectName("badmintonFullScreen");
    top->addWidget(back);top->addWidget(label("火柴人羽毛球",20));top->addStretch();top->addWidget(m_fullScreen);root->addLayout(top);
    connect(back,&QPushButton::clicked,this,[this]{close();});
    connect(m_fullScreen,&QPushButton::clicked,this,&BadmintonPage::toggleFullScreen);
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);
    connect(escape,&QShortcut::activated,this,[this]{if(isFullScreen())toggleFullScreen();});
    auto* full=new QShortcut(QKeySequence(Qt::Key_F11),this);
    connect(full,&QShortcut::activated,this,&BadmintonPage::toggleFullScreen);
    auto* row=new QHBoxLayout;m_friends=new QComboBox;m_friends->setObjectName("badmintonFriends");
    m_create=button("邀请好友开局","primary");m_create->setObjectName("badmintonCreate");
    m_leave=button("退出房间");m_leave->setObjectName("badmintonLeave");
    row->addWidget(m_friends,1);row->addWidget(m_create);row->addWidget(m_leave);root->addLayout(row);
    auto* inviteRow=new QHBoxLayout;m_rooms=new QComboBox;m_rooms->setObjectName("badmintonInvitations");
    m_join=button("加入邀请");m_join->setObjectName("badmintonJoin");inviteRow->addWidget(m_rooms,1);inviteRow->addWidget(m_join);root->addLayout(inviteRow);
    m_status=label({},13,"muted");m_status->setObjectName("badmintonStatus");m_status->setWordWrap(true);root->addWidget(m_status);
    m_score=label("紫方 0 : 0 绿方",18);m_score->setObjectName("badmintonScore");root->addWidget(m_score);
    m_board=new BadmintonBoard(m_controller);root->addWidget(m_board,1);
    auto* help=label("A / D 或左右键移动，W / 上键跳跃，J / 空格 / 下键挥拍。按挥拍键发球，先得 7 分获胜。\n球只能在己方半场击打，不能连续击球；挥拍有距离和间隔限制，球拍碰不到球时需要移动或跳跃。Esc 退出全屏，F11 切换全屏。",12,"muted");help->setWordWrap(true);root->addWidget(help);
    auto* credit=label("玩法与物理移植：b-z / StickFigureBadminton · 种璐瑶、周伯威、钱珺 · Qt 绘制与好友联机适配",11,"muted");credit->setWordWrap(true);root->addWidget(credit);
    auto* practice=button("单人练习","primary");practice->setObjectName("badmintonPractice");row->insertWidget(0,practice);
    connect(practice,&QPushButton::clicked,this,[this]{m_controller->startPractice();m_board->setFocus();});
    connect(m_controller,&BadmintonController::changed,practice,[this,practice]{practice->setEnabled(m_controller->room().isEmpty());});
    connect(m_create,&QPushButton::clicked,this,[this]{if(m_controller->create(m_friends->currentData().toString()))m_board->setFocus();});
    connect(m_leave,&QPushButton::clicked,m_controller,&BadmintonController::leave);
    connect(m_join,&QPushButton::clicked,this,[this]{int i=m_rooms->currentIndex()<0?-1:m_rooms->currentData().toInt();if(i>=0&&i<m_invites.size())openInvitation(m_invites[i]);});
    connect(m_controller,&BadmintonController::chatRequested,this,&BadmintonPage::chatRequested);
    connect(m_controller,&BadmintonController::changed,this,&BadmintonPage::refresh);
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
void BadmintonPage::refresh() {
    auto s=m_controller->state();auto score=s["score"].toArray();
    m_score->setText(QString("紫方 %1 : %2 绿方 · 第 %3 局%4").arg(score[0].toInt()).arg(score[1].toInt()).arg(s["round"].toInt()).arg(s["wait"].toInt()>0?" · 下一局即将开始":""));
    if(!m_controller->networkText().isEmpty())m_score->setText(m_score->text()+"\n"+m_controller->networkText());
    const bool supported=m_service->gameRelaySupported();
    m_status->setText(m_controller->practice()?m_controller->status():!m_service->online()?"登录后可邀请好友进行双人实时对战。":!supported?"当前服务器尚未启用实时游戏；服务器升级后即可邀请好友联机。":m_controller->status());
    m_create->setEnabled(supported&&m_controller->room().isEmpty()&&m_friends->count()>0);
    m_leave->setEnabled(!m_controller->room().isEmpty());m_join->setEnabled(supported&&m_rooms->count()>0&&m_controller->room().isEmpty());
}
void BadmintonPage::invitations() {
    QSignalBlocker block(m_rooms);m_rooms->clear();m_invites.clear();
    for(const auto& c:m_service->conversations())if(!c.group)for(const auto& m:m_service->messages(c.id)) {
        const auto o=GomokuWire::decode(m.text);
        if(!m.outgoing&&o["game"]=="badminton"&&o["type"]=="invite"&&o["white"]==m_service->username()&&m_service->isFriend(m.sender)) {
            m_rooms->addItem(m.sender+" · "+o["room"].toString().left(8),m_invites.size());m_invites.append(m);
        }
    }
    refresh();
}
void BadmintonPage::openInvitation(const Message& m) {if(m_controller->openInvitation(m))m_board->setFocus();}
void BadmintonPage::openWindow() {
    m_controller->stopInput();showFullScreen();m_fullScreen->setText("退出全屏（Esc）");
    raise();activateWindow();m_board->setFocus();
}
void BadmintonPage::toggleFullScreen() {
    m_controller->stopInput();
    if(isFullScreen()) {showNormal();m_fullScreen->setText("全屏（F11）");}
    else {showFullScreen();m_fullScreen->setText("退出全屏（Esc）");}
    m_board->setFocus();
}
void BadmintonPage::closeEvent(QCloseEvent* e) {
    m_controller->stopInput();QWidget::closeEvent(e);emit returned();
}
}

