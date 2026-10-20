#include "mainwindow.h"
#include "accountdialog.h"
#include "profilepage.h"
#include "momentspage.h"
#include "miniappspage.h"
#include "services/gomokuwire.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace Messager {
namespace {
void clearLayout(QLayout* layout) {
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget())
            delete w;
        if (auto* l = item->layout())
            clearLayout(l);
        delete item;
    }
}
QFrame* line() {
    auto* f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    auto p = f->palette();
    p.setColor(QPalette::WindowText, Theme::instance().colors().border);
    f->setPalette(p);
    f->setFixedHeight(1);
    return f;
}
} // namespace
MainWindow::~MainWindow() {
    delete m_accountDialog.data();
}
AccountDialog* MainWindow::accountDialog() const {
    return m_accountDialog.data();
}
MainWindow::MainWindow(AppController* c, QWidget* parent) : QMainWindow(parent), m_controller(c) {
    setWindowTitle(c->service()->isDemo() ? "Messager · 本地 UI 原型" : "CipherChat · Messager");
    setMinimumSize(960, 640);
    resize(1280, 800);
    setWindowIcon(applicationIcon());
    m_model = new ConversationModel(c->service(), this);
    m_filter = new ConversationFilter(this);
    m_filter->setSourceModel(m_model);
    auto* central = new PaperFrame;
    setCentralWidget(central);
    auto* h = new QHBoxLayout(central);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(buildRail());
    m_pages = new QStackedWidget;
    m_pages->setObjectName("pages");
    h->addWidget(m_pages, 1);
    m_pages->addWidget(buildChats());
    m_pages->addWidget(buildContacts());
    m_pages->addWidget(buildFiles());
    m_pages->addWidget(buildNotifications());
    m_pages->addWidget(buildSettings());
    m_profilePage=new ProfilePage(c->service());m_pages->addWidget(m_profilePage);
    m_momentsPage=new MomentsPage(c->service());m_pages->addWidget(m_momentsPage);
    m_miniApps=new MiniAppsPage(c->service());m_pages->addWidget(m_miniApps);
    connect(m_miniApps,&MiniAppsPage::chatRequested,this,[this](const QString& peer){m_controller->openDirectConversation(peer);showPage(0);});
    connect(m_momentsPage,&MomentsPage::profileRequested,this,&MainWindow::showProfile);
    connect(m_profilePage,&ProfilePage::chatRequested,this,[this](const QString& user){
        m_controller->openDirectConversation(user);
        if(m_controller->selectedId()=="peer:"+user)showPage(0);
    });
    connect(m_profilePage,&ProfilePage::backRequested,this,[this]{showPage(m_profileReturnPage);});
    connect(c->service(),&ChatService::profileSummaryChanged,this,[this](const QString& user){
        const auto value=m_controller->service()->profileSummary(user);
        for(auto* widget:findChildren<QWidget*>())
            if(widget->property("profileUser").toString()==user)
                if(auto* avatar=dynamic_cast<Avatar*>(widget))avatar->setImage(QByteArray::fromBase64(value["avatar"].toString().toLatin1()));
        if(user==m_controller->service()->username())if(auto* b=findChild<QPushButton*>("profileButton")){auto image=profileAvatar(value["avatar"].toString(),80);b->setIcon(image.isNull()?QIcon{}:QIcon(image));b->setIconSize({38,38});b->setText(image.isNull()?QStringLiteral("我"):QString{});b->setToolTip(value["nickname"].toString(user)+" · 个人资料");}
        const auto c=m_controller->service()->conversation(m_controller->selectedId());
        if(!c.group&&c.peer==user){m_chatTitle->setText(value["nickname"].toString(user));if(auto* avatar=dynamic_cast<Avatar*>(m_headerAvatar))avatar->setImage(QByteArray::fromBase64(value["avatar"].toString().toLatin1()));}
    });
    buildDetails();
    connect(c, &AppController::selectionChanged, this, [this] { syncSelection(); });
    connect(c->service(), &ChatService::messagesChanged, this, [this](const QString& id) {
        if (id == m_controller->selectedId())
            renderMessages();
    });
    connect(c->service(), &ChatService::transfersChanged, this, &MainWindow::refreshTransfers);
    connect(c->service(), &ChatService::conversationsChanged, this, [this] {
        syncSelection();
        refreshSession();
    });
    connect(c->service(), &ChatService::sessionChanged, this, [this] {
        refreshSession();
        if (m_controller->service()->online())
            statusBar()->clearMessage();
    });
    connect(c->service(), &ChatService::operationError, this,
            [this](const QString& message) { statusBar()->showMessage(message, 12000); });
    connect(m_filter, &QAbstractItemModel::modelReset, this, [this] {
        m_emptyList->setVisible(!m_filter->rowCount());
        syncListSelection();
    });
    connect(m_filter, &QAbstractItemModel::rowsInserted, this, [this] {
        m_emptyList->setVisible(m_filter->rowCount() == 0);
        syncListSelection();
    });
    connect(m_filter, &QAbstractItemModel::rowsRemoved, this, [this] {
        m_emptyList->setVisible(m_filter->rowCount() == 0);
        syncListSelection();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        m_conversations->viewport()->update();
        m_messageRenderKey.clear();
        renderMessages();
    });
    auto* find = new QShortcut(QKeySequence::Find, this);
    connect(find, &QShortcut::activated, this, [this] {
        showPage(0);
        m_search->setFocus();
        m_search->selectAll();
    });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] {
        setDetailsVisible(false);
        if (m_messageSearch->isVisible()) {
            m_messageSearch->clear();
            m_messageSearch->hide();
        }
    });
    m_reflowTimer = new QTimer(this);
    m_reflowTimer->setSingleShot(true);
    m_reflowTimer->setInterval(35);
    connect(m_reflowTimer, &QTimer::timeout, this, [this] { renderMessages(); });
    refreshTransfers();
    syncSelection();
    showPage(0);
    refreshSession();
}
QWidget* MainWindow::buildRail() {
    auto* rail = new PaperFrame;
    rail->setProperty("surface", "rail");
    rail->setFixedWidth(68);
    auto* v = new QVBoxLayout(rail);
    v->setContentsMargins(10, 18, 10, 18);
    v->setSpacing(7);
    auto* brand = new QPushButton;
    brand->setFixedSize(44, 44);
    brand->setProperty("variant", "primary");
    brand->setIcon(applicationIcon());
    brand->setIconSize({44, 44});
    brand->setStyleSheet("padding:0;border:0;background:transparent;");
    brand->setAccessibleName("Messager 品牌");
    v->addWidget(brand, 0, Qt::AlignHCenter);
    v->addSpacing(10);
    QStringList icons = {"chat", "users", "moments", "apps", "file", "bell", "settings"},
                names = {"聊天", "联系人", "朋友圈", "小程序", "文件传输", "通知", "设置"};
    const QVector<int> pages={0,1,6,7,2,3,4};
    for (int i = 0; i < icons.size(); ++i) {
        if (pages[i] == 4)
            v->addStretch();
        auto* b = new IconButton(icons[i], names[i]);
        b->setObjectName(pages[i]==6?QStringLiteral("momentsNav"):"nav_" + QString::number(pages[i]));
        b->setProperty("pageIndex",pages[i]);
        b->setFixedSize(46, 46);
        b->setIconSize({23, 23});
        b->setCheckable(true);
        b->setProperty("variant", "chip");
        v->addWidget(b, 0, Qt::AlignHCenter);
        m_nav.push_back(b);
        const QString icon = icons[i];
        auto recolor = [b, icon] {
            b->setIcon(glyph(icon, b->isChecked() ? Theme::instance().colors().accent
                                                  : Theme::instance().colors().muted));
        };
        connect(b, &QPushButton::toggled, b, recolor);
        connect(&Theme::instance(), &Theme::changed, b, recolor);
        connect(b, &QPushButton::clicked, this, [this, page=pages[i]] { showPage(page); });
    }
    v->addSpacing(7);
    auto* profile = button("我", "chip");
    profile->setObjectName("profileButton");
    profile->setFixedSize(40, 40);
    profile->setToolTip(m_controller->service()->isDemo() ? "演示账号 / 登录原型" : "CipherChat 账号与连接");
    profile->setAccessibleName("个人资料");
    v->addWidget(profile, 0, Qt::AlignHCenter);
    connect(profile, &QPushButton::clicked, this, [this]{auto* s=m_controller->service();if(s->online())showProfile(s->username());else showLogin();});
    return rail;
}
QWidget* MainWindow::buildChats() {
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setObjectName("chatSplitter");
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    auto* listPanel = new PaperFrame;
    listPanel->setProperty("surface", "white");
    listPanel->setMinimumWidth(260);
    listPanel->setMaximumWidth(420);
    auto* lv = new QVBoxLayout(listPanel);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(0);
    auto* tools = new QWidget;
    auto* tv = new QVBoxLayout(tools);
    tv->setContentsMargins(22, 25, 22, 14);
    tv->setSpacing(17);
    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(label("聊天", 26, nullptr, true));
    titleRow->addStretch();
    auto* newChat = new IconButton("plus", "新建会话 / 选择联系人");
    titleRow->addWidget(newChat);
    connect(newChat, &QPushButton::clicked, this, [this] { showPage(1); });
    tv->addLayout(titleRow);
    m_search = new QLineEdit;
    m_search->setObjectName("conversationSearch");
    m_search->setPlaceholderText("搜索会话或消息");
    m_search->setAccessibleName("搜索会话");
    m_search->addAction(glyph("search", Theme::instance().colors().muted, 18), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    tv->addWidget(m_search);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& q) {
        m_filter->setQuery(q);
        m_emptyList->setVisible(!m_filter->rowCount());
        syncListSelection();
    });
    auto* filters = new QHBoxLayout;
    filters->setSpacing(3);
    auto* group = new QButtonGroup(this);
    QStringList modes = {"全部", "未查看", "群组"};
    for (int i = 0; i < 3; ++i) {
        auto* b = button(modes[i], "chip");
        b->setObjectName("filter_" + QString::number(i));
        b->setCheckable(true);
        if (!i)
            b->setChecked(true);
        group->addButton(b, i);
        filters->addWidget(b);
    }
    filters->addStretch();
    tv->addLayout(filters);
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        m_filterMode = id;
        m_filter->setMode(id);
        m_emptyList->setVisible(!m_filter->rowCount());
        syncListSelection();
    });
    lv->addWidget(tools);
    m_conversations = new QListView;
    m_conversations->setObjectName("conversationList");
    m_conversations->setAccessibleName("会话列表");
    m_conversations->setModel(m_filter);
    m_conversations->setItemDelegate(new ConversationDelegate(m_conversations));
    m_conversations->setMouseTracking(true);
    m_conversations->setSelectionMode(QAbstractItemView::SingleSelection);
    m_conversations->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lv->addWidget(m_conversations, 1);
    auto select = [this](const QModelIndex& i) {
        if (i.isValid())
            m_controller->selectConversation(i.data(ConversationModel::Id).toString());
    };
    connect(m_conversations, &QListView::clicked, this, select);
    connect(m_conversations, &QListView::activated, this, select);
    m_emptyList = label("没有找到匹配的会话\n换个关键词试试", 13, "muted");
    m_emptyList->setAlignment(Qt::AlignCenter);
    m_emptyList->setMinimumHeight(100);
    m_emptyList->hide();
    lv->addWidget(m_emptyList);
    auto* foot = label(m_controller->service()->statusText(), 11, "muted");
    m_railStatus = foot;
    foot->setWordWrap(true);
    foot->setContentsMargins(22, 16, 10, 18);
    lv->addWidget(foot);
    m_chat = new PaperFrame;
    m_chat->setObjectName("chatArea");
    m_chat->setMinimumWidth(520);
    m_chat->installEventFilter(this);
    auto* cv = new QVBoxLayout(m_chat);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    auto* header = new PaperFrame;
    header->setProperty("role", "header");
    header->setFixedHeight(64);
    auto* hh = new QHBoxLayout(header);
    hh->setContentsMargins(24, 9, 20, 9);
    hh->setSpacing(12);
    m_headerAvatar = new Avatar("D", QColor("#6554d9"), 40);
    hh->addWidget(m_headerAvatar);
    m_headerText = new QVBoxLayout;
    m_headerText->setSpacing(2);
    m_chatTitle = label("", 15, nullptr, true);
    m_chatTitle->setTextFormat(Qt::PlainText);m_chatTitle->installEventFilter(this);m_chatTitle->setCursor(Qt::PointingHandCursor);
    m_chatSubtitle = label("", 11, "muted");
    m_headerText->addWidget(m_chatTitle);
    m_headerText->addWidget(m_chatSubtitle);
    hh->addLayout(m_headerText, 1);
    auto* search = new IconButton("search", "搜索当前会话内容");
    search->setObjectName("messageSearchButton");
    hh->addWidget(search);
    auto* personal=button("个人资料");personal->setObjectName("chatProfileButton");hh->addWidget(personal);
    connect(personal,&QPushButton::clicked,this,[this]{const auto c=m_controller->service()->conversation(m_controller->selectedId());if(!c.group&&!c.peer.isEmpty())showProfile(c.peer);});
    auto* details = new IconButton("info", "会话详情");
    details->setObjectName("detailsButton");
    hh->addWidget(details);
    connect(details, &QPushButton::clicked, this, [this] { setDetailsVisible(!detailsVisible()); });
    cv->addWidget(header);
    auto* notice = label(m_controller->service()->isDemo() ? "演示工作区    ·    消息与文件均为本地模拟"
                                                           : m_controller->service()->statusText(),
                         11, "muted");
    m_sessionNotice = notice;
    notice->setAlignment(Qt::AlignCenter);
    notice->setContentsMargins(6, 10, 6, 5);
    cv->addWidget(notice);
    m_messageSearch = new QLineEdit;
    m_messageSearch->setObjectName("messageSearch");
    m_messageSearch->setPlaceholderText("筛选当前会话中的消息 · Esc 关闭");
    m_messageSearch->setClearButtonEnabled(true);
    m_messageSearch->hide();
    cv->addWidget(m_messageSearch);
    connect(search, &QPushButton::clicked, this, [this] {
        m_messageSearch->setVisible(!m_messageSearch->isVisible());
        if (m_messageSearch->isVisible())
            m_messageSearch->setFocus();
        else
            m_messageSearch->clear();
    });
    connect(m_messageSearch, &QLineEdit::textChanged, this, [this] { renderMessages(true); });
    m_scroll = new QScrollArea;
    m_scroll->setObjectName("messageScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* bar = m_scroll->verticalScrollBar();
    connect(bar, &QScrollBar::sliderMoved, this,
            [this, bar](int pos) { m_followTail = pos >= bar->maximum() - 24; });
    connect(bar, &QScrollBar::actionTriggered, this,
            [this, bar] { m_followTail = bar->sliderPosition() >= bar->maximum() - 24; });
    connect(bar, &QScrollBar::rangeChanged, this, [this, bar](int, int maximum) {
        if (m_followTail)
            bar->setValue(maximum);
    });
    cv->addWidget(m_scroll, 1);
    auto* composerOuter = new QWidget;
    auto* outer = new QVBoxLayout(composerOuter);
    outer->setContentsMargins(24, 10, 24, 14);
    outer->setSpacing(8);
    auto* composer = new PaperFrame;
    composer->setProperty("role", "composer");
    auto* comp = new QVBoxLayout(composer);
    comp->setContentsMargins(12, 9, 12, 9);
    comp->setSpacing(4);
    m_editor = new MessageEditor;
    comp->addWidget(m_editor);
    auto* actions = new QHBoxLayout;
    auto* attach = new IconButton("attach", m_controller->service()->isDemo() ? "选择文件 · 仅模拟传输"
                                                                              : "发送文件给当前联系人");
    attach->setObjectName("attachButton");
    actions->addWidget(attach);
    connect(attach, &QPushButton::clicked, this, &MainWindow::chooseFile);
    auto* simulate = new IconButton("spark", "模拟收到一条新消息");
    simulate->setVisible(m_controller->service()->isDemo());
    simulate->setObjectName("simulateMessage");
    actions->addWidget(simulate);
    connect(simulate, &QPushButton::clicked, m_controller, &AppController::simulateIncoming);
    actions->addStretch();
    m_send = button("发送  ↑", "primary");
    m_send->setObjectName("sendButton");
    m_send->setAccessibleName("发送消息");
    m_send->setEnabled(false);
    actions->addWidget(m_send);
    connect(m_send, &QPushButton::clicked, this, &MainWindow::submit);
    connect(m_editor, &MessageEditor::submitted, this, &MainWindow::submit);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] { refreshSession(); });
    comp->addLayout(actions);
    outer->addWidget(composer);
    auto* hint = label("Enter 发送   ·   Shift + Enter 换行   ·   Ctrl + F 搜索会话", 10, "muted");
    hint->setAlignment(Qt::AlignRight);
    outer->addWidget(hint);
    cv->addWidget(composerOuter);
    m_splitter->addWidget(listPanel);
    m_splitter->addWidget(m_chat);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({320, 890});
    return m_splitter;
}
void MainWindow::buildDetails() {
    m_details = new PaperFrame(m_chat);
    m_details->setObjectName("conversationDetails");
    m_details->setProperty("role", "detail");
    m_details->setFixedWidth(288);
    auto* v = new QVBoxLayout(m_details);
    v->setContentsMargins(22, 18, 22, 22);
    v->setSpacing(20);
    auto* top = new QHBoxLayout;
    top->addWidget(label("会话详情", 16, nullptr, true));
    top->addStretch();
    auto* close = new IconButton("close", "关闭详情");
    close->setObjectName("closeDetails");
    top->addWidget(close);
    connect(close, &QPushButton::clicked, this, [this] { setDetailsVisible(false); });
    v->addLayout(top);
    auto* detailContent = new QWidget;
    m_detailBody = new QVBoxLayout(detailContent);
    m_detailBody->setContentsMargins(0, 0, 0, 0);
    m_detailBody->setSpacing(17);
    auto* detailScroll = new QScrollArea;
    detailScroll->setWidgetResizable(true);
    detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailScroll->setWidget(detailContent);
    v->addWidget(detailScroll, 1);
    m_details->hide();
}
void MainWindow::bindProfileAvatar(Avatar* avatar,const QString& user) {
    avatar->setProperty("profileUser",user);
    avatar->setImage(QByteArray::fromBase64(m_controller->service()->profileSummary(user)["avatar"].toString().toLatin1()));
}
void MainWindow::refreshDetails() {
    if (!m_details)
        return;
    clearLayout(m_detailBody);
    auto c = m_controller->service()->conversation(m_controller->selectedId());
    auto* detailAvatar=new Avatar(c.initials,c.color,64);
    if(!c.group)bindProfileAvatar(detailAvatar,c.peer);
    m_detailBody->addWidget(detailAvatar,0,Qt::AlignHCenter);
    auto* n = label(c.name, 17, nullptr, true);
    n->setAlignment(Qt::AlignCenter);
    m_detailBody->addWidget(n);
    auto* s = label(c.subtitle, 12, "muted");
    s->setAlignment(Qt::AlignCenter);
    s->setWordWrap(true);
    m_detailBody->addWidget(s);
    m_detailBody->addWidget(line());
    m_detailBody->addWidget(label(c.group ? "群组成员 · 演示" : "联系人信息", 12, "eyebrow"));
    if (c.group && m_controller->service()->isDemo()) {
        for (const QString& name : QStringList{"林予 · 产品设计", "周屿 · 客户端开发", "许知夏 · 视觉设计",
                                               "陈序 · 项目协作", "我 · 演示账号"})
            m_detailBody->addWidget(label(name, 13));
    } else if (c.group) {
        for (const auto& member : c.members)
            m_detailBody->addWidget(label(member, 13));
    } else if (!m_controller->service()->isDemo()) {
        m_detailBody->addWidget(label(c.peer, 13));
        auto* profile=button("查看个人资料");m_detailBody->addWidget(profile);
        connect(profile,&QPushButton::clicked,this,[this,peer=c.peer]{showProfile(peer);});
    } else
        m_detailBody->addWidget(label("账号资料尚未接入服务", 12, "muted"));
    auto* files = button("查看共享文件", "ghost", "file");
    m_detailBody->addWidget(files);
    connect(files, &QPushButton::clicked, this, [this] { showPage(2); });
    auto* note = label(m_controller->service()->isDemo() ? "演示成员与共享文件。按 Esc 可关闭详情。"
                                                         : "按 Esc 可关闭详情。",
                       11, "muted");
    note->setWordWrap(true);
    m_detailBody->addWidget(note);
    m_detailBody->addStretch();
}
void MainWindow::showPage(int index) {
    if (!m_pages)
        return;
    m_pages->setCurrentIndex(index);
    for (int i = 0; i < m_nav.size(); ++i)
        m_nav[i]->setChecked(m_nav[i]->property("pageIndex").toInt() == index);
    if(index==6)m_momentsPage->openFeed();
    if (index != 0)
        setDetailsVisible(false);
    m_controller->service()->markViewed(index == 0 ? m_controller->selectedId() : QString());
}
void MainWindow::syncListSelection() {
    for (int r = 0; r < m_filter->rowCount(); ++r) {
        auto i = m_filter->index(r, 0);
        if (i.data(ConversationModel::Id).toString() == m_controller->selectedId()) {
            if (m_conversations->currentIndex() != i) {
                QSignalBlocker block(m_conversations->selectionModel());
                m_conversations->setCurrentIndex(i);
            }
            return;
        }
    }
    QSignalBlocker block(m_conversations->selectionModel());
    m_conversations->clearSelection();
    m_conversations->setCurrentIndex({});
}
void MainWindow::syncSelection() {
    const QString id = m_controller->selectedId();
    const bool changedConversation = m_displayedId != id;
    if (changedConversation) {
        if (!m_displayedId.isEmpty())
            m_drafts[m_displayedId] = m_editor->toPlainText();
        m_displayedId = id;
        m_editor->setPlainText(m_drafts.value(id));
        m_messageSearch->clear();
    }
    const auto c = m_controller->service()->conversation(id);
    m_chatTitle->setText(c.id.isEmpty() ? "选择一个会话" : c.group?c.name:m_controller->service()->profileSummary(c.peer).value("nickname").toString(c.name));
    m_chatSubtitle->setText(c.id.isEmpty() ? "从联系人打开私聊，或选择已有群聊" : c.subtitle);
    auto* headerLayout = qobject_cast<QHBoxLayout*>(m_headerAvatar->parentWidget()->layout());
    auto* avatar =
        new Avatar(c.id.isEmpty() ? "M" : c.initials, c.color.isValid() ? c.color : QColor("#6554d9"), 40);
    headerLayout->replaceWidget(m_headerAvatar, avatar);
    delete m_headerAvatar;
    m_headerAvatar = avatar;
    avatar->setObjectName("chatProfileAvatar");avatar->installEventFilter(this);avatar->setCursor(Qt::PointingHandCursor);
    if(!c.group)bindProfileAvatar(avatar,c.peer);
    if(auto* b=findChild<QPushButton*>("chatProfileButton"))b->setEnabled(!c.group&&!c.peer.isEmpty()&&m_controller->service()->online());
    syncListSelection();
    refreshDetails();
    renderMessages(changedConversation);
    refreshSession();
}
void MainWindow::renderMessages(bool forceBottom) {
    if (!m_scroll)
        return;
    auto* bar = m_scroll->verticalScrollBar();
    const int old = bar->value();
    if (forceBottom)
        m_followTail = true;
    const bool bottom = m_followTail;
    const auto messages = m_controller->service()->messages(m_controller->selectedId());
    // Reserve scrollbar width so its appearance cannot trigger another bubble reflow.
    const int availableWidth = m_scroll->maximumViewportSize().width() -
                               m_scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent) - 100;
    QByteArray renderKey;
    QDataStream key(&renderKey, QIODevice::WriteOnly);
    key << m_controller->selectedId() << m_messageSearch->text() << availableWidth
        << m_controller->service()->isDemo() << QDate::currentDate();
    for (const auto& message : messages) {
        if(GomokuWire::isControl(message.text))continue;
        key << message.id << message.sender << message.text << message.time << message.outgoing
            << message.file << int(message.state) << message.transportStatus << message.timestamp;
    }
    if (m_scroll->widget() && renderKey == m_messageRenderKey) {
        if (forceBottom)
            bar->setValue(bar->maximum());
        return;
    }
    QString anchorId;
    int anchorOffset = 0;
    if (!bottom && m_scroll->widget() && m_scroll->widget()->layout()) {
        auto* oldLayout = m_scroll->widget()->layout();
        for (int i = 0; i < oldLayout->count(); ++i) {
            auto* row = oldLayout->itemAt(i)->widget();
            if (row && row->property("messageId").isValid() && row->geometry().bottom() >= old) {
                anchorId = row->property("messageId").toString();
                anchorOffset = row->y() - old;
                break;
            }
        }
    }
    auto* body = new PaperFrame;
    auto* v = new QVBoxLayout(body);
    QHash<QString, QWidget*> rows;
    v->setContentsMargins(24, 18, 24, 20);
    v->setSpacing(14);
    if (m_controller->service()->isDemo()) {
        auto* date = label(QDate::currentDate().toString("M月d日") + " · 演示记录", 11, "muted");
        date->setAlignment(Qt::AlignCenter);
        v->addWidget(date);
    }
    QString previousDate;
    int count = 0;
    for (const auto& m : messages) {
        if(GomokuWire::isControl(m.text))continue;
        if (!m_messageSearch->text().isEmpty() &&
            !m.text.contains(m_messageSearch->text(), Qt::CaseInsensitive))
            continue;
        ++count;
        if (!m_controller->service()->isDemo()) {
            QString day = m.timestamp ? QDateTime::fromMSecsSinceEpoch(m.timestamp).toString("yyyy年M月d日")
                                      : "时间未记录";
            if (day != previousDate) {
                auto* date = label(day, 11, "muted");
                date->setAlignment(Qt::AlignCenter);
                v->addWidget(date);
                previousDate = day;
            }
        }
        auto* row = new QWidget;
        row->setProperty("messageId", m.id);
        rows.insert(m.id, row);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);
        h->setAlignment(Qt::AlignTop);
        if (m.outgoing)
            h->addStretch(1);
        else {
            auto* senderAvatar=new Avatar(m.sender.left(1),QColor(m.sender=="周屿"?"#b57d54":"#458995"),30);
            senderAvatar->setObjectName("messageAvatar_"+m.id);
            bindProfileAvatar(senderAvatar,m.sender);
            h->addWidget(senderAvatar,0,Qt::AlignTop);
        }
        auto* column = new QVBoxLayout;
        column->setSpacing(5);
        if (!m.outgoing)
            column->addWidget(label(m.sender, 11, "muted"));
        auto* bubble = new MessageBubble(m, availableWidth);
        bubble->setProperty("locatedMessage", m.id == m_highlightedMessageId);
        connect(bubble, &MessageBubble::openFileRequested, this, [this] { showPage(2); });
        connect(bubble,&MessageBubble::openGomokuRequested,this,[this,m]{showPage(7);m_miniApps->openGomokuInvitation(m);});
        column->addWidget(bubble, 0, m.outgoing ? Qt::AlignRight : Qt::AlignLeft);
        QString status = m.time;
        if (m.outgoing && !m.transportStatus.isEmpty())
            status += "  ·  " + m.transportStatus;
        else if (m.outgoing)
            status += "  ·  " + QString(m.state == SendState::Sending  ? "模拟发送中…"
                                        : m.state == SendState::Failed ? "模拟发送失败"
                                                                       : "本地已保存");
        auto* footer = label(status, 10, m.state == SendState::Failed ? "danger" : "muted");
        column->addWidget(footer, 0, m.outgoing ? Qt::AlignRight : Qt::AlignLeft);
        if (m.state == SendState::Failed) {
            auto* retry = button("重试", "ghost");
            retry->setObjectName("retry_" + m.id);
            column->addWidget(retry, 0, Qt::AlignRight);
            connect(retry, &QPushButton::clicked, this,
                    [this, id = m.id] { m_controller->service()->retryMessage(id); });
        }
        h->addLayout(column, 0);
        if (!m.outgoing)
            h->addStretch(1);
        v->addWidget(row);
    }
    if (!count) {
        auto* empty = label(m_messageSearch->text().isEmpty() ? "从一句问候开始新的对话" : "没有匹配的消息",
                            14, "muted");
        empty->setAlignment(Qt::AlignCenter);
        v->addWidget(empty);
    }
    v->addStretch(1);
    // Install and lay out the replacement in one paint transaction. QScrollArea resets
    // its range/value during setWidget; those intermediate values must stay invisible.
    const bool updatesEnabled = m_scroll->updatesEnabled();
    m_scroll->setUpdatesEnabled(false);
    QWidget* previous = nullptr;
    {
        const QSignalBlocker block(bar);
        previous = m_scroll->takeWidget();
        body->ensurePolished();
        m_scroll->setWidget(body);
        v->activate();
        body->resize(body->width(), qMax(v->minimumSize().height(), m_scroll->viewport()->height()));
        v->activate();
    }
    const auto* anchor = rows.value(anchorId, nullptr);
    bar->setValue(bottom ? bar->maximum() : anchor ? anchor->y() - anchorOffset : old);
    m_messageRenderKey = renderKey;
    m_scroll->setUpdatesEnabled(updatesEnabled);
    if (previous)
        previous->deleteLater();
}
void MainWindow::submit() {
    auto text = m_editor->toPlainText();
    if (text.trimmed().isEmpty())
        return;
    if (m_controller->sendText(text, m_failNext).isEmpty())
        return;
    m_editor->clear();
    m_drafts[m_displayedId].clear();
    if (m_failNext) {
        m_failNext = false;
        if (auto* check = findChild<QCheckBox*>("failNextMessage"))
            check->setChecked(false);
    }
    renderMessages(true);
    m_editor->setFocus();
}
void MainWindow::chooseFile() {
    if (!m_controller->service()->canSendFiles(m_controller->selectedId()))
        return;
    auto path = QFileDialog::getOpenFileName(
        this, m_controller->service()->isDemo() ? "选择文件（仅模拟）" : "选择发送给当前联系人的文件");
    if (!path.isEmpty()) {
        m_controller->attachFile(path);
        showPage(2);
    }
}
void MainWindow::refreshTransfers() {
    if (!m_transferLayout)
        return;
    QSet<QString> currentIds;
    for (const auto& t : m_controller->service()->transfers())
        currentIds.insert(t.id);
    for (auto it = m_transferCards.begin(); it != m_transferCards.end();) {
        if (!currentIds.contains(it.key())) {
            m_transferLayout->removeWidget(it.value());
            it.value()->deleteLater();
            it = m_transferCards.erase(it);
        } else
            ++it;
    }
    int active = 0, completed = 0, shown = 0;
    const int mode = m_transferFilter->currentIndex();
    const bool initial = m_transferCards.isEmpty();
    for (const auto& t : m_controller->service()->transfers()) {
        if (!m_transferCards.contains(t.id)) {
            auto* f = new TransferCard(t);
            if (initial)
                m_transferLayout->addWidget(f);
            else
                m_transferLayout->insertWidget(0, f);
            m_transferCards[t.id] = f;
            connect(f, &TransferCard::action, this, [this](const QString& id, const QString& action) {
                if (action == "locate")
                    locateTransfer(id);
                else
                    m_controller->service()->transferAction(id, action);
            });
        }
        auto* f = m_transferCards[t.id];
        f->refresh(t);
        bool running = t.state == TransferState::Running || t.state == TransferState::Paused;
        bool done = t.state == TransferState::Completed;
        active += running;
        completed += done;
        bool visible =
            mode == 0 || (mode == 1 && running) || (mode == 2 && done) || (mode == 3 && !running && !done);
        f->setVisible(visible);
        shown += visible;
    }
    m_transferSummary->setText(QString(m_controller->service()->isDemo()
                                           ? "%1 项进行中 / 暂停   ·   %2 项模拟完成   ·   当前显示 %3 项"
                                           : "%1 项进行中 / 待恢复   ·   %2 项已完成   ·   当前显示 %3 项")
                                   .arg(active)
                                   .arg(completed)
                                   .arg(shown));
}
void MainWindow::locateTransfer(const QString& id) {
    QString conversation, messageId;
    for (const auto& transfer : m_controller->service()->transfers())
        if (transfer.id == id) {
            conversation = transfer.conversation;
            messageId = transfer.messageId;
            break;
        }
    bool found = false;
    for (const auto& message : m_controller->service()->messages(conversation))
        if (!messageId.isEmpty() && message.id == messageId) {
            found = true;
            break;
        }
    if (!found || m_controller->service()->conversation(conversation).id.isEmpty()) {
        statusBar()->showMessage("未找到这次文件传输对应的消息记录", 6000);
        return;
    }
    m_controller->selectConversation(conversation);
    m_messageSearch->clear();
    setDetailsVisible(false);
    showPage(0);
    if (m_chat->layout())
        m_chat->layout()->activate();
    if (m_reflowTimer)
        m_reflowTimer->stop();
    m_followTail = false;
    m_highlightedMessageId = messageId;
    renderMessages();
    QWidget* target = nullptr;
    for (auto* row : m_scroll->widget()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (row->property("messageId").toString() == messageId)
            target = row;
        if (auto* bubble = row->findChild<MessageBubble*>()) {
            bubble->setProperty("locatedMessage", row->property("messageId").toString() == messageId);
            bubble->update();
        }
    }
    if (target) {
        const int margin = qMax(16, (m_scroll->viewport()->height() - target->height()) / 2);
        m_scroll->verticalScrollBar()->setValue(target->y() - margin);
    }
    const auto generation = ++m_locationGeneration;
    QTimer::singleShot(2500, this, [this, generation] {
        if (generation != m_locationGeneration)
            return;
        m_highlightedMessageId.clear();
        for (auto* bubble : m_scroll->findChildren<MessageBubble*>()) {
            bubble->setProperty("locatedMessage", false);
            bubble->update();
        }
    });
}
void MainWindow::setDetailsVisible(bool visible) {
    if (!m_details)
        return;
    if (visible) {
        refreshDetails();
        m_details->setGeometry(m_chat->width() - 288, 64, 288, m_chat->height() - 64);
        m_details->raise();
    }
    m_details->setVisible(visible);
}
bool MainWindow::detailsVisible() const {
    return m_details && !m_details->isHidden();
}
void MainWindow::showProfile(const QString& user) {
    if(!m_controller->service()->online()){showLogin();return;}
    if(m_pages->currentIndex()!=5)m_profileReturnPage=m_pages->currentIndex();
    m_pages->setCurrentIndex(5);for(auto* b:m_nav)b->setChecked(false);m_profilePage->openUser(user);
}
bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if((obj==m_headerAvatar||obj==m_chatTitle)&&event->type()==QEvent::MouseButtonRelease){const auto c=m_controller->service()->conversation(m_controller->selectedId());if(!c.group&&!c.peer.isEmpty())showProfile(c.peer);return true;}
    if (obj == m_chat && event->type() == QEvent::Resize) {
        if (detailsVisible())
            m_details->setGeometry(m_chat->width() - 288, 64, 288, m_chat->height() - 64);
        if (m_reflowTimer) {
            m_reflowTimer->start();
        }
    }
    return QMainWindow::eventFilter(obj, event);
}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_splitter && width() < 1100 && m_splitter->sizes().value(0) > 300)
        m_splitter->setSizes({280, width() - 349});
}
} // namespace Messager
