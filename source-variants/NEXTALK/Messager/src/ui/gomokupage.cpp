#include "gomokupage.h"
#include "components.h"
#include "services/gomokuwire.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
namespace Messager {
GomokuBoard::GomokuBoard(QWidget* parent, const QString& game) : QWidget(parent) {
    m_room.game=game;
    GomokuController::replay(m_room,{});
    setObjectName(game+"Board");
    setMinimumSize(320, 320);
    setMaximumSize(480, 480);
    setAccessibleName(GomokuWire::title(game)+"棋盘");
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}
QRectF GomokuBoard::gridRect() const {
    const double side = qMin(width(), height()) - 56;
    const double h=m_room.game=="connectfour" ? side*6/7 : side;
    return QRectF((width() - side) / 2, (height() - h) / 2, side, h);
}
QPoint GomokuBoard::cellCenter(int x, int y) const {
    const auto grid = gridRect();
    if (m_room.game!="gomoku")
        return QPoint(qRound(grid.left()+(x+.5)*grid.width()/m_room.columns()),qRound(grid.top()+(y+.5)*grid.height()/m_room.rows()));
    return QPoint(qRound(grid.left() + x * grid.width() / 14), qRound(grid.top() + y * grid.height() / 14));
}
void GomokuBoard::setRoom(const GomokuRoom& room, bool canMove) {
    m_room = room;
    m_canMove = canMove;
    setCursor(canMove ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}
void GomokuBoard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto grid = gridRect();
    if (m_room.game!="gomoku") {
        const int columns=m_room.columns(),rows=m_room.rows();
        const double step=grid.width()/columns;
        const bool connect=m_room.game=="connectfour";
        p.setPen(Qt::NoPen);
        p.setBrush(connect?QColor("#2855ad"):QColor("#237358"));
        p.drawRoundedRect(grid.adjusted(-8,-8,8,8),12,12);
        const auto legal= m_canMove ? GomokuController::legalMoves(m_room) : QVector<QPoint>{};
        for(int y=0;y<rows;++y)for(int x=0;x<columns;++x) {
            const auto center=cellCenter(x,y);const int player=m_room.board[y*columns+x];
            if (!connect) { p.setBrush((x+y)%2?QColor("#287e60"):QColor("#308768"));p.drawRect(QRectF(grid.left()+x*step,grid.top()+y*step,step,step)); }
            if (player || connect) {
                p.setBrush(!player?Theme::instance().colors().surface : connect?(player==1?QColor("#f3c74e"):QColor("#ec6b61")):(player==1?QColor("#20272d"):QColor("#f4f1e9")));
                p.setPen(QPen(player?QColor("#00000033"):QColor("#204791"),1));
                p.drawEllipse(center,step*.39,step*.39);p.setPen(Qt::NoPen);
            }
            if (legal.contains({x,y})) {p.setBrush(connect?QColor("#9caee0"):QColor("#a7d9af"));p.drawEllipse(center,step*.09,step*.09);}
        }
        if(!m_room.moves.isEmpty()) {
            const auto last=m_room.moves.last();p.setPen(QPen(QColor("#ffffff"),2));p.setBrush(Qt::NoBrush);
            p.drawEllipse(cellCenter(last.x(),last.y()),step*.30,step*.30);
        }
        p.setPen(Theme::instance().colors().muted);
        for(int x=0;x<columns;++x)p.drawText(QRectF(grid.left()+x*step,grid.top()-26,step,20),Qt::AlignCenter,QString(QChar('A'+x)));
        for(int y=0;y<rows;++y)p.drawText(QRectF(grid.left()-25,grid.top()+y*step,20,step),Qt::AlignCenter,QString::number(y+1));
        return;
    }
    const double step = grid.width() / 14;
    p.setPen(QPen(QColor("#9a784c"), 1));
    p.setBrush(Theme::instance().dark() ? QColor("#c0a176") : QColor("#efd7ad"));
    p.drawRoundedRect(grid.adjusted(-24, -24, 24, 24), 12, 12);
    p.setPen(QPen(QColor("#85653f"), .8));
    for (int i = 0; i < 15; ++i) {
        const auto a = cellCenter(i, 0), b = cellCenter(0, i);
        p.drawLine(a, cellCenter(i, 14));
        p.drawLine(b, cellCenter(14, i));
        p.drawText(QRectF(a.x() - 10, grid.top() - 22, 20, 18), Qt::AlignCenter, QString(QChar('A' + i)));
        p.drawText(QRectF(grid.left() - 24, b.y() - 9, 20, 18), Qt::AlignCenter, QString::number(i + 1));
    }
    p.setBrush(QColor("#85653f"));
    p.setPen(Qt::NoPen);
    for (auto pos : QVector<QPoint>{{3, 3}, {11, 3}, {7, 7}, {3, 11}, {11, 11}})
        p.drawEllipse(cellCenter(pos.x(), pos.y()), 2.5, 2.5);
    for (int y = 0; y < 15; ++y)
        for (int x = 0; x < 15; ++x)
            if (m_room.board[y * 15 + x]) {
                const bool black = m_room.board[y * 15 + x] == 1;
                const auto center = cellCenter(x, y);
                const double radius = step * .42;
                QRadialGradient gradient(center - QPointF(radius * .3, radius * .4), radius * 1.5);
                gradient.setColorAt(0, black ? QColor("#646464") : Qt::white);
                gradient.setColorAt(1, black ? QColor("#111111") : QColor("#dddddd"));
                p.setBrush(gradient);
                p.setPen(QPen(black ? QColor("#171717") : QColor("#aaa49c"), .7));
                p.drawEllipse(center, radius, radius);
            }
    if (!m_room.moves.isEmpty()) {
        const auto last = m_room.moves.last();
        p.setBrush(QColor("#da564c"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(cellCenter(last.x(), last.y()), 2.5, 2.5);
    }
}
void GomokuBoard::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_canMove)
        return;
    const auto grid = gridRect();
    const auto pos = e->position();
    if(m_room.game!="gomoku") {
        if(!grid.contains(pos))return;
        int x=int((pos.x()-grid.left())/grid.width()*m_room.columns());
        int y=int((pos.y()-grid.top())/grid.height()*m_room.rows());
        emit cellClicked(x,y);return;
    }
    const double step = grid.width() / 14;
    int x = qRound((pos.x() - grid.left()) / step), y = qRound((pos.y() - grid.top()) / step);
    if (x < 0 || x > 14 || y < 0 || y > 14)
        return;
    const auto center = cellCenter(x, y);
    if (std::abs(center.x() - pos.x()) > step * .45 || std::abs(center.y() - pos.y()) > step * .45)
        return;
    emit cellClicked(x, y);
}
GomokuPage::GomokuPage(ChatService* service, QWidget* parent, const QString& game) : QWidget(parent), m_service(service), m_gameId(game) {
    setObjectName(game+"Page");
    auto* body = new QHBoxLayout(this);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(18);
    m_board = new GomokuBoard(nullptr,game);
    body->addWidget(m_board, 0, Qt::AlignTop);
    auto* side = new QWidget;
    side->setMinimumWidth(260);
    auto* root = new QVBoxLayout(side);
    body->addWidget(side, 1);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);
    m_game = new GomokuController(service, this, game);
    auto* invite = new QVBoxLayout;
    m_friends = new QComboBox;
    m_friends->setObjectName("gomokuFriends");
    m_friends->setMinimumWidth(160);
    invite->addWidget(m_friends, 1);
    m_create = button("创建房间并邀请好友", "primary");
    m_create->setObjectName("gomokuCreate");
    invite->addWidget(m_create);
    root->addLayout(invite);
    auto* rooms = new QVBoxLayout;
    m_rooms = new QComboBox;
    m_rooms->setObjectName("gomokuRooms");
    m_rooms->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_rooms->setMinimumContentsLength(12);
    rooms->addWidget(m_rooms, 1);
    m_join = button("加入房间", "primary");
    m_join->setObjectName("gomokuJoin");
    rooms->addWidget(m_join);
    root->addLayout(rooms);
    m_players = label({}, 13, "muted");
    m_players->setWordWrap(true);
    m_players->setTextFormat(Qt::PlainText);
    root->addWidget(m_players);
    m_status = label({}, 16, nullptr, true);
    m_status->setObjectName("gomokuStatus");
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    auto* actions = new QVBoxLayout;
    m_sync = button("重新同步");
    m_sync->setObjectName("gomokuSync");
    actions->addWidget(m_sync);
    m_chat = button("查看好友聊天");
    m_chat->setObjectName("gomokuChat");
    actions->addWidget(m_chat);
    m_leave = button("退出 / 认输");
    m_leave->setObjectName("gomokuLeave");
    actions->addWidget(m_leave);
    actions->addStretch();
    root->addLayout(actions);
    m_error = label({}, 12, "danger");
    m_error->setObjectName("gomokuError");
    m_error->setWordWrap(true);
    root->addWidget(m_error);
    const auto ruleText=game=="connectfour"?QString("7 × 6 · 黄色先行 · 点击一列，棋子自动落到底部。横、竖、斜连成四子获胜。\n双方均需使用支持四子棋的版本。"):
                        game=="reversi"?QString("8 × 8 · 黑方先行 · 夹住对方棋子即可翻转，浅绿点提示可下位置。无合法落子时自动跳过，双方都无棋可下时按棋子数定胜负。\n双方均需使用支持黑白棋的版本。"):
                        QString("15 × 15 · 黑方先行 · 五子及以上连线获胜 · 无禁手\n通过好友消息同步，双方均需使用支持五子棋的版本。");
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
    connect(m_board, &GomokuBoard::cellClicked, this,
            [this](int x, int y) { m_game->move(m_selected, x, y); });
    connect(m_rooms, &QComboBox::currentIndexChanged, this, [this] {
        m_selected = m_rooms->currentData().toString();
        refresh();
    });
    connect(m_game, &GomokuController::changed, this, &GomokuPage::refresh);
    connect(m_game, &GomokuController::error, this,
            [this](const QString& error) { m_error->setText(error); });
    connect(m_game, &GomokuController::roomOpened, this, [this](const QString& id) {
        m_selected = id;
        refresh();
        emit activated();
    });
    connect(service, &ChatService::contactsChanged, this, &GomokuPage::refreshFriends);
    connect(service, &ChatService::sessionChanged, this, [this] {
        refreshFriends();
        refresh();
    });
    refreshFriends();
    refresh();
}
void GomokuPage::refreshFriends() {
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
void GomokuPage::refresh() {
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
    if(r.id.isEmpty()) {r.game=m_gameId;GomokuController::replay(r,{});}
    const auto me = m_service->username();
    const bool available = m_service->online() && !m_service->isDemo();
    m_join->setEnabled(available && !r.id.isEmpty() && me == r.white && !r.started && !r.closed &&
                       !r.pending);
    m_join->setVisible(!r.id.isEmpty() && me==r.white && !r.started && !r.closed && !r.winner);
    m_sync->setEnabled(available && !r.id.isEmpty());
    m_chat->setEnabled(!r.id.isEmpty());
    m_leave->setEnabled(available && !r.id.isEmpty() && !r.closed && !r.winner && !r.pending);
    m_error->setText(m_game->lastError());
    m_board->setRoom(r, m_game->canMove(r.id));
    const auto first=m_gameId=="connectfour"?QString("黄方"):QString("黑方");
    const auto second=m_gameId=="connectfour"?QString("红方"):QString("白方");
    m_players->setText(r.id.isEmpty() ? "选择好友创建房间，或在聊天中点击"+GomokuWire::title(m_gameId)+"邀请加入。"
                                      : "房间 " + r.id.left(8) + " · "+first+"：" + r.black + "　"+second+"：" +
                                            r.white + "　你是" + (me == r.black ? first : second));
    if(m_gameId=="reversi" && !r.id.isEmpty())m_players->setText(m_players->text()+QString("\n棋子数：黑 %1 · 白 %2").arg(std::count(r.board.begin(),r.board.begin()+64,1)).arg(std::count(r.board.begin(),r.board.begin()+64,2)));
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
                 (m_game->canMove(r.id) ? "轮到你落子" : "等待对方落子");
    m_status->setText(status);
}
void GomokuPage::openInvitation(const Message& message) {
    m_game->openInvitation(message);
}
} // namespace Messager
