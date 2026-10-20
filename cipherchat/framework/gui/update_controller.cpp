#include "update_controller.hpp"
#include "../update/update.hpp"
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
namespace ccfw::gui {
namespace {
std::filesystem::path p(const QString& value) { return std::filesystem::path(value.toStdWString()); }
QString q(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
QString amount(std::uint64_t value) { return QString::number(double(value) / 1048576, 'f', 2) + " MiB"; }
}
UpdateController::UpdateController(QWidget* window, QPushButton* entry, const QString& directory, const QString& helper, bool startupPrompt)
    : QObject(window), window_(window), entry_(entry), current_(directory.isEmpty() ? QCoreApplication::applicationDirPath() : directory),
      helper_(helper.isEmpty() ? current_ + "/cipherchat-updater.exe" : helper) {
    startupPrompt_=startupPrompt;
    dialog_ = new QDialog(window); dialog_->setObjectName("updateDialog"); dialog_->setWindowTitle(QStringLiteral("软件更新")); dialog_->resize(460,370);
    auto* layout = new QVBoxLayout(dialog_); layout->setContentsMargins(24,24,24,24); layout->setSpacing(14);
    version_ = new QLabel(QStringLiteral("开发版本")); version_->setObjectName("updateVersion"); version_->setTextFormat(Qt::PlainText); version_->setWordWrap(true); layout->addWidget(version_);
    status_ = new QLabel; status_->setObjectName("updateStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); status_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status_,1);
    progress_ = new QProgressBar; progress_->setRange(0,100); progress_->hide(); layout->addWidget(progress_);
    auto make = [&](const char* name, const QString& text) { auto* b = new QPushButton(text); b->setObjectName(name); layout->addWidget(b); return b; };
    check_ = make("checkUpdate",QStringLiteral("检查更新")); download_ = make("downloadUpdate",QStringLiteral("下载更新"));
    pause_ = make("pauseUpdate",QStringLiteral("暂停下载"));
    apply_ = make("applyUpdate",QStringLiteral("退出并更新")); rollback_ = make("rollbackUpdate",QStringLiteral("回退上一版"));
    process_ = new QProcess(this);
    watchdog_ = new QTimer(this); watchdog_->setInterval(1000);
    connect(watchdog_, &QTimer::timeout, this, [this] {
        if (process_->state() != QProcess::NotRunning && activity_.isValid() && activity_.elapsed() > 45000) {
            terminal_ = true; resumable_ = mode_ == "--stage";
            status_->setText(QStringLiteral("更新任务长时间无响应，已暂停。已校验的内容保留，可点击继续下载。")); process_->kill();
        }
    });
    connect(entry_, &QPushButton::clicked,this,[this] { open(); });
    connect(check_, &QPushButton::clicked,this,[this] { run("--check"); });
    connect(download_, &QPushButton::clicked,this,[this] { run("--stage"); });
    connect(pause_, &QPushButton::clicked,this,[this] { pause(); });
    connect(apply_, &QPushButton::clicked,this,[this] { switchVersion(false); });
    connect(rollback_, &QPushButton::clicked,this,[this] { switchVersion(true); });
    connect(process_, &QProcess::readyReadStandardOutput,this,[this] { consume(); });
    connect(process_, &QProcess::errorOccurred,this,[this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { status_->setText(QStringLiteral("更新程序无法启动，请检查安装目录是否完整。")); terminal_ = true; refresh(); }
    });
    connect(process_, qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus) {
        consume(); watchdog_->stop(); if (!terminal_) {
            resumable_ = mode_ == "--stage";
            status_->setText(code ? QStringLiteral("更新连接已结束，已校验内容保留，可继续下载。") : QStringLiteral("更新程序未返回有效结果，可重新尝试。"));
        }
        refresh();
    });
    try {
        root_ = QString::fromStdWString(updates::installationRoot(p(current_)).wstring());
        const auto release = updates::readJson(p(current_) / "release.json"); updates::validate(release);
        ready_ = QFileInfo::exists(helper_) && QFileInfo::exists(root_ + "/cipherchat-launcher.exe");
        version_->setText(QStringLiteral("当前版本：") + q(release.at("version").get<std::string>()));
        status_->setText(ready_ ? QStringLiteral("从中心服务器获取版本，只下载变化的部分。") : QStringLiteral("更新组件不完整，请重新解压完整安装包。"));
        if (QFileInfo::exists(root_ + "/.updates/last-result.json")) {
            const auto result = updates::readJson(p(root_) / ".updates/last-result.json");
            if (result.value("event",std::string{}) == "error") status_->setText(QStringLiteral("上次更新：") + q(result.value("message",std::string{})));
        }
    } catch (...) { status_->setText(QStringLiteral("此目录未包含版本清单。请使用带更新模块的完整安装包。")); }
    refresh();
    if (ready_ && (directory.isEmpty()||startupPrompt_)) QTimer::singleShot(startupPrompt_?500:5000,this,[this] { if (process_->state() == QProcess::NotRunning) run("--check"); });
}
QString UpdateController::statusText() const { return status_->text(); }
UpdateController::~UpdateController() {
    if (process_->state() != QProcess::NotRunning) { process_->kill(); process_->waitForFinished(2000); }
}
void UpdateController::open() { dialog_->show(); dialog_->raise(); dialog_->activateWindow(); }
void UpdateController::refresh() {
    emit statusChanged(status_->text());
    const bool idle = process_->state() == QProcess::NotRunning;
    check_->setEnabled(ready_ && idle); download_->setEnabled(ready_ && idle && available_ && staged_.isEmpty());
    download_->setText(resumable_ ? QStringLiteral("继续下载") : QStringLiteral("下载更新"));
    pause_->setVisible(!idle && mode_ == "--stage"); pause_->setEnabled(!idle && mode_ == "--stage");
    apply_->setEnabled(ready_ && idle && !staged_.isEmpty());
    rollback_->setEnabled(ready_ && idle && QFileInfo::exists(root_ + "/previous.txt"));
}
void UpdateController::run(const QString& mode) {
    if (!ready_ || process_->state() != QProcess::NotRunning) return;
    mode_ = mode; pending_.clear(); terminal_ = false;
    activity_.start(); downloadClock_.start(); rateAt_ = 0; lastDownloaded_ = 0; watchdog_->start();
    status_->setText(mode == "--check" ? QStringLiteral("正在连接中心服务器…") : QStringLiteral("正在准备更新，可继续使用聊天。"));
    progress_->setVisible(mode == "--stage"); progress_->setValue(0);
    process_->setWorkingDirectory(current_); process_->start(helper_,{mode,current_}); refresh();
}
void UpdateController::pause() {
    if (process_->state() == QProcess::NotRunning || mode_ != "--stage") return;
    terminal_ = true; resumable_ = true;
    status_->setText(QStringLiteral("已暂停下载，已校验的分块保留。点击继续下载即可恢复。"));
    process_->kill();
}
void UpdateController::consume() {
    const auto output = process_->readAllStandardOutput(); if (!output.isEmpty()) activity_.restart();
    pending_ += output;
    if (pending_.size() > 1048576) { terminal_ = true; status_->setText(QStringLiteral("更新结果异常，可重新尝试。")); process_->kill(); pending_.clear(); return; }
    while (pending_.contains('\n')) {
        const auto newline = pending_.indexOf('\n'); const auto line = pending_.left(newline); pending_.remove(0,newline+1);
        try {
            const auto event = updates::Json::parse(line.toStdString()); const auto kind = event.value("event",std::string{});
            if (kind == "checked") {
                terminal_ = true; available_ = event.at("available").get<bool>();
                if (!available_ || (!staged_.isEmpty() && staged_ != q(event.at("version").get<std::string>()))) staged_.clear();
                version_->setText(QStringLiteral("当前版本：%1\n最新版本：%2").arg(q(event.at("current").get<std::string>()),q(event.at("version").get<std::string>())));
                status_->setText((available_ ? QStringLiteral("发现新版本\n") : QStringLiteral("已经是最新版本\n")) + q(event.value("notes",std::string{})));
                entry_->setText(available_ ? QStringLiteral("软件更新 · 有新版") : QStringLiteral("软件更新"));
                if(startupPrompt_ && available_ && !prompted_){prompted_=true;open();}
            } else if (kind == "progress") {
                const auto total = event.at("total").get<std::uint64_t>();
                progress_->setValue(total ? int(event.at("completed").get<std::uint64_t>() * 100 / total) : 100);
                const auto downloaded = event.at("downloaded").get<std::uint64_t>(); const auto now = downloadClock_.elapsed();
                const auto seconds = double(std::max<qint64>(1, now - rateAt_)) / 1000;
                const auto rate = double(downloaded >= lastDownloaded_ ? downloaded - lastDownloaded_ : 0) / 1048576 / seconds;
                if (now - rateAt_ >= 500) { rateAt_ = now; lastDownloaded_ = downloaded; }
                status_->setText(QStringLiteral("已下载 %1 · 已复用 %2\n速度 %3 MiB/s · %4 路并行").arg(amount(downloaded),amount(event.at("reused")),QString::number(rate,'f',2)).arg(event.value("connections",4u)));
            } else if (kind == "strategy") {
                status_->setText(event.value("mode",std::string{}) == "zip" ? QStringLiteral("正在并行下载压缩包，完成后自动解压。") : QStringLiteral("正在并行下载变化的内容。"));
            } else if (kind == "extracting") {
                status_->setText(QStringLiteral("下载完成，正在本地解压…"));
            } else if (kind == "waiting") {
                status_->setText(QStringLiteral("正在等待更新服务器响应（第 %1 次连接，%2 秒）\n连接停滞时会自动重试，也可暂停后继续。").arg(event.value("attempt",1u)).arg(event.value("elapsedMs",0LL)/1000));
            } else if (kind == "retry") {
                status_->setText(QStringLiteral("下载连接未响应，正在自动重新连接（%1/%2）\n已校验分块保留，无需重开客户端。").arg(event.value("attempt",1u)).arg(event.value("attempts",4u)));
            } else if (kind == "staged") {
                terminal_ = true; resumable_ = false; staged_ = q(event.at("version").get<std::string>()); progress_->setValue(100);
                status_->setText(QStringLiteral("版本 %1 已准备好\n下载 %2 · 复用 %3\n点击“退出并更新”完成安装。账号和聊天记录保留。").arg(staged_,amount(event.at("downloaded")),amount(event.at("reused"))));
            } else if (kind == "error") {
                terminal_ = true; resumable_ = mode_ == "--stage";
                const auto message = q(event.at("message").get<std::string>());
                status_->setText(message.contains(QStringLiteral("服务器版本比本机旧"))
                    ? QStringLiteral("当前为本地新版，服务器尚未发布适用的更高版本。已保留当前界面与数据。") : message);
            }
        } catch (...) { terminal_ = true; status_->setText(QStringLiteral("无法解析更新结果，请稍后重试。")); }
    }
}
void UpdateController::switchVersion(bool previous) {
    if (QMessageBox::question(dialog_,QStringLiteral("重新启动"), previous ? QStringLiteral("退出客户端并回退上一版？正在进行的传输会中断。") : QStringLiteral("退出客户端并安装已下载的版本？正在进行的传输会中断。")) != QMessageBox::Yes) return;
    try {
        const auto pid = QCoreApplication::applicationPid(); updates::Json args = updates::Json::array();
        for (const auto& arg : QCoreApplication::arguments().mid(1)) args.push_back(arg.toUtf8().toStdString());
        updates::writeAtomic(p(root_) / ".updates" / ("restart-" + std::to_string(pid) + ".json"),args.dump());
        QStringList command{previous ? "--rollback" : "--activate",root_}; if (!previous) command << staged_;
        command << "--wait-pid" << QString::number(pid);
        if (!QProcess::startDetached(helper_,command,current_)) throw std::runtime_error("无法启动更新程序");
        dialog_->hide(); window_->setProperty("exitForUpdate", true); window_->close();
        if(startupPrompt_)QCoreApplication::quit();
    } catch (const std::exception& e) { status_->setText(q(e.what())); }
}
}
