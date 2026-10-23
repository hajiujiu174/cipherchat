#include "mainwindow.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
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
#include <QSignalBlocker>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "pagewidgets.h"
namespace Messager {
QWidget* MainWindow::buildContacts() {
    if (!m_controller->service()->isDemo())
        return buildLiveContacts();
    QVBoxLayout* v;
    auto* body = pageBase("联系人", "每一次好沟通，从找到对的人开始。", &v);
    auto* search = new QLineEdit;
    search->setObjectName("contactSearch");
    search->setPlaceholderText("搜索姓名或介绍");
    search->setClearButtonEnabled(true);
    search->setMaximumWidth(420);
    v->addWidget(search);
    v->addWidget(label("团队联系人", 12, "eyebrow"));
    auto* group = card();
    auto* rows = new QVBoxLayout(group);
    rows->setContentsMargins(24, 8, 24, 8);
    rows->setSpacing(0);
    QVector<QPair<QWidget*, QString>> items;
    for (const auto& c : m_controller->service()->conversations()) {
        if (c.group || c.id == "notes")
            continue;
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 18, 0, 18);
        h->setSpacing(16);
        h->addWidget(new Avatar(c.initials, c.color, 48));
        auto* text = new QVBoxLayout;
        text->setSpacing(5);
        text->addWidget(label(c.name, 15, nullptr, true));
        text->addWidget(label(c.subtitle, 12, "muted"));
        h->addLayout(text, 1);
        auto* open = button("发消息", "ghost", "chat");
        open->setObjectName("contact_" + c.id);
        h->addWidget(open);
        connect(open, &QPushButton::clicked, this, [this, id = c.id] {
            m_controller->selectConversation(id);
            showPage(0);
        });
        rows->addWidget(row);
        items.push_back({row, c.name + c.subtitle});
    }
    v->addWidget(group);
    auto* empty = label("没有找到联系人", 14, "muted");
    empty->hide();
    v->addWidget(empty);
    connect(search, &QLineEdit::textChanged, this, [items, empty](const QString& q) {
        int count = 0;
        for (const auto& item : items) {
            bool show = item.second.contains(q, Qt::CaseInsensitive);
            item.first->setVisible(show);
            count += show;
        }
        empty->setVisible(!count);
    });
    v->addWidget(banner("这些联系人均为虚构的演示数据。真实联系人同步、邀请与好友验证将在接入服务后提供。"));
    v->addStretch();
    return scrollPage(body);
}
QWidget* MainWindow::buildFiles() {
    QVBoxLayout* v;
    auto* body = pageBase("文件传输", "在一个地方，掌握文件的每一步。", &v);
    auto* tools = new QHBoxLayout;
    m_transferSummary = label("", 13, "muted");
    tools->addWidget(m_transferSummary);
    tools->addStretch();
    m_transferFilter = new QComboBox;
    m_transferFilter->setObjectName("transferFilter");
    m_transferFilter->addItems({"全部传输", "进行中 / 暂停", "已完成", "失败 / 已取消"});
    tools->addWidget(m_transferFilter);
    const bool demo = m_controller->service()->isDemo();
    auto* add = button(demo ? "添加演示文件" : "发送文件", "primary");
    add->setObjectName("addDemoTransfer");
    tools->addWidget(add);
    connect(add, &QPushButton::clicked, this, [this] {
        if (m_controller->service()->isDemo())
            m_controller->service()->addTransfer("项目素材包.zip", 12400000, m_controller->selectedId());
        else
            chooseFile();
    });
    v->addLayout(tools);
    v->addWidget(
        banner(demo ? "模拟传输 · 进度由本地计时器驱动，不会上传、下载或写入真实文件。"
                    : "真实文件传输 · 进度来自后端已验证字节。未完成项可恢复，取消后需重新选择文件发送。"));
    m_transferLayout = new QVBoxLayout;
    m_transferLayout->setSpacing(14);
    v->addLayout(m_transferLayout);
    connect(m_transferFilter, &QComboBox::currentIndexChanged, this, &MainWindow::refreshTransfers);
    v->addStretch();
    return scrollPage(body);
}
QWidget* MainWindow::buildNotifications() {
    if (!m_controller->service()->isDemo())
        return buildLiveNotifications();
    QVBoxLayout* v;
    auto* body = pageBase("通知", "与你有关的动态，都在这里。", &v);
    auto* row = new QHBoxLayout;
    auto* count = label("3 条演示通知", 13, "muted");
    row->addWidget(count);
    row->addStretch();
    auto* mark = button("全部标为已查看");
    mark->setObjectName("markNotifications");
    row->addWidget(mark);
    v->addLayout(row);
    QStringList titles = {"林予在产品设计讨论中提到了你", "桌面端原型.fig 的模拟传输失败",
                          "欢迎来到 NEXTALK"};
    QStringList subtitles = {"「下午一起过一遍界面稿吧。」 · 10:28",
                             "重试可以重新体验进度和完成状态。 · 09:56",
                             "这是一份可以交互的桌面 UI 原型，所有内容仅在本机演示。"};
    QVector<QLabel*> badges;
    for (int i = 0; i < 3; ++i) {
        auto* f = card();
        auto* h = new QHBoxLayout(f);
        h->setContentsMargins(22, 22, 22, 22);
        h->setSpacing(18);
        h->addWidget(new Avatar(i == 0   ? "@"
                                : i == 1 ? "!"
                                         : "M",
                                QColor(i == 1 ? "#b77b6e" : "#8271bb"), 44));
        auto* text = new QVBoxLayout;
        text->setSpacing(8);
        text->addWidget(label(titles[i], 15, nullptr, true));
        auto* sub = label(subtitles[i], 12, "muted");
        sub->setWordWrap(true);
        text->addWidget(sub);
        h->addLayout(text, 1);
        auto* badge = label("未查看", 11, "accent");
        badges.push_back(badge);
        h->addWidget(badge);
        auto* open = button("查看");
        h->addWidget(open);
        connect(open, &QPushButton::clicked, this, [this, i, badge] {
            badge->setText("已查看");
            if (i == 1)
                showPage(2);
            else {
                m_controller->selectConversation("design");
                showPage(0);
            }
        });
        v->addWidget(f);
    }
    connect(mark, &QPushButton::clicked, this, [badges, count, mark] {
        for (auto* b : badges)
            b->setText("已查看");
        count->setText("所有演示通知均已查看");
        mark->setEnabled(false);
    });
    v->addStretch();
    return scrollPage(body);
}
QWidget* MainWindow::buildSettings() {
    QVBoxLayout* v;
    auto* body = pageBase("设置", "让 NEXTALK 更合你的习惯。", &v);
    auto* appearance = card();
    auto* a = new QVBoxLayout(appearance);
    a->setContentsMargins(26, 22, 26, 22);
    a->setSpacing(18);
    a->addWidget(label("外观", 17, nullptr, true));
    auto* skinRow=new QHBoxLayout;
    auto* skinText=new QVBoxLayout;
    skinText->addWidget(label("界面皮肤",14,nullptr,true));
    auto* skinHint=label("彩铅纸纹与云朵气泡，也可搭配深色主题。",12,"muted");skinHint->setWordWrap(true);skinText->addWidget(skinHint);
    skinRow->addLayout(skinText,1);
    auto* skin=new QComboBox;skin->setObjectName("appearanceSkin");skin->setAccessibleName("界面皮肤");
    skin->addItem("默认皮肤",int(Theme::DefaultSkin));skin->addItem("彩铅手绘 · 云朵气泡",int(Theme::PencilSkin));skin->setCurrentIndex(int(Theme::instance().skin()));
    skinRow->addWidget(skin);a->addLayout(skinRow);
    connect(skin,&QComboBox::currentIndexChanged,this,[skin](int index){Theme::instance().setSkin(Theme::Skin(skin->itemData(index).toInt()));});
    connect(&Theme::instance(),&Theme::changed,skin,[skin]{QSignalBlocker blocker(skin);skin->setCurrentIndex(int(Theme::instance().skin()));});
    a->addWidget(label("主题", 13, "muted"));
    auto* themes = new QHBoxLayout;
    themes->setSpacing(14);
    auto* themeGroup = new QButtonGroup(this);
    QStringList names = {"跟随系统", "浅色", "深色"};
    for (int i = 0; i < 3; ++i) {
        auto* b = button(names[i], "chip");
        b->setObjectName("theme_" + QString::number(i));
        b->setMinimumHeight(74);
        b->setCheckable(true);
        b->setChecked(i == Theme::instance().mode());
        themeGroup->addButton(b, i);
        themes->addWidget(b, 1);
    }
    a->addLayout(themes);
    connect(themeGroup, &QButtonGroup::idClicked, this,
            [](int mode) { Theme::instance().setMode(Theme::Mode(mode)); });
    connect(&Theme::instance(), &Theme::changed, themeGroup, [themeGroup] {
        if (auto* selected = themeGroup->button(int(Theme::instance().mode())))
            selected->setChecked(true);
    });
    auto* fontRow = new QHBoxLayout;
    auto* ft = new QVBoxLayout;
    ft->addWidget(label("界面字体大小", 14, nullptr, true));
    ft->addWidget(label("即时应用于消息及标准界面控件", 12, "muted"));
    fontRow->addLayout(ft, 1);
    auto* font = new QComboBox;
    font->setObjectName("fontSize");
    for (int size = 13; size <= 17; ++size)
        font->addItem(QString::number(size) + " px", size);
    font->setCurrentIndex(Theme::instance().fontSize() - 13);
    fontRow->addWidget(font);
    a->addLayout(fontRow);
    connect(font, &QComboBox::currentIndexChanged, this,
            [font] { Theme::instance().setFontSize(font->currentData().toInt()); });
    v->addWidget(appearance);
    auto* demo = card();
    auto* d = new QVBoxLayout(demo);
    d->setContentsMargins(26, 22, 26, 22);
    d->setSpacing(14);
    const bool isDemo = m_controller->service()->isDemo();
    d->addWidget(label(isDemo ? "原型体验" : "NEXTALK 账号", 17, nullptr, true));
    auto* hint =
        label(isDemo ? "通过模拟状态检查交互。设置会保存在本机，演示消息与传输进度在重启后重置。"
                     : "消息、文件和账号历史由 NEXTALK 原生业务层处理。连接信息可在账号窗口中设置。",
              12, "muted");
    hint->setWordWrap(true);
    d->addWidget(hint);
    auto* fail = new QCheckBox("下一条文本消息模拟发送失败（发送后自动复位）");
    fail->setObjectName("failNextMessage");
    d->addWidget(fail);
    fail->setVisible(isDemo);
    connect(fail, &QCheckBox::toggled, this, [this](bool on) { m_failNext = on; });
    auto* login = button(isDemo ? "查看登录 / 注册原型" : "账号与连接设置");
    login->setObjectName("loginPrototype");
    d->addWidget(login, 0, Qt::AlignLeft);
    connect(login, &QPushButton::clicked, this, &MainWindow::showLogin);
    v->addWidget(demo);
    v->addWidget(banner(
        isDemo ? "NEXTALK · 本地演示模式 · 不连接网络"
               : "NEXTALK 0.3.25.0\n支持聊天、文件传输与应用广场。签收不代表已读。"));
    v->addStretch();
    return scrollPage(body);
}

} // namespace Messager
