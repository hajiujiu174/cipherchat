#include "miniappspage.h"
#include "components.h"
#include "pagewidgets.h"
#include "scientificcalculator.h"
#include "gomokupage.h"
#include "tankpage.h"
#include "badmintonpage.h"
#include "chesspage.h"
#include "casualgamepage.h"
#include "services/chatservice.h"
#include "services/gomokuwire.h"
#include <QComboBox>
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace Messager {
namespace {
struct App {
    const char* id;
    const char* title;
    const char* description;
    const char* icon;
};
const App apps[] = {{"notes", "便签", "随手记录想法，内容自动保存在本机。", "file"},
                    {"todos", "待办清单", "安排今天要做的事，逐项勾选完成。", "users"},
                    {"calculator", "科学计算器", "三角函数、对数、幂运算与 DEG / RAD 模式。", "apps"},
                    {"games", "小游戏", "单人休闲、棋类对弈与双人动作游戏。", "apps"}};
const App gameApps[] = {{"gomoku", "双人五子棋", "创建在线房间，发送消息邀请好友一起对弈。", "apps"},
                       {"connectfour", "四子棋", "点击一列落子，横竖斜连成四子。", "apps"},
                       {"reversi", "黑白棋", "夹住并翻转对方棋子，支持落点提示和自动跳过。", "apps"},
                       {"tanktrouble", "坦克动荡", "双人迷宫坦克对战，炮弹碰墙反弹。", "apps"},
                       {"badminton", "火柴人羽毛球", "移动、跳跃、挥拍，单人练习或邀请好友在线对战。", "apps"},
                       {"chess", "国际象棋", "白方先行，支持易位、吃过路兵和升变，邀请好友对弈。", "apps"},
                       {"xiangqi", "中国象棋", "楚河汉界，红方先行，合法走法提示与将军判定。", "apps"},
                       {"jumpjump", "跳一跳", "单人离线 · 长按蓄力，松手跳跃，挑战最高分。", "apps"},
                       {"sheep", "羊了个羊", "单人休闲版 · 叠层三消、七格暂存槽与两关挑战。", "apps"}};
bool isGame(const QString& id) {
    for (const auto& app : gameApps) if (id == app.id) return true;
    return false;
}

} // namespace

MiniAppsPage::MiniAppsPage(ChatService* service, QWidget* parent) : QWidget(parent), m_service(service) {
    setObjectName("miniAppsPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget;
    root->addWidget(m_stack);
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(450);
    connect(m_saveTimer, &QTimer::timeout, this, &MiniAppsPage::save);
    QVBoxLayout* catalog;
    auto* body = pageBase("小程序", "让常用的小工具，留在聊天身边。", &catalog);
    m_search = new QLineEdit;
    m_search->setObjectName("miniAppSearch");
    m_search->setPlaceholderText("搜索小程序");
    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(m_search, 1);
    m_filter = new QComboBox;
    m_filter->setObjectName("miniAppFilter");
    m_filter->addItems({"全部应用", "我的收藏", "最近使用"});
    searchRow->addWidget(m_filter);
    catalog->addLayout(searchRow);
    m_recentLabel = label({}, 13, "muted");
    m_recentLabel->setWordWrap(true);
    catalog->addWidget(m_recentLabel);
    auto addAppCard = [this](const App& app, QVBoxLayout* layout) {
        QString id = app.id;
        auto* row = card();
        row->setObjectName("miniAppCard_" + id);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(20, 18, 20, 18);
        h->setSpacing(16);
        auto* open = button(app.title, "primary", app.icon);
        open->setObjectName("miniAppOpen_" + id);
        open->setMinimumWidth(110);
        h->addWidget(open);
        auto* description = label(app.description, 13, "muted");
        description->setWordWrap(true);
        h->addWidget(description, 1);
        auto* favorite = button("收藏", "chip");
        favorite->setCheckable(true);
        favorite->setObjectName("miniAppFavorite_" + id);
        h->addWidget(favorite);
        m_cards[id] = row;
        m_stars[id] = favorite;
        layout->addWidget(row);
        connect(open, &QPushButton::clicked, this, [this, id] { openApp(id); });
        connect(favorite, &QPushButton::clicked, this, [this, id](bool checked) {
            m_favorites.removeAll(id);
            if (checked)
                m_favorites.append(id);
            save();
            refreshCatalog();
        });
    };
    for (const auto& app : apps) addAppCard(app, catalog);
    m_empty = label("没有找到匹配的小程序", 14, "muted");
    catalog->addWidget(m_empty);
    catalog->addStretch();
    m_status = label({}, 12, "muted");
    m_status->setWordWrap(true);
    m_status->setContentsMargins(38, 0, 38, 12);
    root->addWidget(m_status);
    m_stack->addWidget(scrollPage(body));
    auto makeApp = [this](const QString& id, const QString& title, const QString& subtitle, QVBoxLayout** layout) {
        auto* page = pageBase(title, subtitle, layout);
        auto* back = button(isGame(id) ? "返回小游戏" : "返回小程序", "ghost", "back");
        back->setObjectName(isGame(id) ? "miniGameBack_" + id : "miniAppBack_" + QString::number(m_stack->count()));
        (*layout)->insertWidget(0, back, 0, Qt::AlignLeft);
        connect(back, &QPushButton::clicked, this, [this, id] {
            if (isGame(id)) openApp("games");
            else { save(); m_stack->setCurrentIndex(0); }
        });
        auto* container = scrollPage(page);
        container->setObjectName("miniAppContainer_" + id);
        m_pages[id] = container;
        m_stack->addWidget(container);
    };
    QVBoxLayout* notes;
    makeApp("notes", "便签", "自动保存 · 仅保存在当前账号的本机空间", &notes);
    m_note = new QPlainTextEdit;
    m_note->setObjectName("miniAppNote");
    m_note->setPlaceholderText("写下想法、灵感或临时记录……");
    m_note->setMinimumHeight(240);
    notes->addWidget(m_note, 1);
    connect(m_note, &QPlainTextEdit::textChanged, this, [this] { m_saveTimer->start(); });
    QVBoxLayout* todos;
    makeApp("todos", "待办清单", "输入事项后按回车添加，勾选方框标记完成。", &todos);
    auto* addRow = new QHBoxLayout;
    m_todoInput = new QLineEdit;
    m_todoInput->setObjectName("miniAppTodoInput");
    m_todoInput->setPlaceholderText("下一件要做的事");
    m_todoInput->setMaxLength(300);
    auto* add = button("添加", "primary");
    add->setObjectName("miniAppTodoAdd");
    addRow->addWidget(m_todoInput, 1);
    addRow->addWidget(add);
    todos->addLayout(addRow);
    m_todos = new QListWidget;
    m_todos->setObjectName("miniAppTodos");
    m_todos->setMinimumHeight(200);
    todos->addWidget(m_todos, 1);
    m_count = label({}, 13, "muted");
    todos->addWidget(m_count);
    auto* clear = button("清理已完成");
    clear->setObjectName("miniAppTodoClear");
    todos->addWidget(clear, 0, Qt::AlignLeft);
    auto addTodo = [this] {
        auto text = m_todoInput->text().trimmed();
        if (text.isEmpty())
            return;
        auto* item = new QListWidgetItem(text, m_todos);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        m_todoInput->clear();
        updateTodoCount();
        save();
    };
    connect(add, &QPushButton::clicked, this, addTodo);
    connect(m_todoInput, &QLineEdit::returnPressed, this, addTodo);
    connect(m_todos, &QListWidget::itemChanged, this, [this] {
        updateTodoCount();
        save();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        for (int i = m_todos->count() - 1; i >= 0; --i)
            if (m_todos->item(i)->checkState() == Qt::Checked)
                delete m_todos->takeItem(i);
        updateTodoCount();
        save();
    });
    QVBoxLayout* calculator;
    makeApp("calculator", "科学计算器", "三角函数、对数与幂运算 · 支持角度 / 弧度切换", &calculator);
    calculator->setSpacing(10);
    m_calculator = new ScientificCalculator;
    m_expression = m_calculator->expression();
    calculator->addWidget(m_calculator, 0, Qt::AlignHCenter);
    calculator->addStretch();
    QVBoxLayout* games;
    makeApp("games", "小游戏", "选一个单人游戏放松，或邀请好友在线对战。", &games);
    for (const auto& app : gameApps) addAppCard(app, games);
    games->addStretch();
    QVBoxLayout* gomoku;
    makeApp("gomoku", "双人五子棋", "和好友下一局 · 通过好友消息同步棋盘", &gomoku);
    gomoku->setSpacing(10);
    m_gomoku=new GomokuPage(service);gomoku->addWidget(m_gomoku);
    m_gamePages["gomoku"]=m_gomoku;
    connect(m_gomoku,&GomokuPage::activated,this,[this]{openApp("gomoku");});
    connect(m_gomoku,&GomokuPage::chatRequested,this,&MiniAppsPage::chatRequested);
    for(const auto& id:QStringList{"connectfour","reversi"}) {
        QVBoxLayout* layout;
        makeApp(id, GomokuWire::title(id),"好友在线对战 · 创建房间，发送聊天邀请",&layout);
        auto* page=new GomokuPage(service,nullptr,id);
        layout->addWidget(page);m_gamePages[id]=page;
        connect(page,&GomokuPage::activated,this,[this,id]{openApp(id);});
        connect(page,&GomokuPage::chatRequested,this,&MiniAppsPage::chatRequested);
    }
    QVBoxLayout* tanks;
    makeApp("tanktrouble", "坦克动荡", "迷宫弹射 · 双人实时对战", &tanks);
    m_tank=new TankPage(service,this);
    auto* launchTank=button("打开全屏游戏窗口","primary");launchTank->setObjectName("tankLaunchWindow");
    tanks->addWidget(label("坦克动荡在独立窗口中运行。按 Esc 退出全屏，F11 切换全屏。",14,"muted"));
    tanks->addWidget(launchTank,0,Qt::AlignLeft);tanks->addStretch();
    connect(launchTank,&QPushButton::clicked,m_tank,&TankPage::openWindow);
    connect(m_tank,&TankPage::returned,this,[this]{openApp("games");});
    connect(m_tank,&TankPage::chatRequested,this,&MiniAppsPage::chatRequested);
    QVBoxLayout* badminton;
    makeApp("badminton","火柴人羽毛球","先得 7 分获胜 · 单人练习与好友在线对战",&badminton);
    m_badminton=new BadmintonPage(service,this);
    auto* launchBadminton=button("打开全屏游戏窗口","primary");launchBadminton->setObjectName("badmintonLaunchWindow");
    badminton->addWidget(label("火柴人羽毛球在独立全屏窗口中运行。Esc 退出全屏，F11 切换全屏。",14,"muted"));
    badminton->addWidget(launchBadminton,0,Qt::AlignLeft);badminton->addStretch();
    connect(launchBadminton,&QPushButton::clicked,m_badminton,&BadmintonPage::openWindow);
    connect(m_badminton,&BadmintonPage::returned,this,[this]{openApp("games");});
    connect(m_badminton,&BadmintonPage::chatRequested,this,&MiniAppsPage::chatRequested);
    for(const auto& id:QStringList{"chess","xiangqi"}) {
        QVBoxLayout* layout;makeApp(id,GomokuWire::title(id),"在线好友对弈 · 独立全屏棋盘",&layout);
        auto* page=new ChessPage(service,this,id);m_chessPages[id]=page;
        auto* launch=button("打开全屏棋盘","primary");launch->setObjectName(id+"LaunchWindow");
        layout->addWidget(label("棋盘在独立窗口中打开，可用 Esc / F11 切换全屏。",14,"muted"));layout->addWidget(launch,0,Qt::AlignLeft);layout->addStretch();
        connect(launch,&QPushButton::clicked,page,&ChessPage::openWindow);
        connect(page,&ChessPage::returned,this,[this]{openApp("games");});
        connect(page,&ChessPage::activated,this,[this,id]{openApp(id);});
        connect(page,&ChessPage::chatRequested,this,&MiniAppsPage::chatRequested);
    }
    for(const auto& id:QStringList{"jumpjump","sheep"}){
        const QString title=id=="jumpjump"?"跳一跳":"羊了个羊";
        QVBoxLayout* layout;makeApp(id,title,"单人离线 · 独立全屏窗口",&layout);
        auto* page=new CasualGamePage(id,this);m_casualPages[id]=page;
        auto* launch=button("打开全屏游戏窗口","primary");launch->setObjectName(id+"LaunchWindow");
        layout->addWidget(label("无需好友在线，打开即可玩。Esc / F11 切换全屏。",14,"muted"));layout->addWidget(launch,0,Qt::AlignLeft);layout->addStretch();
        connect(launch,&QPushButton::clicked,page,&CasualGamePage::openWindow);
        connect(page,&CasualGamePage::returned,this,[this]{openApp("games");});
    }
    connect(m_search, &QLineEdit::textChanged, this, &MiniAppsPage::refreshCatalog);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &MiniAppsPage::refreshCatalog);
    connect(service, &ChatService::sessionChanged, this, &MiniAppsPage::updateScope);
    auto recolor = [this] {
        const auto& c = Theme::instance().colors();
        m_todos->setStyleSheet(
            QString("QListWidget { background:%1; color:%2; border:1px solid %3; border-radius:9px; "
                    "padding:8px; } QListWidget::item { padding:10px; } QListWidget::item:selected { "
                    "background:%4; color:%2; }")
                .arg(c.surface.name(), c.text.name(), c.border.name(), c.accentSoft.name()));
    };
    connect(&Theme::instance(), &Theme::changed, this, recolor);
    recolor();
    updateScope();
}
MiniAppsPage::~MiniAppsPage() {
    save();
}
void MiniAppsPage::updateScope() {
    auto config = m_service->configuration();
    QJsonArray identity{m_service->isDemo() ? "demo" : "account",
                        config["host"],
                        config["port"],
                        config["dataDirectory"],
                        config["accountScope"],
                        m_service->username()};
    auto scope =
        QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                                     QCryptographicHash::Sha256)
                                .toHex());
    if (scope == m_scope)
        return;
    if (!m_scope.isEmpty())
        save();
    m_saveTimer->stop();
    m_scope = scope;
    for(auto* page:m_casualPages)page->setScope(scope);
    m_stack->setCurrentIndex(0);
    load();
}
void MiniAppsPage::load() {
    QSettings settings;
    settings.beginGroup("miniApps/" + m_scope);
    const QSignalBlocker noteBlock(m_note), todoBlock(m_todos);
    m_note->setPlainText(settings.value("note").toString());
    m_todos->clear();
    const auto list = QJsonDocument::fromJson(settings.value("todos").toByteArray()).array();
    for (const auto& value : list) {
        auto object = value.toObject();
        auto* item = new QListWidgetItem(object["text"].toString(), m_todos);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(object["done"].toBool() ? Qt::Checked : Qt::Unchecked);
    }
    m_favorites = settings.value("favorites").toStringList();
    m_recent = settings.value("recent").toStringList();
    m_todoInput->clear();
    m_calculator->reset();
    updateTodoCount();
    refreshCatalog();
    m_status->setText("内置应用 · 本机保存，不进行云同步");
}
void MiniAppsPage::save() {
    if (m_scope.isEmpty())
        return;
    m_saveTimer->stop();
    QSettings settings;
    settings.beginGroup("miniApps/" + m_scope);
    settings.setValue("note", m_note->toPlainText());
    QJsonArray list;
    for (int i = 0; i < m_todos->count(); ++i) {
        auto* item = m_todos->item(i);
        list.append(QJsonObject{{"text", item->text()}, {"done", item->checkState() == Qt::Checked}});
    }
    settings.setValue("todos", QJsonDocument(list).toJson(QJsonDocument::Compact));
    settings.setValue("favorites", m_favorites);
    settings.setValue("recent", m_recent);
    settings.sync();
    m_status->setText(settings.status() == QSettings::NoError ? "已保存到本机 · 不进行云同步"
                                                              : "保存失败，请检查本机设置目录是否可写");
}
void MiniAppsPage::refreshCatalog() {
    QStringList recent, favorites;
    const bool favoriteGame = std::any_of(m_favorites.cbegin(), m_favorites.cend(), isGame);
    const bool recentGame = std::any_of(m_recent.cbegin(), m_recent.cend(), isGame);
    for (const auto& id : m_recent)
        for (const auto& app : apps)
            if (id == app.id) recent.append(app.title);
    if (recentGame && !recent.contains("小游戏")) recent.append("小游戏");
    auto updateStar = [this](const App& app) {
        const bool favorite = m_favorites.contains(app.id);
        auto* star = m_stars.value(app.id);
        star->setChecked(favorite);
        star->setText(favorite ? "已收藏" : "收藏");
    };
    for (const auto& app : gameApps) updateStar(app);
    int visible = 0;
    for (const auto& app : apps) {
        updateStar(app);
        const bool favorite = m_favorites.contains(app.id) || (QString(app.id) == "games" && favoriteGame);
        const bool used = m_recent.contains(app.id) || (QString(app.id) == "games" && recentGame);
        if (favorite) favorites.append(app.title);
        const bool matches = (QString(app.title) + app.description)
            .contains(m_search->text().trimmed(), Qt::CaseInsensitive) &&
            (m_filter->currentIndex() != 1 || favorite) &&
            (m_filter->currentIndex() != 2 || used);
        m_cards.value(app.id)->setVisible(matches);
        visible += matches;
    }
    m_recentLabel->setText("最近使用：" + (recent.isEmpty() ? QString("暂无") : recent.join("、")) +
                         "\n我的收藏：" + (favorites.isEmpty() ? QString("暂无") : favorites.join("、")));
    m_empty->setVisible(visible == 0);
}
void MiniAppsPage::openApp(const QString& id) {
    if (!m_pages.contains(id)) return;
    if (isGame(id)) {
        m_recent.removeAll("games");
        m_recent.prepend("games");
    }
    m_recent.removeAll(id);
    m_recent.prepend(id);
    save();
    refreshCatalog();
    m_stack->setCurrentWidget(m_pages.value(id));
    if (isGame(id)) m_status->setText("好友在线对战 · 可以从聊天邀请直接加入房间");
    else if (id == "games") m_status->setText("单人游戏可离线运行；好友对战需要双方在线。");
    if(m_casualPages.contains(id))m_status->setText("单人离线 · 成绩按当前账号保存在本机");
    if (id == "notes") m_note->setFocus();
    else if (id == "todos") m_todoInput->setFocus();
    else if (id == "calculator") m_expression->setFocus();
    else if (id == "tanktrouble") m_tank->openWindow();
    else if (id == "badminton") m_badminton->openWindow();
    else if (m_chessPages.contains(id)) m_chessPages[id]->openWindow();
    else if(m_casualPages.contains(id))m_casualPages[id]->openWindow();
}
void MiniAppsPage::openGomokuInvitation(const Message& message) {
    const auto o=GomokuWire::decode(message.text);
    if(o.isEmpty() || o["type"]!="invite")return;
    const auto id=GomokuWire::gameId(o);
    if(id=="tanktrouble") {openApp(id);m_tank->openInvitation(message);return;}
    if(id=="badminton") {openApp(id);m_badminton->openInvitation(message);return;}
    if(m_chessPages.contains(id)) {openApp(id);m_chessPages[id]->openInvitation(message);return;}
    if(!m_gamePages.contains(id))return;
    openApp(id);m_gamePages[id]->openInvitation(message);
}
void MiniAppsPage::updateTodoCount() {
    int done = 0;
    for (int i = 0; i < m_todos->count(); ++i)
        done += m_todos->item(i)->checkState() == Qt::Checked;
    m_count->setText(QString("%1 项待完成 · %2 项已完成").arg(m_todos->count() - done).arg(done));
}
} // namespace Messager

