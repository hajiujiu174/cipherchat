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
    // 构造音视频通话控制器，初始化后续操作需要的状态或依赖。
    explicit CallManager(ChatService*,QWidget*);
    // 销毁音视频通话控制器时释放它持有的资源。
    ~CallManager() override;
    // 邀请音视频通话控制器。
    void invite(const QString&,bool video);
private:
    // 处理来电、接听、结束和媒体事件，并把接收的数据交给播放或画面控件。
    void receive(const QJsonObject&);/* 打开音视频通话控制器。 */ void open(bool incoming);/* 在通话被接受后打开可用的本机音视频设备及定时发送。 */ void activate();/* 结束通话，并停止本机设备、计时器和媒体流。 */ void end(bool notify=true);/* 读取简短一段录音及可用画面，封装后沿通话信令路径提交。 */ void packet();/* 发送音视频通话控制器。 */ void send(const QString& action);
    ChatService* m_service;QWidget* m_owner;QPointer<QWidget> m_window;QLabel *m_status=nullptr,*m_remote=nullptr;QPushButton* m_accept=nullptr;
    QString m_id,m_peer;bool m_video=false,m_active=false,m_muted=false;quint64 m_sequence=0;
    QTimer m_timer,m_timeout;QAudioSource* m_input=nullptr;QAudioSink* m_output=nullptr;QIODevice *m_capture=nullptr,*m_playback=nullptr;
    QCamera* m_camera=nullptr;QMediaCaptureSession* m_session=nullptr;QVideoSink* m_sink=nullptr;QImage m_frame;
};
}
