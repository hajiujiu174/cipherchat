// Presentation-only shell. The two existing timelines and their commands share
// the same worker; native Rhine remains an independent presentation.
QStackedWidget *sessionPages = nullptr, *messengerPages = nullptr, *conversationPages = nullptr;
QWidget *messengerRoot = nullptr, *groupConversationPane = nullptr;
QSplitter *messageSplit = nullptr;
QSplitter *contactsSplit = nullptr;
QListWidget *recentList = nullptr, *contactGroups = nullptr;
QLineEdit *recentSearch = nullptr, *contactSearch = nullptr;
QLabel *contactDetails = nullptr, *composerFeedback = nullptr, *groupFeedback = nullptr, *attachmentRecipient = nullptr;
QLabel* chatRemark = nullptr;
QPushButton *contactChat = nullptr, *newGroupMessages = nullptr, *requestSectionButton = nullptr;
QComboBox* fileFilter = nullptr;
QComboBox* skinChoice = nullptr;
std::uint64_t skinChangeSequence = 0;
bool recentLayoutQueued = false;
QSystemTrayIcon* tray = nullptr;
QDialog *settingsDialog = nullptr, *createGroupDialog = nullptr;
MessengerPage activePage = MessengerPage::Messages;
ConversationTarget activeConversation, selectedContact;
bool sessionAuthenticated = false, explicitExit = false, loggingOut = false, presentationLoaded = false;
bool exitAfterSave = false;
std::string closingRequest, presentationScope, attachmentPeer;
std::int64_t lastGroup = 0;
std::uint64_t submissionSequence = 0;
struct Submission { std::string key, text; bool attachment = false; std::uint64_t draftRevision = 0; };
std::map<std::string, std::uint64_t> draftRevisions;
std::map<std::string, Submission> submissions;
std::map<std::string, Submission> identitySubmissions;
std::map<std::string, std::set<std::string>> unreadMessages;
std::map<std::string, int> readPositions, historyLimits;
std::map<std::string, std::int64_t> recentCreated;
std::map<std::string, GroupSendReport> groupReports;
std::map<std::string, PeerIdentity> deferredIdentities;

QString presentationKey() const { return "ui/messenger/" + q(presentationScope); }
QString remarkKey(const std::string& username) const {
    const auto scope = featureUi->scope;
    const auto payload = nlohmann::json::array({scope.server,scope.username,username}).dump();
    const auto hash = cc::crypto::sha256(payload);
    return "ui/contactRemarks/" + QString::fromLatin1(QByteArray(hash.data(),static_cast<int>(hash.size())).toHex());
}
QString contactRemark(const std::string& username) const {
    if (!dataDir || featureUi->scope.server.empty() || featureUi->scope.username.empty() || username.empty()) return {};
    QSettings preferences(QDir(dataDir->text()).filePath("preferences.ini"),QSettings::IniFormat);
    return preferences.value(remarkKey(username)).toString();
}
void updateChatRemark() {
    if (!chatRemark || !peer) return;
    const auto note = contactRemark(s(peer->text().trimmed()));
    chatRemark->setVisible(!note.isEmpty()); chatRemark->setToolTip(note);
    chatRemark->setText(chatRemark->fontMetrics().elidedText(QStringLiteral("备注：")+note.simplified(),Qt::ElideRight,std::max(80,peer->width())));
}
bool saveContactRemark(const std::string& username,const QString& note) {
    if (!sessionAuthenticated || username.empty() || note.size()>1024) return false;
    QSettings preferences(QDir(dataDir->text()).filePath("preferences.ini"),QSettings::IniFormat);
    const auto key=remarkKey(username); const bool had=preferences.contains(key); const auto previous=preferences.value(key);
    if (note.isEmpty()) preferences.remove(key); else preferences.setValue(key,note);
    preferences.sync(); if (preferences.status()!=QSettings::NoError) {
        if (had) preferences.setValue(key,previous); else preferences.remove(key);
        preferences.sync(); return false;
    }
    updateChatRemark(); return true;
}
void persistPresentation() {
    if (!presentationLoaded || presentationScope.empty()) return;
    nlohmann::json state{{"page", static_cast<int>(activePage)}, {"target", activeConversation.key()},
        {"unread", unreadMessages}, {"positions", readPositions}, {"created", recentCreated}};
    QSettings preferences(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat);
    preferences.setValue(presentationKey(), q(state.dump())); preferences.sync();
    if (preferences.status() != QSettings::NoError) notify(QStringLiteral("界面状态保存失败，请检查本地目录权限。"), true);
}
ConversationTarget targetFromKey(const std::string& key) const {
    ConversationTarget target;
    if (key.starts_with("group:")) { target.kind = ConversationTarget::Kind::Group; try { target.group = std::stoll(key.substr(6)); } catch (...) {} }
    else if (key.starts_with("peer:")) target.peer = key.substr(5);
    return target;
}
void restorePresentation() {
    const auto hash = cc::crypto::sha256(featureUi->scope.server + "/" + featureUi->scope.username);
    presentationScope = QByteArray(hash.data(), static_cast<int>(hash.size())).toHex().toStdString();
    unreadMessages.clear(); readPositions.clear(); recentCreated.clear(); historyLimits.clear();
    QSettings preferences(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat);
    try {
        const auto state = nlohmann::json::parse(s(preferences.value(presentationKey()).toString()));
        unreadMessages = state.value("unread", decltype(unreadMessages){});
        readPositions = state.value("positions", decltype(readPositions){});
        recentCreated = state.value("created", decltype(recentCreated){});
        activePage = static_cast<MessengerPage>(std::clamp(state.value("page", 0), 0, 2));
        activeConversation = targetFromKey(state.value("target", std::string{}));
    } catch (...) { activePage = MessengerPage::Messages; activeConversation = {ConversationTarget::Kind::Peer, sharedWorkspace.selectedPeer, 0}; }
    presentationLoaded = true;
    if (activeConversation.valid()) openConversation(activeConversation, false);
    else conversationPages->setCurrentIndex(2);
    navigate(activePage, false);
}
void navigate(MessengerPage page, bool save = true) {
    if (!sessionAuthenticated) return;
    activePage = page; messengerPages->setCurrentIndex(static_cast<int>(page));
    for (int i = 0; i < 3; ++i) if (auto* action = window.findChild<QPushButton*>(i == 0 ? "messagesNav" : i == 1 ? "contactsNav" : "filesNav")) action->setChecked(i == static_cast<int>(page));
    if (save) persistPresentation();
    renderRecent(); renderFiles();
}
void openConversation(ConversationTarget target, bool focus = true) {
    if (!target.valid()) { activeConversation = {}; peer->clear(); conversationPages->setCurrentIndex(2); navigate(MessengerPage::Messages); return; }
    activeConversation = std::move(target);
    if (!recentCreated.count(activeConversation.key())) recentCreated[activeConversation.key()] = QDateTime::currentMSecsSinceEpoch();
    if (activeConversation.kind == ConversationTarget::Kind::Peer) {
        if (!friendNames.count(activeConversation.peer) && temporaryPeers.insert(activeConversation.peer).second) { updateContacts(); filterContacts(); }
        peer->setText(q(activeConversation.peer)); conversationPages->setCurrentIndex(0); tabs->setCurrentIndex(1);
    } else {
        const int index = groupPicker->findData(static_cast<qlonglong>(activeConversation.group));
        if (index < 0) { activeConversation = {}; conversationPages->setCurrentIndex(2); return; }
        groupPicker->setCurrentIndex(index); conversationPages->setCurrentIndex(1); tabs->setCurrentIndex(2);
    }
    if (focus) { navigate(MessengerPage::Messages); (activeConversation.kind == ConversationTarget::Kind::Peer ? message : groupMessage)->setFocus(); }
    renderChats(); renderRecent(); saveWorkspace(); persistPresentation();
}
void markRead(const std::string& key) {
    if (unreadMessages[key].empty()) return;
    unreadMessages[key].clear(); persistPresentation(); renderRecent();
}
void markCurrentRead() {
    if (!sessionAuthenticated || !window.isVisible() || !window.isActiveWindow() || activePage != MessengerPage::Messages || !activeConversation.valid()) return;
    auto* area = activeConversation.kind == ConversationTarget::Kind::Peer ? chatScroll : groupScroll;
    if (!area->property("timelineRendering").toBool() && area->verticalScrollBar()->value() >= area->verticalScrollBar()->maximum() - 30) markRead(activeConversation.key());
}
void noteIncoming(const ChatRecord& item) {
    if (history.count(item.messageId) || item.from == currentUser) return;
    const auto key = item.groupId ? "group:" + std::to_string(item.groupId) : "peer:" + item.from;
    const auto id = item.logicalGroupMessageId.empty() ? item.messageId : item.from + ":" + item.logicalGroupMessageId;
    unreadMessages[key].insert(id); persistPresentation();
}
void renderRecent() {
    if (!recentList) return;
    struct Row { ConversationTarget target; QString title, preview; std::int64_t time = 0; };
    std::map<std::string, Row> rows;
    auto ensure = [&](const std::string& key) -> Row& {
        auto& row = rows[key]; row.target = targetFromKey(key);
        row.title = row.target.kind == ConversationTarget::Kind::Peer ? q(row.target.peer) : groups.count(row.target.group) ? q(groups.at(row.target.group).name) : QStringLiteral("群聊 %1").arg(row.target.group);
        return row;
    };
    for (const auto& [key, time] : recentCreated) ensure(key).time = time;
    for (const auto& [id, group] : groups) ensure("group:" + std::to_string(id));
    for (const auto& [name, identity] : deferredIdentities) ensure("peer:" + name).preview = QStringLiteral("待核对身份");
    for (const auto& [key, draft] : sharedWorkspace.drafts) if (!draft.empty() && targetFromKey(key).valid()) ensure(key);
    if (activeConversation.valid()) ensure(activeConversation.key());
    for (const auto& [id, item] : history) {
        const auto key = item.groupId ? "group:" + std::to_string(item.groupId) : "peer:" + (item.from == currentUser ? item.to : item.from);
        auto& row = ensure(key); if (row.preview.isEmpty() || item.timestamp >= row.time) { row.time = item.timestamp; row.preview = q(item.text).simplified(); }
    }
    for (const auto& [id, file] : transfers) { auto& row = ensure("peer:" + file.peer); if (row.preview.isEmpty() || file.timestamp >= row.time) { row.time = file.timestamp; row.preview = QStringLiteral("[文件] ") + q(file.name); } }
    std::vector<Row> sorted; for (auto& [key, row] : rows) if (row.target.valid()) sorted.push_back(row);
    std::sort(sorted.begin(), sorted.end(), [](const Row& a, const Row& b) { return a.time == b.time ? a.target.key() < b.target.key() : a.time > b.time; });
    const auto search = recentSearch->text().trimmed(); const int position = recentList->verticalScrollBar()->value();
    const QSignalBlocker blocked(recentList); recentList->clear();
    int totalUnread = 0; for (const auto& [key, ids] : unreadMessages) totalUnread += static_cast<int>(ids.size());
    for (const auto& row : sorted) {
        if (!search.isEmpty() && !row.title.contains(search, Qt::CaseInsensitive) && !q(row.target.key()).contains(search, Qt::CaseInsensitive)) continue;
        const auto key = row.target.key(); const auto count = unreadMessages[key].size();
        QString preview = row.preview.isEmpty() ? QStringLiteral("开始聊天") : row.preview;
        if (sharedWorkspace.drafts.count(key) && !sharedWorkspace.drafts.at(key).empty()) preview = QStringLiteral("[草稿] ") + q(sharedWorkspace.drafts.at(key)).simplified();
        const bool direct = row.target.kind == ConversationTarget::Kind::Peer;
        auto* item = new QListWidgetItem; item->setData(Qt::UserRole, q(key)); item->setData(Qt::UserRole + 1, static_cast<int>(count)); item->setSizeHint(QSize(0, 72));
        item->setData(Qt::UserRole + 2, row.title); item->setToolTip(row.title + "\n" + preview); recentList->addItem(item);
        auto* widget = new QWidget; widget->setAttribute(Qt::WA_TransparentForMouseEvents); widget->setMinimumWidth(0); auto* box = new QHBoxLayout(widget); box->setSizeConstraint(QLayout::SetNoConstraint); box->setContentsMargins(9, 9, 9, 9); box->setSpacing(9);
        box->addWidget(avatarLabel(row.title, 36)); auto* texts = new QVBoxLayout; texts->setSpacing(3);
        auto* title = label(row.title + (direct ? ((online.count(row.target.peer) and online.at(row.target.peer)) ? QStringLiteral("  ●") : QString{}) : QStringLiteral("  [群]"))); title->setWordWrap(false); title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); title->setStyleSheet("font-weight:600;background:transparent;"); texts->addWidget(title);
        auto* summary = label(window.fontMetrics().elidedText(preview, Qt::ElideRight, std::max(80, recentList->viewport()->width() - 126))); summary->setObjectName("recentPreview"); summary->setWordWrap(false); summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); texts->addWidget(summary); box->addLayout(texts, 1);
        auto* meta = new QVBoxLayout; auto* time = label(row.time ? QDateTime::fromMSecsSinceEpoch(row.time).toString("HH:mm") : QString{}); time->setObjectName("recentTime"); time->setWordWrap(false); time->setFixedWidth(42); time->setAlignment(Qt::AlignRight); meta->addWidget(time);
        if (count) { auto* badge = label(QString::number(count)); badge->setObjectName("unreadBadge"); badge->setAlignment(Qt::AlignCenter); meta->addWidget(badge); } else meta->addStretch();
        box->addLayout(meta); recentList->setItemWidget(item, widget); if (key == activeConversation.key()) recentList->setCurrentItem(item);
    }
    recentList->verticalScrollBar()->setValue(position);
    // Splitter changes can reach the list before its viewport receives the new
    // size. Settle index-widget widths after the pending Qt layout completes.
    if (!recentLayoutQueued) {
        recentLayoutQueued = true;
        QTimer::singleShot(0, &window, [this] {
            recentLayoutQueued = false; recentList->doItemsLayout();
            const int width = std::max(0, recentList->viewport()->width() - (pencilSkin ? 18 : 8));
            for (int i = 0; i < recentList->count(); ++i) if (auto* row = recentList->itemWidget(recentList->item(i))) row->setMaximumWidth(width);
        });
    }
    if (tray) tray->setToolTip(QStringLiteral("CipherChat · %1\n%2 条未读消息%3").arg(q(currentUser)).arg(totalUnread).arg(deferredIdentities.empty() ? QString{} : QStringLiteral("\n%1 个身份待核对").arg(deferredIdentities.size())));
    window.setProperty("unreadCount", totalUnread);
}
void refreshContactGroups() {
    if (!contactGroups) return;
    contactGroups->clear(); for (const auto& [id, group] : groups) {
        auto* item = new QListWidgetItem(q(group.name) + QStringLiteral("\n%1 人").arg(group.members.size()), contactGroups); item->setData(Qt::UserRole, static_cast<qlonglong>(id)); item->setSizeHint(QSize(220, 56));
    }
    filterContacts();
}
void filterContacts() {
    if (!contactSearch) return;
    const auto text = contactSearch->text().trimmed();
    for (auto* list : {contacts, contactGroups, friendRequests, temporaryChats}) if (list) for (int i = 0; i < list->count(); ++i) {
        auto* item = list->item(i); const auto value = item->data(Qt::UserRole).toString();
        const auto title = list == contactGroups ? item->text() : value.isEmpty() ? item->text() : value;
        item->setHidden(!text.isEmpty() && !title.contains(text, Qt::CaseInsensitive));
    }
    if (requestSectionButton) requestSectionButton->setText(incomingRequests.empty() ? QStringLiteral("新朋友") : QStringLiteral("新朋友 (%1)").arg(incomingRequests.size()));
}
void displayContact(ConversationTarget target) {
    selectedContact = std::move(target); contactChat->setEnabled(selectedContact.valid());
    if (selectedContact.kind == ConversationTarget::Kind::Peer) contactDetails->setText(q(selectedContact.peer) + "\n\n" + ((online.count(selectedContact.peer) and online.at(selectedContact.peer)) ? QStringLiteral("在线") : QStringLiteral("离线")) + QStringLiteral("\n\n私密聊天 · 身份核对从会话标题进入"));
    else if (groups.count(selectedContact.group)) { QStringList names; const auto& group = groups.at(selectedContact.group); for (const auto& name : group.members) names << q(name); contactDetails->setText(q(group.name) + QStringLiteral("\n\n成员（%1）\n").arg(names.size()) + names.join(QStringLiteral("、"))); }
}
void openNewChat() {
    if (!sessionAuthenticated) return;
    auto* dialog = new QDialog(&window); dialog->setObjectName("newChatDialog"); dialog->setWindowModality(Qt::WindowModal); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(QStringLiteral("发起聊天")); dialog->resize(360, 180);
    auto* layout = new QVBoxLayout(dialog); layout->addWidget(label(QStringLiteral("接收人用户名"))); auto* input = field("newChatPeer"); layout->addWidget(input); auto* start = button("confirmNewChat", QStringLiteral("开始聊天"), "primary"); layout->addWidget(start);
    auto confirm = [this, dialog, input] {
        const auto name = input->text().trimmed();
        if (!QRegularExpression("^[A-Za-z0-9_]{3,32}$").match(name).hasMatch() || name == q(currentUser)) { notify(QStringLiteral("请输入其他用户的有效用户名。"), true); return; }
        openConversation({ConversationTarget::Kind::Peer, s(name), 0}); dialog->accept();
    };
    QObject::connect(start, &QPushButton::clicked, dialog, confirm); QObject::connect(input, &QLineEdit::returnPressed, dialog, confirm); dialog->show(); input->setFocus();
}
void openAddFriend() {
    if (accountPhase != AccountPhase::Online) { notify(QStringLiteral("请恢复连接后添加好友。"), true); return; }
    navigate(MessengerPage::Contacts);
    if (directorySection->currentIndex() == 2) searchStrangers->click(); else directorySection->setCurrentIndex(2);
    strangerQuery->setFocus(); strangerQuery->selectAll();
}
void openCreateGroup() {
    if (accountPhase != AccountPhase::Online) { notify(QStringLiteral("请恢复连接后创建群聊。"), true); return; }
    if (!createGroupDialog) {
        createGroupDialog = new QDialog(&window); createGroupDialog->setObjectName("createGroupDialog"); createGroupDialog->setWindowModality(Qt::WindowModal); createGroupDialog->setWindowTitle(QStringLiteral("创建群聊")); createGroupDialog->resize(430, 520);
        auto* layout = new QVBoxLayout(createGroupDialog); layout->addWidget(label(QStringLiteral("群名"))); layout->addWidget(groupName);
        auto* choices = new QListWidget; choices->setObjectName("groupContactChoices"); layout->addWidget(choices, 1);
        layout->addWidget(label(QStringLiteral("补充用户名（逗号分隔，含自己共 2–64 人）"))); layout->addWidget(groupMembers);
        auto* create = button("confirmCreateGroup", QStringLiteral("创建"), "primary"); layout->addWidget(create);
        QObject::connect(create, &QPushButton::clicked, &window, [this, choices] {
            std::set<std::string> selected; for (int i = 0; i < choices->count(); ++i) if (choices->item(i)->checkState() == Qt::Checked) selected.insert(s(choices->item(i)->text()));
            for (const auto& name : groupMembers->text().split(QRegularExpression(QStringLiteral("[,，\\s]+")), Qt::SkipEmptyParts)) selected.insert(s(name));
            selected.erase(currentUser); if (groupName->text().trimmed().isEmpty() || selected.empty() || selected.size() > 63) { notify(QStringLiteral("请填写群名，选择 1–63 位其他成员。"), true); return; }
            if (post(CreateGroup{s(groupName->text().trimmed()), {selected.begin(), selected.end()}})) createGroupDialog->accept();
        });
    }
    auto* choices = createGroupDialog->findChild<QListWidget*>("groupContactChoices"); choices->clear();
    for (const auto& name : friendNames) { auto* item = new QListWidgetItem(q(name), choices); item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(Qt::Unchecked); }
    createGroupDialog->show(); createGroupDialog->raise(); groupName->setFocus();
}
void showGroupReport(const std::string& logicalId) {
    const auto found = groupReports.find(logicalId); if (found == groupReports.end()) { notify(QStringLiteral("该消息没有失败成员报告，可从消息状态查看签收。")); return; }
    auto* dialog = new QDialog(&window); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setObjectName("groupReportDialog"); dialog->setWindowTitle(QStringLiteral("群消息发送结果")); dialog->resize(500, 360); auto* layout = new QVBoxLayout(dialog);
    auto* table = new QTableWidget(static_cast<int>(found->second.members.size()), 2); table->setHorizontalHeaderLabels({QStringLiteral("成员"), QStringLiteral("结果")}); table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bool failed = false; for (int i = 0; i < table->rowCount(); ++i) { const auto& row = found->second.members[i]; failed |= !row.messageId; table->setItem(i, 0, new QTableWidgetItem(q(row.recipient))); table->setItem(i, 1, new QTableWidgetItem(row.messageId ? QStringLiteral("已进入发送队列") : q(row.error))); }
    layout->addWidget(table); auto* retry = button("retryReportMembers", QStringLiteral("重试失败成员")); retry->setEnabled(failed && accountPhase == AccountPhase::Online); layout->addWidget(retry);
    QObject::connect(retry, &QPushButton::clicked, dialog, [this, logicalId, dialog] { post(RetryGroup{logicalId}); dialog->accept(); }); dialog->show();
}
void showHistory() {
    if (!activeConversation.valid()) return;
    const auto target = activeConversation;
    auto* dialog = new QDialog(&window); dialog->setObjectName("conversationHistoryDialog"); dialog->setWindowModality(Qt::WindowModal); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(QStringLiteral("当前会话的聊天记录")); dialog->resize(560, 480);
    auto* layout = new QVBoxLayout(dialog); auto* search = field("historySearch"); search->setPlaceholderText(QStringLiteral("搜索当前会话的本地记录")); layout->addWidget(search); auto* results = new QListWidget; layout->addWidget(results, 1);
    auto populate = [this, search, results, target] {
        results->clear(); std::vector<ChatRecord> found; std::set<std::string> logical;
        for (const auto& [id, item] : history) if ((target.kind == ConversationTarget::Kind::Group ? item.groupId == target.group : !item.groupId && (item.from == target.peer || item.to == target.peer)) && q(item.text).contains(search->text(), Qt::CaseInsensitive)) {
            if (item.groupId && !item.logicalGroupMessageId.empty() && !logical.insert(item.from + item.logicalGroupMessageId).second) continue;
            found.push_back(item);
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.timestamp > b.timestamp; });
        for (const auto& item : found) { auto* row = new QListWidgetItem(q(item.from) + "  " + QDateTime::fromMSecsSinceEpoch(item.timestamp).toString("MM-dd HH:mm") + "\n" + q(item.text).left(160), results); row->setData(Qt::UserRole, q(item.messageId)); }
    };
    QObject::connect(search, &QLineEdit::textChanged, dialog, populate); populate();
    QObject::connect(results, &QListWidget::itemClicked, dialog, [this, dialog, target](QListWidgetItem* item) {
        historyLimits[target.key()] = static_cast<int>(history.size() + transfers.size()); openConversation(target);
        auto* area = target.kind == ConversationTarget::Kind::Group ? groupScroll : chatScroll;
        const auto key = "message:" + item->data(Qt::UserRole).toString();
        QTimer::singleShot(0, area->widget(), [area, key] { for (auto* bubble : area->widget()->findChildren<QWidget*>()) if (bubble->property("timelineKey").toString() == key) { area->ensureWidgetVisible(bubble); break; } }); dialog->accept();
    }); dialog->show(); search->setFocus();
}
void setSendHabit(bool enter) {
    for (auto* input : {static_cast<MessageComposer*>(message), static_cast<MessageComposer*>(groupMessage)}) { input->enterSends = enter; input->setPlaceholderText(enter ? QStringLiteral("Enter 发送 · Shift+Enter 换行") : QStringLiteral("Ctrl+Enter 发送 · Enter 换行")); }
    QSettings preferences(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat); preferences.setValue("ui/enterSends", enter);
}
void showSettings(bool display = true) {
    if (!settingsDialog) {
        settingsDialog = new QDialog(&window); settingsDialog->setObjectName("settingsDialog"); settingsDialog->setWindowModality(Qt::WindowModal); settingsDialog->setWindowTitle(QStringLiteral("设置")); settingsDialog->resize(490, 350);
        auto* layout = new QVBoxLayout(settingsDialog); auto* pages = new QTabWidget; layout->addWidget(pages);
        auto page = [&](const QString& title) { auto* widget = new QWidget; auto* box = new QVBoxLayout(widget); box->setContentsMargins(18, 18, 18, 18); pages->addTab(widget, title); return box; };
        auto* look = page(QStringLiteral("外观")); look->addWidget(label(QStringLiteral("界面皮肤"))); skinChoice = new QComboBox; skinChoice->setObjectName("skinChoice"); skinChoice->addItem(QStringLiteral("默认皮肤"), "classic"); skinChoice->addItem(QStringLiteral("彩铅手绘 · 云朵气泡"), "pencil"); skinChoice->setCurrentIndex(pencilSkin ? 1 : 0); look->addWidget(skinChoice);
        QObject::connect(skinChoice, &QComboBox::currentIndexChanged, &window, [this](int index) { changeSkin(index == 1); });
        look->addWidget(appearance); appearance->show(); auto* color = button("settingsThemeColor", QStringLiteral("主题颜色")); look->addWidget(color); QObject::connect(color, &QPushButton::clicked, settingsDialog, [this] { chooseColor(); }); look->addStretch();
        auto* chat = page(QStringLiteral("聊天")); auto* habit = new QComboBox; habit->setObjectName("sendHabit"); habit->addItems({QStringLiteral("Enter 发送，Shift+Enter 换行"), QStringLiteral("Ctrl+Enter 发送，Enter 换行")}); habit->setCurrentIndex(static_cast<MessageComposer*>(message)->enterSends ? 0 : 1); chat->addWidget(habit); chat->addWidget(label(QStringLiteral("中文输入法选词时不会发送消息。"))); chat->addStretch(); QObject::connect(habit, &QComboBox::currentIndexChanged, settingsDialog, [this](int index) { setSendHabit(index == 0); });
        auto* files = page(QStringLiteral("文件")); files->addWidget(directEnabled); directEnabled->show(); files->addStretch();
        auto* connection = page(QStringLiteral("连接")); auto* summary = label({}); summary->setObjectName("connectionSummary"); summary->setTextInteractionFlags(Qt::TextSelectableByMouse); connection->addWidget(summary); connection->addWidget(label(QStringLiteral("连接配置只读。退出登录后，可在登录页的连接设置中修改。"))); connection->addStretch();
        auto* about = page(QStringLiteral("关于")); QString version = QStringLiteral("开发版"); QFile manifest(QCoreApplication::applicationDirPath() + "/release.json"); if (manifest.open(QIODevice::ReadOnly)) { try { version = q(nlohmann::json::parse(manifest.readAll().toStdString()).value("version", std::string{"开发版"})); } catch (...) {} }
        about->addWidget(label(QStringLiteral("CipherChat\n版本：") + version + QStringLiteral("\n端到端加密私聊、群聊及文件传输"))); about->addStretch();
    }
    settingsDialog->findChild<QLabel*>("connectionSummary")->setText(host->text() + ":" + QString::number(port->value()) + "\n" + q(startupConnection.expectedServerName) + "\n\n" + dataDir->text());
    if (display) { settingsDialog->show(); settingsDialog->raise(); settingsDialog->activateWindow(); }
}
void changeSkin(bool selected) {
    struct Anchor { QScrollArea* area; QString key; int offset, position; bool bottom; };
    std::vector<Anchor> anchors;
    for (auto* area : {chatScroll, groupScroll}) {
        const auto* bar = area->verticalScrollBar(); Anchor anchor{area, {}, 0, bar->value(), bar->value() >= bar->maximum() - 30};
        if (!anchor.bottom) for (auto* bubble : area->widget()->findChildren<QWidget*>()) {
            if (!bubble->property("timelineKey").isValid()) continue;
            const int top = bubble->mapTo(area->viewport(), QPoint()).y();
            if (top + bubble->height() > 0 && top < area->viewport()->height()) { anchor.key = bubble->property("timelineKey").toString(); anchor.offset = top; break; }
        }
        anchors.push_back(anchor);
    }
    pencilSkin = selected; applyDesign(); renderRecent(); const auto sequence = ++skinChangeSequence;
    QTimer::singleShot(0, &window, [this, sequence, anchors] {
        if (sequence != skinChangeSequence) return;
        for (const auto& anchor : anchors) {
            auto* bar = anchor.area->verticalScrollBar(); int position = anchor.bottom ? bar->maximum() : anchor.position;
            if (!anchor.bottom && !anchor.key.isEmpty()) for (auto* bubble : anchor.area->widget()->findChildren<QWidget*>()) if (bubble->property("timelineKey").toString() == anchor.key) { position = bubble->mapTo(anchor.area->widget(), QPoint()).y() - anchor.offset; break; }
            bar->setValue(position);
        }
    });
    QSettings preferences(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat); preferences.setValue("ui/skin", pencilSkin ? "pencil" : "classic"); preferences.sync();
    if (preferences.status() != QSettings::NoError) notify(QStringLiteral("皮肤已切换，但设置保存失败，请检查数据目录权限。"), true);
}
void submitText(bool group) {
    auto* input = group ? groupMessage : message; auto* feedback = group ? groupFeedback : composerFeedback;
    const ConversationTarget target = group ? ConversationTarget{ConversationTarget::Kind::Group, {}, groupPicker->currentData().toLongLong()} : ConversationTarget{ConversationTarget::Kind::Peer, s(peer->text().trimmed()), 0};
    if (!target.valid() || input->toPlainText().trimmed().isEmpty()) { feedback->setText(QStringLiteral("请选择会话并输入消息，草稿已保留。")); return; }
    if (accountPhase != AccountPhase::Online) { feedback->setText(QStringLiteral("连接已断开，请重连后发送，草稿已保留。")); return; }
    for (const auto& [id, value] : submissions) if (value.key == target.key() && !value.attachment) { feedback->setText(QStringLiteral("正在处理上一条提交，请稍候。")); return; }
    for (const auto& [id, value] : identitySubmissions) if (value.key == target.key() && value.text == s(input->toPlainText())) { feedback->setText(QStringLiteral("消息已保存，等待核对身份，请勿重复发送。")); post(InspectIdentity{target.peer}); return; }
    const auto id = "text-" + std::to_string(++submissionSequence); const auto text = s(input->toPlainText());
    Command command = group ? Command{SendGroup{target.group, text}} : Command{SendText{target.peer, text, 0, ""}};
    if (post(std::move(command), id)) { submissions[id] = {target.key(), text, false, draftRevisions[target.key()]}; feedback->setText(QStringLiteral("正在提交…")); }
}
void handleSubmitResult(const CommandResult& result) {
    if (result.requestId == closingRequest) {
        closingRequest.clear();
        messengerRoot->setEnabled(true);
        if (!result.accepted) { loggingOut = false; notify(QStringLiteral("草稿保存失败，已取消退出：") + (result.error ? q(result.error->message) : QString{}), true); return; }
        persistPresentation();
        if (exitAfterSave) { explicitExit = true; window.close(); }
        else { post(Disconnect{}); sessionAuthenticated = false; presentationLoaded = false; sessionPages->setCurrentIndex(0); password->clear(); recallLoginPassword(); attachmentPeer.clear(); filePath->clear(); for (auto* dialog : window.findChildren<QDialog*>()) dialog->close(); tray->hide(); window.setProperty("sessionAuthenticated", false); loggingOut = false; }
        return;
    }
    const auto found = submissions.find(result.requestId); if (found == submissions.end()) return;
    const auto submitted = found->second; submissions.erase(found);
    const auto target = targetFromKey(submitted.key);
    auto* feedback = target.kind == ConversationTarget::Kind::Group ? groupFeedback : composerFeedback;
    if (!result.accepted) { feedback->setText(result.error ? q(result.error->message) + QStringLiteral(" · 草稿已保留") : QStringLiteral("提交失败，草稿已保留")); return; }
    if (!submitted.attachment && target.kind == ConversationTarget::Kind::Peer && !result.operationId.empty() && (!featureUi->identities.count(target.peer) || !featureUi->identities.at(target.peer).manuallyVerified)) {
        identitySubmissions[result.operationId] = submitted; feedback->setText(QStringLiteral("消息已保存，等待核对身份；输入暂时保留。")); return;
    }
    if (submitted.attachment) { if (attachmentPeer == target.peer && s(filePath->text()) == submitted.text) { filePath->clear(); attachmentPeer.clear(); transferPanel->hide(); fileTransferToggle->setChecked(false); } }
    else {
        auto* input = target.kind == ConversationTarget::Kind::Group ? groupMessage : message;
        const bool visibleTarget = target.kind == ConversationTarget::Kind::Group ? groupPicker->currentData().toLongLong() == target.group : s(peer->text().trimmed()) == target.peer;
        if (draftRevisions[submitted.key] == submitted.draftRevision) {
            if (visibleTarget && s(input->toPlainText()) == submitted.text) input->clear();
            else if (sharedWorkspace.drafts[submitted.key] == submitted.text) { sharedWorkspace.drafts[submitted.key].clear(); saveWorkspace(); }
        }
    }
    feedback->setText(QStringLiteral("已提交，签收状态显示在消息下方。")); renderRecent();
}
void identitySubmissionProgress(const MessageStageEvent& event) {
    if (event.stage != MessageStage::EncryptedPersisted && event.stage != MessageStage::RecipientReceived) return;
    const auto found = identitySubmissions.find(event.messageId); if (found == identitySubmissions.end()) return;
    const auto submitted = found->second; identitySubmissions.erase(found);
    if (submitted.key == "peer:" + s(peer->text())) composerFeedback->setText(QStringLiteral("已提交，签收状态显示在消息下方。"));
    if (draftRevisions[submitted.key] == submitted.draftRevision) {
        if ("peer:" + s(peer->text()) == submitted.key && s(message->toPlainText()) == submitted.text) message->clear();
        else if (sharedWorkspace.drafts[submitted.key] == submitted.text) { sharedWorkspace.drafts[submitted.key].clear(); saveWorkspace(); }
    }
    renderRecent();
}
bool hasActiveTransfers() const { return std::any_of(transfers.begin(), transfers.end(), [](const auto& row) { return !row.second.complete && !row.second.cancelled; }); }
void requestLeave(bool exit, bool confirmed = false) {
    if (!sessionAuthenticated) { explicitExit = true; window.close(); return; }
    if (!closingRequest.empty()) return;
    if (!submissions.empty()) { QTimer::singleShot(50, &window, [this, exit, confirmed] { requestLeave(exit, confirmed); }); return; }
    if (!confirmed && hasActiveTransfers() && QMessageBox::question(&window, exit ? QStringLiteral("退出 CipherChat") : QStringLiteral("退出登录"), QStringLiteral("正在进行的传输会中断，已验证进度保留供续传。确定继续？"), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    loggingOut = true; exitAfterSave = exit; workspaceSave->stop(); saveWorkspace(); workspaceSave->stop();
    closingRequest = "save-close-" + std::to_string(++submissionSequence);
    messengerRoot->setEnabled(false);
    if (!post(SetWorkspace{sharedWorkspace}, closingRequest)) { closingRequest.clear(); loggingOut = false; messengerRoot->setEnabled(true); }
}
void syncSessionShell() {
    if (!sessionPages) return;
    if (accountPhase == AccountPhase::Online) {
        const bool first = !sessionAuthenticated; sessionAuthenticated = true; loggingOut = false; sessionPages->setCurrentIndex(1);
        if (auto* avatar = window.findChild<QPushButton*>("accountMenu")) { avatar->setIcon(QIcon(theme::avatar(q(currentUser),34))); avatar->setToolTip(q(currentUser)); }
        if (first) { restorePresentation(); tray->show(); }
    }
    window.setProperty("sessionAuthenticated", sessionAuthenticated);
    if (auto* reconnect = window.findChild<QPushButton*>("reconnectButton")) reconnect->setVisible(sessionAuthenticated && accountPhase != AccountPhase::Online);
    for (const auto& name : {"sendMessage", "sendGroup", "sendFile", "newConversation"}) if (auto* action = window.findChild<QPushButton*>(name)) action->setEnabled(accountPhase == AccountPhase::Online);
    if (sessionAuthenticated && accountPhase != AccountPhase::Online) notify(QStringLiteral("连接已断开，当前记录与草稿保留。点击“重新登录”恢复连接。"), true);
}
void buildMessengerShell() {
    auto* legacy = window.takeCentralWidget(); legacy->setObjectName("legacyPresentation");
    auto* root = new pencil::Paper; root->setObjectName("appRoot"); auto* outer = new QVBoxLayout(root); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    legacy->setParent(root); legacy->hide();
    sessionPages = new QStackedWidget; sessionPages->setObjectName("sessionPages"); outer->addWidget(sessionPages, 1);
    auto* loginPage = new pencil::Paper; auto* loginOuter = new QVBoxLayout(loginPage); loginOuter->addStretch(); auto* center = new QHBoxLayout; center->addStretch(); center->addWidget(accountCard); center->addStretch(); loginOuter->addLayout(center); loginOuter->addStretch(); accountCard->show(); sessionPages->addWidget(loginPage);
    messengerRoot = new QWidget; messengerRoot->setObjectName("messengerRoot"); auto* shell = new QHBoxLayout(messengerRoot); shell->setContentsMargins(0, 0, 0, 0); shell->setSpacing(0); sessionPages->addWidget(messengerRoot);
    sessionPages->addWidget(buildRegistrationPage());
    auto* nav = new pencil::Paper(pencil::Tone::Rose,false); nav->setObjectName("messengerNav"); nav->setFixedWidth(64); auto* navLayout = new QVBoxLayout(nav); navLayout->setContentsMargins(5, 12, 5, 12); navLayout->setSpacing(9);
    auto* avatar = button("accountMenu", QStringLiteral("账号")); avatar->setIcon(theme::icon(theme::Glyph::Account, activeAccent())); avatar->setText({}); avatar->setAccessibleName(QStringLiteral("账号菜单")); avatar->setIconSize(QSize(34,34)); avatar->setMinimumHeight(46); avatar->setToolTip(QStringLiteral("我的账号")); navLayout->addWidget(avatar);
    auto* accountMenu = new QMenu(avatar); accountMenu->addAction(QStringLiteral("我的账号与身份资料"), &window, [this] { showOwnIdentity(); }); accountMenu->addSeparator(); accountMenu->addAction(QStringLiteral("退出登录"), &window, [this] { requestLeave(false); }); avatar->setMenu(accountMenu);
    const char* names[] = {"messagesNav", "contactsNav", "filesNav"}; const QString titles[] = {QStringLiteral("消息"), QStringLiteral("联系人"), QStringLiteral("文件")};
    for (int i = 0; i < 3; ++i) { auto* action = button(names[i], titles[i]); action->setCheckable(true); navLayout->addWidget(action); QObject::connect(action, &QPushButton::clicked, &window, [this, i] { navigate(static_cast<MessengerPage>(i)); }); }
    navLayout->addStretch(); auto* more = button("mainMenu", QStringLiteral("菜单")); navLayout->addWidget(more); auto* menu = new QMenu(more); more->setMenu(menu);
    menu->addAction(QStringLiteral("设置"), &window, [this] { showSettings(); }); menu->addAction(QStringLiteral("软件更新"), &window, [this] { updateController->show(); });
#ifdef CCFW_HAS_RHINE_NATIVE
    menu->addAction(QStringLiteral("切换界面"), &window, [this] { const auto error = switchFrontend("rhine"); if (!error.isEmpty()) notify(error, true); });
#endif
    menu->addSeparator(); menu->addAction(QStringLiteral("退出"), &window, [this] { requestLeave(true); }); shell->addWidget(nav);
    auto* main = new QWidget; auto* mainBox = new QVBoxLayout(main); mainBox->setContentsMargins(0, 0, 0, 0); mainBox->setSpacing(0);
    auto* connection = new QHBoxLayout; connection->setContentsMargins(16, 8, 16, 6); connection->addWidget(status); status->show(); connection->addStretch(); auto* reconnect = button("reconnectButton", QStringLiteral("重新登录")); reconnect->hide(); connection->addWidget(reconnect); QObject::connect(reconnect, &QPushButton::clicked, &window, [this] { loggingOut = true; sessionAuthenticated = false; sessionPages->setCurrentIndex(0); tray->hide(); post(Disconnect{}); username->setText(q(currentUser)); recallLoginPassword(); password->setFocus(); }); mainBox->addLayout(connection);
    messengerPages = new QStackedWidget; messengerPages->setObjectName("messengerPages"); mainBox->addWidget(messengerPages, 1); shell->addWidget(main, 1);
    auto* messagesPage = new QWidget; auto* messagesBox = new QVBoxLayout(messagesPage); messagesBox->setContentsMargins(0, 0, 0, 0); messageSplit = new QSplitter; messageSplit->setObjectName("messageSplit"); messageSplit->setChildrenCollapsible(false); messagesBox->addWidget(messageSplit);
    auto* listPane = new pencil::Paper(pencil::Tone::Cream); listPane->setObjectName("recentPanel"); listPane->setMinimumWidth(240); listPane->setMaximumWidth(360); auto* listBox = new QVBoxLayout(listPane); listBox->setContentsMargins(10, 10, 10, 10); listBox->setSpacing(8); auto* searchRow = new QHBoxLayout;
    recentSearch = field("recentSearch"); recentSearch->setPlaceholderText(QStringLiteral("搜索消息会话")); searchRow->addWidget(recentSearch, 1); auto* plus = button("newConversation", "+"); plus->setAccessibleName(QStringLiteral("新建会话")); plus->setFixedWidth(34); plus->setToolTip(QStringLiteral("发起聊天、添加好友或创建群聊")); searchRow->addWidget(plus); listBox->addLayout(searchRow);
    auto* plusMenu = new QMenu(plus); plus->setMenu(plusMenu); plusMenu->addAction(QStringLiteral("发起聊天"), &window, [this] { openNewChat(); }); plusMenu->addAction(QStringLiteral("添加好友"), &window, [this] { openAddFriend(); }); plusMenu->addAction(QStringLiteral("创建群聊"), &window, [this] { openCreateGroup(); });
    recentList = new ResponsiveRecentList; recentList->setObjectName("recentConversations"); recentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); recentList->setContextMenuPolicy(Qt::CustomContextMenu); listBox->addWidget(recentList, 1); messageSplit->addWidget(listPane);
    conversationPages = new QStackedWidget; conversationPages->setObjectName("conversationPages"); conversationPages->addWidget(conversationPane); conversationPages->addWidget(groupConversationPane); auto* empty = label(QStringLiteral("选择左侧会话，或点击 ＋ 开始聊天")); empty->setAlignment(Qt::AlignCenter); conversationPages->addWidget(empty); conversationPages->setCurrentIndex(2); messageSplit->addWidget(conversationPages); messageSplit->setStretchFactor(1, 1); messageSplit->setSizes({280, 736}); messengerPages->addWidget(messagesPage);
    QObject::connect(recentSearch, &QLineEdit::textChanged, &window, [this] { renderRecent(); }); QObject::connect(recentList, &QListWidget::itemClicked, &window, [this](QListWidgetItem* item) { openConversation(targetFromKey(s(item->data(Qt::UserRole).toString()))); });
    QObject::connect(recentList, &QListWidget::customContextMenuRequested, &window, [this](const QPoint& position) { auto* item = recentList->itemAt(position); if (!item) return; const auto target = targetFromKey(s(item->data(Qt::UserRole).toString())); QMenu menu(recentList); menu.addAction(QStringLiteral("打开"), &window, [this, target] { openConversation(target); }); menu.addAction(QStringLiteral("标为已读"), &window, [this, target] { markRead(target.key()); }); menu.addAction(target.kind == ConversationTarget::Kind::Group ? QStringLiteral("群成员") : QStringLiteral("查看资料"), &window, [this, target] { displayContact(target); navigate(MessengerPage::Contacts); }); menu.exec(recentList->viewport()->mapToGlobal(position)); });
    auto* contactPage = new QWidget; auto* contactsLayout = new QHBoxLayout(contactPage); contactsLayout->setContentsMargins(0, 0, 0, 0); contactsLayout->setSpacing(0); contactSide->setMinimumWidth(240); contactSide->setMaximumWidth(360); contactsSplit = new QSplitter; contactsSplit->setChildrenCollapsible(false); contactsSplit->addWidget(contactSide); contactsLayout->addWidget(contactsSplit); contactSide->show(); directorySection->hide(); directoryHint->show();
    auto* contactBox = qobject_cast<QVBoxLayout*>(contactSide->layout()); contactBox->itemAt(0)->widget()->hide(); contactSearch = field("contactSearch"); contactSearch->setPlaceholderText(QStringLiteral("搜索当前联系人列表")); contactBox->insertWidget(0, contactSearch);
    auto* sectionRow = new QWidget; sectionRow->setObjectName("contactSections"); auto* sectionLayout = new QVBoxLayout(sectionRow); sectionLayout->setContentsMargins(0, 0, 0, 0); sectionLayout->setSpacing(4);
    auto* primarySections = new QHBoxLayout; primarySections->setSpacing(3); sectionLayout->addLayout(primarySections);
    auto* extraSections = new QHBoxLayout; extraSections->setSpacing(3); sectionLayout->addLayout(extraSections);
    contactGroups = new QListWidget; contactGroups->setObjectName("contactGroups"); directoryPages->addWidget(contactGroups); directorySection->addItem(QStringLiteral("群聊"));
    auto* sections = new QButtonGroup(sectionRow); sections->setExclusive(true);
    const char* sectionNames[]{"friendsSection", "groupsSection", "requestsSection", "temporarySection", "strangersSection"};
    const QStringList sectionTitles{QStringLiteral("好友"), QStringLiteral("群聊"), QStringLiteral("新朋友"), QStringLiteral("临时会话"), QStringLiteral("陌生人")};
    const int sectionIndexes[]{0, 4, 3, 1, 2};
    for (int i = 0; i < 5; ++i) {
        auto* action = button(sectionNames[i], sectionTitles[i]); action->setCheckable(true); action->setChecked(directorySection->currentIndex() == sectionIndexes[i]);
        if (i == 2) requestSectionButton = action;
        sections->addButton(action, sectionIndexes[i]); (i < 3 ? primarySections : extraSections)->addWidget(action);
        QObject::connect(action, &QPushButton::clicked, &window, [this, index = sectionIndexes[i]] { directorySection->setCurrentIndex(index); });
    }
    QObject::connect(directorySection, &QComboBox::currentIndexChanged, &window, [this, sections](int index) { if (auto* action = sections->button(index)) action->setChecked(true); contactSearch->setVisible(index != 2); filterContacts(); });
    contactBox->insertWidget(1, sectionRow);
    QObject::connect(contactSearch, &QLineEdit::textChanged, &window, [this] { filterContacts(); });
    auto* details = new pencil::Paper; auto* detailsBox = new QVBoxLayout(details); detailsBox->setContentsMargins(30, 30, 30, 30); detailsBox->addStretch(); contactDetails = label(QStringLiteral("选择联系人查看资料")); contactDetails->setObjectName("contactDetails"); contactDetails->setTextInteractionFlags(Qt::TextSelectableByMouse); detailsBox->addWidget(contactDetails); contactChat = button("contactStartChat", QStringLiteral("发消息"), "primary"); contactChat->setEnabled(false); detailsBox->addWidget(contactChat); detailsBox->addStretch(); contactsSplit->addWidget(details); contactsSplit->setStretchFactor(1, 1); contactsSplit->setSizes({280, 736}); messengerPages->addWidget(contactPage);
    QObject::connect(contactChat, &QPushButton::clicked, &window, [this] { openConversation(selectedContact); });
    for (auto* list : {contacts, temporaryChats, strangers}) {
        QObject::connect(list, &QListWidget::itemClicked, &window, [this](QListWidgetItem* item) { displayContact({ConversationTarget::Kind::Peer, s(item->data(Qt::UserRole).toString()), 0}); });
        QObject::connect(list, &QListWidget::itemDoubleClicked, &window, [this](QListWidgetItem* item) { openConversation({ConversationTarget::Kind::Peer, s(item->data(Qt::UserRole).toString()), 0}); });
    }
    QObject::connect(contactGroups, &QListWidget::itemClicked, &window, [this](QListWidgetItem* item) { displayContact({ConversationTarget::Kind::Group, {}, item->data(Qt::UserRole).toLongLong()}); }); QObject::connect(contactGroups, &QListWidget::itemDoubleClicked, &window, [this](QListWidgetItem* item) { openConversation({ConversationTarget::Kind::Group, {}, item->data(Qt::UserRole).toLongLong()}); });
    auto* filesBox = qobject_cast<QVBoxLayout*>(fileQueuePane->layout()); filesBox->itemAt(0)->widget()->hide();
    auto* fileToolbar = new QWidget; auto* fileToolbarBox = new QHBoxLayout(fileToolbar); fileToolbarBox->setContentsMargins(12, 0, 12, 0); fileToolbarBox->setSpacing(12);
    fileFilter = new QComboBox; fileFilter->setObjectName("fileFilter"); fileFilter->addItems({QStringLiteral("全部传输"), QStringLiteral("进行中"), QStringLiteral("已完成")}); fileFilter->setFixedWidth(148); fileToolbarBox->addWidget(fileFilter);
    auto* summary = label({}); summary->setObjectName("fileQueueSummary"); fileToolbarBox->addWidget(summary); fileToolbarBox->addStretch();
    filesBox->insertWidget(0, fileToolbar); messengerPages->addWidget(fileQueuePane); QObject::connect(fileFilter, &QComboBox::currentIndexChanged, &window, [this] { renderFiles(); });
    auto* notification = new QWidget; auto* noticeLayout = new QHBoxLayout(notification); noticeLayout->setContentsMargins(12, 6, 12, 8); noticeLayout->addWidget(banner); banner->show(); outer->addWidget(notification);
    auto* focusSearch = new QShortcut(QKeySequence::Find, &window); QObject::connect(focusSearch, &QShortcut::activated, &window, [this] { if (sessionAuthenticated) { auto* input = activePage == MessengerPage::Contacts ? (directorySection->currentIndex() == 2 ? strangerQuery : contactSearch) : recentSearch; if (activePage == MessengerPage::Files) navigate(MessengerPage::Messages); input->setFocus(); input->selectAll(); } });
    tray = new QSystemTrayIcon(theme::icon(theme::Glyph::Account, activeAccent()), &window); tray->setObjectName("messengerTray"); auto* trayMenu = new QMenu(&window); trayMenu->addAction(QStringLiteral("打开 CipherChat"), &window, [this] { window.showNormal(); window.raise(); window.activateWindow(); }); trayMenu->addAction(QStringLiteral("退出"), &window, [this] { window.show(); requestLeave(true); }); tray->setContextMenu(trayMenu); QObject::connect(tray, &QSystemTrayIcon::activated, &window, [this](QSystemTrayIcon::ActivationReason reason) { if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) { window.showNormal(); window.raise(); window.activateWindow(); } });
    window.setCentralWidget(root); sessionPages->setCurrentIndex(0); window.setProperty("sessionAuthenticated", false);
    QSettings preferences(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat); setSendHabit(preferences.value("ui/enterSends", true).toBool());
    auto* privateHeader = qobject_cast<QVBoxLayout*>(conversationPane->layout())->itemAt(0)->layout(); auto* chatMenuButton = button("conversationMenu", QStringLiteral("详情")); privateHeader->addWidget(chatMenuButton); auto* chatMenu = new QMenu(chatMenuButton); chatMenuButton->setMenu(chatMenu); chatMenu->addAction(QStringLiteral("聊天记录"), &window, [this] { showHistory(); }); chatMenu->addAction(QStringLiteral("UID 与备注"), &window, [this] { const auto name=s(peer->text().trimmed()); if (name.empty()) return; const auto found=featureUi->identities.find(name); if (found!=featureUi->identities.end()) featureUi->identityCard(found->second); else post(InspectIdentity{name}); }); chatMenu->addAction(QStringLiteral("文件交付与版本"), &window, [this] { featureUi->deliveries(peer->text()); });
    auto* groupMenuButton = button("groupConversationMenu", QStringLiteral("详情")); qobject_cast<QVBoxLayout*>(groupConversationPane->layout())->insertWidget(1, groupMenuButton, 0, Qt::AlignRight); auto* groupMenu = new QMenu(groupMenuButton); groupMenuButton->setMenu(groupMenu); groupMenu->addAction(QStringLiteral("群成员"), &window, [this] { displayContact(activeConversation); navigate(MessengerPage::Contacts); }); groupMenu->addAction(QStringLiteral("聊天记录"), &window, [this] { showHistory(); });
    for (auto* area : {chatScroll, groupScroll}) QObject::connect(area->verticalScrollBar(), &QScrollBar::valueChanged, &window, [this, area](int position) {
        if (area->property("timelineRendering").toBool() || !presentationLoaded) return;
        const auto key = s(area->property("timelineTarget").toString());
        if (!key.empty()) { readPositions[key] = position >= area->verticalScrollBar()->maximum() - 30 ? -1 : position; persistPresentation(); }
        auto* indicator = area == chatScroll ? newMessages : newGroupMessages; if (position >= area->verticalScrollBar()->maximum() - 30) indicator->hide(); markCurrentRead();
    });
    showSettings(false); applyDesign(false); renderRecent();
}
