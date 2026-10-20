#include "main_window.hpp"
#include "local_file_actions.hpp"
#include "pencil_skin.hpp"
#include "messenger.hpp"
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QUrl>
#include "../src/feature_codec.hpp"
#include "business_worker.hpp"
#include "feature_ui.hpp"
#include "login_preferences.hpp"
#include "ccfw/delivery_service.hpp"
#include "theme.hpp"
#include "codex_theme.hpp"
#include "accent_theme.hpp"
#include "appearance_theme.hpp"
#include "appearance_switch.hpp"
#include "color_picker.hpp"
#include "update_controller.hpp"
#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDate>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QProgressBar>
#include <QFileInfo>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <set>
#ifdef CCFW_HAS_RHINE_NATIVE
#include "rhine_native/chat_bridge.hpp"
#include "rhine_native/archive_field.hpp"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJSValue>
static void initializeRhineResources() { Q_INIT_RESOURCE(rhine_native); }
#endif
namespace ccfw::gui {
namespace {
QString q(const std::string& text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }
std::string s(const QString& text) { const auto bytes = text.toUtf8(); return {bytes.constData(), static_cast<std::size_t>(bytes.size())}; }
std::filesystem::path path(const QString& text) { return std::filesystem::path(text.toStdWString()); }
QString qpath(const std::filesystem::path& value) { return QString::fromStdWString(value.wstring()); }
QString fingerprint(const PeerIdentity& identity) {
    if (identity.signingPublicKey.empty() || identity.dhPublicKey.empty()) return QStringLiteral("登录后显示");
    const auto bytes = cc::crypto::sha256(identity.signingPublicKey + identity.dhPublicKey);
    const auto hex = QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())).toHex().toUpper();
    QStringList groups; for (int i = 0; i < hex.size(); i += 8) groups << QString::fromLatin1(hex.mid(i, 8));
    return groups.join(' ');
}
QString amount(std::uint64_t bytes) {
    if (bytes < 1024) return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < 1024 * 1024) return QString::number(double(bytes) / 1024, 'f', 1) + QStringLiteral(" KiB");
    return QString::number(double(bytes) / (1024 * 1024), 'f', 2) + QStringLiteral(" MiB");
}
int filePercent(const FileProgress& file) {
    return file.complete || file.cacheUploaded ? 100 : file.totalBytes ? static_cast<int>(file.authenticatedBytes * 100 / file.totalBytes) : 0;
}
QString fileState(const FileProgress& file) {
    if (file.cancelled) return QStringLiteral("已取消");
    if (file.complete) return file.outgoing ? QStringLiteral("已发送") : QStringLiteral("已接收");
    if (file.cacheUploaded) return QStringLiteral("已缓存 · 等待接收");
    return file.outgoing ? QStringLiteral("发送中") : QStringLiteral("接收中");
}
QLabel* label(const QString& text, QWidget* parent = nullptr) {
    auto* result = new QLabel(text, parent); result->setTextFormat(Qt::PlainText); result->setWordWrap(true); return result;
}
QLabel* messageLabel(const QString& value) {
    auto* result = label(value); result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // QLabel owns the selectable document. Keep that same document and original
    // text, while allowing a long identifier to wrap inside its assigned width.
    if (auto* document = result->findChild<QTextDocument*>()) {
        auto wrap = [document, result] {
            auto option = document->defaultTextOption();
            if (option.wrapMode() != QTextOption::WrapAtWordBoundaryOrAnywhere) { option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); document->setDefaultTextOption(option); result->updateGeometry(); }
        };
        QObject::connect(document->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, result, [wrap](const QSizeF&) { wrap(); }); wrap();
    }
    return result;
}
QLineEdit* field(const char* name, const QString& value = {}) {
    auto* result = new QLineEdit(value); result->setObjectName(QString::fromLatin1(name)); return result;
}
QPushButton* button(const char* name, const QString& text, const char* role = nullptr) {
    auto* result = new pencil::Button(text); result->setObjectName(QString::fromLatin1(name));
    if (role && *role) result->setProperty(role, true);
    return result;
}
QLabel* caption(const QString& text) { auto* result = label(text); result->setObjectName("panelCaption"); return result; }
QLabel* avatarLabel(const QString& name, int size) {
    auto* result = new pencil::Avatar; result->setPixmap(theme::avatar(name, size)); result->setFixedSize(size, size);
    result->setAlignment(Qt::AlignCenter); return result;
}
class ResponsiveRecentList final : public QListWidget {
protected:
    bool viewportEvent(QEvent* event) override {
        const bool handled = QListWidget::viewportEvent(event);
        if (event->type() == QEvent::Resize && !queued_) {
            queued_ = true;
            QTimer::singleShot(0, this, [this] {
                queued_ = false; doItemsLayout();
                const int width = std::max(0, viewport()->width() - (pencil::enabled(this) ? 18 : 8));
                for (int i = 0; i < count(); ++i) if (auto* row = itemWidget(item(i))) row->setMaximumWidth(width);
            });
        }
        return handled;
    }
private:
    bool queued_ = false;
};
QString dayText(const QDate& day) {
    const auto today = QDate::currentDate();
    if (day == today) return QStringLiteral("今天");
    if (day == today.addDays(-1)) return QStringLiteral("昨天");
    return day.year() == today.year() ? day.toString(QStringLiteral("M月d日")) : day.toString(QStringLiteral("yyyy年M月d日"));
}
// 页签容器仍由 QTabWidget 承担（页面发现与切换语义依赖它），
// 子类只在页签增删时通知侧边导航重建按钮。
class NavTabs final : public QTabWidget {
public:
    std::function<void()> structureChanged;
    void tabInserted(int) override { if (structureChanged) structureChanged(); }
    void tabRemoved(int) override { if (structureChanged) structureChanged(); }
};
class StageLabel final : public QLabel {
public:
    std::function<void()> clicked;
    void mousePressEvent(QMouseEvent* event) override { if (clicked) clicked(); QLabel::mousePressEvent(event); }
};
class ElidedFileLabel final : public QLabel {
public:
    explicit ElidedFileLabel(const QString& text = {}) : full_(text) {
        setTextFormat(Qt::PlainText); setWordWrap(false);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); setMinimumWidth(0);
        setFullText(text);
    }
    void setFullText(const QString& text) {
        full_ = text; setToolTip(text); setProperty("fullText", text); updateText();
    }
protected:
    void resizeEvent(QResizeEvent* event) override { QLabel::resizeEvent(event); updateText(); }
    void changeEvent(QEvent* event) override { QLabel::changeEvent(event); if (event->type() == QEvent::FontChange) updateText(); }
private:
    QString full_;
    void updateText() { setText(fontMetrics().elidedText(full_, Qt::ElideMiddle, std::max(0, contentsRect().width()))); }
};
}
struct MainWindow::Impl {
    MainWindow& window;
    BusinessWorker worker;
    LoginPreferences rememberedLogin;
    QCheckBox *rememberAccount = nullptr, *rememberPassword = nullptr;
    bool recalledPassword = false;
    QLineEdit *registrationUsername = nullptr, *registrationPassword = nullptr, *registrationConfirmation = nullptr;
    QPushButton *registrationSubmit = nullptr, *registrationBack = nullptr;
    QLabel* registrationFeedback = nullptr;
    bool registrationPending = false;
    std::unique_ptr<FeatureUi> featureUi;
    WorkspaceState sharedWorkspace;
    std::string lastPeer;
    bool restoringWorkspace = false, wideWorkbench = false;
    QTimer* workspaceSave = nullptr;
    QSplitter *contactSplit = nullptr, *workbenchSplit = nullptr;
    QWidget *contactSide = nullptr, *conversationPane = nullptr, *fileQueuePane = nullptr, *workbench = nullptr;
    QVBoxLayout* workbenchLayout = nullptr;
    QTabWidget* workbenchTabs = nullptr;
    QPushButton* newMessages = nullptr;
    QScrollArea* fileQueueScroll = nullptr;
#ifdef CCFW_HAS_RHINE_NATIVE
    std::unique_ptr<ChatBridge> nativeBridge;
    std::unique_ptr<QQmlApplicationEngine> nativeEngine;
    QQuickWindow* nativeWindow = nullptr;
    QVariantMap nativeSessionState;
#endif
    AccountPhase accountPhase = AccountPhase::Disconnected;
    UpdateController* updateController = nullptr;
    QTimer* timer = nullptr;
    NavTabs* tabs = nullptr;
    QLabel *status = nullptr, *banner = nullptr, *typing = nullptr, *members = nullptr;
    QLabel* chatAvatar = nullptr;
    QVBoxLayout* navButtonsLayout = nullptr;
    std::vector<QPushButton*> navButtons;
    bool darkMode = false;
    bool pencilSkin = false;
    QPushButton* appearance = nullptr;
    QColor customAccent;
    QWidget* accountCard = nullptr;
    QLineEdit *host = nullptr, *username = nullptr, *password = nullptr, *dataDir = nullptr;
    QLineEdit *peer = nullptr, *filePath = nullptr, *groupName = nullptr, *groupMembers = nullptr;
    QSpinBox* port = nullptr;
    QPushButton *login = nullptr, *registerAccount = nullptr;
    QPushButton *advancedToggle = nullptr, *ownSecurity = nullptr;
    PeerIdentity ownIdentity;
    QPushButton* sendFile = nullptr;
    QPushButton* fileTransferToggle = nullptr;
    QWidget* transferPanel = nullptr;
    std::map<std::string, QWidget*> fileCards;
    std::map<std::string, QWidget*> fileQueueRows;
    std::vector<std::string> fileQueueOrder;
    QWidget* fileQueueBody = nullptr;
    std::string highlightedFile;
    std::uint64_t fileAnchorSequence = 0;
    std::function<bool(const QString&)> localFileOpener, localFileRevealer;
    QPlainTextEdit *message = nullptr, *groupMessage = nullptr;
    QListWidget *contacts = nullptr, *strangers = nullptr, *temporaryChats = nullptr, *friendRequests = nullptr;
    QComboBox* directorySection = nullptr;
    QStackedWidget* directoryPages = nullptr;
    QLabel* directoryHint = nullptr;
    QLineEdit* strangerQuery = nullptr;
    QPushButton *searchStrangers = nullptr, *moreStrangers = nullptr;
    std::string activeQuery, nextStranger;
    std::set<std::string> incomingRequests, outgoingRequests, temporaryPeers;
    std::set<std::string> friendNames;
    std::map<std::string, bool> strangerOnline;
    QComboBox* groupPicker = nullptr;
    QTableWidget* reportTable = nullptr;
    QScrollArea *chatScroll = nullptr, *groupScroll = nullptr;
    std::string currentUser, lastGroupSend;
    std::map<std::string, bool> online, activeTyping;
    std::map<std::string, ChatRecord> history;
    std::map<std::string, FileProgress> transfers;
    std::map<std::int64_t, GroupSnapshot> groups;
    std::map<std::string, QDialog*> identityDialogs;
    std::map<std::string, std::string> promptedIdentities;
    std::set<std::string> requestedIdentities;
    bool shuttingDown = false;
    QCheckBox* directEnabled = nullptr;
    std::optional<bool> directOverride;
    ConnectionOptions startupConnection;
    std::string accountServerId;
#include "messenger_shell.inc"

    Impl(MainWindow& window, WindowOptions options) : window(window), worker(options.transportFactory), directOverride(options.directEnabled), startupConnection(options.connection), accountServerId(options.accountServerId) {
        localFileOpener = options.localFileOpener ? std::move(options.localFileOpener) : openLocalFile;
        localFileRevealer = options.localFileRevealer ? std::move(options.localFileRevealer) : revealLocalFile;
        featureUi = std::make_unique<FeatureUi>(&window, [this](Command command) { return post(std::move(command)); });
        featureUi->readRemark = [this](const std::string& username) { return contactRemark(username); };
        featureUi->saveRemark = [this](const std::string& username,const QString& note) { return saveContactRemark(username,note); };
        auto* root = new QWidget(&window); root->setObjectName("appRoot");
        auto* outer = new QVBoxLayout(root); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
        auto* top = new QWidget; top->setObjectName("topBar"); auto* topLayout = new QHBoxLayout(top);
        topLayout->setContentsMargins(20, 14, 20, 12); topLayout->setSpacing(10);
        auto* logo = new QLabel; logo->setPixmap(theme::brand(34)); logo->setFixedSize(34, 34);
        logo->setProperty("brandSize", 34);
        topLayout->addWidget(logo, 0, Qt::AlignVCenter);
        auto* brandStack = new QVBoxLayout; brandStack->setContentsMargins(0, 0, 0, 0); brandStack->setSpacing(1);
        auto* title = label(QStringLiteral("CipherChat")); title->setObjectName("brandTitle");
        auto* subtitle = label(QStringLiteral("私密聊天 · 便捷传输")); subtitle->setObjectName("brandSubtitle");
        brandStack->addWidget(title); brandStack->addWidget(subtitle);
        topLayout->addLayout(brandStack); topLayout->addStretch();
#ifdef CCFW_HAS_RHINE_NATIVE
        {
            auto* switchButton = button("switchQtRhine", QStringLiteral("Qt 复刻版"));
            switchButton->setToolTip(QStringLiteral("热切换到 Rhine 动态界面，保留登录状态、草稿和传输"));
            topLayout->addWidget(switchButton);
            QObject::connect(switchButton, &QPushButton::clicked, &window, [this] {
                const auto error = switchFrontend("rhine");
                if (!error.isEmpty()) notify(error, true);
            });
        }
#endif
        auto* colorButton = button("chooseThemeColor", QStringLiteral("主题色"));
        colorButton->setToolTip(QStringLiteral("自由选择当前设计的主题颜色"));
        topLayout->addWidget(colorButton);
        appearance = new AppearanceSwitch; topLayout->addWidget(appearance);
        QObject::connect(colorButton, &QPushButton::clicked, &window, [this] { chooseColor(); });
        status = label(QStringLiteral("未连接")); status->setObjectName("accountStatus");
        status->setWordWrap(false); status->setProperty("phase", QStringLiteral("idle"));
        topLayout->addWidget(status, 0, Qt::AlignVCenter);
        outer->addWidget(top);
        // 左侧导航列 + 页签容器：原生页签隐藏，导航按钮与页签索引一一对应。
        auto* body = new QWidget; auto* bodyLayout = new QHBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0); bodyLayout->setSpacing(0);
        auto* nav = new QWidget; nav->setObjectName("sideNav"); nav->setFixedWidth(164);
        auto* navLayout = new QVBoxLayout(nav); navLayout->setContentsMargins(12, 16, 12, 14); navLayout->setSpacing(4);
        auto* workspaceCaption = label(QStringLiteral("W O R K S P A C E")); workspaceCaption->setObjectName("workspaceCaption");
        navLayout->addWidget(workspaceCaption);
        navButtonsLayout = new QVBoxLayout; navButtonsLayout->setContentsMargins(0, 0, 0, 0); navButtonsLayout->setSpacing(4);
        navLayout->addLayout(navButtonsLayout); navLayout->addStretch();
        auto* updates = button("softwareUpdate", QStringLiteral("软件更新")); navLayout->addWidget(updates);
        updateController = new UpdateController(&window, updates);
        auto* footer = new QWidget; auto* footerLayout = new QHBoxLayout(footer);
        footerLayout->setContentsMargins(4, 0, 4, 0); footerLayout->setSpacing(6);
        auto* footerMark = new QLabel; footerMark->setPixmap(theme::glyph(theme::Glyph::Lock, QColor(0x7F, 0xA3, 0x9C), 14)); footerMark->setFixedSize(14, 14);
        auto* footerText = label(QStringLiteral("消息端到端加密")); footerText->setObjectName("sideNavFooter");
        footerLayout->addWidget(footerMark, 0, Qt::AlignVCenter); footerLayout->addWidget(footerText, 0, Qt::AlignVCenter);
        navLayout->addWidget(footer);
        auto* workspace = new QWidget; auto* workLayout = new QVBoxLayout(workspace);
        workLayout->setContentsMargins(0, 0, 0, 0); workLayout->setSpacing(0);
        workLayout->addWidget(tabs = new NavTabs, 1);
        bodyLayout->addWidget(nav); bodyLayout->addWidget(workspace, 1);
        tabs->setObjectName("mainTabs"); tabs->setElideMode(Qt::ElideNone);
        outer->addWidget(body, 1);
        banner = label(QStringLiteral("输入账号和密码即可登录。新账号选择“注册账号”。")); banner->setObjectName("notification");
        banner->setProperty("noticeKind", "neutral");
        auto* bannerRow = new QWidget; auto* bannerLayout = new QHBoxLayout(bannerRow);
        bannerLayout->setContentsMargins(20, 8, 20, 14); bannerLayout->addWidget(banner);
        outer->addWidget(bannerRow);
        window.setCentralWidget(root);
        buildAccount(std::move(options)); buildChat(); buildGroups();
        workspaceSave = new QTimer(&window); workspaceSave->setSingleShot(true); workspaceSave->setInterval(220);
        QObject::connect(workspaceSave, &QTimer::timeout, &window, [this] { if (!currentUser.empty() && !restoringWorkspace) post(SetWorkspace{sharedWorkspace}); });
        tabs->tabBar()->hide();
        tabs->structureChanged = [this] { rebuildSidebar(); };
        QObject::connect(tabs->tabBar(), &QTabBar::tabMoved, &window, [this](int, int) { rebuildSidebar(); });
        rebuildSidebar();
        loadDesign();
        QObject::connect(appearance, &QPushButton::toggled, &window, [this](bool checked) {
            darkMode = checked; applyDesign(false);
            QSettings settings(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat);
            settings.setValue("ui/appearance", darkMode ? "dark" : "light"); settings.sync();
            if (settings.status() != QSettings::NoError) notify(QStringLiteral("外观已切换，但设置保存失败，请检查数据目录权限。"), true);
        });
        QObject::connect(dataDir, &QLineEdit::editingFinished, &window, [this] { loadDesign(); });
        QObject::connect(tabs, &QTabWidget::currentChanged, &window, [this](int) { syncSidebar(); renderChats(); });
        timer = new QTimer(&window); timer->setInterval(40);
        QObject::connect(timer, &QTimer::timeout, &window, [this] { drain(); }); timer->start();
        buildMessengerShell();
    }
    QString switchFrontend(const QString& selected) {
#ifdef CCFW_HAS_RHINE_NATIVE
        if (selected != "rhine" && selected != "codex") return QStringLiteral("未知界面");
        if (selected == "rhine") {
            if (!nativeEngine) {
                initializeRhineResources();
                qmlRegisterType<ArchiveField>("Rhine", 1, 0, "ArchiveField");
                QVariantMap config{{"host", host->text().trimmed()}, {"port", port->value()},
                    {"serverName", q(startupConnection.expectedServerName)}, {"caFile", qpath(startupConnection.caFile)},
                    {"dataDirectory", dataDir->text()}, {"username", username->text()},
                    {"accountScope", q(accountServerId)}, {"directEnabled", directEnabled->isChecked()}};
                nativeBridge = std::make_unique<ChatBridge>(config, worker);
                nativeBridge->commandDispatcher = [this](Command command) { return post(std::move(command)); };
                nativeBridge->frontendSwitch = [this](const QString& name) { return switchFrontend(name); };
                nativeBridge->featureAction = [this](const QString& action, const QString& value) {
                    if (action == "messageDetails") featureUi->messageDetails(value);
                    else if (action == "deliveries") featureUi->deliveries(value);
                    else if (action == "identity") { if (value == q(currentUser)) featureUi->identityCard(ownIdentity, true); else if (featureUi->identities.count(s(value))) featureUi->identityCard(featureUi->identities.at(s(value))); else post(InspectIdentity{s(value)}); }
                };
                nativeBridge->showUpdates = [this] { updateController->show(); };
                QObject::connect(nativeBridge.get(), &ChatBridge::configChanged, &window, [this] {
                    const auto config = nativeBridge->config();
                    host->setText(config.value("host").toString()); port->setValue(config.value("port").toInt());
                    username->setText(config.value("username").toString()); dataDir->setText(config.value("dataDirectory").toString());
                    startupConnection.expectedServerName = s(config.value("serverName").toString());
                    startupConnection.caFile = path(config.value("caFile").toString());
                    const QSignalBlocker blocker(directEnabled); directEnabled->setChecked(config.value("directEnabled").toBool());
                });
                nativeEngine = std::make_unique<QQmlApplicationEngine>();
                const auto families = QFontDatabase::families();
                const QString font = families.contains("MiSans") ? "MiSans" : "Microsoft YaHei UI";
                auto* context = nativeEngine->rootContext();
                context->setContextProperty("chat", nativeBridge.get());
                context->setContextProperty("embeddedHotSwitch", true);
                context->setContextProperty("uiFont", font); context->setContextProperty("boldFont", font);
                context->setContextProperty("skipOpening", false); context->setContextProperty("captureDir", "");
                context->setContextProperty("tune", QVariantMap{}); context->setContextProperty("freezeAt", -1.0);
                nativeEngine->load(QUrl("qrc:/rhine_native/Main.qml"));
                if (nativeEngine->rootObjects().isEmpty()) {
                    nativeEngine.reset(); nativeBridge.reset(); return QStringLiteral("Qt 复刻界面加载失败，请检查运行包。");
                }
                nativeWindow = qobject_cast<QQuickWindow*>(nativeEngine->rootObjects().first());
                if (!nativeWindow) return QStringLiteral("Qt 复刻窗口无效。");
                nativeWindow->setObjectName("rhineHotWindow");
                QObject::connect(nativeWindow, &QQuickWindow::closing, &window, [this] { if (!shuttingDown) this->window.close(); });
                RestoredView view; view.username = currentUser; view.ownIdentity = ownIdentity;
                view.scope = featureUi->scope; view.workspace = sharedWorkspace; view.bundles = featureUi->bundles; view.stages = featureUi->stages;
                for (const auto& [name, identity] : featureUi->identities) view.peerIdentities.push_back(identity);
                for (const auto& [id, item] : history) view.history.push_back(item);
                for (const auto& [id, item] : transfers) view.files.push_back(item);
                for (const auto& [id, item] : groups) view.groups.push_back(item);
                nativeBridge->publish(WorkerEvent{view});
                nativeBridge->publish(WorkerEvent{ClientEvent{AccountEvent{accountPhase}}});
                ContactListEvent contactsEvent;
                for (const auto& name : friendNames) contactsEvent.users.emplace_back(name, online[name]);
                contactsEvent.incoming = {incomingRequests.begin(), incomingRequests.end()};
                contactsEvent.outgoing = {outgoingRequests.begin(), outgoingRequests.end()};
                nativeBridge->publish(WorkerEvent{ClientEvent{contactsEvent}});
            }
            nativeBridge->setConfig({{"host", host->text().trimmed()}, {"port", port->value()},
                {"username", currentUser.empty() ? username->text() : q(currentUser)}, {"dataDirectory", dataDir->text()},
                {"serverName", q(startupConnection.expectedServerName)}, {"caFile", qpath(startupConnection.caFile)},
                {"directEnabled", directEnabled->isChecked()}});
            QVariantMap drafts; for (const auto& [key, value] : sharedWorkspace.drafts) drafts[q(key)] = q(value);
            const QString peerName = peer->text().trimmed();
            const auto groupId = groupPicker->currentData().toLongLong();
            drafts["peer:" + peerName] = message->toPlainText();
            drafts["group:" + QString::number(groupId)] = groupMessage->toPlainText();
            QStringList pinned; for (const auto& value : sharedWorkspace.pinnedPeers) pinned << q(value);
            nativeSessionState = {{"drafts", drafts}, {"selectedPeer", peerName}, {"selectedGroup", groupId}, {"pinnedPeers", pinned}};
            QMetaObject::invokeMethod(nativeWindow, "restoreSessionState", Q_ARG(QVariant, QVariant(nativeSessionState)));
            nativeWindow->show(); nativeWindow->raise(); nativeWindow->requestActivate(); window.hide();
        } else {
            if (nativeWindow) {
                QVariant state;
                QMetaObject::invokeMethod(nativeWindow, "sessionState", Q_RETURN_ARG(QVariant, state));
                nativeSessionState = state.canConvert<QJSValue>() ? state.value<QJSValue>().toVariant().toMap() : state.toMap();
                const auto drafts = nativeSessionState.value("drafts").toMap();
                sharedWorkspace.drafts.clear(); for (auto it = drafts.begin(); it != drafts.end(); ++it) sharedWorkspace.drafts[s(it.key())] = s(it.value().toString());
                sharedWorkspace.pinnedPeers.clear(); for (const auto& pin : nativeSessionState.value("pinnedPeers").toStringList()) sharedWorkspace.pinnedPeers.push_back(s(pin));
                peer->setText(nativeSessionState.value("selectedPeer").toString());
                const auto groupId = nativeSessionState.value("selectedGroup").toLongLong();
                const auto index = groupPicker->findData(groupId); if (index >= 0) groupPicker->setCurrentIndex(index);
                message->setPlainText(drafts.value("peer:" + peer->text().trimmed()).toString());
                groupMessage->setPlainText(drafts.value("group:" + QString::number(groupId)).toString());
                saveWorkspace();
                window.show(); window.raise(); window.activateWindow(); nativeWindow->hide();
            }
        }
        window.setProperty("activeFrontend", selected);
        QSettings settings(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat);
        settings.setValue("ui/nativeFrontend", selected); settings.sync();
        return {};
#else
        return selected == "codex" ? QString{} : QStringLiteral("当前运行包未包含 Qt 复刻界面。");
#endif
    }
    void rebuildSidebar() {
        for (auto* button : navButtons) { navButtonsLayout->removeWidget(button); button->deleteLater(); }
        navButtons.clear();
        for (int i = 0; i < tabs->count(); ++i) {
            auto* button = new QPushButton(tabs->tabText(i));
            button->setObjectName("sideNavButton"); button->setIcon(tabs->tabIcon(i)); button->setIconSize(QSize(20, 20));
            button->setCheckable(true); button->setCursor(Qt::PointingHandCursor);
            button->setProperty("sideNavIndex", i);
            button->setProperty("sideNavTarget", QVariant::fromValue<QObject*>(tabs));
            QObject::connect(button, &QPushButton::clicked, &window, [this, i] { tabs->setCurrentIndex(i); });
            navButtonsLayout->addWidget(button); navButtons.push_back(button);
        }
        syncSidebar();
    }
    void syncSidebar() {
        for (int i = 0; i < static_cast<int>(navButtons.size()); ++i) {
            navButtons[i]->setChecked(i == tabs->currentIndex());
            navButtons[i]->setText(tabs->tabText(i));
            auto icon = tabs->tabIcon(i);
            if (!icon.isNull()) {
                auto pixmap = icon.pixmap(20, 20);
                QPainter painter(&pixmap); painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                const QString color = i == tabs->currentIndex() ? "#5146BD" : "#9590A6";
                painter.fillRect(pixmap.rect(), darkMode ? theme::blend(activeAccent(), Qt::white, .60) : QColor(theme::recolor(color, customAccent))); painter.end();
                icon = QIcon(pixmap);
            }
            navButtons[i]->setIcon(icon);
        }
    }
    void loadDesign() {
        QSettings settings(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat);
        QString fallbackSkin = "classic";
        QFile defaults(QCoreApplication::applicationDirPath() + "/appearance-defaults.json");
        if (defaults.open(QIODevice::ReadOnly)) { try { if (nlohmann::json::parse(defaults.readAll().toStdString()).value("skin", std::string{}) == "pencil") fallbackSkin = "pencil"; } catch (...) {} }
        pencilSkin = settings.value("ui/skin", fallbackSkin).toString() == "pencil";
        darkMode = settings.value("ui/appearance", "light").toString() == "dark";
        { QSignalBlocker blocker(appearance); appearance->setChecked(darkMode); }
        appearance->setText(darkMode ? QStringLiteral("深色") : QStringLiteral("浅色"));
        appearance->setToolTip(darkMode ? QStringLiteral("点击切换为浅色") : QStringLiteral("点击切换为深色"));
        // Earlier releases saved GLM or Rhine in ui/design; every value now opens Codex.
        applyDesign();
    }
    QString accentKey() const { return QStringLiteral("ui/colors/codex"); }
    QColor defaultAccent() const { return QColor("#6151C6"); }
    QColor activeAccent() const { return pencilSkin ? pencil::accent(darkMode) : customAccent.isValid() ? customAccent : defaultAccent(); }
    void chooseColor() {
        QSettings settings(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat);
        QStringList favorites;
        for (const auto& item : settings.value("ui/customColors").toStringList())
            if (QColor(item).isValid()) favorites << QColor(item).name().toUpper();
        const auto previous = customAccent;
        ThemeColorPicker picker(activeAccent(), defaultAccent(), favorites, &window);
        picker.preview = [this](const QColor& color) { customAccent = color.rgb() == defaultAccent().rgb() ? QColor{} : color; applyDesign(false); };
        if (picker.exec() == QDialog::Accepted) {
            customAccent = picker.selectedColor().rgb() == defaultAccent().rgb() ? QColor{} : picker.selectedColor();
            if (customAccent.isValid()) settings.setValue(accentKey(), customAccent.name()); else settings.remove(accentKey());
            settings.setValue("ui/customColors", picker.favorites()); settings.sync();
            if (settings.status() != QSettings::NoError) notify(QStringLiteral("主题色已应用，但保存失败，请检查数据目录权限。"), true);
        } else customAccent = previous;
        applyDesign(false);
    }
    void applyDesign(bool loadAccent = true) {
        if (loadAccent) {
            QSettings settings(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat);
            customAccent = QColor(settings.value(accentKey()).toString());
        }
        window.setProperty("frontendDesign", "codex");
        window.setProperty("themeAccent", activeAccent().name());
        window.setProperty("appearanceMode", darkMode ? "dark" : "light");
        window.setProperty("uiSkin", pencilSkin ? "pencil" : "classic");
        if (skinChoice) { QSignalBlocker blocked(skinChoice); skinChoice->setCurrentIndex(pencilSkin ? 1 : 0); }
        auto stylesheet = theme::stylesheet() + theme::codexStylesheet();
        if (customAccent.isValid()) stylesheet = theme::recolor(stylesheet, customAccent);
        if (darkMode) stylesheet += theme::darkStylesheet(activeAccent());
        stylesheet.replace("#loginCard", "#loginCard, #registrationCard");
        stylesheet.replace("#loginTitle", "#loginTitle, #registrationTitle");
        stylesheet.replace("#loginSubtitle", "#loginSubtitle, #registrationSubtitle");
        stylesheet += QStringLiteral("QLabel#registrationFeedback[error=\"true\"]{color:%1;}").arg(darkMode ? "#FFB5A5" : "#9A3B25");
        const auto ink = theme::accentInk(activeAccent()).name();
        stylesheet += QStringLiteral("QPushButton[primary=\"true\"]:enabled{background:%3;color:%1;border:1px solid %2;} QPushButton[primary=\"true\"]:enabled:hover,QPushButton[primary=\"true\"]:enabled:pressed{background:%3;color:%1;}")
            .arg(ink, activeAccent().darker(115).name(), activeAccent().name());
        window.setPalette(darkMode ? theme::darkPalette(QApplication::palette()) : theme::lightPalette(QApplication::palette()));
        stylesheet += QStringLiteral("QWidget#messengerNav{border-right:1px solid %1;} QWidget#messengerNav QPushButton{padding:8px 1px;min-width:0;border-radius:8px;} QWidget#messengerNav QPushButton:checked{background:%2;color:%3;} QWidget#recentPanel{border-right:1px solid %1;} QLabel#unreadBadge{background:#D94D68;color:white;border-radius:9px;padding:1px 5px;} QLabel#recentPreview{color:%4;} QLineEdit#chatPeer{border:none;background:transparent;font-size:17px;font-weight:600;padding:0;}").arg(darkMode ? "#303747" : "#E1E5EC", activeAccent().name(), theme::accentInk(activeAccent()).name(), darkMode ? "#A8B1C2" : "#7C8394");
        stylesheet += "QPushButton#newConversation{font-size:22px;padding:0;} QPushButton#newConversation::menu-indicator{image:none;width:0;} QPushButton#accountMenu::menu-indicator{image:none;width:0;}";
        stylesheet += QStringLiteral("QWidget#contactSections QPushButton{padding:7px 3px;min-width:0;} QWidget#contactSections QPushButton:checked{background:%1;color:%2;border-color:%1;}").arg(activeAccent().name(), theme::accentInk(activeAccent()).name());
        stylesheet += QStringLiteral("QLabel#chatRemark{font-size:11px;color:%1;background:transparent;}").arg(darkMode ? "#A8B1C2" : "#7C8394");
        stylesheet += QStringLiteral(
            "QWidget#fileQueueRow{background:%1;border:1px solid %2;border-radius:9px;}"
            "QLabel#queueFileName{font-weight:600;font-size:13px;background:transparent;}"
            "QLabel#queueFileInfo,QLabel#queueConversationLabel,QLabel#fileQueueSummary{font-size:11px;color:%3;background:transparent;}"
            "QLabel#queueFileState{font-size:11px;font-weight:600;background:transparent;}"
            "QPushButton[queueAction=\"true\"]{padding:5px 9px;font-size:11px;min-width:0;}"
            "QProgressBar#queueFileProgress{min-height:4px;max-height:4px;border-radius:2px;}"
            "QProgressBar#queueFileProgress::chunk{border-radius:2px;}"
            "QComboBox#fileFilter{padding:5px 9px;}")
            .arg(darkMode ? "#191E28" : "#FFFFFF", darkMode ? "#303949" : "#E4E1EE", darkMode ? "#A8B1C2" : "#7C8394");
        stylesheet += QStringLiteral("QTabWidget#identityCardTabs::pane,QTabWidget#identityCardTabs>QStackedWidget{background:%1;border:0;} QLabel#identityUid{font-family:Consolas;font-size:28px;font-weight:600;}")
            .arg(darkMode ? "#191E28" : "#F5F5F8");
        if (pencilSkin) { stylesheet += pencil::stylesheet(darkMode); window.setPalette(pencil::palette(window.palette(), darkMode)); }
        window.setStyleSheet(stylesheet);
        if (auto* color = window.findChild<QPushButton*>("settingsThemeColor")) { color->setEnabled(!pencilSkin); color->setToolTip(pencilSkin ? QStringLiteral("彩铅手绘使用固定粉彩配色；默认皮肤可自定义主题色。") : QString{}); }
        for (auto* dialog : window.findChildren<QDialog*>()) if (dialog->objectName() != "themeColorDialog") dialog->setPalette(window.palette());
        styleNotice();
        syncSidebar();
        for (auto* mark : window.findChildren<QLabel*>()) {
            const int size = mark->property("brandSize").toInt();
            if (size) mark->setPixmap(pencilSkin ? theme::glyph(theme::Glyph::Lock, activeAccent(), size) : theme::accentMark(size, activeAccent(), true));
            if (mark->property("fileChipSize").isValid()) mark->setPixmap(theme::fileChip(34, activeAccent()));
        }
        for (auto* bubble : window.findChildren<QWidget*>()) {
            if (!bubble->property("timelineKey").isValid()) continue;
            styleBubble(bubble);
        }
        auto* send = window.findChild<QPushButton*>("sendMessage");
        send->setIcon(theme::icon(theme::Glyph::Send, theme::accentInk(activeAccent())));
        fileTransferToggle->setIcon(theme::icon(theme::Glyph::Attach, darkMode ? QColor("#B5C0D2") : QColor("#40607A")));
        if (auto* account = window.findChild<QPushButton*>("accountMenu")) account->setIcon(theme::icon(theme::Glyph::Account, activeAccent()));
        window.findChild<QLabel*>("conversationLock")->setPixmap(theme::glyph(theme::Glyph::Lock, darkMode ? theme::blend(activeAccent(), Qt::white, .60) : QColor("#1F6156"), 14));
    }
    bool post(Command command, std::string requestId = {}) {
        const bool authenticating = std::holds_alternative<Authenticate>(command);
        if (!authenticating && sessionAuthenticated && accountPhase != AccountPhase::Online && !std::holds_alternative<SetWorkspace>(command) && !std::holds_alternative<Disconnect>(command) && !std::holds_alternative<SetDirectEnabled>(command)) { notify(QStringLiteral("请重新登录恢复连接；操作尚未提交，草稿保留。"), true); return false; }
        if (authenticating && rememberedLogin.authenticating()) { notify(QStringLiteral("正在登录，请等待当前登录完成。")); return false; }
        if (authenticating) { registrationPending = std::get<Authenticate>(command).registerNew; rememberedLogin.beginAuthentication(std::get<Authenticate>(command)); }
        std::optional<WorkspaceState> workspace;
        if (const auto* update = std::get_if<SetWorkspace>(&command)) workspace = update->state;
        const auto inspect = std::get_if<InspectIdentity>(&command);
        const auto inspectedPeer = inspect ? inspect->peer : std::string{};
        if (shuttingDown || !worker.post(std::move(command), std::move(requestId))) { if (authenticating) rememberedLogin.cancelAuthentication(); notify(QStringLiteral("操作队列已满或窗口正在关闭，请稍后重试。"), true); return false; }
        if (authenticating && sessionPages) { sessionAuthenticated = false; deferredIdentities.clear(); sessionPages->setCurrentIndex(registrationPending ? 2 : 0); if (tray) tray->hide(); }
        if (!inspectedPeer.empty()) requestedIdentities.insert(inspectedPeer);
        if (workspace && nlohmann::json(*workspace) != nlohmann::json(sharedWorkspace)) {
            sharedWorkspace = std::move(*workspace); featureUi->workspace = sharedWorkspace;
            restoringWorkspace = true;
            peer->setText(q(sharedWorkspace.selectedPeer));
            message->setPlainText(q(sharedWorkspace.drafts["peer:" + sharedWorkspace.selectedPeer]));
            const auto index = groupPicker->findData(sharedWorkspace.selectedGroup);
            if (index >= 0) groupPicker->setCurrentIndex(index);
            groupMessage->setPlainText(q(sharedWorkspace.drafts["group:" + std::to_string(sharedWorkspace.selectedGroup)]));
            restoringWorkspace = false;
        }
        return true;
    }
    void notify(const QString& text, bool error = false) {
        banner->setText(text);
        banner->setProperty("noticeKind", error ? "error" : "success"); styleNotice();
        if (registrationFeedback && sessionPages && sessionPages->currentIndex() == 2) {
            registrationFeedback->setText(text); registrationFeedback->setProperty("error", error);
            registrationFeedback->style()->unpolish(registrationFeedback); registrationFeedback->style()->polish(registrationFeedback);
        }
    }
    void styleNotice() {
        const auto kind = banner->property("noticeKind").toString();
        if (pencilSkin) { banner->setStyleSheet(darkMode ? (kind == "error" ? "background:#50313b;color:#f2bccb;border:1px dashed #a8788d;" : "background:#493e2a;color:#e4cca5;border:1px dashed #9c855e;") : (kind == "error" ? "background:#fceff0;color:#a94d66;border:1px dashed #d8a8b7;" : "background:#f8efce;color:#947951;border:1px dashed #ceb585;")); return; }
        banner->setStyleSheet(darkMode ? (kind == "error" ? "background:#442D2C;color:#FFB5A5;" : kind == "success" ? "background:#203C35;color:#8CE1B6;" : "background:#252D3B;color:#B5C0D2;") :
            (kind == "error" ? "background:#FBECE7;color:#9A3B25;" : kind == "success" ? "background:#EAF3F1;color:#22635B;" : "background:#EDF2F7;color:#456078;"));
    }
    void styleBubble(QWidget* bubble) {
        const bool own = bubble->property("ownMessage").toBool();
        bubble->setProperty("pencilBubble", pencilSkin);
        bubble->setStyleSheet(pencilSkin ? pencil::bubbleStylesheet(darkMode) : darkMode ? theme::darkBubble(own, activeAccent()) : theme::recolor(theme::codexBubble(own), customAccent));
        if (!pencilSkin && bubble->property("locatedFile").toBool()) bubble->setStyleSheet(bubble->styleSheet() + QStringLiteral("QWidget#fileCard{border:2px solid %1;}").arg(activeAccent().name()));
        bubble->update();
    }
    QWidget* pathRow(QLineEdit* edit, const char* name, bool directory) {
        auto* row = new QWidget; auto* box = new QHBoxLayout(row); box->setContentsMargins(0, 0, 0, 0); box->setSpacing(8); box->addWidget(edit, 1);
        auto* browse = button(name, QStringLiteral("浏览…"));
        box->addWidget(browse);
        QObject::connect(browse, &QPushButton::clicked, &window, [this, edit, directory] {
            const auto selected = directory ? QFileDialog::getExistingDirectory(&window, QStringLiteral("选择数据目录"), edit->text()) :
                QFileDialog::getOpenFileName(&window, QStringLiteral("选择文件"), edit->text());
            if (!selected.isEmpty()) edit->setText(selected);
        });
        return row;
    }
    void buildAccount(WindowOptions options) {
        auto* page = new QWidget; auto* outer = new QVBoxLayout(page);
        outer->setContentsMargins(24, 20, 24, 16); outer->setSpacing(0);
        outer->addStretch(2);
        auto* center = new QHBoxLayout; center->setSpacing(18); center->addStretch(1);
        auto* card = new pencil::Paper(pencil::Tone::Card); card->setObjectName("loginCard"); card->setMinimumWidth(360); card->setMaximumWidth(430);
        accountCard = card;
        auto* form = new QVBoxLayout(card); form->setContentsMargins(28, 26, 28, 22); form->setSpacing(12);
        auto* mark = new QLabel; mark->setPixmap(theme::brand(52)); mark->setAlignment(Qt::AlignCenter);
        mark->setObjectName("loginMark");
        mark->setProperty("brandSize", 52);
        form->addWidget(mark);
        auto* headline = label(QStringLiteral("CipherChat")); headline->setObjectName("loginTitle"); headline->setAlignment(Qt::AlignCenter);
        form->addWidget(headline);
        auto* tagline = label(QStringLiteral("端到端加密 · 与联系人私密通讯")); tagline->setObjectName("loginSubtitle");
        tagline->setAlignment(Qt::AlignCenter); form->addWidget(tagline);
        form->addSpacing(10);
        auto* userCaption = label(QStringLiteral("用户名")); userCaption->setObjectName("fieldCaption");
        form->addWidget(userCaption);
        username = field("username", q(options.username)); username->setPlaceholderText(QStringLiteral("3–32 位字母、数字或下划线"));
        form->addWidget(username);
        auto* passwordCaption = label(QStringLiteral("密码")); passwordCaption->setObjectName("fieldCaption");
        form->addWidget(passwordCaption);
        password = field("password"); password->setEchoMode(QLineEdit::Password); password->setPlaceholderText(QStringLiteral("至少 8 个字符"));
        form->addWidget(password);
        auto* remembered = new QHBoxLayout; remembered->setSpacing(20);
        rememberAccount = new QCheckBox(QStringLiteral("记住账号")); rememberAccount->setObjectName("rememberAccount");
        rememberPassword = new QCheckBox(QStringLiteral("记住密码")); rememberPassword->setObjectName("rememberPassword");
        remembered->addWidget(rememberAccount); remembered->addWidget(rememberPassword); remembered->addStretch();
        form->addLayout(remembered);
        form->addSpacing(4);
        login = button("loginButton", QStringLiteral("登录"), "primary"); login->setMinimumHeight(38);
        form->addWidget(login);
        auto* actions = new QHBoxLayout; actions->setSpacing(8);
        registerAccount = button("registerButton", QStringLiteral("注册账号"), "ghost");
        auto* disconnect = button("disconnectButton", QStringLiteral("断开连接"), "ghost");
        QObject::connect(login, &QPushButton::clicked, &window, [this] { authenticate(false); });
        QObject::connect(registerAccount, &QPushButton::clicked, &window, [this] { showRegistration(); });
        QObject::connect(disconnect, &QPushButton::clicked, &window, [this] { post(Disconnect{}); });
        actions->addWidget(registerAccount); disconnect->setParent(card); disconnect->hide(); actions->addStretch();
        form->addLayout(actions);
        advancedToggle = button("advancedSettingsToggle", QStringLiteral("连接设置")); advancedToggle->setCheckable(true);
        form->addWidget(advancedToggle);
        auto* advanced = new QGroupBox(QStringLiteral("服务器与本地数据"));
        auto* advancedForm = new QFormLayout(advanced); advancedForm->setSpacing(10);
        advancedForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        host = field("serverHost", q(options.connection.host)); host->setAccessibleName(QStringLiteral("服务器地址"));
        port = new QSpinBox; port->setObjectName("serverPort"); port->setRange(1, 65535); port->setValue(options.connection.port);
        auto* address = new QWidget; auto* addressBox = new QHBoxLayout(address); addressBox->setContentsMargins(0, 0, 0, 0); addressBox->setSpacing(8);
        addressBox->addWidget(host, 1); addressBox->addWidget(port);
        advancedForm->addRow(QStringLiteral("服务器"), address);
        if (options.dataDirectory.empty()) options.dataDirectory = path(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) / "profiles";
        dataDir = field("dataDirectory", qpath(options.dataDirectory));
        advancedForm->addRow(QStringLiteral("本地数据目录"), pathRow(dataDir, "browseData", true));
        auto* certNote = label(QStringLiteral("连接证书由启动配置自动提供并验证。")); certNote->setObjectName("hintNote");
        advancedForm->addRow(certNote);
        auto* securityRow = new QWidget; auto* securityLayout = new QHBoxLayout(securityRow); securityLayout->setContentsMargins(0, 0, 0, 0); securityLayout->setSpacing(8);
        ownSecurity = button("showOwnSecurity", QStringLiteral("本机安全详情")); ownSecurity->setEnabled(false);
        auto* inspect = button("inspectIdentity", QStringLiteral("联系人安全详情"));
        securityLayout->addWidget(ownSecurity); securityLayout->addWidget(inspect); securityLayout->addStretch();
        advancedForm->addRow(securityRow); securityRow->hide();
        QObject::connect(ownSecurity, &QPushButton::clicked, &window, [this] { showOwnIdentity(); });
        QObject::connect(inspect, &QPushButton::clicked, &window, [this] {
            const auto target = s(peer->text().trimmed());
            if (target.empty()) { notify(QStringLiteral("请先在聊天页选择联系人。")); return; }
            post(InspectIdentity{target});
        });
        // 可选项放在折叠区，小窗口也能保证登录按钮可见。
        auto* advancedScroll = new QScrollArea; advancedScroll->setObjectName("advancedSettingsScroll");
        advancedScroll->setWidgetResizable(true); advancedScroll->setFrameShape(QFrame::NoFrame); advancedScroll->setWidget(advanced);
        form->addWidget(advancedScroll); advancedScroll->hide();
        QObject::connect(advancedToggle, &QPushButton::toggled, &window, [this, advancedScroll](bool open) { advancedScroll->setVisible(open); });
        center->addWidget(card, 0, Qt::AlignVCenter); center->addStretch(1);
        outer->addLayout(center);
        outer->addStretch(3);
        QObject::connect(&rememberedLogin, &LoginPreferences::errorOccurred, &window, [this](const QString& text) { notify(text,true); });
        rememberedLogin.setContext(loginConfiguration());
        rememberAccount->setChecked(rememberedLogin.rememberAccount()); rememberPassword->setChecked(rememberedLogin.rememberPassword());
        username->setText(rememberedLogin.initialUsername(username->text())); recallLoginPassword();
        QObject::connect(rememberAccount, &QCheckBox::clicked, &window, [this](bool value) { rememberedLogin.setRememberAccount(value); });
        QObject::connect(rememberPassword, &QCheckBox::clicked, &window, [this](bool value) { rememberedLogin.setRememberPassword(value); });
        QObject::connect(&rememberedLogin, &LoginPreferences::changed, &window, [this] {
            const QSignalBlocker accountBlocked(rememberAccount), passwordBlocked(rememberPassword);
            rememberAccount->setChecked(rememberedLogin.rememberAccount()); rememberPassword->setChecked(rememberedLogin.rememberPassword());
            rememberAccount->setEnabled(!rememberedLogin.authenticating()); rememberPassword->setEnabled(!rememberedLogin.authenticating());
            const bool idle = accountPhase == AccountPhase::Disconnected || accountPhase == AccountPhase::Connected || accountPhase == AccountPhase::Online;
            login->setEnabled(idle && !rememberedLogin.authenticating()); registerAccount->setEnabled(idle && !rememberedLogin.authenticating());
            syncRegistrationControls();
        });
        QObject::connect(password, &QLineEdit::textEdited, &window, [this] { recalledPassword = false; });
        QObject::connect(username, &QLineEdit::textChanged, &window, [this] { if (recalledPassword || password->text().isEmpty()) recallLoginPassword(); });
        auto refreshRememberedContext = [this] { rememberedLogin.setContext(loginConfiguration()); if (recalledPassword || password->text().isEmpty()) recallLoginPassword(); };
        QObject::connect(host, &QLineEdit::textChanged, &window, refreshRememberedContext);
        QObject::connect(port, &QSpinBox::valueChanged, &window, refreshRememberedContext);
        QObject::connect(dataDir, &QLineEdit::textChanged, &window, refreshRememberedContext);
        tabs->addTab(page, theme::icon(theme::Glyph::Account, QColor(0xA9, 0xC6, 0xC0)), QStringLiteral("账号与连接"));
    }
    void syncRegistrationControls() {
        if (!registrationSubmit) return;
        const bool ready = !rememberedLogin.authenticating() && (accountPhase == AccountPhase::Disconnected || accountPhase == AccountPhase::Connected || accountPhase == AccountPhase::Online);
        for (auto* field : {registrationUsername, registrationPassword, registrationConfirmation}) field->setEnabled(ready);
        registrationSubmit->setEnabled(ready); registrationBack->setEnabled(ready);
    }
    void showRegistration() {
        if (sessionAuthenticated || rememberedLogin.authenticating()) return;
        registrationPending = false; advancedToggle->setChecked(false); registrationFeedback->clear();
        sessionPages->setCurrentIndex(2); registrationUsername->setFocus();
        notify(QStringLiteral("填写新账号信息，注册成功后将自动登录。"));
    }
    void returnToLogin() {
        if (rememberedLogin.authenticating()) return;
        registrationPending = false; registrationPassword->clear(); registrationConfirmation->clear(); registrationFeedback->clear();
        sessionPages->setCurrentIndex(0); username->setFocus();
        notify(QStringLiteral("输入账号和密码即可登录。新账号选择“注册账号”。"));
    }
    QWidget* buildRegistrationPage() {
        auto* page = new pencil::Paper; page->setObjectName("registrationPage");
        auto* outer = new QVBoxLayout(page); outer->addStretch(); auto* center = new QHBoxLayout; center->addStretch();
        auto* card = new pencil::Paper(pencil::Tone::Card); card->setObjectName("registrationCard"); card->setMinimumWidth(360); card->setMaximumWidth(430);
        auto* form = new QVBoxLayout(card); form->setContentsMargins(28,26,28,22); form->setSpacing(12);
        auto* mark = new QLabel; mark->setObjectName("registrationMark"); mark->setProperty("brandSize",52); mark->setPixmap(theme::brand(52)); mark->setAlignment(Qt::AlignCenter); form->addWidget(mark);
        auto* title = label(QStringLiteral("创建账号")); title->setObjectName("registrationTitle"); title->setAlignment(Qt::AlignCenter); form->addWidget(title);
        auto* subtitle = label(QStringLiteral("注册 CipherChat，开始私密通讯")); subtitle->setObjectName("registrationSubtitle"); subtitle->setAlignment(Qt::AlignCenter); form->addWidget(subtitle);
        form->addSpacing(10);
        registrationUsername = field("registrationUsername"); registrationUsername->setPlaceholderText(QStringLiteral("填写新账号用户名"));
        registrationPassword = field("registrationPassword"); registrationPassword->setEchoMode(QLineEdit::Password); registrationPassword->setPlaceholderText(QStringLiteral("设置登录密码"));
        registrationConfirmation = field("registrationConfirmation"); registrationConfirmation->setEchoMode(QLineEdit::Password); registrationConfirmation->setPlaceholderText(QStringLiteral("再次输入密码"));
        const QString captions[] = {QStringLiteral("用户名"),QStringLiteral("密码"),QStringLiteral("确认密码")};
        QLineEdit* fields[] = {registrationUsername,registrationPassword,registrationConfirmation};
        for (int i=0;i<3;++i) { auto* caption=label(captions[i]); caption->setObjectName("fieldCaption"); form->addWidget(caption); form->addWidget(fields[i]); }
        registrationFeedback = label({}); registrationFeedback->setObjectName("registrationFeedback"); form->addWidget(registrationFeedback);
        registrationSubmit = button("submitRegistration",QStringLiteral("注册"),"primary"); registrationSubmit->setMinimumHeight(38); form->addWidget(registrationSubmit);
        registrationBack = button("backToLogin",QStringLiteral("返回登录"),"ghost"); form->addWidget(registrationBack);
        QObject::connect(registrationSubmit,&QPushButton::clicked,&window,[this]{ authenticate(true); });
        QObject::connect(registrationBack,&QPushButton::clicked,&window,[this]{ returnToLogin(); });
        QObject::connect(registrationUsername,&QLineEdit::returnPressed,&window,[this]{ registrationPassword->setFocus(); });
        QObject::connect(registrationPassword,&QLineEdit::returnPressed,&window,[this]{ registrationConfirmation->setFocus(); });
        QObject::connect(registrationConfirmation,&QLineEdit::returnPressed,&window,[this]{ registrationSubmit->click(); });
        auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), &window);
        QObject::connect(escape,&QShortcut::activated,&window,[this]{ if (sessionPages->currentIndex()==2) returnToLogin(); });
        center->addWidget(card); center->addStretch(); outer->addLayout(center); outer->addStretch(); syncRegistrationControls(); return page;
    }
    QVariantMap loginConfiguration() const {
        return {{"host",host->text().trimmed()},{"port",port->value()},
            {"serverName",q(startupConnection.expectedServerName)},{"caFile",qpath(startupConnection.caFile)},{"dataDirectory",dataDir->text()}};
    }
    void recallLoginPassword() {
        const auto value = rememberedLogin.passwordFor(username->text().trimmed());
        password->setText(value); recalledPassword = !value.isEmpty();
    }
    void showOwnIdentity() {
        if (!ownIdentity.signingPublicKey.empty()) { featureUi->identityCard(ownIdentity, true); return; }
        if (ownIdentity.signingPublicKey.empty() || ownIdentity.dhPublicKey.empty()) return;
        if (auto* existing = window.findChild<QDialog*>("ownIdentityDialog")) { existing->raise(); existing->activateWindow(); return; }
        auto* dialog = new QDialog(&window); dialog->setPalette(window.palette()); dialog->setAutoFillBackground(true); dialog->setObjectName("ownIdentityDialog"); dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QStringLiteral("本机安全详情")); dialog->resize(560, 250);
        auto* layout = new QVBoxLayout(dialog); layout->setContentsMargins(24, 22, 24, 20); layout->setSpacing(14);
        auto* head = new QHBoxLayout; head->setSpacing(10);
        auto* mark = new QLabel; mark->setPixmap(theme::glyph(theme::Glyph::Lock, QColor(0x1F, 0x7A, 0x6D), 24)); mark->setFixedSize(24, 24);
        auto* title = label(QStringLiteral("本机安全详情")); title->setObjectName("dialogTitle");
        head->addWidget(mark); head->addWidget(title); head->addStretch(); layout->addLayout(head);
        layout->addWidget(label(QStringLiteral("账号：") + q(ownIdentity.username)));
        layout->addWidget(label(QStringLiteral("需要核对身份时，可通过当面或电话等可信方式向联系人提供以下验证信息。")));
        auto* value = label(fingerprint(ownIdentity)); value->setObjectName("ownFingerprint"); value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(value);
        auto* actions = new QHBoxLayout;
        auto* copy = button("copyOwnFingerprint", QStringLiteral("复制指纹"), "ghost");
        QObject::connect(copy, &QPushButton::clicked, &window, [this, value] {
            QApplication::clipboard()->setText(value->text()); notify(QStringLiteral("已复制本机身份指纹，请通过可信渠道发送。"));
        });
        actions->addWidget(copy); actions->addStretch();
        auto* close = button("closeOwnSecurity", QStringLiteral("关闭"), "ghost"); actions->addWidget(close);
        layout->addLayout(actions);
        QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::accept); dialog->show();
    }
    void authenticate(bool create) {
        auto* submittedUsername = create ? registrationUsername : username;
        auto* submittedPassword = create ? registrationPassword : password;
        if (submittedUsername->text().trimmed().isEmpty() || submittedPassword->text().isEmpty()) { notify(QStringLiteral("请填写用户名和密码。"), true); return; }
        if (create && registrationPassword->text() != registrationConfirmation->text()) { notify(QStringLiteral("两次输入的密码不一致，请重新确认。"),true); registrationConfirmation->setFocus(); return; }
        if (startupConnection.caFile.empty() || startupConnection.expectedServerName.empty()) {
            notify(QStringLiteral("程序连接配置缺失，请关闭后使用随附的启动脚本重新打开。"), true); return;
        }
        if (host->text().trimmed().isEmpty()) {
            if (!create) advancedToggle->setChecked(true);
            notify(create ? QStringLiteral("请返回登录页，在连接设置中填写服务器地址。") : QStringLiteral("请在连接设置中填写服务器地址。"), true); return;
        }
        const auto submittedUser = s(submittedUsername->text().trimmed());
        Authenticate request; request.username = submittedUser; request.password = s(submittedPassword->text()); request.registerNew = create;
        request.directEnabled = directEnabled->isChecked();
        request.dataDirectory = path(dataDir->text()); request.connection = {s(host->text().trimmed()), static_cast<std::uint16_t>(port->value()), startupConnection.caFile, startupConnection.expectedServerName, 5000};
        if (request.connection.host == startupConnection.host && request.connection.port == startupConnection.port &&
            request.connection.expectedServerName == startupConnection.expectedServerName && request.connection.caFile == startupConnection.caFile)
            request.accountServerId = accountServerId;
        if (post(std::move(request))) {
            sessionAuthenticated = false; presentationLoaded = false; submissions.clear(); identitySubmissions.clear(); unreadMessages.clear(); recentCreated.clear(); activeConversation = {};
            selectedContact = {}; contactChat->setEnabled(false); contactDetails->setText(QStringLiteral("选择联系人查看资料")); recentSearch->clear(); contactSearch->clear();
            if (currentUser != submittedUser) { groupName->clear(); groupMembers->clear(); }
            if (sessionPages) sessionPages->setCurrentIndex(create ? 2 : 0);
            if (tray) tray->hide();
            friendNames.clear(); strangerOnline.clear(); online.clear(); incomingRequests.clear(); outgoingRequests.clear(); temporaryPeers.clear(); activeTyping.clear();
            peer->clear(); strangerQuery->clear(); activeQuery.clear();
            nextStranger.clear(); history.clear(); transfers.clear(); updateContacts(); updateStrangers(); updateRequests();
            advancedToggle->setChecked(false);
            if (create) { registrationPassword->clear(); registrationConfirmation->clear(); }
            else password->clear();
            login->setEnabled(false); registerAccount->setEnabled(false); syncRegistrationControls(); notify(QStringLiteral("正在连接并验证服务器…"));
        }
    }
    QScrollArea* messagesArea(const char* name) {
        auto* area = new QScrollArea; area->setObjectName(QString::fromLatin1(name)); area->setWidgetResizable(true); area->setFrameShape(QFrame::NoFrame);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); area->setWidget(new QWidget); return area;
    }
    void buildChat() {
        auto* page = new QWidget; auto* layout = new QHBoxLayout(page);
        layout->setContentsMargins(18, 14, 18, 14); layout->setSpacing(14);
        auto* splitter = new QSplitter;
        auto* side = new pencil::Paper(pencil::Tone::Cream); side->setObjectName("contactPanel");
        side->setMinimumWidth(270); side->setMaximumWidth(360);
        auto* sideLayout = new QVBoxLayout(side); sideLayout->setContentsMargins(10, 12, 10, 10); sideLayout->setSpacing(6);
        sideLayout->addWidget(caption(QStringLiteral("联系人与发现")));
        directorySection = new QComboBox; directorySection->setObjectName("directorySection");
        directorySection->addItems({QStringLiteral("好友"), QStringLiteral("临时会话"), QStringLiteral("陌生人"), QStringLiteral("好友申请")});
        sideLayout->addWidget(directorySection);
        directoryHint = label(QStringLiteral("互相同意后，好友会显示在这里。")); directoryHint->setObjectName("directoryHint"); sideLayout->addWidget(directoryHint);
        directoryPages = new QStackedWidget; sideLayout->addWidget(directoryPages, 1);
        contacts = new QListWidget; contacts->setObjectName("contacts");
        directoryPages->addWidget(contacts);
        temporaryChats = new QListWidget; temporaryChats->setObjectName("temporaryChats"); directoryPages->addWidget(temporaryChats);
        auto* strangersBox = new QWidget; strangersBox->setObjectName("strangersBox");
        auto* strangersLayout = new QVBoxLayout(strangersBox); strangersLayout->setContentsMargins(0, 0, 0, 0); strangersLayout->setSpacing(5);
        auto* queryRow = new QHBoxLayout; queryRow->setSpacing(5);
        strangerQuery = field("strangerQuery"); strangerQuery->setPlaceholderText(QStringLiteral("按用户名查询，留空显示全部"));
        searchStrangers = button("searchStrangers", QStringLiteral("查询"), "primary");
        queryRow->addWidget(strangerQuery, 1); queryRow->addWidget(searchStrangers); strangersLayout->addLayout(queryRow);
        strangers = new QListWidget; strangers->setObjectName("strangers"); strangers->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); strangersLayout->addWidget(strangers, 1);
        moreStrangers = button("moreStrangers", QStringLiteral("加载更多")); moreStrangers->setEnabled(false); strangersLayout->addWidget(moreStrangers);
        directoryPages->addWidget(strangersBox);
        friendRequests = new QListWidget; friendRequests->setObjectName("friendRequests"); friendRequests->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); directoryPages->addWidget(friendRequests);
        QObject::connect(directorySection, &QComboBox::currentIndexChanged, &window, [this](int index) {
            directoryPages->setCurrentIndex(index);
            const QStringList hints{QStringLiteral("互相同意后，好友会显示在这里。"), QStringLiteral("与陌生人的聊天记录保存在这里。"), QStringLiteral("浏览其他未加好友的用户，或查询用户名。"), QStringLiteral("收到的申请需由你同意。")};
            directoryHint->setText(index == 4 ? QStringLiteral("选择群查看成员，双击开始群聊。") : hints.value(index)); if (index == 2) searchStrangers->click();
        });
        QObject::connect(searchStrangers, &QPushButton::clicked, &window, [this] { activeQuery = s(strangerQuery->text().trimmed()); nextStranger.clear(); moreStrangers->setEnabled(false); post(SearchUsers{activeQuery, {}}); });
        QObject::connect(moreStrangers, &QPushButton::clicked, &window, [this] { if (!nextStranger.empty()) post(SearchUsers{activeQuery, nextStranger}); });
        QObject::connect(strangerQuery, &QLineEdit::returnPressed, &window, [this] { searchStrangers->click(); });
        temporaryChats->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        splitter->addWidget(side);
        auto* content = new pencil::Paper; content->setObjectName("conversationPanel"); auto* box = new QVBoxLayout(content); box->setContentsMargins(0, 0, 0, 0); box->setSpacing(10);
        auto* header = new QHBoxLayout; header->setSpacing(10);
        chatAvatar = avatarLabel(QStringLiteral("?"), 40); header->addWidget(chatAvatar, 0, Qt::AlignTop);
        auto* headText = new QVBoxLayout; headText->setContentsMargins(0, 0, 0, 0); headText->setSpacing(1);
        peer = field("chatPeer"); peer->setPlaceholderText(QStringLiteral("请选择会话")); peer->setReadOnly(true); peer->setFrame(false);
        headText->addWidget(peer);
        chatRemark = label({}); chatRemark->setObjectName("chatRemark"); chatRemark->setTextFormat(Qt::PlainText); chatRemark->setWordWrap(false); chatRemark->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred); headText->addWidget(chatRemark); chatRemark->hide();
        typing = label(QStringLiteral("选择联系人开始私密对话")); typing->setObjectName("typingStatus");
        headText->addWidget(typing);
        header->addLayout(headText, 1);
        auto* badge = new QWidget; badge->setObjectName("encryptionBadge");
        auto* badgeLayout = new QHBoxLayout(badge); badgeLayout->setContentsMargins(10, 5, 12, 5); badgeLayout->setSpacing(6);
        auto* lockMark = new QLabel; lockMark->setPixmap(theme::glyph(theme::Glyph::Lock, QColor(0x1F, 0x61, 0x56), 14)); lockMark->setFixedSize(14, 14);
        lockMark->setObjectName("conversationLock");
        badgeLayout->addWidget(lockMark, 0, Qt::AlignVCenter);
        badgeLayout->addWidget(label(QStringLiteral("端到端加密")), 0, Qt::AlignVCenter);
        header->addWidget(badge, 0, Qt::AlignTop);
        box->addLayout(header);
        chatScroll = messagesArea("chatMessages"); chatScroll->setMinimumHeight(60); box->addWidget(chatScroll, 1);
        newMessages = button("newMessages", QStringLiteral("新消息 ↓")); newMessages->setParent(chatScroll->viewport()); newMessages->hide();
        QObject::connect(newMessages, &QPushButton::clicked, &window, [this] { chatScroll->verticalScrollBar()->setValue(chatScroll->verticalScrollBar()->maximum()); newMessages->hide(); markRead("peer:" + s(peer->text().trimmed())); });
        QObject::connect(chatScroll->verticalScrollBar(), &QScrollBar::valueChanged, &window, [this](int position) { if (position >= chatScroll->verticalScrollBar()->maximum() - 30) newMessages->hide(); });
        buildAttachmentBox(box);
        auto* composer = new QHBoxLayout; composer->setSpacing(8);
        fileTransferToggle = button("fileTransferToggle", QStringLiteral("发送文件"));
        fileTransferToggle->setIcon(theme::icon(theme::Glyph::Attach, QColor(0x40, 0x60, 0x7A)));
        fileTransferToggle->setCheckable(true);
        QObject::connect(fileTransferToggle, &QPushButton::toggled, &window, [this](bool open) { transferPanel->setVisible(open); });
        fileTransferToggle->setToolTip(QStringLiteral("展开或收起附件工具，文件记录直接显示在聊天中"));
        composer->addWidget(fileTransferToggle, 0, Qt::AlignBottom);
        message = new MessageComposer; message->setObjectName("chatInput"); message->setPlaceholderText(QStringLiteral("输入消息 · Ctrl+Enter 发送"));
        message->setMinimumHeight(50); message->setMaximumHeight(65); composer->addWidget(message, 1);
        auto* send = button("sendMessage", QStringLiteral("发送"), "primary");
        send->setIcon(theme::icon(theme::Glyph::Send, Qt::white));
        composer->addWidget(send, 0, Qt::AlignBottom); box->addLayout(composer);
        QObject::connect(send, &QPushButton::clicked, &window, [this] { sendChat(); });
        static_cast<MessageComposer*>(message)->submit = [this] { sendChat(); };
        composerFeedback = label({}); composerFeedback->setObjectName("composerFeedback"); box->addWidget(composerFeedback);
        QObject::connect(message, &QPlainTextEdit::textChanged, &window, [this] {
            if (!restoringWorkspace) { const auto key = "peer:" + s(peer->text().trimmed()); ++draftRevisions[key]; sharedWorkspace.drafts[key] = s(message->toPlainText()); saveWorkspace(); }
            if (accountPhase == AccountPhase::Online && !message->toPlainText().isEmpty() && !peer->text().trimmed().isEmpty()) post(TypeTo{s(peer->text().trimmed())});
        });
        QObject::connect(peer, &QLineEdit::textChanged, &window, [this] {
            const auto selected = s(peer->text().trimmed());
            if (!restoringWorkspace && selected != lastPeer) {
                sharedWorkspace.drafts["peer:" + lastPeer] = s(message->toPlainText());
                const QSignalBlocker blocked(message); message->setPlainText(q(sharedWorkspace.drafts["peer:" + selected]));
            }
            lastPeer = selected; saveWorkspace(); renderChats(); renderFiles(); updateTyping();
        });
        // Contact selection is handled by the messenger details page.
        conversationPane = content; contactSide = side; contactSplit = splitter;
        workbench = new QWidget; workbench->setObjectName("adaptiveWorkbench");
        workbenchLayout = new QVBoxLayout(workbench); workbenchLayout->setContentsMargins(0, 0, 0, 0); workbenchLayout->setSpacing(6);
        auto* toolbar = new QHBoxLayout;
        auto* directory = button("toggleContacts", QStringLiteral("联系人")); toolbar->addWidget(directory);
        QObject::connect(directory, &QPushButton::clicked, &window, [this] {
            if (window.width() >= 1250) { contactSide->setVisible(!contactSide->isVisible()); return; }
            auto* popup = new QDialog(&window); popup->setObjectName("contactDrawer"); popup->setWindowTitle(QStringLiteral("联系人与发现")); popup->setAttribute(Qt::WA_DeleteOnClose); popup->resize(350, 510);
            auto* box = new QVBoxLayout(popup); contactSide->setMaximumWidth(1000); box->addWidget(contactSide); contactSide->show();
            QObject::connect(popup, &QDialog::finished, &window, [this] { contactSide->setMaximumWidth(360); contactSplit->insertWidget(0, contactSide); contactSide->hide(); }); popup->show();
        });
        toolbar->addStretch(); workbenchLayout->addLayout(toolbar);
        workbenchTabs = new QTabWidget; workbenchTabs->setObjectName("workbenchTabs");
        workbenchSplit = new QSplitter; workbenchSplit->setObjectName("workbenchSplit"); workbenchSplit->setChildrenCollapsible(false);
        fileQueuePane = new pencil::Paper; fileQueuePane->setObjectName("fileQueuePane"); auto* queue = new QVBoxLayout(fileQueuePane); queue->setContentsMargins(8, 8, 8, 8);
        auto* delivery = button("newDelivery", QStringLiteral("文件交付与版本")); queue->addWidget(delivery); QObject::connect(delivery, &QPushButton::clicked, &window, [this] { featureUi->deliveries(peer->text()); });
        fileQueueScroll = new QScrollArea; fileQueueScroll->setObjectName("fileQueue"); fileQueueScroll->setWidgetResizable(true); queue->addWidget(fileQueueScroll, 1);
        workbenchTabs->addTab(content, QStringLiteral("聊天")); workbenchTabs->addTab(fileQueuePane, QStringLiteral("传输与交付"));
        workbenchLayout->addWidget(workbenchTabs, 1); workbenchLayout->addWidget(workbenchSplit, 1); workbenchSplit->hide();
        QObject::connect(workbenchSplit, &QSplitter::splitterMoved, &window, [this] { QSettings settings(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat); settings.setValue("workspace/codexSplit", workbenchSplit->saveState()); });
        splitter->addWidget(workbench); splitter->setStretchFactor(1, 1); splitter->setSizes({280, 760}); layout->addWidget(splitter);
        QTimer::singleShot(0, &window, [this] { resizeWorkbench(); });
        tabs->addTab(page, theme::icon(theme::Glyph::Chat, QColor(0xA9, 0xC6, 0xC0)), QStringLiteral("聊天"));
        renderFiles();
    }
    void sendChat() { submitText(false); }
    void saveWorkspace() {
        if (restoringWorkspace || !workspaceSave) return;
        sharedWorkspace.selectedPeer = s(peer->text().trimmed()); sharedWorkspace.selectedGroup = groupPicker ? groupPicker->currentData().toLongLong() : 0;
        featureUi->workspace = sharedWorkspace; workspaceSave->start(); persistPresentation();
    }
    void resizeWorkbench() {
        updateChatRemark();
        if (window.width() < 960) for (auto* split : {messageSplit, contactsSplit}) if (split && split->sizes().value(0) != 240) split->setSizes({240, window.width() - 304});
    }
    void loadDirectPreference() {
        const QSignalBlocker blocker(directEnabled);
        QSettings settings(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat);
        directEnabled->setChecked(directOverride.value_or(settings.value("files/allowDirect", true).toBool()));
    }
    void buildAttachmentBox(QVBoxLayout* conversation) {
        auto* section = new QWidget; section->setObjectName("fileTransferSection");
        section->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        auto* sectionLayout = new QVBoxLayout(section); sectionLayout->setContentsMargins(0, 0, 0, 0); sectionLayout->setSpacing(6);
        transferPanel = new QWidget; transferPanel->setObjectName("conversationFiles");
        auto* layout = new QVBoxLayout(transferPanel); layout->setContentsMargins(12, 10, 12, 10); layout->setSpacing(8);
        auto* head = new QHBoxLayout;
        head->addWidget(caption(QStringLiteral("发送文件"))); head->addStretch();
        auto* hint = label(QStringLiteral("最大 1 GiB · 文件直接显示在聊天中")); hint->setObjectName("hintNote"); head->addWidget(hint);
        layout->addLayout(head);
        auto* attachment = new QHBoxLayout; attachment->setSpacing(8);
        filePath = field("filePath"); filePath->setPlaceholderText(QStringLiteral("选择要发送的文件，发送给当前联系人"));
        filePath->setReadOnly(true); attachment->addWidget(filePath, 1);
        auto* browse = button("browseFile", QStringLiteral("选择文件")); attachment->addWidget(browse);
        attachmentRecipient = label({}); attachmentRecipient->setObjectName("attachmentRecipient"); layout->addWidget(attachmentRecipient);
        QObject::connect(browse, &QPushButton::clicked, &window, [this] {
            const auto recipient = s(peer->text().trimmed());
            const auto scope = presentationScope;
            if (recipient.empty() || accountPhase != AccountPhase::Online) { composerFeedback->setText(QStringLiteral("请先选择会话并恢复连接。")); return; }
            const auto selected = QFileDialog::getOpenFileName(&window, QStringLiteral("发送给 ") + q(recipient));
            if (!selected.isEmpty() && sessionAuthenticated && scope == presentationScope) { attachmentPeer = recipient; filePath->setText(QDir::toNativeSeparators(selected)); attachmentRecipient->setText(QStringLiteral("接收人：") + q(recipient)); transferPanel->show(); }
        });
        sendFile = button("sendFile", QStringLiteral("发送文件"), "primary"); attachment->addWidget(sendFile);
        QObject::connect(filePath, &QLineEdit::textChanged, &window, [this] { renderFiles(); });
        QObject::connect(sendFile, &QPushButton::clicked, &window, [this] {
            const auto target = attachmentPeer.empty() ? peer->text().trimmed() : q(attachmentPeer);
            if (accountPhase != AccountPhase::Online || target.isEmpty() || filePath->text().isEmpty()) { notify(QStringLiteral("请选择联系人和文件。"), true); return; }
            const auto id = "file-" + std::to_string(++submissionSequence);
            attachmentPeer = s(target);
            if (post(SendFile{s(target), path(filePath->text())}, id)) submissions[id] = {"peer:" + s(target), s(filePath->text()), true};
        });
        layout->addLayout(attachment);
        directEnabled = new QCheckBox(QStringLiteral("允许文件直连 · 不可达时自动中转"));
        directEnabled->setObjectName("allowDirectFiles");
        directEnabled->setToolTip(QStringLiteral("选择会自动保存。首次直连可能需要 Windows 网络许可；关闭后仍可通过服务器传文件。"));
        layout->addWidget(directEnabled); loadDirectPreference();
        QObject::connect(directEnabled, &QCheckBox::toggled, &window, [this](bool enabled) {
            directOverride.reset();
            QSettings settings(QDir(dataDir->text()).filePath("preferences.ini"), QSettings::IniFormat);
            settings.setValue("files/allowDirect", enabled); settings.sync();
            post(SetDirectEnabled{enabled});
            if (settings.status() != QSettings::NoError) notify(QStringLiteral("当前传输选项已切换，但设置保存失败，请检查数据目录权限。"), true);
            else notify(enabled ? QStringLiteral("已启用文件直连，无法连接时自动中转。") : QStringLiteral("已关闭文件直连，文件经服务器中转。"));
        });
        QObject::connect(dataDir, &QLineEdit::textChanged, &window, [this] { loadDirectPreference(); });
        sectionLayout->addWidget(transferPanel); conversation->addWidget(section); transferPanel->hide();
    }
    void buildGroups() {
        auto* page = new pencil::Paper; auto* layout = new QVBoxLayout(page); groupConversationPane = page;
        layout->setContentsMargins(16, 12, 16, 12); layout->setSpacing(10);
        auto* creation = new QHBoxLayout; creation->setSpacing(8);
        groupName = field("groupName"); groupName->setPlaceholderText(QStringLiteral("新群名称（2–64 人）"));
        groupMembers = field("groupMembers"); groupMembers->setPlaceholderText(QStringLiteral("成员用户名，以逗号分隔"));
        auto* create = button("createGroup", QStringLiteral("创建群"), "primary");
        creation->addWidget(groupName, 1); creation->addWidget(groupMembers, 2); creation->addWidget(create);
        auto* creationHost = new QWidget(page); creationHost->setLayout(creation); creationHost->hide();
        auto* pickRow = new QHBoxLayout; pickRow->setSpacing(8);
        auto* groupTitle = label(QStringLiteral("请选择群聊")); groupTitle->setObjectName("groupTitle"); pickRow->addWidget(groupTitle, 1);
        groupPicker = new QComboBox; groupPicker->setObjectName("groupPicker"); groupPicker->setParent(page); groupPicker->hide();
        layout->addLayout(pickRow);
        members = label(QStringLiteral("创建或选择一个群后开始群聊")); members->setObjectName("memberPill");
        layout->addWidget(members);
        groupScroll = messagesArea("groupMessages"); layout->addWidget(groupScroll, 1);
        groupMessage = new MessageComposer; groupMessage->setObjectName("groupInput"); groupMessage->setMaximumHeight(70);
        groupMessage->setPlaceholderText(QStringLiteral("发送给群内每位成员 · Ctrl+Enter 发送")); layout->addWidget(groupMessage);
        auto* actions = new QHBoxLayout; actions->setSpacing(8);
        auto* retry = button("retryGroup", QStringLiteral("重试失败成员"), "ghost");
        auto* send = button("sendGroup", QStringLiteral("发送群消息"), "primary");
        retry->setParent(page); retry->hide(); actions->addStretch(); actions->addWidget(send); layout->addLayout(actions);
        reportTable = new QTableWidget(0, 2, page); reportTable->setObjectName("groupSendReport");
        reportTable->setHorizontalHeaderLabels({QStringLiteral("收件成员"), QStringLiteral("排队结果")});
        reportTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); reportTable->verticalHeader()->hide();
        reportTable->setMaximumHeight(110); reportTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        reportTable->setShowGrid(false); reportTable->setAlternatingRowColors(true); reportTable->hide();
        QObject::connect(create, &QPushButton::clicked, &window, [this] {
            std::vector<std::string> names; for (const auto& name : groupMembers->text().split(QRegularExpression(QStringLiteral("[,，\\s]+")), Qt::SkipEmptyParts)) names.push_back(s(name));
            post(CreateGroup{s(groupName->text().trimmed()), names});
        });
        QObject::connect(groupPicker, &QComboBox::currentIndexChanged, &window, [this] {
            const auto id = groupPicker->currentData().toLongLong();
            if (!restoringWorkspace && id != lastGroup) {
                if (lastGroup > 0) sharedWorkspace.drafts["group:" + std::to_string(lastGroup)] = s(groupMessage->toPlainText());
                const QSignalBlocker blocked(groupMessage); groupMessage->setPlainText(q(sharedWorkspace.drafts["group:" + std::to_string(id)]));
            }
            lastGroup = id; saveWorkspace(); renderChats();
        });
        QObject::connect(groupMessage, &QPlainTextEdit::textChanged, &window, [this] {
            if (!restoringWorkspace && groupPicker->currentData().toLongLong() > 0) { const auto key = "group:" + std::to_string(groupPicker->currentData().toLongLong()); ++draftRevisions[key]; sharedWorkspace.drafts[key] = s(groupMessage->toPlainText()); saveWorkspace(); }
        });
        groupFeedback = label({}); groupFeedback->setObjectName("groupFeedback"); layout->addWidget(groupFeedback);
        static_cast<MessageComposer*>(groupMessage)->submit = [this] { submitText(true); };
        newGroupMessages = button("newGroupMessages", QStringLiteral("新消息 ↓")); newGroupMessages->setParent(groupScroll->viewport()); newGroupMessages->hide();
        QObject::connect(newGroupMessages, &QPushButton::clicked, &window, [this] { groupScroll->verticalScrollBar()->setValue(groupScroll->verticalScrollBar()->maximum()); newGroupMessages->hide(); markRead("group:" + std::to_string(groupPicker->currentData().toLongLong())); });
        QObject::connect(send, &QPushButton::clicked, &window, [this] { submitText(true); });
        QObject::connect(retry, &QPushButton::clicked, &window, [this] { if (!lastGroupSend.empty()) post(RetryGroup{lastGroupSend}); });
        auto* wrapper = new QWidget; auto* wrapperBox = new QVBoxLayout(wrapper); wrapperBox->addWidget(page);
        tabs->addTab(wrapper, theme::icon(theme::Glyph::Group, QColor(0xA9, 0xC6, 0xC0)), QStringLiteral("群聊"));
    }
    void updateContacts() {
        for (const auto& [_, record] : history) if (!record.groupId) temporaryPeers.insert(record.from == currentUser ? record.to : record.from);
        for (const auto& [_, file] : transfers) temporaryPeers.insert(file.peer);
        if (presentationLoaded) {
            for (const auto& [key, _] : recentCreated) { const auto target = targetFromKey(key); if (target.kind == ConversationTarget::Kind::Peer && target.valid()) temporaryPeers.insert(target.peer); }
            for (const auto& [key, draft] : sharedWorkspace.drafts) if (!draft.empty()) { const auto target = targetFromKey(key); if (target.kind == ConversationTarget::Kind::Peer && target.valid()) temporaryPeers.insert(target.peer); }
        }
        temporaryChats->clear();
        for (const auto& name : temporaryPeers) if (!name.empty() && name != currentUser && !friendNames.count(name)) {
            auto* item = new QListWidgetItem(q(name), temporaryChats); item->setData(Qt::UserRole, q(name)); item->setToolTip(q(name)); item->setSizeHint(QSize(200, 46));
        }
        contacts->clear();
        for (const auto& name : friendNames) {
            const bool connected = online.count(name) && online.at(name);
            auto* item = new QListWidgetItem; item->setData(Qt::UserRole, q(name)); item->setSizeHint(QSize(220, 56));
            auto* row = new QWidget; auto* box = new QHBoxLayout(row);
            box->setContentsMargins(8, 6, 10, 6); box->setSpacing(10);
            box->addWidget(avatarLabel(q(name), 32), 0, Qt::AlignVCenter);
            auto* column = new QVBoxLayout; column->setContentsMargins(0, 0, 0, 0); column->setSpacing(1);
            auto* title = label(q(name)); title->setWordWrap(false); title->setMinimumWidth(0); title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); title->setToolTip(q(name)); title->setStyleSheet(QStringLiteral("font-weight:600;background:transparent;"));
            column->addWidget(title);
            // 最近一条消息作为列表预览；文件显示名称，文字消息截断。
            QString preview; std::int64_t latest = -1;
            for (const auto& [_, record] : history) {
                if (record.groupId || (record.from != name && record.to != name)) continue;
                if (record.timestamp > latest) { latest = record.timestamp; preview = q(record.text); }
            }
            for (const auto& [_, file] : transfers) if (file.peer == name && file.timestamp > latest) { latest = file.timestamp; preview = QStringLiteral("文件 · ") + q(file.name); }
            auto* detail = label(connected ? QStringLiteral("在线") : (preview.isEmpty() ? QStringLiteral("离线") : preview.left(22)));
            detail->setObjectName("contactPresence"); detail->setProperty("online", connected);
            column->addWidget(detail);
            box->addLayout(column, 1);
            contacts->addItem(item); contacts->setItemWidget(item, row);
            if (peer->text() == q(name)) item->setSelected(true);
        }
    }
    void updateStrangers() {
        strangers->clear();
        for (const auto& [name, connected] : strangerOnline) {
            if (name == currentUser || friendNames.count(name)) continue;
            auto* item = new QListWidgetItem; item->setData(Qt::UserRole, q(name)); item->setSizeHint(QSize(220, 102));
            auto* row = new QWidget; auto* box = new QVBoxLayout(row); box->setContentsMargins(6, 6, 6, 6); box->setSpacing(6);
            auto* line = new QHBoxLayout; line->setSpacing(6); line->addWidget(avatarLabel(q(name), 28));
            auto* title = label(q(name)); title->setWordWrap(false); title->setMinimumWidth(0); title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); title->setToolTip(q(name)); line->addWidget(title, 1);
            auto* presence = label(connected ? QStringLiteral("在线") : QStringLiteral("离线")); presence->setObjectName("strangerPresence"); line->addWidget(presence); box->addLayout(line);
            auto* actions = new QHBoxLayout; actions->setSpacing(5); box->addLayout(actions);
            auto* temporary = button("temporaryChat", QStringLiteral("临时会话"), "ghost"); temporary->setToolTip(QStringLiteral("打开与该用户的临时加密会话，不自动添加好友"));
            auto* add = button("addFriend", QStringLiteral("加好友"), "primary");
            if (outgoingRequests.count(name)) { add->setText(QStringLiteral("已申请")); add->setEnabled(false); }
            else if (incomingRequests.count(name)) add->setText(QStringLiteral("处理申请"));
            temporary->setProperty("username", q(name)); add->setProperty("username", q(name));
            actions->addWidget(temporary); actions->addWidget(add);
            QObject::connect(temporary, &QPushButton::clicked, &window, [this, name] { temporaryPeers.insert(name); openConversation({ConversationTarget::Kind::Peer, name, 0}); updateContacts(); directorySection->setCurrentIndex(1); notify(QStringLiteral("已打开临时会话。")); });
            QObject::connect(add, &QPushButton::clicked, &window, [this, name] { if (incomingRequests.count(name)) { navigate(MessengerPage::Contacts); directorySection->setCurrentIndex(3); } else post(AddFriend{name}); });
            strangers->addItem(item); strangers->setItemWidget(item, row);
        }
        moreStrangers->setEnabled(!nextStranger.empty());
    }
    void updateRequests() {
        directorySection->setItemText(3, incomingRequests.empty() ? QStringLiteral("好友申请") : QStringLiteral("好友申请 (%1)").arg(incomingRequests.size()));
        friendRequests->clear();
        for (const auto& name : incomingRequests) {
            auto* item = new QListWidgetItem; item->setData(Qt::UserRole, q(name)); item->setSizeHint(QSize(220, 100));
            auto* row = new QWidget; auto* box = new QVBoxLayout(row); box->setContentsMargins(6, 6, 6, 6);
            auto* title = label(q(name)); title->setWordWrap(false); title->setMinimumWidth(0); title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); title->setToolTip(q(name)); box->addWidget(title);
            box->addWidget(label(QStringLiteral("请求加你为好友")));
            auto* actions = new QHBoxLayout; box->addLayout(actions);
            for (bool accept : {true, false}) {
                auto* action = button(accept ? "acceptFriend" : "declineFriend", accept ? QStringLiteral("同意") : QStringLiteral("拒绝"));
                action->setProperty("username", q(name)); actions->addWidget(action);
                QObject::connect(action, &QPushButton::clicked, &window, [this, name, accept] { post(RespondFriend{name, accept}); });
            }
            friendRequests->addItem(item); friendRequests->setItemWidget(item, row);
        }
        for (const auto& name : outgoingRequests) { auto* item = new QListWidgetItem(q(name) + QStringLiteral("\n等待对方同意"), friendRequests); item->setToolTip(q(name)); }
    }
    void updateTyping() {
        updateChatRemark();
        const auto target = s(peer->text().trimmed());
        chatAvatar->setPixmap(theme::avatar(target.empty() ? QStringLiteral("?") : q(target), 40));
        if (target.empty()) typing->setText(QStringLiteral("选择联系人开始私密对话"));
        else if (activeTyping.count(target) && activeTyping.at(target)) typing->setText(q(target) + QStringLiteral(" 正在输入…"));
        else typing->setText(!online.count(target) ? QStringLiteral("在线状态未知 · 消息端到端加密") : online.at(target) ? QStringLiteral("在线 · 消息端到端加密") : QStringLiteral("离线 · 消息会在对方上线后补投"));
    }
    QString availableFilePath(const FileProgress& progress) const {
        if (progress.path.empty() || (!progress.outgoing && (!progress.complete || progress.cancelled))) return {};
        const QFileInfo info(qpath(progress.path));
        return info.isFile() ? info.absoluteFilePath() : QString{};
    }
    void updateLocalFileButtons(QWidget* parent, const FileProgress& progress, const char* openName, const char* revealName) {
        const bool available = !availableFilePath(progress).isEmpty();
        const auto hint = available ? qpath(progress.path) : !progress.outgoing && !progress.complete
            ? QStringLiteral("文件尚未完整接收并验证，暂时不能打开") : QStringLiteral("本地文件已移动、删除或不可访问");
        for (const auto* name : {openName, revealName}) if (auto* action = parent->findChild<QPushButton*>(name)) { action->setEnabled(available); action->setToolTip(hint); }
    }
    void accessLocalFile(const std::string& id, bool reveal) {
        const auto found = transfers.find(id);
        const auto file = found == transfers.end() ? QString{} : availableFilePath(found->second);
        if (file.isEmpty()) { notify(QStringLiteral("无法打开：文件未完成接收，或本地文件已移动、删除。"), true); renderFiles(); return; }
        const bool opened = reveal ? localFileRevealer(file) : localFileOpener(file);
        if (!opened) notify(reveal ? QStringLiteral("无法在文件管理器中定位该文件，请检查本地路径。") : QStringLiteral("系统未能打开该文件，请检查默认打开程序。"), true);
    }
    void locateFileConversation(const std::string& id) {
        const auto found = transfers.find(id);
        if (!sessionAuthenticated || found == transfers.end() || found->second.peer.empty()) { notify(QStringLiteral("该文件的会话记录不可用。"), true); return; }
        const auto recipient = found->second.peer;
        const auto anchor = std::make_pair(found->second.timestamp, "file:" + id);
        std::size_t newer = 0;
        for (const auto& [key, item] : history) if (!item.groupId && (item.from == recipient || item.to == recipient) && std::make_pair(item.timestamp, "message:" + key) > anchor) ++newer;
        for (const auto& [key, item] : transfers) if (item.peer == recipient && std::make_pair(item.timestamp, "file:" + key) > anchor) ++newer;
        const auto target = ConversationTarget{ConversationTarget::Kind::Peer, recipient, 0};
        const int needed = static_cast<int>(((newer + 100) / 100) * 100);
        historyLimits[target.key()] = std::max(std::max(100, historyLimits[target.key()]), needed);
        highlightedFile = id; const auto sequence = ++fileAnchorSequence;
        openConversation(target, false); navigate(MessengerPage::Messages);
        QTimer::singleShot(0, chatScroll->widget(), [this, id, recipient, sequence] {
            if (sequence != fileAnchorSequence || activePage != MessengerPage::Messages || activeConversation.kind != ConversationTarget::Kind::Peer || activeConversation.peer != recipient) return;
            const auto card = fileCards.find(id);
            if (card == fileCards.end()) { notify(QStringLiteral("未找到对应的文件记录。"), true); return; }
            chatScroll->widget()->layout()->activate(); chatScroll->ensureWidgetVisible(card->second, 0, 24); chatScroll->setFocus();
            window.setProperty("locatedFileId", q(id));
        });
        QTimer::singleShot(2200, &window, [this, id, sequence] {
            if (sequence != fileAnchorSequence) return;
            highlightedFile.clear(); window.setProperty("locatedFileId", QString{});
            if (const auto card = fileCards.find(id); card != fileCards.end()) { card->second->setProperty("locatedFile", false); styleBubble(card->second); }
        });
    }
    void updateFileCard(QWidget* card, const FileProgress& progress) {
        auto* size = card->findChild<QLabel*>("fileSize");
        size->setText(amount(progress.authenticatedBytes) + " / " + amount(progress.totalBytes));
        auto* bar = card->findChild<QProgressBar*>("fileProgress");
        bar->setValue(filePercent(progress));
        bar->setProperty("transportPath", static_cast<int>(progress.transportPath));
        const auto route = progress.transportPath == 4 ? QStringLiteral("服务器缓存") : progress.transportPath == 2 ? QStringLiteral("直连") : progress.transportPath == 1 ? QStringLiteral("连接中") : QStringLiteral("中转");
        bar->setFormat(progress.cancelled ? QStringLiteral("已取消") : progress.complete ? QStringLiteral("完成 · 100%") : route + QStringLiteral(" · %p%"));
        const auto time = progress.timestamp > 0 ? QDateTime::fromMSecsSinceEpoch(progress.timestamp).toString("MM-dd HH:mm:ss") : QStringLiteral("历史文件 · 时间未记录");
        card->findChild<QLabel*>("fileTime")->setText(time + QStringLiteral(" · ") + fileState(progress));
        const bool active = !progress.complete && !progress.cancelled;
        const bool again=progress.complete && !progress.outgoing && progress.transportPath==4;
        card->findChild<QWidget*>("fileActions")->setVisible(active || again);
        card->findChild<QPushButton*>("resumeFile")->setEnabled(active || again);
        card->findChild<QPushButton*>("resumeFile")->setText(again ? QStringLiteral("重新下载") : QStringLiteral("恢复"));
        card->findChild<QPushButton*>("cancelFile")->setEnabled(active);
        updateLocalFileButtons(card, progress, "openFile", "revealFile");
        card->setToolTip(q(progress.name) + ("\n" + qpath(progress.path)));
    }
    void renderArea(QScrollArea* area, const std::vector<ChatRecord>& messages) {
        if (!area) return;
        struct Entry { std::int64_t timestamp; std::string key; const ChatRecord* message; const FileProgress* file; };
        std::vector<Entry> entries;
        std::set<std::string> logicalIds;
        for (const auto& item : messages) {
            if (item.groupId && !item.logicalGroupMessageId.empty() && !logicalIds.insert(item.from + ":" + item.logicalGroupMessageId).second) continue;
            entries.push_back({item.timestamp, "message:" + item.messageId, &item, nullptr});
        }
        if (area == chatScroll) {
            fileCards.clear(); const auto target = s(peer->text().trimmed());
            for (const auto& [id, file] : transfers) if (file.peer == target) entries.push_back({file.timestamp, "file:" + id, nullptr, &file});
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return std::tie(a.timestamp, a.key) < std::tie(b.timestamp, b.key); });
        const auto targetKey = area == chatScroll ? "peer:" + s(peer->text().trimmed()) : "group:" + std::to_string(groupPicker->currentData().toLongLong());
        const bool changedTarget = area->property("timelineTarget").toString() != q(targetKey);
        const int limit = std::max(100, historyLimits[targetKey]);
        const bool hasOlder = entries.size() > static_cast<std::size_t>(limit);
        if (hasOlder) entries.erase(entries.begin(), entries.end() - limit);
        const int oldMaximum = area->verticalScrollBar()->maximum();
        const bool prepending = area->property("loadingOlder").toBool(); area->setProperty("loadingOlder", false);
        const int oldScroll = changedTarget ? (readPositions.count(targetKey) ? readPositions[targetKey] : -1) : area->verticalScrollBar()->value();
        const auto newestKey = entries.empty() ? QString{} : q(entries.back().key);
        auto* newer = area == chatScroll ? newMessages : newGroupMessages;
        if (newer) {
            if (changedTarget) newer->hide();
            else if (oldScroll < oldMaximum - 30 && area->property("latestKey").toString() != newestKey) { newer->adjustSize(); newer->move(std::max(0, area->viewport()->width() - newer->width() - 12), std::max(0, area->viewport()->height() - newer->height() - 12)); newer->show(); newer->raise(); }
        }
        area->setProperty("latestKey", newestKey); area->setProperty("timelineTarget", q(targetKey));
        area->setProperty("timelineRendering", true);
        const bool atBottom = oldScroll < 0 || (!prepending && !changedTarget && oldScroll >= oldMaximum - 30);
        auto* body = new pencil::Paper(pencil::Tone::Paper,false); body->setObjectName("timelineBody");
        auto* layout = new QVBoxLayout(body); layout->setContentsMargins(10, 12, 10, 12); layout->setSpacing(8);
        if (hasOlder) {
            auto* more = button("loadOlderMessages", QStringLiteral("加载更早的消息")); layout->addWidget(more, 0, Qt::AlignHCenter);
            QObject::connect(more, &QPushButton::clicked, &window, [this, area, targetKey] { historyLimits[targetKey] = std::max(100, historyLimits[targetKey]) + 100; area->setProperty("loadingOlder", true); renderChats(); });
        }
        if (entries.empty()) {
            auto* emptyWrap = new QWidget; auto* emptyLayout = new QVBoxLayout(emptyWrap);
            emptyLayout->setContentsMargins(0, 30, 0, 30); emptyLayout->setSpacing(8);
            auto* mark = new QLabel; mark->setPixmap(theme::glyph(theme::Glyph::Chat, QColor(0xC9, 0xD5, 0xDE), 46));
            mark->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(mark);
            auto* empty = label(QStringLiteral("还没有消息。发送第一条消息开始对话。"));
            empty->setObjectName("emptyState"); empty->setAlignment(Qt::AlignCenter);
            emptyLayout->addWidget(empty);
            layout->addWidget(emptyWrap, 0, Qt::AlignHCenter);
        }
        QDate lastDay;
        for (const auto& entry : entries) {
            const auto* file = entry.file;
            const bool own = file ? file->outgoing : entry.message->from == currentUser;
            if (entry.timestamp > 0) {
                const auto day = QDateTime::fromMSecsSinceEpoch(entry.timestamp).date();
                if (day != lastDay) {
                    lastDay = day;
                    auto* separatorRow = new QHBoxLayout;
                    auto* separator = label(dayText(day)); separator->setObjectName("dateSeparator");
                    separatorRow->addStretch(); separatorRow->addWidget(separator); separatorRow->addStretch();
                    layout->addLayout(separatorRow);
                }
            }
            auto* row = new QHBoxLayout; row->setSpacing(8);
            auto* bubble = new pencil::Bubble;
            bubble->setObjectName(file ? "fileCard" : "chatBubble"); bubble->setProperty("timelineKey", q(entry.key)); bubble->setProperty("timestamp", static_cast<qlonglong>(entry.timestamp));
            const int maxWidth = file ? std::min(430, std::max(280, area->viewport()->width() - 20)) : std::max(270, area->viewport()->width() * 3 / 4);
            bubble->setMaximumWidth(maxWidth);
            bubble->setMinimumWidth(file ? std::min(300, maxWidth) : std::min(std::max(180, window.fontMetrics().horizontalAdvance(q(entry.message->text).left(32)) + 30), maxWidth));
            bubble->setProperty("ownMessage", own);
            styleBubble(bubble);
            auto* content = new QVBoxLayout(bubble); content->setContentsMargins(12, 9, 12, 8); content->setSpacing(4);
            if (!own) {
                const auto sender = file ? q(file->peer) : q(entry.message->from);
                auto* from = label(sender); from->setObjectName("messageName");
                content->addWidget(from);
            }
            if (file) {
                bubble->setProperty("fileId", q(file->fileId)); fileCards[file->fileId] = bubble;
                bubble->setProperty("locatedFile", file->fileId == highlightedFile); styleBubble(bubble);
                auto* head = new QHBoxLayout; head->setSpacing(9);
                auto* chip = new QLabel; chip->setPixmap(theme::fileChip(34, activeAccent())); chip->setFixedSize(34, 34); chip->setProperty("fileChipSize", 34);
                head->addWidget(chip, 0, Qt::AlignTop);
                auto* headText = new QVBoxLayout; headText->setContentsMargins(0, 0, 0, 0); headText->setSpacing(2);
                auto* name = messageLabel(QStringLiteral("文件 · ") + q(file->name)); name->setObjectName("fileName");
                name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); headText->addWidget(name);
                auto* size = label({}); size->setObjectName("fileSize"); headText->addWidget(size);
                head->addLayout(headText, 1); content->addLayout(head);
                auto* bar = new QProgressBar; bar->setObjectName("fileProgress"); bar->setRange(0, 100); content->addWidget(bar);
                auto* detail = label({}); detail->setObjectName("fileTime"); content->addWidget(detail);
                auto* actions = new QWidget; actions->setObjectName("fileActions"); auto* buttons = new QHBoxLayout(actions); buttons->setContentsMargins(0, 2, 0, 0); buttons->setSpacing(6);
                auto* resume = button("resumeFile", QStringLiteral("恢复"), "ghost"); auto* cancel = button("cancelFile", QStringLiteral("取消"), "ghost");
                buttons->addWidget(resume); buttons->addWidget(cancel); buttons->addStretch(); content->addWidget(actions);
                for (auto* action : {resume, cancel}) QObject::connect(action, &QPushButton::clicked, &window, [this, id = file->fileId, resumeAction = action == resume] {
                    const auto found = transfers.find(id);
                    if (found == transfers.end() || found->second.peer != s(peer->text().trimmed()) || found->second.complete || found->second.cancelled) return;
                    if (resumeAction) post(ResumeFile{id}); else post(CancelFile{id});
                });
                auto* local = new QHBoxLayout; local->setSpacing(6);
                auto* open = button("openFile", QStringLiteral("打开")); auto* reveal = button("revealFile", QStringLiteral("在文件管理器中打开"));
                local->addWidget(open); local->addWidget(reveal); local->addStretch(); content->addLayout(local);
                QObject::connect(open, &QPushButton::clicked, &window, [this, id = file->fileId] { accessLocalFile(id, false); });
                QObject::connect(reveal, &QPushButton::clicked, &window, [this, id = file->fileId] { accessLocalFile(id, true); });
                updateFileCard(bubble, *file);
            } else {
                const auto& item = *entry.message;
                auto* text = messageLabel(q(item.text)); text->setObjectName("messageText"); content->addWidget(text);
                const auto time = QDateTime::fromMSecsSinceEpoch(item.timestamp).toString("HH:mm");
                QString state;
                if (own) state = item.state == DeliveryState::RecipientReceived ? QStringLiteral(" · ✓✓ 已签收") : item.state == DeliveryState::ServerAccepted ? QStringLiteral(" · ✓ 已送达服务器") : item.state == DeliveryState::Failed ? QStringLiteral(" · ⚠ 失败") : QStringLiteral(" · ◌ 待发送");
                auto* detail = new StageLabel; detail->setText(time + state); detail->setObjectName("messageDelivery"); detail->setCursor(Qt::PointingHandCursor); detail->setToolTip(QStringLiteral("查看这条消息的安全流程与签收详情")); content->addWidget(detail);
                detail->clicked = [this, id = q(item.messageId)] { featureUi->messageDetails(id); };
                if (own && item.groupId && !item.logicalGroupMessageId.empty()) {
                    auto* report = button("messageGroupReport", QStringLiteral("群发送结果")); content->addWidget(report);
                    QObject::connect(report, &QPushButton::clicked, &window, [this, id = item.logicalGroupMessageId] { showGroupReport(id); });
                }
                if (item.extensions.contains("delivery") && item.extensions["delivery"].value("action", "") == "manifest") {
                    const auto id = item.extensions["delivery"]["manifest"].value("id", "");
                    for (const auto& b : featureUi->bundles) if (b.id == id) {
                        auto* bar = new QProgressBar; bar->setObjectName("deliveryProgress"); bar->setValue(qRound(100 * deliveryProgress(b))); content->addWidget(bar);
                        content->addWidget(label(b.manuallyConfirmed ? QStringLiteral("用户已人工确认收到") : b.state == BundleState::Complete ? QStringLiteral("文件已验证 · 等待人工确认") : QStringLiteral("多文件交付正在传输")));
                    }
                    auto* open = button("openDelivery", QStringLiteral("交付详情与版本")); content->addWidget(open); QObject::connect(open, &QPushButton::clicked, &window, [this] { featureUi->deliveries(peer->text()); });
                }
            }
            if (!own) {
                auto* avatar = avatarLabel(file ? q(file->peer) : q(entry.message->from), 28);
                row->addWidget(avatar, 0, Qt::AlignTop);
            }
            if (own) row->addStretch();
            row->addWidget(bubble);
            if (!own) row->addStretch();
            layout->addLayout(row);
        }
        layout->addStretch(); area->setWidget(body); body->setAutoFillBackground(false);
        QTimer::singleShot(0, body, [this, area, atBottom, oldScroll, oldMaximum, prepending, targetKey] {
            const auto maximum = area->verticalScrollBar()->maximum();
            area->verticalScrollBar()->setValue(atBottom ? maximum : std::max(0, oldScroll) + (prepending ? maximum - oldMaximum : 0));
            area->setProperty("timelineRendering", false);
            readPositions[targetKey] = area->verticalScrollBar()->value() >= maximum - 30 ? -1 : area->verticalScrollBar()->value();
            markCurrentRead();
        });
    }
    void renderChats() {
        if (!peer || !groupPicker) return;
        std::vector<ChatRecord> direct, grouped; const auto target = s(peer->text().trimmed()); const auto groupId = groupPicker->currentData().toLongLong();
        for (const auto& [_, item] : history) {
            if (!item.groupId && (item.from == target || item.to == target)) direct.push_back(item);
            if (item.groupId && item.groupId == groupId) grouped.push_back(item);
        }
        renderArea(chatScroll, std::move(direct)); renderArea(groupScroll, std::move(grouped));
        if (groups.count(groupId)) { QStringList names; for (const auto& name : groups.at(groupId).members) names << q(name); members->setText(QStringLiteral("%1 位成员 · 群消息分别加密投递").arg(names.size())); members->setToolTip(names.join(QStringLiteral("、"))); window.findChild<QLabel*>("groupTitle")->setText(q(groups.at(groupId).name)); }
    }
    QWidget* makeFileQueueRow(const std::string& id) {
        auto* row = new QWidget; row->setObjectName("fileQueueRow"); row->setProperty("fileId", q(id));
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        auto* box = new QHBoxLayout(row); box->setContentsMargins(12, 10, 12, 10); box->setSpacing(12);
        auto* chip = new QLabel; chip->setPixmap(theme::fileChip(34, activeAccent())); chip->setFixedSize(34, 34); chip->setProperty("fileChipSize", 34); box->addWidget(chip, 0, Qt::AlignTop);
        auto* details = new QVBoxLayout; details->setContentsMargins(0, 0, 0, 0); details->setSpacing(5); box->addLayout(details, 1);
        auto* head = new QHBoxLayout; head->setSpacing(10); details->addLayout(head);
        auto* name = new ElidedFileLabel; name->setObjectName("queueFileName"); head->addWidget(name, 1);
        auto* progress = new QProgressBar; progress->setObjectName("queueFileProgress"); progress->setRange(0, 100); progress->setTextVisible(false); progress->setFixedWidth(104); head->addWidget(progress);
        auto* state = label({}); state->setObjectName("queueFileState"); state->setWordWrap(false); head->addWidget(state);
        auto* info = new ElidedFileLabel; info->setObjectName("queueFileInfo"); details->addWidget(info);
        auto* footer = new QHBoxLayout; footer->setSpacing(6); details->addLayout(footer);
        auto* conversation = new ElidedFileLabel; conversation->setObjectName("queueConversationLabel"); footer->addWidget(conversation, 1);
        for (const auto& item : std::vector<std::pair<const char*, QString>>{{"queueResume", QStringLiteral("恢复")}, {"queueCancel", QStringLiteral("取消")}, {"queueConversation", QStringLiteral("对话位置")}, {"queueOpenFile", QStringLiteral("打开")}, {"queueOpenDirectory", QStringLiteral("在文件管理器中打开")}}) {
            auto* action = button(item.first, item.second); action->setProperty("queueAction", true); action->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); footer->addWidget(action);
            QObject::connect(action, &QPushButton::clicked, &window, [this, id, name = std::string(item.first)] {
                if (name == "queueResume") post(ResumeFile{id});
                else if (name == "queueCancel") post(CancelFile{id});
                else if (name == "queueConversation") locateFileConversation(id);
                else accessLocalFile(id, name == "queueOpenDirectory");
            });
        }
        return row;
    }
    void updateFileQueueRow(QWidget* row, const FileProgress& file) {
        static_cast<ElidedFileLabel*>(row->findChild<QLabel*>("queueFileName"))->setFullText(q(file.name));
        const auto time = file.timestamp > 0 ? QDateTime::fromMSecsSinceEpoch(file.timestamp).toString("yyyy-MM-dd HH:mm") : QStringLiteral("时间未记录");
        const bool active = !file.complete && !file.cancelled;
        static_cast<ElidedFileLabel*>(row->findChild<QLabel*>("queueFileInfo"))->setFullText((active || file.cancelled ? amount(file.authenticatedBytes) + " / " : QString{}) + amount(file.totalBytes) + QStringLiteral(" · ") + (file.outgoing ? QStringLiteral("我发送") : QStringLiteral("我接收")) + QStringLiteral(" · ") + time);
        static_cast<ElidedFileLabel*>(row->findChild<QLabel*>("queueConversationLabel"))->setFullText(QStringLiteral("会话：") + q(file.peer));
        auto* bar = row->findChild<QProgressBar*>("queueFileProgress");
        const int percent = filePercent(file);
        bar->setValue(percent); bar->setVisible(active);
        row->findChild<QLabel*>("queueFileState")->setText(fileState(file) + QStringLiteral(" · %1%").arg(percent));
        for (const auto* name : {"queueResume", "queueCancel"}) row->findChild<QPushButton*>(name)->setVisible(active);
        const bool again=file.complete && !file.outgoing && file.transportPath==4;
        row->findChild<QPushButton*>("queueResume")->setVisible(active || again);
        row->findChild<QPushButton*>("queueResume")->setText(again ? QStringLiteral("重新下载") : QStringLiteral("恢复"));
        row->findChild<QPushButton*>("queueResume")->setEnabled((active || again) && accountPhase == AccountPhase::Online);
        row->findChild<QPushButton*>("queueConversation")->setToolTip(QStringLiteral("定位到与 %1 的会话中的这个文件").arg(q(file.peer)));
        updateLocalFileButtons(row, file, "queueOpenFile", "queueOpenDirectory");
    }
    void renderFiles() {
        if (fileQueueScroll) {
            std::vector<std::string> visible;
            std::size_t active = 0;
            for (const auto& [id, file] : transfers) {
                if (!file.complete && !file.cancelled) ++active;
                if (!fileFilter || fileFilter->currentIndex() == 0 || (fileFilter->currentIndex() == 1 && !file.complete && !file.cancelled) || (fileFilter->currentIndex() == 2 && file.complete)) visible.push_back(id);
            }
            std::sort(visible.begin(), visible.end(), [this](const auto& a, const auto& b) {
                const auto& left = transfers.at(a); const auto& right = transfers.at(b);
                return std::tie(left.timestamp, a) > std::tie(right.timestamp, b);
            });
            if (!fileQueueBody || visible != fileQueueOrder) {
                const auto scroll = fileQueueScroll->verticalScrollBar()->value();
                auto* body = new QWidget; body->setObjectName("fileQueueBody"); body->setMaximumWidth(1100);
                auto* queue = new QVBoxLayout(body); queue->setContentsMargins(12, 12, 12, 12); queue->setSpacing(6);
                fileQueueRows.clear();
                for (const auto& id : visible) { auto* row = makeFileQueueRow(id); fileQueueRows[id] = row; queue->addWidget(row); }
                if (visible.empty()) {
                    queue->addWidget(label(QStringLiteral("当前没有符合筛选条件的文件。可从私聊输入区选择并发送附件。")));
                    auto* start = button("queueStartChat", QStringLiteral("前往消息")); queue->addWidget(start, 0, Qt::AlignLeft);
                    QObject::connect(start, &QPushButton::clicked, &window, [this] { navigate(MessengerPage::Messages); });
                }
                queue->addStretch(); fileQueueBody = body; fileQueueOrder = visible;
                fileQueueScroll->setAlignment(Qt::AlignTop | Qt::AlignHCenter); fileQueueScroll->setWidget(body); body->setAutoFillBackground(false);
                QTimer::singleShot(0, body, [this, scroll] { fileQueueScroll->verticalScrollBar()->setValue(scroll); });
            }
            for (const auto& [id, row] : fileQueueRows) updateFileQueueRow(row, transfers.at(id));
            if (auto* summary = window.findChild<QLabel*>("fileQueueSummary")) summary->setText(QStringLiteral("%1 个文件").arg(visible.size()) + (active ? QStringLiteral(" · %1 个进行中").arg(active) : QString{}));
        }
        const bool atBottom = chatScroll->verticalScrollBar()->value() >= chatScroll->verticalScrollBar()->maximum() - 30;
        for (const auto& [id, card] : fileCards) {
            const auto found = transfers.find(id);
            if (found != transfers.end()) updateFileCard(card, found->second);
        }
        if (atBottom) QTimer::singleShot(0, chatScroll->widget(), [this] { chatScroll->verticalScrollBar()->setValue(chatScroll->verticalScrollBar()->maximum()); });
        sendFile->setEnabled(accountPhase == AccountPhase::Online && (!attachmentPeer.empty() || !peer->text().trimmed().isEmpty()) && !filePath->text().trimmed().isEmpty());
    }
    void showIdentity(const PeerIdentity& identity) {
        if (identity.username.empty()) return;
        const bool requested = requestedIdentities.erase(identity.username) != 0;
        const auto hash = s(fingerprint(identity));
        if (!requested && (identity.manuallyVerified || promptedIdentities[identity.username] == hash)) return;
        if (!requested) promptedIdentities[identity.username] = hash;
        auto* card = featureUi->identityCard(identity); identityDialogs[identity.username] = card;
        QObject::connect(card, &QObject::destroyed, &window, [this, name = identity.username, card] {
            auto current=identityDialogs.find(name); if (current!=identityDialogs.end() && current->second==card) identityDialogs.erase(current);
        });
    }
    void phase(AccountPhase phase) {
        const auto previousPhase = accountPhase; accountPhase = phase;
        QString text, kind;
        switch (phase) {
            case AccountPhase::Disconnected: text = QStringLiteral("未连接"); kind = QStringLiteral("idle"); break;
            case AccountPhase::TransportConnected: case AccountPhase::Negotiating: text = QStringLiteral("验证连接中…"); kind = QStringLiteral("busy"); break;
            case AccountPhase::Connected: text = QStringLiteral("已连接 · 未登录"); kind = QStringLiteral("idle"); break;
            case AccountPhase::Registering: text = QStringLiteral("注册中…"); kind = QStringLiteral("busy"); break;
            case AccountPhase::ChallengePending: text = QStringLiteral("登录验证中…"); kind = QStringLiteral("busy"); break;
            case AccountPhase::Online: text = QStringLiteral("已登录 · ") + q(currentUser); kind = QStringLiteral("online");
                if (registrationPending) { username->setText(q(currentUser)); registrationPending = false; registrationFeedback->clear(); }
                if (previousPhase != AccountPhase::Online) tabs->setCurrentIndex(1);
                notify(QStringLiteral("已登录，可以开始聊天或发送文件。")); break;
        }
        status->setText(text);
        if (status->property("phase").toString() != kind) {
            status->setProperty("phase", kind);
            status->style()->unpolish(status); status->style()->polish(status);
        }
        syncSessionShell();
        const bool idle = phase == AccountPhase::Disconnected || phase == AccountPhase::Connected || phase == AccountPhase::Online;
        login->setEnabled(idle && !rememberedLogin.authenticating()); registerAccount->setEnabled(idle && !rememberedLogin.authenticating());
        syncRegistrationControls();
    }
    void drain() {
        bool messagesChanged = false, usersChanged = false, filesChanged = false;
        for (auto& event : worker.drain()) {
            rememberedLogin.consume(event);
            if (const auto* view = std::get_if<RestoredView>(&event); view && view->username != currentUser) {
                promptedIdentities.clear(); requestedIdentities.clear();
                for (auto* card : window.findChildren<QDialog*>("identityDialog")) card->close();
            }
            if (const auto* completed = std::get_if<CommandResult>(&event)) { handleSubmitResult(*completed); continue; }
            if (auto* client = std::get_if<ClientEvent>(&event)) if (auto* workspace = std::get_if<WorkspaceState>(client)) {
                // Every embedded edit is coordinated here before it reaches the worker.
                // An older persisted snapshot must not replace a newer local edit.
                if (nlohmann::json(*workspace) != nlohmann::json(sharedWorkspace)) continue;
            }
            featureUi->consume(event);
#ifdef CCFW_HAS_RHINE_NATIVE
            if (nativeBridge) nativeBridge->publish(event);
#endif
            if (auto* view = std::get_if<RestoredView>(&event)) {
                sharedWorkspace = view->workspace; restoringWorkspace = true;
                peer->setText(q(sharedWorkspace.selectedPeer)); message->setPlainText(q(sharedWorkspace.drafts["peer:" + sharedWorkspace.selectedPeer]));
                restoringWorkspace = false;
                const bool identityChanged = currentUser != view->username || ownIdentity.signingPublicKey != view->ownIdentity.signingPublicKey || ownIdentity.dhPublicKey != view->ownIdentity.dhPublicKey;
                currentUser = view->username; window.setWindowTitle(QStringLiteral("CipherChat · ") + q(currentUser)); ownIdentity = view->ownIdentity;
                ownSecurity->setEnabled(!ownIdentity.signingPublicKey.empty() && !ownIdentity.dhPublicKey.empty());
                if (identityChanged) if (auto* details = window.findChild<QDialog*>("ownIdentityDialog")) details->close();
                history.clear(); for (const auto& item : view->history) history[item.messageId] = item;
                transfers.clear(); for (const auto& item : view->files) transfers[item.fileId] = item;
                groups.clear(); for (const auto& item : view->groups) groups[item.id] = item; refreshGroupPicker();
                const int selectedGroup = groupPicker->findData(static_cast<qlonglong>(sharedWorkspace.selectedGroup));
                restoringWorkspace = true; if (selectedGroup >= 0) groupPicker->setCurrentIndex(selectedGroup); lastGroup = groupPicker->currentData().toLongLong(); groupMessage->setPlainText(q(sharedWorkspace.drafts["group:" + std::to_string(lastGroup)])); restoringWorkspace = false;
                groupReports.clear(); for (const auto& draft : view->groupDrafts) groupReports[draft.report.logicalMessageId] = draft.report;
                if (sessionAuthenticated) restorePresentation();
                messagesChanged = usersChanged = filesChanged = true; continue;
            }
            std::visit([&](auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, AccountEvent>) { phase(item.phase); if (item.phase == AccountPhase::Online && directorySection->currentIndex() == 2) searchStrangers->click(); }
                else if constexpr (std::is_same_v<T, MessageStageEvent>) identitySubmissionProgress(item);
                else if constexpr (std::is_same_v<T, ChatRecord>) { noteIncoming(item); history[item.messageId] = item; messagesChanged = usersChanged = true; }
                else if constexpr (std::is_same_v<T, FileProgress>) {
                    if (!item.outgoing && !transfers.count(item.fileId)) { unreadMessages["peer:" + item.peer].insert("file:" + item.fileId); persistPresentation(); }
                    if (!transfers.count(item.fileId)) messagesChanged = true;
                    transfers[item.fileId] = item; filesChanged = usersChanged = true;
                    if (item.cancelled) notify(QStringLiteral("已取消文件传输，临时文件与记录已保留。"));
                    else if (item.complete) notify(QStringLiteral("文件传输已完成：") + q(item.name));
                    // Byte progress belongs to cards and the global queue, not the notice banner.
                }
                else if constexpr (std::is_same_v<T, PeerIdentity>) {
                    if (item.manuallyVerified) deferredIdentities.erase(item.username);
                    if (sessionAuthenticated && window.isVisible()) showIdentity(item);
                    else if (!item.manuallyVerified && (sessionAuthenticated || rememberedLogin.authenticating())) deferredIdentities[item.username] = item;
                    window.setProperty("deferredIdentityCount", static_cast<int>(deferredIdentities.size()));
                    usersChanged = true;
                }
                else if constexpr (std::is_same_v<T, DeliveryBundle>) messagesChanged = true;
                else if constexpr (std::is_same_v<T, WorkspaceState>) {
                    if (!workspaceSave || !workspaceSave->isActive()) sharedWorkspace = item;
#ifdef CCFW_HAS_RHINE_NATIVE
                    if (nativeWindow && nativeWindow->isVisible()) {
                        restoringWorkspace = true; peer->setText(q(item.selectedPeer)); message->setPlainText(q(item.drafts.count("peer:" + item.selectedPeer) ? item.drafts.at("peer:" + item.selectedPeer) : "")); restoringWorkspace = false;
                    }
#endif
                }
                else if constexpr (std::is_same_v<T, PresenceEvent>) { online[item.username] = item.online; if (strangerOnline.count(item.username)) strangerOnline[item.username] = item.online; usersChanged = true; }
                else if constexpr (std::is_same_v<T, TypingEvent>) { activeTyping[item.username] = item.active; updateTyping(); }
                else if constexpr (std::is_same_v<T, UserListEvent>) { online.clear(); for (const auto& [name, active] : item.users) online[name] = active; usersChanged = true; }
                else if constexpr (std::is_same_v<T, ContactListEvent>) {
                    friendNames.clear(); for (const auto& [name, active] : item.users) { friendNames.insert(name); online[name] = active; }
                    incomingRequests = {item.incoming.begin(), item.incoming.end()}; outgoingRequests = {item.outgoing.begin(), item.outgoing.end()}; usersChanged = true;
                }
                else if constexpr (std::is_same_v<T, StrangerListEvent>) {
                    if (item.query != activeQuery) return;
                    if (item.after.empty()) strangerOnline.clear();
                    for (const auto& [name, active] : item.users) { strangerOnline[name] = active; online[name] = active; }
                    nextStranger = item.next; usersChanged = true;
                    directoryHint->setText(strangerOnline.empty() ? QStringLiteral("没有找到匹配的陌生人。") : QStringLiteral("已显示 %1 人 · 可发起临时会话").arg(strangerOnline.size()));
                }
                else if constexpr (std::is_same_v<T, FriendResultEvent>) {
                    if (item.ok) notify(item.state == "pending" ? QStringLiteral("好友申请已发送，等待对方同意。") : item.state == "declined" ? QStringLiteral("已拒绝好友申请。") : QStringLiteral("你们已成为好友。"));
                    else notify(QStringLiteral("好友操作失败：") + q(item.error), true);
                }
                else if constexpr (std::is_same_v<T, GroupSnapshot>) { groups[item.id] = item; refreshGroupPicker(); messagesChanged = true; }
                else if constexpr (std::is_same_v<T, GroupSendReport>) {
                    groupReports[item.logicalMessageId] = item; lastGroupSend = item.logicalMessageId; reportTable->setRowCount(static_cast<int>(item.members.size()));
                    for (int row = 0; row < static_cast<int>(item.members.size()); ++row) {
                        reportTable->setItem(row, 0, new QTableWidgetItem(q(item.members[row].recipient)));
                        reportTable->setItem(row, 1, new QTableWidgetItem(item.members[row].messageId ? QStringLiteral("已进入可靠发送队列") : q(item.members[row].error)));
                    }
                    const auto failed = std::count_if(item.members.begin(), item.members.end(), [](const auto& member) { return !member.messageId; });
                    notify(failed ? QStringLiteral("部分成员发送失败，请核对身份或重试失败成员。") : QStringLiteral("群消息已进入发送队列，签收状态将在聊天记录中更新。"), failed != 0);
                } else if constexpr (std::is_same_v<T, Error>) {
                    window.setProperty("lastBusinessError", QVariantMap{{"code", static_cast<int>(item.code)}, {"task", q(item.task)}});
                    if (item.task == "M3-02") { sessionAuthenticated = false; deferredIdentities.clear(); window.setProperty("sessionAuthenticated", false); if (sessionPages) sessionPages->setCurrentIndex(registrationPending ? 2 : 0); if (tray) tray->hide(); for (auto* dialog : window.findChildren<QDialog*>()) dialog->close(); }
                    notify(q(item.message), item.code != ErrorCode::IdentityChanged);
                }
            }, std::get<ClientEvent>(event));
        }
        if (messagesChanged) renderChats();
        if (usersChanged) { updateContacts(); updateStrangers(); updateRequests(); updateTyping(); }
        if (filesChanged) renderFiles();
        if (messagesChanged || usersChanged || filesChanged) { renderRecent(); refreshContactGroups(); filterContacts(); }
        markCurrentRead();
        if (sessionAuthenticated && window.isVisible() && !deferredIdentities.empty()) {
            const auto pending = std::move(deferredIdentities); deferredIdentities.clear();
            window.setProperty("deferredIdentityCount", 0);
            for (const auto& [name, identity] : pending) showIdentity(identity);
            renderRecent();
        }
    }
    void refreshGroupPicker() {
        const auto selected = groupPicker->currentData(); groupPicker->blockSignals(true); groupPicker->clear();
        for (const auto& [id, group] : groups) groupPicker->addItem(q(group.name) + QStringLiteral("  ·  ") + QString::number(group.members.size()) + QStringLiteral(" 人"), static_cast<qlonglong>(id));
        const int index = groupPicker->findData(selected); if (index >= 0) groupPicker->setCurrentIndex(index); groupPicker->blockSignals(false);
    }
};
MainWindow::MainWindow(WindowOptions options) {
    setWindowTitle(QStringLiteral("CipherChat")); resize(1080, 800); setMinimumSize(860, 640);
    setStyleSheet(theme::stylesheet());
    impl_ = std::make_unique<Impl>(*this, std::move(options));
}
MainWindow::~MainWindow() {
    if (!impl_) return;
    impl_->shuttingDown = true;
    impl_->timer->stop(); impl_->workspaceSave->stop();
    impl_->worker.requestStop(); impl_->worker.join();
    // Impl is destroyed before QMainWindow's QObject children. In particular,
    // identity-dialog destroyed signals must not call a freed Impl.
    for (auto* child : findChildren<QObject*>()) QObject::disconnect(child, nullptr, this, nullptr);
    QObject::disconnect(this, nullptr, this, nullptr);
#ifdef CCFW_HAS_RHINE_NATIVE
    if (impl_->nativeWindow) QObject::disconnect(impl_->nativeWindow, nullptr, this, nullptr);
#endif
}
QString MainWindow::switchFrontend(const QString& design) { return impl_->switchFrontend(design); }
void MainWindow::navigate(MessengerPage page) { impl_->navigate(page); }
void MainWindow::openConversation(ConversationTarget target) { if (impl_->sessionAuthenticated) impl_->openConversation(std::move(target)); }
void MainWindow::closeEvent(QCloseEvent* event) {
    if (property("exitForUpdate").toBool() && !impl_->explicitExit && impl_->sessionAuthenticated) { setProperty("exitForUpdate", false); event->ignore(); impl_->requestLeave(true, true); return; }
    if (!impl_->explicitExit && impl_->sessionAuthenticated && QSystemTrayIcon::isSystemTrayAvailable()) { event->ignore(); hide(); impl_->tray->show(); return; }
    if (!impl_->explicitExit && impl_->sessionAuthenticated) { event->ignore(); impl_->requestLeave(true); return; }
    impl_->shuttingDown = true; impl_->timer->stop(); impl_->workspaceSave->stop(); impl_->worker.requestStop();
    impl_->worker.join();
#ifdef CCFW_HAS_RHINE_NATIVE
    if (impl_->nativeWindow && impl_->nativeWindow->isVisible()) impl_->nativeWindow->close();
#endif
    event->accept();
}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (impl_) QTimer::singleShot(0, this, [this] { impl_->resizeWorkbench(); impl_->renderChats(); });
}
}
