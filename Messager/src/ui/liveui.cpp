#include "accountdialog.h"
#include "groupmemberpicker.h"
#include "mainwindow.h"
#include "pagewidgets.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QStackedWidget>
#include <QVBoxLayout>
namespace Messager {
namespace {
void clearRows(QLayout* layout) {
    while (auto* item = layout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        if (item->layout())
            clearRows(item->layout());
        delete item;
    }
}
QWidget* personRow(const QString& name, const QString& sub, QHBoxLayout** layout) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(12, 12, 12, 12);
    h->setSpacing(14);
    h->addWidget(new Avatar(name.left(1).toUpper(), QColor("#6554a0"), 42));
    auto* text = new QVBoxLayout;
    text->addWidget(label(name, 14, nullptr, true));
    auto* detail = label(sub, 12, "muted");
    detail->setWordWrap(true);
    text->addWidget(detail);
    h->addLayout(text, 1);
    *layout = h;
    return row;
}
} // namespace
void MainWindow::refreshSession() {
    if (!m_send || !m_editor)
        return;
    auto* service = m_controller->service();
    const bool demo = service->isDemo();
    const bool selected = !service->conversation(m_controller->selectedId()).id.isEmpty();
    m_send->setEnabled((demo || service->online()) && selected &&
                       !m_editor->toPlainText().trimmed().isEmpty());
    m_editor->setEnabled(demo || selected);
    if (auto* attach = findChild<QPushButton*>("attachButton")) {
        attach->setEnabled((demo || service->online()) && selected &&
                           service->canSendFiles(m_controller->selectedId()));
        attach->setToolTip(!demo && selected && !service->conversation(m_controller->selectedId()).group &&
                                   !service->canSendFiles(m_controller->selectedId())
                               ? "临时会话仅支持文字消息，成为好友后才能发送文件"
                           : demo ? "选择文件 · 仅模拟传输"
                                  : "发送文件给当前好友");
    }
    if(!service->online()){
        if(auto* b=findChild<QPushButton*>("profileButton")){b->setIcon({});b->setText("我");b->setToolTip("登录 / 个人资料");}
        if(m_pages&&(m_pages->currentIndex()==5||m_pages->currentIndex()==6))showPage(0);
    }
    if (m_railStatus)
        m_railStatus->setText(service->statusText());
    if (m_sessionNotice)
        m_sessionNotice->setText(demo ? "演示工作区 · 消息与文件均为本地模拟" : service->statusText());
    if (!demo && service->conversations().isEmpty() && !service->online()) {
        m_drafts.clear();
        if (!m_editor->toPlainText().isEmpty())
            m_editor->clear();
    }
}
QWidget* MainWindow::buildLiveContacts() {
    QVBoxLayout* v;
    auto* body = pageBase("联系人", "好友、临时会话与好友申请", &v);
    auto* actions = new QHBoxLayout;
    auto* newGroup = button("创建群聊", "primary");
    newGroup->setObjectName("createGroupButton");
    actions->addWidget(newGroup);
    connect(newGroup, &QPushButton::clicked, this, &MainWindow::showCreateGroup);
    actions->addStretch();
    auto* account = button("账号与连接");
    actions->addWidget(account);
    connect(account, &QPushButton::clicked, this, &MainWindow::showLogin);
    v->addLayout(actions);
    auto* queries = new QHBoxLayout;
    m_liveSearch = new QLineEdit;
    m_liveSearch->setObjectName("serverUserSearch");
    m_liveSearch->setPlaceholderText("查询服务器上的用户名");
    auto* find = button("查询");
    find->setObjectName("searchUsersButton");
    queries->addWidget(m_liveSearch, 1);
    queries->addWidget(find);
    m_searchMore = button("下一页");
    m_searchMore->setEnabled(false);
    queries->addWidget(m_searchMore);
    v->addLayout(queries);
    auto search = [this] { m_controller->service()->searchUsers(m_liveSearch->text().trimmed()); };
    connect(find, &QPushButton::clicked, this, search);
    connect(m_liveSearch, &QLineEdit::returnPressed, this, search);
    connect(m_searchMore, &QPushButton::clicked, this, [this] {
        m_controller->service()->searchUsers(m_liveSearch->text().trimmed(),
                                             m_controller->service()->searchNext());
    });
    v->addWidget(label("好友申请", 12, "eyebrow"));
    m_liveRequests = new QVBoxLayout;
    v->addLayout(m_liveRequests);
    v->addWidget(label("好友", 12, "eyebrow"));
    m_liveContacts = new QVBoxLayout;
    v->addLayout(m_liveContacts);
    v->addWidget(label("用户查询结果", 12, "eyebrow"));
    m_liveResults = new QVBoxLayout;
    v->addLayout(m_liveResults);
    v->addStretch();
    connect(m_controller->service(), &ChatService::contactsChanged, this, &MainWindow::refreshLiveContacts);
    refreshLiveContacts();
    return scrollPage(body);
}
void MainWindow::refreshLiveContacts() {
    if (!m_liveContacts)
        return;
    clearRows(m_liveContacts);
    clearRows(m_liveRequests);
    clearRows(m_liveResults);
    auto* service = m_controller->service();
    for (const auto& user : service->incomingRequests()) {
        QHBoxLayout* h;
        auto* row = personRow(user, "请求添加你为好友", &h);
        auto* accept = button("同意");
        accept->setObjectName("acceptFriend_" + user);
        auto* reject = button("拒绝");
        reject->setObjectName("rejectFriend_" + user);
        h->addWidget(accept);
        h->addWidget(reject);
        connect(accept, &QPushButton::clicked, this, [service, user] { service->respondFriend(user, true); });
        connect(reject, &QPushButton::clicked, this,
                [service, user] { service->respondFriend(user, false); });
        m_liveRequests->addWidget(row);
    }
    if (service->incomingRequests().isEmpty())
        m_liveRequests->addWidget(label("没有待处理的好友申请", 12, "muted"));
    for (const auto& user : service->outgoingRequests())
        m_liveRequests->addWidget(label("等待 " + user + " 同意好友申请", 12, "muted"));
    for (const auto& c : service->contacts()) {
        QHBoxLayout* h;
        auto* row = personRow(c.username, c.online ? "在线" : "离线", &h);
        auto* profile=button("资料");profile->setObjectName("contactProfile_"+c.username);h->addWidget(profile);
        connect(profile,&QPushButton::clicked,this,[this,user=c.username]{showProfile(user);});
        auto* open = button("发消息");
        open->setObjectName("contact_" + c.username);
        h->addWidget(open);
        connect(open, &QPushButton::clicked, this, [this, user = c.username] {
            m_controller->selectConversation("peer:" + user);
            showPage(0);
        });
        m_liveContacts->addWidget(row);
    }
    if (service->contacts().isEmpty())
        m_liveContacts->addWidget(
            label(service->online() ? "还没有好友。可在上方查询用户名并发送申请。" : "登录后加载你的好友。",
                  12, "muted"));
    for (const auto& c : service->searchResults()) {
        if (c.username == service->username())
            continue;
        QHBoxLayout* h;
        auto* row = personRow(c.username, c.online ? "在线" : "离线", &h);
        auto* profile=button("资料");profile->setObjectName("contactProfile_"+c.username);h->addWidget(profile);
        connect(profile,&QPushButton::clicked,this,[this,user=c.username]{showProfile(user);});
        const bool friendContact = service->isFriend(c.username);
        const bool requested = service->outgoingRequests().contains(c.username);
        const bool submitting = service->friendRequestPending(c.username);
        auto* add = button(friendContact ? "已是好友"
                           : requested   ? "已申请"
                           : submitting  ? "申请中…"
                                         : "添加好友",
                           "secondary");
        add->setObjectName("addFriend_" + c.username);
        add->setEnabled(service->online() && !friendContact && !requested && !submitting);
        if (requested)
            add->setToolTip("等待对方同意好友申请");
        h->addWidget(add);
        connect(add, &QPushButton::clicked, this, [service, user = c.username] { service->addFriend(user); });
        auto* open = button(friendContact ? "发消息" : "临时会话");
        open->setObjectName("temporaryChat_" + c.username);
        open->setEnabled(service->online());
        if (!friendContact)
            open->setToolTip("开启临时会话，仅支持文字消息");
        h->addWidget(open);
        connect(open, &QPushButton::clicked, this, [this, user = c.username] {
            m_controller->openDirectConversation(user);
            if (m_controller->selectedId() == "peer:" + user)
                showPage(0);
        });
        m_liveResults->addWidget(row);
    }
    m_searchMore->setEnabled(!service->searchNext().isEmpty());
}
QWidget* MainWindow::buildLiveNotifications() {
    QVBoxLayout* v;
    auto* body = pageBase("通知", "来自当前账号的真实活动", &v);
    auto* mark = button("全部标为已查看");
    v->addWidget(mark, 0, Qt::AlignRight);
    connect(mark, &QPushButton::clicked, m_controller->service(), &ChatService::markActivitiesViewed);
    m_liveActivities = new QVBoxLayout;
    v->addLayout(m_liveActivities);
    v->addStretch();
    connect(m_controller->service(), &ChatService::activitiesChanged, this,
            &MainWindow::refreshLiveActivities);
    refreshLiveActivities();
    return scrollPage(body);
}
void MainWindow::refreshLiveActivities() {
    if (!m_liveActivities)
        return;
    clearRows(m_liveActivities);
    auto items = m_controller->service()->activities();
    for (const auto& a : items) {
        auto* f = card();
        auto* v = new QVBoxLayout(f);
        v->setContentsMargins(20, 16, 20, 16);
        auto* h = new QHBoxLayout;
        h->addWidget(label(a.title, 14, nullptr, true));
        h->addStretch();
        h->addWidget(label(a.viewed ? "已查看" : "未查看", 11, "muted"));
        v->addLayout(h);
        auto* text = label(a.text, 13, "muted");
        text->setWordWrap(true);
        v->addWidget(text);
        if (!a.conversation.isEmpty()) {
            auto* open = button("打开会话");
            v->addWidget(open, 0, Qt::AlignRight);
            connect(open, &QPushButton::clicked, this, [this, id = a.conversation] {
                m_controller->selectConversation(id);
                showPage(0);
            });
        }
        m_liveActivities->addWidget(f);
    }
    if (items.isEmpty())
        m_liveActivities->addWidget(label("暂无账号活动。此处不会生成演示通知。", 13, "muted"));
}
void MainWindow::showCreateGroup() {
    auto* service = m_controller->service();
    if (!service->online()) {
        statusBar()->showMessage("请先登录账号", 8000);
        return;
    }
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("createGroupDialog");
    dialog->setWindowTitle("创建 CipherChat 群聊");
    dialog->setMinimumWidth(410);
    auto* v = new QVBoxLayout(dialog);
    v->setContentsMargins(25, 22, 25, 22);
    auto* name = new QLineEdit;
    name->setObjectName("groupName");
    name->setPlaceholderText("群聊名称");
    v->addWidget(name);
    auto* members = new GroupMemberPicker;
    members->setContacts(service->contacts(), service->username());
    v->addWidget(members);
    auto* hint = label("从好友中勾选成员，支持多选；你会自动加入群聊。", 12, "muted");
    hint->setWordWrap(true);
    v->addWidget(hint);
    auto* create = button("创建群聊", "primary");
    create->setObjectName("createGroupSubmit");
    v->addWidget(create);
    auto updateSubmit = [service, name, members, create] {
        create->setEnabled(service->online() && !name->text().trimmed().isEmpty() &&
                           !members->selectedMembers().isEmpty());
    };
    connect(name, &QLineEdit::textChanged, dialog, updateSubmit);
    connect(members, &GroupMemberPicker::selectionChanged, dialog, updateSubmit);
    connect(service, &ChatService::sessionChanged, dialog, updateSubmit);
    connect(service, &ChatService::contactsChanged, dialog,
            [service, members] { members->setContacts(service->contacts(), service->username()); });
    updateSubmit();
    connect(create, &QPushButton::clicked, dialog, [service, name, members, dialog] {
        const auto users = members->selectedMembers();
        if (!service->online() || name->text().trimmed().isEmpty() || users.isEmpty())
            return;
        service->createGroup(name->text().trimmed(), users);
        dialog->accept();
    });
    dialog->open();
}
void MainWindow::showAccountDialog() {
    auto* service = m_controller->service();
    if (!service->online()) {
        hide();
        if (m_accountDialog && m_accountDialog->isVisible()) {
            m_accountDialog->raise();
            m_accountDialog->activateWindow();
            return;
        }
        delete m_accountDialog.data();
        auto* dialog = new AccountDialog(service);
        m_accountDialog = dialog;
        connect(dialog, &AccountDialog::authenticated, this, [this] {
            show();
            raise();
            activateWindow();
        });
        connect(dialog, &AccountDialog::preferenceSaveFailed, this,
                [this] { statusBar()->showMessage("登录成功，但系统未能保存记住选项", 10000); });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        emit loginOpened(dialog);
        return;
    }
    if (auto* existing = findChild<QDialog*>("accountManagement")) {
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto* dialog = new QDialog(this);
    dialog->setObjectName("accountManagement");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("CipherChat · 我的账号");
    dialog->setMinimumWidth(460);
    auto* v = new QVBoxLayout(dialog);
    v->setContentsMargins(28, 24, 28, 24);
    v->setSpacing(18);
    v->addWidget(label(service->username(), 24, nullptr, true));
    v->addWidget(label(service->statusText(), 13, "muted"));
    auto* logout = button("退出当前账号");
    logout->setObjectName("logoutButton");
    v->addWidget(logout);
    connect(logout, &QPushButton::clicked, this, [this, service, dialog] {
        service->disconnectAccount();
        hide();
        showAccountDialog();
        dialog->accept();
    });
    dialog->open();
}
} // namespace Messager
