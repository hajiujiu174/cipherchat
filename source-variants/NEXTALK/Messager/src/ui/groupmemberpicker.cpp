#include "groupmemberpicker.h"
#include "components.h"
#include <QCheckBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>

namespace Messager {
GroupMemberPicker::GroupMemberPicker(QWidget* parent) : QPushButton(parent) {
    setObjectName("groupMembers");
    setAutoDefault(false);
    setMinimumHeight(42);
    m_popup = new QMenu(this);
    m_popup->setObjectName("groupMembersPopup");
    setAccessibleDescription("展开好友列表，使用复选框选择多个群聊成员");
    connect(this, &QPushButton::clicked, this,
            [this] { m_popup->popup(mapToGlobal(QPoint(0, height() + 4))); });
    m_content = new QWidget;
    m_content->setProperty("surface", "white");
    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);
    m_search = new QLineEdit;
    m_search->setObjectName("groupMemberSearch");
    m_search->setPlaceholderText("搜索好友");
    m_search->setAccessibleName("搜索群聊成员");
    layout->addWidget(m_search);
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* list = new QWidget;
    list->setProperty("surface", "white");
    m_rows = new QVBoxLayout(list);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(2);
    m_rows->setAlignment(Qt::AlignTop);
    m_scroll->setWidget(list);
    layout->addWidget(m_scroll);
    m_empty = label("暂无好友，请先添加好友", 12, "muted");
    m_empty->setObjectName("groupMemberEmpty");
    m_empty->setWordWrap(true);
    m_rows->addWidget(m_empty);
    m_summary = label({}, 12, "muted");
    m_summary->setObjectName("groupMemberSummary");
    layout->addWidget(m_summary);
    auto* done = button("完成选择", "primary");
    done->setObjectName("groupMemberDone");
    done->setAutoDefault(false);
    layout->addWidget(done);
    auto* action = new QWidgetAction(m_popup);
    action->setDefaultWidget(m_content);
    m_popup->addAction(action);
    connect(done, &QPushButton::clicked, m_popup, &QMenu::hide);
    connect(m_search, &QLineEdit::textChanged, this, &GroupMemberPicker::filterMembers);
    connect(m_search, &QLineEdit::returnPressed, m_popup, &QMenu::hide);
    connect(m_popup, &QMenu::aboutToShow, this, [this] {
        m_popup->setFixedWidth(qMax(300, width()));
        m_content->setFixedWidth(qMax(282, width() - 18));
        m_search->clear();
        QTimer::singleShot(0, m_search, [this] { m_search->setFocus(); });
    });
    setContacts({}, {});
}

void GroupMemberPicker::setContacts(const QVector<Contact>& contacts, const QString& self) {
    const auto previous = selectedMembers();
    QStringList users;
    QSet<QString> seen;
    for (const auto& contact : contacts) {
        if (!contact.friendContact || contact.username.isEmpty() || contact.username == self ||
            seen.contains(contact.username))
            continue;
        seen.insert(contact.username);
        users.append(contact.username);
    }
    std::sort(users.begin(), users.end(),
              [](const QString& a, const QString& b) { return QString::localeAwareCompare(a, b) < 0; });
    QStringList existing;
    for (auto* check : m_checks)
        existing.append(check->property("username").toString());
    if (!m_checks.isEmpty() && existing == users)
        return;
    qDeleteAll(m_checks);
    m_checks.clear();
    for (const auto& user : users) {
        auto* check = new QCheckBox(user);
        check->setObjectName("groupMember_" + user);
        check->setProperty("groupMember", true);
        check->setProperty("username", user);
        check->setToolTip(user);
        check->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        check->setChecked(previous.contains(user));
        m_rows->insertWidget(m_rows->count() - 1, check);
        m_checks.append(check);
        connect(check, &QCheckBox::toggled, this, [this] {
            updateSummary();
            emit selectionChanged();
        });
    }
    m_scroll->setFixedHeight(qBound(86, int(users.size()) * 40 + 4, 244));
    filterMembers();
    updateSummary();
    if (selectedMembers() != previous)
        emit selectionChanged();
}

QStringList GroupMemberPicker::selectedMembers() const {
    QStringList members;
    for (auto* check : m_checks)
        if (check->isChecked())
            members.append(check->property("username").toString());
    return members;
}

void GroupMemberPicker::updateSummary() {
    const auto users = selectedMembers();
    setText(users.isEmpty() ? "选择群聊成员" : QString("已选择 %1 位好友").arg(users.size()));
    setAccessibleName("群聊成员，" + text());
    setToolTip(users.join('\n'));
    m_summary->setText(QString("已选择 %1 位好友 · 最多 63 位").arg(users.size()));
    for (auto* check : m_checks)
        check->setEnabled(check->isChecked() || users.size() < 63);
}

void GroupMemberPicker::filterMembers() {
    const auto query = m_search->text().trimmed();
    int visible = 0;
    for (auto* check : m_checks) {
        const bool matches = check->property("username").toString().contains(query, Qt::CaseInsensitive);
        check->setVisible(matches);
        visible += matches;
    }
    m_empty->setText(m_checks.isEmpty() ? "暂无好友，请先添加好友" : "没有匹配的好友");
    m_empty->setVisible(visible == 0);
}

void GroupMemberPicker::paintEvent(QPaintEvent* event) {
    QPushButton::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Theme::instance().colors().muted, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal x = width() - 19;
    const qreal y = height() / 2.0;
    painter.drawPolyline(QPolygonF{{x - 4, y - 2}, {x, y + 2}, {x + 4, y - 2}});
}

void GroupMemberPicker::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Down) {
        click();
        event->accept();
        return;
    }
    QPushButton::keyPressEvent(event);
}
} // namespace Messager
