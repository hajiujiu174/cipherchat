#include "feature_ui.hpp"
#include "ccfw/delivery_service.hpp"
#include "../src/feature_codec.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <set>
namespace ccfw::gui {
namespace {
// 将标准文本转换为 Qt 字符串。
QString q(const std::string& v) { return QString::fromUtf8(v.data(), static_cast<qsizetype>(v.size())); }
// 将 Qt 字符串转换为 UTF-8 标准字符串。
std::string s(const QString& v) { return v.toUtf8().toStdString(); }
// 返回客户端界面的文本。
QLabel* text(const QString& value) { auto* w = new QLabel(value); w->setWordWrap(true); w->setTextFormat(Qt::PlainText); return w; }
// 返回客户端界面使用的操作。
QPushButton* action(QBoxLayout* box, const QString& value, std::function<void()> clicked) {
    auto* b = new QPushButton(value); box->addWidget(b); QObject::connect(b, &QPushButton::clicked, b, std::move(clicked)); return b;
}
// 返回客户端界面使用的对话框。
QDialog* dialog(QWidget* owner, const char* name, const QString& title, int width, int height) {
    auto* d = new QDialog(owner); d->setObjectName(name); d->setWindowTitle(title); d->setAttribute(Qt::WA_DeleteOnClose); d->resize(width, height);
    d->setStyleSheet("QDialog{background:#182329;color:#e8ece7;} QLabel,QTreeWidget,QTableWidget,QPlainTextEdit,QLineEdit,QComboBox{color:#e8ece7;} QPlainTextEdit,QLineEdit,QTreeWidget,QTableWidget,QComboBox{background:#24333c;border:1px solid #50626b;border-radius:6px;padding:5px;} QPushButton{color:#edf1e9;background:#30444a;border:1px solid #5a736f;border-radius:6px;padding:8px;} QPushButton:disabled{color:#819493;} QHeaderView::section{color:#edf1e9;background:#30444a;padding:6px;} QTabBar::tab{color:#edf1e9;background:#30444a;padding:9px;} QProgressBar{background:#24333c;color:#edf1e9;} QProgressBar::chunk{background:#648980;}");
    d->setMinimumSize(std::min(width, 620), std::min(height, 400)); return d;
}
const QStringList stageLabels{QStringLiteral("本地排队"), QStringLiteral("等待处理"), QStringLiteral("已保存到发送队列"), QStringLiteral("提交传输队列"), QStringLiteral("服务器接受"), QStringLiteral("对端签收"), QStringLiteral("失败")};
const QStringList bundleLabels{QStringLiteral("准备清单"), QStringLiteral("传输中"), QStringLiteral("文件已验证"), QStringLiteral("已取消"), QStringLiteral("人工确认收到")};
}
// 取出并处理客户端界面。
void FeatureUi::consume(const WorkerEvent& event) {
    if (std::holds_alternative<CommandResult>(event)) return;
    if (const auto* v = std::get_if<RestoredView>(&event)) {
        if (scope.server != v->scope.server || scope.username != v->scope.username) uids.clear();
        uidLookupSupported = v->uidLookupSupported; if (v->uid > 0) uids[v->username] = v->uid;
        scope = v->scope; workspace = v->workspace; bundles = v->bundles; stages = v->stages;
        return;
    }
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, MessageStageEvent>) {
            if (std::count_if(stages.begin(), stages.end(), [&](const auto& e) { return e.messageId == v.messageId; }) >= 32) {
                auto first = std::find_if(stages.begin(), stages.end(), [&](const auto& e) { return e.messageId == v.messageId; }); stages.erase(first);
            }
            stages.push_back(v); std::set<std::string> ids; for (const auto& e : stages) ids.insert(e.messageId);
            if (ids.size() > 500) { auto oldest = stages.front().messageId; std::erase_if(stages, [&](const auto& e) { return e.messageId == oldest; }); }
        } else if constexpr (std::is_same_v<T, DeliveryBundle>) {
            auto old = std::find_if(bundles.begin(), bundles.end(), [&](const auto& b) { return b.id == v.id; }); if (old == bundles.end()) bundles.push_back(v); else *old = v;
        } else if constexpr (std::is_same_v<T, WorkspaceState>) workspace = v;
        else if constexpr (std::is_same_v<T, UserUidEvent>) {
            if (v.uid > 0) uids[v.username] = v.uid; else uids.erase(v.username);
            for (auto* card : owner->findChildren<QDialog*>()) if (card->property("identityUsername").toString() == q(v.username)) {
                if (auto* value = card->findChild<QLabel*>("identityUid")) value->setText(v.uid > 0 ? QString::number(v.uid) : QStringLiteral("未找到 UID"));
                if (auto* copy = card->findChild<QPushButton*>("copyUid")) copy->setEnabled(v.uid > 0);
            }
        }
        else if constexpr (std::is_same_v<T, ConnectionSecurity>) tlsValidated = v.tlsValidated;
    }, std::get<ClientEvent>(event));
}
// 更新客户端界面中的消息详情状态。
void FeatureUi::messageDetails(const QString& selected) {
    auto* d = dialog(owner, "messageDetailsDialog", QStringLiteral("消息安全流程"), 790, 540); auto* box = new QVBoxLayout(d);
    auto* connection = text({}); box->addWidget(connection);
    auto* pick = new QComboBox; pick->setObjectName("traceMessagePicker"); box->addWidget(pick);
    auto* table = new QTableWidget(0, 4); table->setObjectName("messageStageTable"); table->setHorizontalHeaderLabels({QStringLiteral("阶段"), QStringLiteral("本机观察时间"), QStringLiteral("本机累计耗时"), QStringLiteral("重试 / 错误")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->setEditTriggers(QAbstractItemView::NoEditTriggers); box->addWidget(table, 1);
    box->addWidget(text(QStringLiteral("签收是协议确认。这里不表示已读；远端耗时不作推算。重启前缺失的阶段时间显示“未记录”。")));
    // 更新客户端界面。
    auto update = [=, this] {
        connection->setText(tlsValidated ? QStringLiteral("服务器连接验证：TLS 证书链与主机名已通过") : QStringLiteral("服务器连接验证：未连接"));
        const auto old = pick->currentData().toString(); std::set<std::string> ids;
        for (auto it = stages.rbegin(); it != stages.rend(); ++it) if (ids.insert(it->messageId).second && pick->findData(q(it->messageId)) < 0) pick->addItem(q(it->messageId), q(it->messageId));
        if (old.isEmpty() && !selected.isEmpty()) pick->setCurrentIndex(pick->findData(selected));
        table->setRowCount(0);
        for (const auto& e : stages) if (q(e.messageId) == pick->currentData().toString()) {
            const int row = table->rowCount(); table->insertRow(row);
            const QStringList cells{stageLabels.value(static_cast<int>(e.stage)), e.observedAt ? QDateTime::fromMSecsSinceEpoch(e.observedAt).toString("HH:mm:ss.zzz") : QStringLiteral("未记录"),
                e.elapsedMs >= 0 ? QString::number(e.elapsedMs) + " ms" : QStringLiteral("未记录"), QString::number(e.retryCount) + (e.errorCode >= 0 ? " / " + QString::number(e.errorCode) : "")};
            for (int col = 0; col < cells.size(); ++col) table->setItem(row, col, new QTableWidgetItem(cells[col]));
        }
    };
    auto* timer = new QTimer(d); QObject::connect(timer, &QTimer::timeout, d, update); timer->start(300); update(); d->show();
}
// 返回客户端界面使用的联系人卡片。
QDialog* FeatureUi::contactCard(const std::string& username, bool own) {
    for (auto* existing : owner->findChildren<QDialog*>(own ? "ownIdentityDialog" : "identityDialog")) {
        if (existing->isVisible() && existing->property("identityUsername").toString() == q(username)) {
            existing->raise();
            return existing;
        }
    }
    const auto viewedScope = scope;
    auto* d = dialog(owner, own ? "ownIdentityDialog" : "identityDialog", QStringLiteral("UID · ") + q(username), 620, 520);
    d->setStyleSheet({}); d->setPalette(owner->palette()); d->setAutoFillBackground(true);
    d->setProperty("identityUsername", q(username));
    auto* box = new QVBoxLayout(d); auto* tabs = new QTabWidget; tabs->setObjectName("identityCardTabs"); box->addWidget(tabs,1);
    auto* page = new QWidget; auto* content = new QVBoxLayout(page);
    content->addWidget(text(QStringLiteral("UID")));
    const auto found = uids.find(username);
    auto* uid = text(found != uids.end() ? QString::number(found->second) : uidLookupSupported ? QStringLiteral("正在读取…") : QStringLiteral("服务器尚未提供 UID"));
    uid->setObjectName("identityUid"); uid->setFont(QFont("Consolas",24)); uid->setTextInteractionFlags(Qt::TextSelectableByMouse); content->addWidget(uid);
    content->addWidget(text(QStringLiteral("账号：") + q(username)));
    auto* copy = action(content,QStringLiteral("复制 UID"),[=,this] { const auto current=uids.find(username); if (current!=uids.end()) QApplication::clipboard()->setText(QString::number(current->second)); });
    copy->setObjectName("copyUid"); copy->setEnabled(found!=uids.end());
    content->addStretch(); tabs->addTab(page, QStringLiteral("UID"));
    if (!own) {
        auto* notes = new QWidget; auto* notesBox = new QVBoxLayout(notes); notesBox->addWidget(text(QStringLiteral("备注仅在本机保存，显示在私聊标题下方。")));
        auto* note = new QPlainTextEdit; note->setObjectName("contactRemarkInput"); note->setPlaceholderText(QStringLiteral("填写联系人备注")); if (readRemark) note->setPlainText(readRemark(username)); notesBox->addWidget(note,1);
        auto* noteResult = text({}); noteResult->setObjectName("contactRemarkResult"); notesBox->addWidget(noteResult);
        auto* save = action(notesBox,QStringLiteral("保存备注"),[=,this] {
            if (scope.server!=viewedScope.server || scope.username!=viewedScope.username) { noteResult->setText(QStringLiteral("账号已切换，请重新打开备注。")); return; }
            if (note->toPlainText().size()>1024) { noteResult->setText(QStringLiteral("备注最多 1024 个字符，输入已保留。")); return; }
            if (!saveRemark || !saveRemark(username,note->toPlainText().trimmed())) { noteResult->setText(QStringLiteral("备注保存失败，输入已保留。")); return; }
            noteResult->setText(QStringLiteral("备注已保存"));
        }); save->setObjectName("saveContactRemark"); save->setEnabled(static_cast<bool>(saveRemark)); tabs->addTab(notes,QStringLiteral("备注"));
    }
    auto* close = action(box,QStringLiteral("关闭"),[d]{ d->close(); }); close->setObjectName(own ? "closeOwnSecurity" : "closePeerSecurity");
    if (found==uids.end() && uidLookupSupported && !post(LookupUid{username})) uid->setText(QStringLiteral("UID 读取失败"));
    QTimer::singleShot(8000,d,[uid] { if (uid->text()==QStringLiteral("正在读取…")) uid->setText(QStringLiteral("UID 暂未读取，请重新打开")); });
    d->show(); return d;
}

// 返回客户端界面使用的投递队列。
void FeatureUi::deliveries(const QString& peer) {
    auto* d = dialog(owner, "deliveryDialog", QStringLiteral("文件交付与版本"), 900, 640); auto* box = new QVBoxLayout(d);
    auto* tree = new QTreeWidget; tree->setObjectName("deliveryTree"); tree->setHeaderLabels({QStringLiteral("交付 / 文件"), QStringLiteral("进度"), QStringLiteral("状态"), QStringLiteral("版本")}); tree->header()->setSectionResizeMode(0, QHeaderView::Stretch); tree->setColumnWidth(1, 110); tree->setColumnWidth(2, 135); tree->setColumnWidth(3, 170); box->addWidget(tree, 1);
    auto* actions = new QHBoxLayout; box->addLayout(actions);
    // 获取客户端界面对应的 chosen，供调用方读取或参与计算。
    auto chosen = [tree] { auto* item = tree->currentItem(); return item ? item->data(0, Qt::UserRole).toString() : QString(); };
    // 创建客户端界面。
    auto create = [=, this](QString previous) {
        auto* form = dialog(owner, "createDeliveryDialog", QStringLiteral("新建文件交付"), 660, 500); auto* content = new QVBoxLayout(form);
        auto* recipient = new QLineEdit(peer); recipient->setObjectName("deliveryRecipient"); auto* title = new QLineEdit; title->setObjectName("deliveryTitle"); auto* note = new QPlainTextEdit; note->setObjectName("deliveryDescription"); note->setMaximumHeight(100);
        auto* fields = new QFormLayout; fields->addRow(QStringLiteral("接收人"), recipient); fields->addRow(QStringLiteral("标题"), title); fields->addRow(QStringLiteral("说明"), note); content->addLayout(fields);
        if (!previous.isEmpty()) content->addWidget(text(QStringLiteral("上一版本：") + previous));
        auto* paths = new QListWidget; paths->setObjectName("deliveryFiles"); content->addWidget(paths, 1);
        auto* row = new QHBoxLayout; content->addLayout(row);
        auto* selectFiles = action(row, QStringLiteral("选择 1～16 个文件"), [=, this] { const auto files = QFileDialog::getOpenFileNames(form, QStringLiteral("选择交付文件")); if (files.size() > 16) return; paths->clear(); for (const auto& f : files) { auto* item = new QListWidgetItem(f, paths); item->setData(Qt::UserRole, f); } }); selectFiles->setEnabled(true);
        action(row, QStringLiteral("创建并发送"), [=, this] { CreateDeliveryBundle command; command.operationId = s(QUuid::createUuid().toString(QUuid::WithoutBraces)); command.recipient = s(recipient->text().trimmed()); command.title = s(title->text()); command.description = s(note->toPlainText()); command.previousId = s(previous);
            for (int i = 0; i < paths->count(); ++i) command.files.emplace_back(paths->item(i)->data(Qt::UserRole).toString().toStdWString()); if (post(command)) form->close(); }); form->show();
    };
    auto* fresh = action(actions, QStringLiteral("新建交付"), [=] { create({}); }); fresh->setObjectName("newDelivery");
    action(actions, QStringLiteral("创建下一版"), [=] { if (!chosen().isEmpty()) create(chosen()); });
    action(actions, QStringLiteral("恢复选中文件"), [=, this] { auto* i = tree->currentItem(); if (i && !i->data(1, Qt::UserRole).toString().isEmpty()) post(ResumeDeliveryItem{s(chosen()), s(i->data(1, Qt::UserRole).toString())}); });
    action(actions, QStringLiteral("取消交付"), [=, this] { if (!chosen().isEmpty()) post(CancelDeliveryBundle{s(chosen())}); });
    auto* confirm = action(actions, QStringLiteral("确认收到"), [=, this] { const auto id = s(chosen()); for (const auto& b : bundles) if (b.id == id) post(ConfirmDelivery{id, b.manifestDigest}); }); confirm->setObjectName("confirmDelivery");
    box->addWidget(text(QStringLiteral("文件验证完成与人工确认分别记录。修改交付创建新版本；已取消或源文件变化的文件通过新版本发送。")));
    auto* timer = new QTimer(d); auto last = std::make_shared<std::string>();
    // 更新客户端界面。
    auto update = [=, this] {
        const auto data = nlohmann::json(bundles).dump(); if (*last == data) return; *last = data;
        const auto selected = chosen(); const auto itemId = tree->currentItem() ? tree->currentItem()->data(1, Qt::UserRole).toString() : QString(); tree->clear();
        for (const auto& b : bundles) if (peer.isEmpty() || b.sender == s(peer) || b.recipient == s(peer)) {
            auto* head = new QTreeWidgetItem(tree, {q(b.title), QString::number(qRound(100 * deliveryProgress(b))) + "%", bundleLabels.value(static_cast<int>(b.state)), b.previousId.empty() ? QStringLiteral("首版") : QStringLiteral("上一版：") + q(b.previousId)}); head->setData(0, Qt::UserRole, q(b.id)); head->setToolTip(0, q(b.description)); head->setExpanded(true);
            if (selected == q(b.id) && itemId.isEmpty()) tree->setCurrentItem(head);
            for (const auto& i : b.items) {
                auto* item = new QTreeWidgetItem(head, {q(i.name), QString::number(i.authenticatedBytes) + "/" + QString::number(i.size), q(i.error.empty() ? i.cancelled ? "已取消" : i.complete ? "已验证" : "传输中" : i.error), {}});
                item->setData(0, Qt::UserRole, q(b.id)); item->setData(1, Qt::UserRole, q(i.id)); if (selected == q(b.id) && itemId == q(i.id)) tree->setCurrentItem(item);
            }
        }
    };
    QObject::connect(timer, &QTimer::timeout, d, update); timer->start(300); update(); d->show();
}

}
