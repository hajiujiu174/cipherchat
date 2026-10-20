#include "miniappspage.h"
#include "components.h"
#include "pagewidgets.h"
#include "scientificcalculator.h"
#include "gomokupage.h"
#include "services/chatservice.h"
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
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
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

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
                    {"web", "网页应用", "添加网页地址，在系统浏览器中打开。", "apps"},
                    {"gomoku", "双人五子棋", "创建在线房间，发送消息邀请好友一起对弈。", "apps"}};

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
    for (const auto& app : apps) {
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
        catalog->addWidget(row);
        connect(open, &QPushButton::clicked, this, [this, id] { openApp(id); });
        connect(favorite, &QPushButton::clicked, this, [this, id](bool checked) {
            m_favorites.removeAll(id);
            if (checked)
                m_favorites.append(id);
            save();
            refreshCatalog();
        });
    }
    m_empty = label("没有找到匹配的小程序", 14, "muted");
    catalog->addWidget(m_empty);
    catalog->addStretch();
    m_status = label({}, 12, "muted");
    m_status->setWordWrap(true);
    m_status->setContentsMargins(38, 0, 38, 12);
    root->addWidget(m_status);
    m_stack->addWidget(scrollPage(body));
    auto makeApp = [this](const QString& title, const QString& subtitle, QVBoxLayout** layout) {
        auto* page = pageBase(title, subtitle, layout);
        auto* back = button("返回小程序", "ghost", "back");
        back->setObjectName("miniAppBack_" + QString::number(m_stack->count()));
        (*layout)->insertWidget(0, back, 0, Qt::AlignLeft);
        connect(back, &QPushButton::clicked, this, [this] {
            save();
            m_stack->setCurrentIndex(0);
        });
        m_stack->addWidget(scrollPage(page));
    };
    QVBoxLayout* notes;
    makeApp("便签", "自动保存 · 仅保存在当前账号的本机空间", &notes);
    m_note = new QPlainTextEdit;
    m_note->setObjectName("miniAppNote");
    m_note->setPlaceholderText("写下想法、灵感或临时记录……");
    m_note->setMinimumHeight(240);
    notes->addWidget(m_note, 1);
    connect(m_note, &QPlainTextEdit::textChanged, this, [this] { m_saveTimer->start(); });
    QVBoxLayout* todos;
    makeApp("待办清单", "输入事项后按回车添加，勾选方框标记完成。", &todos);
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
    makeApp("科学计算器", "三角函数、对数与幂运算 · 支持角度 / 弧度切换", &calculator);
    calculator->setSpacing(10);
    m_calculator = new ScientificCalculator;
    m_expression = m_calculator->expression();
    calculator->addWidget(m_calculator, 0, Qt::AlignHCenter);
    calculator->addStretch();
    QVBoxLayout* web;
    makeApp("网页应用", "输入第三方网页应用地址，使用系统默认浏览器打开。", &web);
    m_webAddress = new QLineEdit;
    m_webAddress->setObjectName("miniAppWebAddress");
    m_webAddress->setPlaceholderText("https://");
    m_webAddress->setMaxLength(2048);
    web->addWidget(m_webAddress);
    auto* openWeb = button("在浏览器中打开", "primary");
    openWeb->setObjectName("miniAppWebOpen");
    web->addWidget(openWeb, 0, Qt::AlignLeft);
    m_webStatus = label("地址按当前账号保存在本机。网页登录由浏览器处理。", 13, "muted");
    m_webStatus->setObjectName("miniAppWebStatus");
    m_webStatus->setWordWrap(true);
    web->addWidget(m_webStatus);
    web->addStretch();
    auto launchWeb = [this] {
        const QUrl url(m_webAddress->text().trimmed(), QUrl::StrictMode);
        if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty()) {
            m_webStatus->setText("请输入完整的 HTTPS 网页地址，地址中不能包含用户名或密码。");
            return;
        }
        save();
        m_webStatus->setText(QDesktopServices::openUrl(url) ? "已交给系统浏览器打开。"
                                                            : "无法打开浏览器，请检查系统默认浏览器设置。");
    };
    connect(openWeb, &QPushButton::clicked, this, launchWeb);
    connect(m_webAddress, &QLineEdit::returnPressed, this, launchWeb);
    connect(m_webAddress, &QLineEdit::textChanged, this, [this] { m_saveTimer->start(); });
    QVBoxLayout* gomoku;
    makeApp("双人五子棋", "和好友下一局 · 通过好友消息同步棋盘", &gomoku);
    gomoku->setSpacing(10);
    m_gomoku=new GomokuPage(service);gomoku->addWidget(m_gomoku);
    connect(m_gomoku,&GomokuPage::activated,this,[this]{openApp("gomoku");});
    connect(m_gomoku,&GomokuPage::chatRequested,this,&MiniAppsPage::chatRequested);
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
    m_stack->setCurrentIndex(0);
    load();
}
void MiniAppsPage::load() {
    QSettings settings;
    settings.beginGroup("miniApps/" + m_scope);
    const QSignalBlocker noteBlock(m_note), todoBlock(m_todos), webBlock(m_webAddress);
    m_webAddress->setText(settings.value("webAddress").toString());
    m_webStatus->setText("地址按当前账号保存在本机。网页登录由浏览器处理。");
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
    settings.setValue("webAddress", m_webAddress->text());
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
    for (const auto& id : m_recent)
        for (const auto& app : apps)
            if (id == app.id)
                recent.append(app.title);
    int visible = 0;
    for (const auto& app : apps) {
        bool favorite = m_favorites.contains(app.id);
        auto* star = m_stars.value(app.id);
        star->setChecked(favorite);
        star->setText(favorite ? "已收藏" : "收藏");
        if (favorite)
            favorites.append(app.title);
        bool matches = (QString(app.title) + app.description)
                           .contains(m_search->text().trimmed(), Qt::CaseInsensitive) &&
                       (m_filter->currentIndex() != 1 || favorite) &&
                       (m_filter->currentIndex() != 2 || m_recent.contains(app.id));
        m_cards.value(app.id)->setVisible(matches);
        visible += matches;
    }
    m_recentLabel->setText("最近使用：" + (recent.isEmpty() ? QString("暂无") : recent.join("、")) +
                           "\n我的收藏：" + (favorites.isEmpty() ? QString("暂无") : favorites.join("、")));
    m_empty->setVisible(visible == 0);
}
void MiniAppsPage::openApp(const QString& id) {
    for (int i = 0; i < int(sizeof(apps) / sizeof(apps[0])); ++i)
        if (id == apps[i].id) {
            m_recent.removeAll(id);
            m_recent.prepend(id);
            save();
            refreshCatalog();
            m_stack->setCurrentIndex(i + 1);
            if(id=="gomoku")m_status->setText("在线对弈通过 CipherChat 好友消息同步");
            if (i == 0)
                m_note->setFocus();
            else if (i == 1)
                m_todoInput->setFocus();
            else if (i == 2)
                m_expression->setFocus();
            else if (i == 3)
                m_webAddress->setFocus();
            return;
        }
}
void MiniAppsPage::openGomokuInvitation(const Message& message) {
    openApp("gomoku");m_gomoku->openInvitation(message);
}
void MiniAppsPage::updateTodoCount() {
    int done = 0;
    for (int i = 0; i < m_todos->count(); ++i)
        done += m_todos->item(i)->checkState() == Qt::Checked;
    m_count->setText(QString("%1 项待完成 · %2 项已完成").arg(m_todos->count() - done).arg(done));
}
} // namespace Messager
