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
    UpdateController(QWidget* window, QPushButton* entry, const QString& directory = {}, const QString& helper = {}, bool startupPrompt = false);
    ~UpdateController() override;
    void show() { open(); }
    QString statusText() const;
signals:
    void statusChanged(const QString& text);
private:
    void open();
    void run(const QString& mode);
    void consume();
    void pause();
    void switchVersion(bool previous);
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
