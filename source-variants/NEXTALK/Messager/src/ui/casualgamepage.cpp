#include "casualgamepage.h"
#include "components.h"
#include "theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QHideEvent>
#include <QShowEvent>
#include <QCloseEvent>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QShortcut>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
namespace Messager {
namespace {
const QColor platformColors[]={QColor("#a698ef"),QColor("#70c8b0"),QColor("#edb966"),QColor("#77b6df"),QColor("#e9a1b6")};
void symbol(QPainter& p,const QRectF& rect,int kind){
    p.save();p.translate(rect.center());p.scale(rect.width()/64,rect.height()/64);p.setPen(Qt::NoPen);
    switch(kind%8){
    case 0:p.setBrush(QColor("#53a867"));p.rotate(-30);p.drawEllipse(QRectF(-15,-18,22,33));p.setPen(QPen(QColor("#2d7344"),3));p.drawLine(QPointF(-2,20),QPointF(-2,-9));break;
    case 1:{p.setBrush(QColor("#ee9250"));QPolygonF carrot;carrot<<QPointF(-14,-9)<<QPointF(15,-9)<<QPointF(0,22);p.drawPolygon(carrot);p.setPen(QPen(QColor("#62a75e"),5,Qt::SolidLine,Qt::RoundCap));p.drawLine(0,-8,-8,-21);p.drawLine(0,-8,8,-21);break;}
    case 2:p.setBrush(QColor("#dc8db3"));for(int n=0;n<5;++n){p.save();p.rotate(n*72);p.drawEllipse(QRectF(-8,-23,16,24));p.restore();}p.setBrush(QColor("#f3ca67"));p.drawEllipse(QPointF(0,0),8.,8.);break;
    case 3:p.setBrush(QColor("#7976b8"));p.drawEllipse(QPointF(-9,4),11.,11.);p.drawEllipse(QPointF(10,4),11.,11.);p.drawEllipse(QPointF(0,-11),11.,11.);p.setBrush(QColor("#a7a3dc"));p.drawEllipse(QPointF(-3,-14),3.,3.);break;
    case 4:p.setBrush(QColor("#e9d7b4"));p.drawRoundedRect(QRectF(-6,-1,12,22),4,4);p.setBrush(QColor("#d98568"));p.drawPie(QRectF(-23,-23,46,42),0,180*16);p.setBrush(QColor("#fff4db"));p.drawEllipse(QPointF(-8,-12),4.,4.);p.drawEllipse(QPointF(10,-12),3.,3.);break;
    case 5:p.setBrush(QColor("#e7bf59"));p.drawRoundedRect(QRectF(-12,-22,24,43),11,11);p.setPen(QPen(QColor("#bf9639"),2));for(int y=-14;y<20;y+=8)p.drawLine(-8,y,8,y);p.setBrush(QColor("#74a967"));p.setPen(Qt::NoPen);p.drawEllipse(QRectF(-19,0,15,26));p.drawEllipse(QRectF(5,0,15,26));break;
    case 6:p.setBrush(QColor("#d9e8e5"));p.drawRoundedRect(QRectF(-14,-13,28,35),6,6);p.drawRect(QRectF(-8,-22,16,12));p.setBrush(QColor("#80b7c9"));p.drawRect(QRectF(-8,-24,16,5));p.drawRect(QRectF(-14,1,28,9));break;
    default:p.setBrush(QColor("#ece9dc"));for(auto c:{QPointF(-12,0),QPointF(0,-10),QPointF(12,0),QPointF(0,10)})p.drawEllipse(c,12.,12.);p.setBrush(QColor("#747583"));p.drawEllipse(QRectF(0,-2,21,25));p.setPen(QPen(Qt::white,3));p.drawPoint(7,7);p.drawPoint(15,7);break;
    }
    p.restore();
}
void tile(QPainter& p,const QRectF& rect,int kind,bool enabled){
    p.setPen(Qt::NoPen);p.setBrush(QColor("#c9c7af"));p.drawRoundedRect(rect.translated(0,5),10,10);
    p.setBrush(enabled?QColor("#fffdf0"):QColor("#bbbfae"));p.setPen(QPen(enabled?QColor("#e4ddc3"):QColor("#a3ab99"),1.5));p.drawRoundedRect(rect,10,10);
    p.save();if(!enabled)p.setOpacity(.30);symbol(p,rect,kind);p.restore();
}
}
CasualBoard::CasualBoard(const QString& game,QWidget* parent):QWidget(parent),m_game(game){
    setObjectName(game+"Board");setAccessibleName(game=="jumpjump"?"跳一跳蓄力战场":"羊了个羊叠层消除棋盘");
    setFocusPolicy(Qt::StrongFocus);setMinimumSize(300,340);m_timer.setInterval(16);m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer,&QTimer::timeout,this,[this]{if(m_flying&&m_animation.elapsed()>=460){m_flying=false;emit changed();}if(m_flying||m_charging)update();});
    connect(&Theme::instance(),&Theme::changed,this,qOverload<>(&CasualBoard::update));restart();
}
double CasualBoard::scale()const{return qMin(width()/720.0,height()/780.0);}
QPointF CasualBoard::origin()const{const auto s=scale();return {(width()-720*s)/2,(height()-780*s)/2};}
QPoint CasualBoard::screenPoint(const QPointF& point)const{return (origin()+point*scale()).toPoint();}
QPointF CasualBoard::logicalPoint(const QPointF& point)const{return (point-origin())/scale();}
void CasualBoard::restart(int level){cancelInput();m_flying=false;const auto seed=QRandomGenerator::global()->generate();m_jump.reset(seed);m_sheep.reset(level,seed);emit changed();update();}
void CasualBoard::cancelInput(){m_charging=false;update();}
bool CasualBoard::undo(){bool ok=m_sheep.undo();if(ok){emit changed();update();}return ok;}
bool CasualBoard::shuffle(){bool ok=m_sheep.shuffle();if(ok){emit changed();update();}return ok;}
bool CasualBoard::moveToReserve(){bool ok=m_sheep.moveToReserve();if(ok){emit changed();update();}return ok;}
void CasualBoard::press(){if(m_game!="jumpjump"||m_jump.lost()||m_flying||m_charging)return;m_charging=true;m_hold.start();update();}
void CasualBoard::release(){
    if(!m_charging)return;const int held=int(qMin<qint64>(m_hold.elapsed(),JumpGame::MaxChargeMs));m_charging=false;
    m_before=m_jump;if(m_jump.jump(held)){m_flying=true;m_animation.start();emit changed();}update();
}
void CasualBoard::mousePressEvent(QMouseEvent* e){
    if(e->button()!=Qt::LeftButton)return;setFocus(Qt::MouseFocusReason);
    if(m_game=="jumpjump"){press();return;}const auto point=logicalPoint(e->position());
    for(int n=0;n<m_sheep.reserve().size();++n)if(QRectF(248+n*72,570,64,64).contains(point)){if(m_sheep.takeReserve(n)){emit changed();update();}return;}
    for(int n=m_sheep.tiles().size()-1;n>=0;--n){const auto& t=m_sheep.tiles()[n];if(!t.removed&&t.rect.contains(point)){if(m_sheep.pick(n)){emit changed();update();}return;}}
}
void CasualBoard::mouseReleaseEvent(QMouseEvent* e){if(e->button()==Qt::LeftButton&&m_game=="jumpjump")release();}
void CasualBoard::keyPressEvent(QKeyEvent* e){if(e->key()==Qt::Key_Space&&m_game=="jumpjump"){if(!e->isAutoRepeat())press();e->accept();}else QWidget::keyPressEvent(e);}
void CasualBoard::keyReleaseEvent(QKeyEvent* e){if(e->key()==Qt::Key_Space&&m_game=="jumpjump"){if(!e->isAutoRepeat())release();e->accept();}else QWidget::keyReleaseEvent(e);}
void CasualBoard::focusOutEvent(QFocusEvent* e){cancelInput();QWidget::focusOutEvent(e);}
void CasualBoard::hideEvent(QHideEvent* e){cancelInput();m_flying=false;m_timer.stop();QWidget::hideEvent(e);}
void CasualBoard::showEvent(QShowEvent* e){m_timer.start();QWidget::showEvent(e);}
void CasualBoard::paintEvent(QPaintEvent*){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.translate(origin());p.scale(scale(),scale());
    const bool dark=Theme::instance().mode()==Theme::Dark;
    QLinearGradient gradient(0,0,720,780);gradient.setColorAt(0,dark?QColor("#222b3d"):QColor("#e7eef8"));gradient.setColorAt(1,dark?QColor("#253b38"):QColor("#edf5df"));
    p.setPen(Qt::NoPen);p.setBrush(gradient);p.drawRoundedRect(QRectF(0,0,720,780),22,22);
    if(m_game=="jumpjump")drawJump(p);else drawSheep(p);
}
void CasualBoard::drawJump(QPainter& p){
    const auto& c=Theme::instance().colors();const auto& view=m_flying?m_before:m_jump;
    const QPointF base(360,510),camera=view.current().center;auto map=[&](QPointF point){return point-camera+base;};
    p.setPen(c.text);QFont font=p.font();font.setPixelSize(66);font.setBold(true);p.setFont(font);p.drawText(QRectF(40,55,640,90),Qt::AlignCenter,QString::number(m_jump.score()));
    font.setPixelSize(18);font.setBold(false);p.setFont(font);p.setPen(c.muted);p.drawText(QRectF(40,155,640,35),Qt::AlignCenter,"长按蓄力，松手跳跃。落在中心得 2 分。");
    for(auto platform:{view.next(),view.current()}){
        const auto pos=map(platform.center);p.setPen(Qt::NoPen);p.setBrush(QColor(20,30,50,25));p.drawEllipse(pos+QPointF(5,38),platform.rx+7,platform.ry);
        p.setBrush(platformColors[platform.color%5].darker(125));p.drawRoundedRect(QRectF(pos.x()-platform.rx,pos.y(),platform.rx*2,29),5,5);p.drawEllipse(pos+QPointF(0,29),platform.rx,platform.ry);
        p.setBrush(platformColors[platform.color%5]);p.drawEllipse(pos,platform.rx,platform.ry);p.setBrush(QColor(255,255,255,65));p.drawEllipse(pos,9.,5.5);
    }
    QPointF pawn=map(view.pawn());double arc=0,spin=0;
    if(m_flying){const double t=qBound(0.,m_animation.elapsed()/460.0,1.);pawn=map(m_before.pawn()+(m_jump.lastLanding()-m_before.pawn())*t);arc=std::sin(t*std::acos(-1.0))*105;spin=t*360;}
    const double compression=m_charging?qMin(1.,m_hold.elapsed()/1200.0)*.30:0;
    p.setPen(Qt::NoPen);p.setBrush(QColor(20,30,40,50));p.drawEllipse(pawn,13.,7.);
    p.save();p.translate(pawn-QPointF(0,arc+21));p.rotate(spin);p.scale(1+compression/2,1-compression);
    p.setBrush(QColor("#4f4974"));p.drawRoundedRect(QRectF(-10,-8,20,30),7,7);p.setBrush(QColor("#796ca5"));p.drawEllipse(QPointF(0,-18),12.,12.);p.restore();
    p.setPen(c.muted);font.setPixelSize(16);p.setFont(font);p.drawText(QRectF(80,633,560,30),Qt::AlignCenter,m_jump.lost()?"没站稳，再来一次吧！":m_charging?"正在蓄力……":m_flying?"跳跃中":m_jump.perfect()?"正中靶心！ +2":"鼠标左键 / 空格：按住，再松开");
    p.setPen(Qt::NoPen);p.setBrush(QColor(100,100,130,35));p.drawRoundedRect(QRectF(160,683,400,15),7,7);
    if(m_charging){p.setBrush(platformColors[0]);p.drawRoundedRect(QRectF(160,683,400*qMin(1.,m_hold.elapsed()/1200.0),15),7,7);}
}
void CasualBoard::drawSheep(QPainter& p){
    const auto& c=Theme::instance().colors();QFont f=p.font();f.setPixelSize(17);p.setFont(f);p.setPen(c.muted);
    p.drawText(QRectF(20,13,680,36),Qt::AlignCenter,m_sheep.lost()?"七格已满：可以撤回、移出，或重新开始":m_sheep.won()?"全部消除，过关啦！":"只能选最上层；三个相同图案自动消除");
    for(const auto& t:m_sheep.tiles())if(!t.removed)tile(p,t.rect,t.kind,m_sheep.exposed(t.id));
    if(!m_sheep.reserve().isEmpty()){
        p.setPen(c.muted);p.drawText(QRectF(30,588,200,30),Qt::AlignRight,"移出区 · 点击取回");
        for(int n=0;n<m_sheep.reserve().size();++n)tile(p,QRectF(248+n*72,570,64,64),m_sheep.tiles()[m_sheep.reserve()[n]].kind,true);
    }
    for(int n=0;n<7;++n){const QRectF cell(116+n*70,660,64,64);p.setPen(QPen(QColor("#aaa68a"),1.5));p.setBrush(QColor(110,110,80,35));p.drawRoundedRect(cell,10,10);
        if(n<m_sheep.tray().size())tile(p,cell,m_sheep.tiles()[m_sheep.tray()[n]].kind,true);}
    p.setPen(c.muted);p.drawText(QRectF(40,738,640,28),Qt::AlignCenter,QString("暂存槽 %1 / 7 · 相同图案会排列在一起").arg(m_sheep.tray().size()));
}
CasualGamePage::CasualGamePage(const QString& game,QWidget* parent):QWidget(parent,Qt::Window),m_game(game){
    setObjectName(game+"Page");const bool jump=game=="jumpjump";const QString title=jump?"跳一跳":"羊了个羊 · 休闲版";
    setWindowTitle(title+" · NEXTALK");resize(1080,900);setMinimumSize(640,540);
    auto* root=new QVBoxLayout(this);root->setContentsMargins(20,14,20,14);auto* top=new QHBoxLayout;
    auto* back=button("返回小游戏","ghost");back->setObjectName(game+"Back");top->addWidget(back);top->addWidget(label(title,22));top->addStretch();
    m_full=button("退出全屏（Esc）");m_full->setObjectName(game+"FullScreen");top->addWidget(m_full);root->addLayout(top);
    connect(back,&QPushButton::clicked,this,&QWidget::close);connect(m_full,&QPushButton::clicked,this,&CasualGamePage::toggleFullScreen);
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);connect(escape,&QShortcut::activated,this,[this]{if(isFullScreen())toggleFullScreen();});
    auto* full=new QShortcut(QKeySequence(Qt::Key_F11),this);connect(full,&QShortcut::activated,this,&CasualGamePage::toggleFullScreen);
    auto* actions=new QHBoxLayout;QHBoxLayout* tools=nullptr;auto* restart=button("重新开始","primary");restart->setObjectName(game+"Restart");actions->addWidget(restart);
    if(!jump){
        m_level=new QComboBox;m_level->addItems({"第 1 关 · 热身","第 2 关 · 挑战"});m_level->setObjectName("sheepLevel");actions->addWidget(m_level);
        m_undo=button("撤回一次");m_shuffle=button("洗牌一次");m_reserve=button("移出三张");m_next=button("下一关");
        m_undo->setObjectName("sheepUndo");m_shuffle->setObjectName("sheepShuffle");m_reserve->setObjectName("sheepReserve");m_next->setObjectName("sheepNext");
        tools=new QHBoxLayout;tools->addWidget(m_undo);tools->addWidget(m_shuffle);tools->addWidget(m_reserve);tools->addWidget(m_next);tools->addStretch();
    }
    actions->addStretch();root->addLayout(actions);if(tools)root->addLayout(tools);m_status=label({},14,"muted");m_status->setObjectName(game+"Status");m_status->setWordWrap(true);root->addWidget(m_status);
    m_board=new CasualBoard(game);root->addWidget(m_board,1);
    auto* help=label(jump?"单人离线 · 长按鼠标左键或空格蓄力，松开跳跃。Esc / F11 切换全屏，离开窗口取消蓄力。":"单人离线 · 点击亮起的方块，三个相同图案消除；七格满时失败。道具每关各一次，移出区的方块可点击取回。",12,"muted");help->setWordWrap(true);root->addWidget(help);
    connect(restart,&QPushButton::clicked,this,[this]{m_board->restart(m_level?m_level->currentIndex()+1:1);m_board->setFocus();});
    connect(m_board,&CasualBoard::changed,this,&CasualGamePage::refresh);
    if(!jump){
        connect(m_level,&QComboBox::currentIndexChanged,this,[this](int n){m_board->restart(n+1);m_board->setFocus();});
        connect(m_undo,&QPushButton::clicked,m_board,&CasualBoard::undo);connect(m_shuffle,&QPushButton::clicked,m_board,&CasualBoard::shuffle);connect(m_reserve,&QPushButton::clicked,m_board,&CasualBoard::moveToReserve);
        connect(m_next,&QPushButton::clicked,this,[this]{m_level->setCurrentIndex(1);});
    }
    refresh();
}
void CasualGamePage::setScope(const QString& scope){
    if(m_scope==scope)return;hide();m_scope=scope;QSettings s;s.beginGroup("casualGames/"+scope+"/"+m_game);
    m_best=qMax(0,s.value("best",0).toInt());m_wins=qMax(0,s.value("wins",0).toInt());m_wasWon=false;
    if(m_level){QSignalBlocker block(m_level);m_level->setCurrentIndex(0);}m_board->restart();
}
void CasualGamePage::refresh(){
    bool save=false;
    if(m_game=="jumpjump"){
        if(m_board->jumpGame().score()>m_best){m_best=m_board->jumpGame().score();save=true;}
        m_status->setText(QString("本局 %1 分 · 最高 %2 分 · %3").arg(m_board->jumpGame().score()).arg(m_best).arg(m_board->jumpGame().lost()?"点击重新开始再挑战":"站稳再出发"));
    }else{
        const auto& g=m_board->sheepGame();if(g.won()&&!m_wasWon){++m_wins;save=true;}m_wasWon=g.won();
        m_status->setText(QString("第 %1 关 · 剩余 %2 张 · 已完成 %3 关 · %4").arg(g.level()).arg(g.remaining()).arg(m_wins).arg(g.won()?"过关啦":g.lost()?"暂存槽已满":"优先凑齐相同图案"));
        m_undo->setEnabled(g.canUndo());m_shuffle->setEnabled(g.canShuffle());m_reserve->setEnabled(g.canReserve());m_next->setEnabled(g.won()&&g.level()==1);
    }
    if(save&&!m_scope.isEmpty()){QSettings s;s.beginGroup("casualGames/"+m_scope+"/"+m_game);s.setValue("best",m_best);s.setValue("wins",m_wins);s.sync();}
}
void CasualGamePage::toggleFullScreen(){m_board->cancelInput();if(isFullScreen()){showNormal();m_full->setText("全屏（F11）");}else{showFullScreen();m_full->setText("退出全屏（Esc）");}m_board->setFocus();}
void CasualGamePage::openWindow(){showFullScreen();m_full->setText("退出全屏（Esc）");raise();activateWindow();m_board->setFocus();}
void CasualGamePage::closeEvent(QCloseEvent* event){m_board->cancelInput();hide();emit returned();event->accept();}
}
