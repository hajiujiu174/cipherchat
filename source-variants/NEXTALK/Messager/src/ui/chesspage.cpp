#include "chesspage.h"
#include "components.h"
#include "services/gomokuwire.h"
#include <QComboBox>
#include <QCloseEvent>
#include <QInputDialog>
#include <QShortcut>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
namespace Messager {
ChessBoard::ChessBoard(QWidget* parent,const QString& game):QWidget(parent) {
    m_room.game=game;ChessController::replay(m_room,{});setObjectName(game+"Board");setMinimumSize(320,360);
    setAccessibleName(GomokuWire::title(game)+"棋盘");connect(&Theme::instance(),&Theme::changed,this,qOverload<>(&QWidget::update));
}
QRectF ChessBoard::gridRect() const {
    const int c=m_room.columns(),r=m_room.rows();const bool x=m_room.game=="xiangqi";
    const double step=qMin((width()-64.0)/(x?c-1:c),(height()-64.0)/(x?r-1:r));
    const double w=step*(x?c-1:c),h=step*(x?r-1:r);return QRectF((width()-w)/2,(height()-h)/2,w,h);
}
QPoint ChessBoard::cellCenter(int x,int y) const {
    if(m_flipped){x=m_room.columns()-1-x;y=m_room.rows()-1-y;}const auto r=gridRect();const bool xi=m_room.game=="xiangqi";
    return QPoint(qRound(r.left()+(x+(xi?0:.5))*r.width()/(xi?8:8)),qRound(r.top()+(y+(xi?0:.5))*r.height()/(xi?9:8)));
}
void ChessBoard::setRoom(const ChessRoom& room,bool canMove,bool flipped) {
    if(room.id!=m_room.id||room.moves!=m_room.moves||!canMove)m_selected=-1;
    m_room=room;m_canMove=canMove;m_flipped=flipped;m_legal=canMove?ChessController::legalMoves(room):QStringList{};
    setCursor(canMove?Qt::PointingHandCursor:Qt::ArrowCursor);update();
}
void ChessBoard::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const auto g=gridRect();const bool xi=m_room.game=="xiangqi";
    const double step=g.width()/8;const QColor ink("#654a2e");
    if(xi){
        p.setBrush(QColor("#e6c58e"));p.setPen(QPen(ink,1));p.drawRoundedRect(g.adjusted(-step*.46,-step*.46,step*.46,step*.46),12,12);
        for(int y=0;y<10;++y)p.drawLine(cellCenter(0,y),cellCenter(8,y));
        for(int x=0;x<9;++x){if(x==0||x==8)p.drawLine(cellCenter(x,0),cellCenter(x,9));else{p.drawLine(cellCenter(x,0),cellCenter(x,4));p.drawLine(cellCenter(x,5),cellCenter(x,9));}}
        p.drawLine(cellCenter(3,0),cellCenter(5,2));p.drawLine(cellCenter(5,0),cellCenter(3,2));p.drawLine(cellCenter(3,7),cellCenter(5,9));p.drawLine(cellCenter(5,7),cellCenter(3,9));
        QFont f("Microsoft YaHei");f.setPixelSize(int(step*.32));p.setFont(f);p.drawText(QRectF(g.left(),g.top()+4*step,g.width(),step),Qt::AlignCenter,"楚 河                         汉 界");
    }else{
        p.setPen(Qt::NoPen);for(int y=0;y<8;++y)for(int x=0;x<8;++x){p.setBrush((x+y)%2?QColor("#739181"):QColor("#eee4d1"));p.drawRect(QRectF(g.left()+x*step,g.top()+y*step,step,step));}
    }
    const int cols=m_room.columns();
    if(!m_room.moves.isEmpty()){
        const auto code=m_room.moves.last();for(int square:{ChessRules::from(m_room.game,code),ChessRules::to(m_room.game,code)})if(square>=0){p.setPen(QPen(QColor("#d8a934"),3));p.setBrush(QColor(255,220,80,60));const auto c=cellCenter(square%cols,square/cols);if(xi)p.drawEllipse(c,step*.38,step*.38);else p.drawRect(QRectF(c.x()-step/2,c.y()-step/2,step,step));}
    }
    for(int y=0;y<m_room.rows();++y)for(int x=0;x<cols;++x){
        const int square=y*cols+x,piece=m_room.board[square];const auto c=cellCenter(x,y);
        if(square==m_selected){p.setBrush(QColor(88,137,239,80));p.setPen(QPen(QColor("#326bc4"),3));p.drawEllipse(c,step*.43,step*.43);}
        if(!piece)continue;
        if(xi){
            p.setPen(QPen(ink,1.5));p.setBrush(QColor("#ffdfaa"));p.drawEllipse(c,step*.39,step*.39);p.setBrush(Qt::NoBrush);p.drawEllipse(c,step*.33,step*.33);
            static const QString red[]={"","兵","傌","相","俥","仕","帥","炮"},black[]={"","卒","馬","象","車","士","將","砲"};
            QFont f("Microsoft YaHei");f.setPixelSize(int(step*.48));f.setBold(true);p.setFont(f);p.setPen(piece>0?QColor("#ad2e24"):QColor("#29352c"));p.drawText(QRectF(c.x()-step/2,c.y()-step/2,step,step),Qt::AlignCenter,piece>0?red[piece]:black[-piece]);
        }else{
            static const int glyph[]={0,0x2659,0x2658,0x2657,0x2656,0x2655,0x2654};
            const QString text(QChar(glyph[std::abs(piece)]+(piece<0?6:0)));QFont f("Segoe UI Symbol");f.setPixelSize(int(step*.77));
            QFontMetricsF metrics(f);QPainterPath path;path.addText(c.x()-metrics.horizontalAdvance(text)/2,c.y()+(metrics.ascent()-metrics.descent())/2,f,text);
            p.setPen(QPen(QColor("#27352e"),1));p.setBrush(piece>0?QColor("#fffef8"):QColor("#26352e"));p.drawPath(path);
        }
    }
    if(m_selected>=0){p.setPen(QPen(QColor("#1e61aa"),2));p.setBrush(QColor(49,113,183,160));QSet<int> seen;
        for(const auto& code:m_legal)if(ChessRules::from(m_room.game,code)==m_selected){int z=ChessRules::to(m_room.game,code);if(seen.contains(z))continue;seen.insert(z);p.drawEllipse(cellCenter(z%cols,z/cols),step*.12,step*.12);}}
    p.setPen(Theme::instance().colors().muted);QFont axis("Microsoft YaHei");axis.setPixelSize(12);p.setFont(axis);
    for(int x=0;x<cols;++x){const int file=m_flipped?cols-1-x:x;p.drawText(QRectF(g.left()+(x+(xi?0:.5))*step-12,g.bottom()+step*(xi?.45:.05),24,20),Qt::AlignCenter,QString(QChar('a'+file)));}
}
void ChessBoard::mousePressEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton||!m_canMove)return;const int cols=m_room.columns();int square=-1;const auto g=gridRect();const double step=g.width()/8;
    for(int y=0;y<m_room.rows();++y)for(int x=0;x<cols;++x){const auto c=cellCenter(x,y);if(std::abs(c.x()-e->position().x())<step*.46&&std::abs(c.y()-e->position().y())<step*.46)square=y*cols+x;}
    if(square<0)return;
    const int piece=m_room.board[square];if(piece&&(piece>0?1:2)==m_room.turn){m_selected=square;update();return;}
    if(m_selected<0)return;QStringList choices;
    for(const auto& code:m_legal)if(ChessRules::from(m_room.game,code)==m_selected&&ChessRules::to(m_room.game,code)==square)choices.append(code);
    if(choices.isEmpty())return;QString chosen=choices.first();
    if(choices.size()>1){bool ok=false;const auto answer=QInputDialog::getItem(this,"兵升变","选择升变棋子",{"后（Q）","车（R）","象（B）","马（N）"},0,false,&ok);if(!ok)return;const int at=QStringList{"后（Q）","车（R）","象（B）","马（N）"}.indexOf(answer);for(const auto& code:choices)if(code.endsWith(QString("qrbn")[at]))chosen=code;}
    m_selected=-1;emit moveChosen(chosen);update();
}
ChessPage::ChessPage(ChatService* service, QWidget* parent, const QString& game) : QWidget(parent,Qt::Window), m_service(service), m_gameId(game) {
    setObjectName(game+"Page");
    setWindowTitle(GomokuWire::title(game)+" · NEXTALK");resize(1280,900);setMinimumSize(720,600);
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(20,16,20,16);
    auto* top=new QHBoxLayout;auto* back=button("返回小游戏","ghost");back->setObjectName("chessWindowBack");
    m_fullScreen=button("退出全屏（Esc）");m_fullScreen->setObjectName("chessFullScreen");top->addWidget(back);top->addWidget(label(GomokuWire::title(game),20));top->addStretch();top->addWidget(m_fullScreen);outer->addLayout(top);
    connect(back,&QPushButton::clicked,this,[this]{close();});connect(m_fullScreen,&QPushButton::clicked,this,&ChessPage::toggleFullScreen);
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);connect(escape,&QShortcut::activated,this,[this]{if(isFullScreen())toggleFullScreen();});
    auto* full=new QShortcut(QKeySequence(Qt::Key_F11),this);connect(full,&QShortcut::activated,this,&ChessPage::toggleFullScreen);
    auto* body = new QHBoxLayout;outer->addLayout(body,1);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(18);
    m_board = new ChessBoard(nullptr,game);
    body->addWidget(m_board,1);
    auto* side = new QWidget;
    side->setMinimumWidth(260);side->setMaximumWidth(330);
    auto* root = new QVBoxLayout(side);
    body->addWidget(side);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);
    m_game = new ChessController(service, this, game);
    auto* invite = new QVBoxLayout;
    m_friends = new QComboBox;
    m_friends->setObjectName("chessFriends");
    m_friends->setMinimumWidth(160);
    invite->addWidget(m_friends, 1);
    m_create = button("创建房间并邀请好友", "primary");
    m_create->setObjectName("chessCreate");
    invite->addWidget(m_create);
    root->addLayout(invite);
    auto* rooms = new QVBoxLayout;
    m_rooms = new QComboBox;
    m_rooms->setObjectName("chessRooms");
    m_rooms->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_rooms->setMinimumContentsLength(12);
    rooms->addWidget(m_rooms, 1);
    m_join = button("加入房间", "primary");
    m_join->setObjectName("chessJoin");
    rooms->addWidget(m_join);
    root->addLayout(rooms);
    m_players = label({}, 13, "muted");
    m_players->setWordWrap(true);
    m_players->setTextFormat(Qt::PlainText);
    root->addWidget(m_players);
    m_status = label({}, 16, nullptr, true);
    m_status->setObjectName("chessStatus");
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    auto* actions = new QVBoxLayout;
    m_sync = button("重新同步");
    m_sync->setObjectName("chessSync");
    actions->addWidget(m_sync);
    m_chat = button("查看好友聊天");
    m_chat->setObjectName("chessChat");
    actions->addWidget(m_chat);
    m_leave = button("退出 / 认输");
    m_leave->setObjectName("chessLeave");
    actions->addWidget(m_leave);
    actions->addStretch();
    root->addLayout(actions);
    m_error = label({}, 12, "danger");
    m_error->setObjectName("chessError");
    m_error->setWordWrap(true);
    root->addWidget(m_error);
    const auto ruleText=game=="chess"?QString("白方先行。点选棋子，再点击提示落点；支持王车易位、吃过路兵与四种升变。将死获胜，逼和及子力不足和棋。\n休闲规则：三次重复或连续 50 回合未吃子/动兵自动和棋，无需申请。"):
        QString("红方先行。点选棋子，再点击提示落点；遵守蹩马腿、塞象眼、炮架、九宫、过河和将帅照面规则。将死或困毙均判负。\n休闲规则：三次重复时长将方判负，其余重复局面和棋；连续 120 手未吃子和棋，不作长捉赛事裁决。");
    auto* rules = label(ruleText,
                        12, "muted");
    rules->setWordWrap(true);
    root->addWidget(rules);
    connect(m_create, &QPushButton::clicked, this,
            [this] { m_game->create(m_friends->currentData().toString()); });
    connect(m_join, &QPushButton::clicked, this, [this] { m_game->join(m_selected); });
    connect(m_sync, &QPushButton::clicked, this, [this] { m_game->sync(m_selected); });
    connect(m_leave, &QPushButton::clicked, this, [this] { m_game->leave(m_selected); });
    connect(m_chat, &QPushButton::clicked, this, [this] {
        const auto r = m_game->room(m_selected);
        if (!r.id.isEmpty())
            emit chatRequested(m_service->username() == r.black ? r.white : r.black);
    });
    connect(m_board,&ChessBoard::moveChosen,this,[this](const QString& code){m_game->move(m_selected,code);});
    connect(m_rooms, &QComboBox::currentIndexChanged, this, [this] {
        m_selected = m_rooms->currentData().toString();
        refresh();
    });
    connect(m_game, &ChessController::changed, this, &ChessPage::refresh);
    connect(m_game, &ChessController::error, this,
            [this](const QString& error) { m_error->setText(error); });
    connect(m_game, &ChessController::roomOpened, this, [this](const QString& id) {
        m_selected = id;
        refresh();
        openWindow();emit activated();
    });
    connect(service, &ChatService::contactsChanged, this, &ChessPage::refreshFriends);
    connect(service, &ChatService::sessionChanged, this, [this] {
        refreshFriends();
        refresh();
    });
    refreshFriends();
    refresh();
}
void ChessPage::refreshFriends() {
    const QSignalBlocker block(m_friends);
    const auto previous = m_friends->currentData().toString();
    m_friends->clear();
    for (const auto& c : m_service->contacts())
        if (c.friendContact && c.username != m_service->username())
            m_friends->addItem(c.username + (c.online ? " · 在线" : " · 离线"), c.username);
    if (m_friends->findData(previous) >= 0)
        m_friends->setCurrentIndex(m_friends->findData(previous));
    m_create->setEnabled(m_service->online() && !m_service->isDemo() && m_friends->count() > 0);
}
void ChessPage::refresh() {
    const QSignalBlocker block(m_rooms);
    m_rooms->clear();
    for (const auto& r : m_game->rooms())
        m_rooms->addItem(r.id.left(8) + " · " + r.black + " vs " + r.white, r.id);
    int index = m_rooms->findData(m_selected);
    if (index < 0 && m_rooms->count())
        index = 0;
    m_rooms->setCurrentIndex(index);
    m_selected = m_rooms->currentData().toString();
    auto r = m_game->room(m_selected);
    if(r.id.isEmpty()) {r.game=m_gameId;ChessController::replay(r,{});}
    const auto me = m_service->username();
    const bool available = m_service->online() && !m_service->isDemo();
    m_join->setEnabled(available && !r.id.isEmpty() && me == r.white && !r.started && !r.closed &&
                       !r.pending);
    m_join->setVisible(!r.id.isEmpty() && me==r.white && !r.started && !r.closed && !r.winner);
    m_sync->setEnabled(available && !r.id.isEmpty());
    m_chat->setEnabled(!r.id.isEmpty());
    m_leave->setEnabled(available && !r.id.isEmpty() && !r.closed && !r.winner && !r.pending);
    m_error->setText(m_game->lastError());
    m_board->setRoom(r,m_game->canMove(r.id),me==r.white&&!r.id.isEmpty());
    const auto first=m_gameId=="chess"?QString("白方"):QString("红方");
    const auto second=QString("黑方");
    m_players->setText(r.id.isEmpty() ? "选择好友创建房间，或在聊天中点击"+GomokuWire::title(m_gameId)+"邀请加入。"
                                      : "房间 " + r.id.left(8) + " · "+first+"：" + r.black + "　"+second+"：" +
                                            r.white + "　你是" + (me == r.black ? first : second));
    QString status;
    if (!available)
        status = "请登录后进行双人在线对弈";
    else if (r.id.isEmpty())
        status = "准备好，邀请好友下一局";
    else if (r.closed)
        status = r.resigned.isEmpty() ? "房间已关闭" : r.resigned + " 已退出，对局结束";
    else if (r.winner)
        status = r.winner == 3 ? "和棋" : (r.winner == 1 ? r.black : r.white) + " 获胜";
    else if (r.pending)
        status = "等待对方确认……";
    else if (!r.started)
        status = me == r.black ? "邀请已发送，等待好友加入" : "收到邀请，点击加入房间";
    else
        status = QString("第 %1 手 · ").arg(r.moves.size() + 1) +
                 (m_game->canMove(r.id) ? "轮到你走棋" : "等待对方走棋");
    if(!r.reason.isEmpty())status+=" · "+r.reason;else if(r.check)status+=" · 将军！";
    m_status->setText(status);
}
void ChessPage::openInvitation(const Message& message) {
    m_game->openInvitation(message);
}
void ChessPage::openWindow(){showFullScreen();m_fullScreen->setText("退出全屏（Esc）");raise();activateWindow();}
void ChessPage::toggleFullScreen(){if(isFullScreen()){showNormal();m_fullScreen->setText("全屏（F11）");}else{showFullScreen();m_fullScreen->setText("退出全屏（Esc）");}}
void ChessPage::closeEvent(QCloseEvent* e){QWidget::closeEvent(e);emit returned();}
} // namespace Messager
