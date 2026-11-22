#pragma once
#include <QObject>
#include <QPointer>
#include <QJsonObject>
#include <QTimer>
#include <QImage>
class QWidget;class QLabel;class QPushButton;class QAudioSource;class QAudioSink;class QCamera;class QMediaCaptureSession;class QVideoSink;class QIODevice;
namespace Messager {
class ChatService;
class CallManager:public QObject {
    Q_OBJECT
public:
    explicit CallManager(ChatService*,QWidget*);
    ~CallManager() override;
    void invite(const QString&,bool video);
private:
    void receive(const QJsonObject&);void open(bool incoming);void activate();void end(bool notify=true);void packet();void send(const QString& action);
    ChatService* m_service;QWidget* m_owner;QPointer<QWidget> m_window;QLabel *m_status=nullptr,*m_remote=nullptr;QPushButton* m_accept=nullptr;
    QString m_id,m_peer;bool m_video=false,m_active=false,m_muted=false;quint64 m_sequence=0;
    QTimer m_timer,m_timeout;QAudioSource* m_input=nullptr;QAudioSink* m_output=nullptr;QIODevice *m_capture=nullptr,*m_playback=nullptr;
    QCamera* m_camera=nullptr;QMediaCaptureSession* m_session=nullptr;QVideoSink* m_sink=nullptr;QImage m_frame;
};
}
