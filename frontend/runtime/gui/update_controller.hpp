#pragma once
#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <cstdint>
class QWidget;
class QPushButton;
class QDialog;
class QLabel;
class QProgressBar;
class QProcess;
class QTimer;
namespace ccfw::gui {
class UpdateController final : public QObject {
    Q_OBJECT
public:
    // 构造软件更新界面控制器，初始化后续操作需要的状态或依赖。
    UpdateController(QWidget* window, QPushButton* entry, const QString& directory = {}, const QString& helper = {}, bool startupPrompt = false);
    // 销毁软件更新界面控制器时释放它持有的资源。
    ~UpdateController() override;
    // 显示软件更新界面控制器。
    void show() { open(); }
    // 返回软件更新界面控制器的当前状态的显示文字。
    QString statusText() const;
signals:
    // 通知监听方状态发生变化，供界面或调用方刷新。
    void statusChanged(const QString& text);
private:
    // 打开软件更新界面控制器。
    void open();
    // 运行软件更新界面控制器。
    void run(const QString& mode);
    // 读取更新器子进程的进度和结果消息，刷新桌面更新界面。
    void consume();
    // 暂停软件更新界面控制器。
    void pause();
    // 发起安装或回退动作，并处理当前进程与版本切换。
    void switchVersion(bool previous);
    // 刷新软件更新界面控制器。
    void refresh();
    QWidget* window_;
    QPushButton* entry_;
    QDialog* dialog_;
    QLabel *version_, *status_;
    QProgressBar* progress_;
    QPushButton *check_, *download_, *apply_, *rollback_, *pause_;
    QProcess* process_;
    QTimer* watchdog_;
    QElapsedTimer activity_, downloadClock_;
    std::uint64_t lastDownloaded_ = 0;
    qint64 rateAt_ = 0;
    QString current_, helper_, root_, staged_, mode_;
    QByteArray pending_;
    bool ready_ = false, available_ = false, terminal_ = false, resumable_ = false;
    bool startupPrompt_ = false, prompted_ = false;
};
}
