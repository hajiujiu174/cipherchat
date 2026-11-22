#include "callmanager.h"
#include "pagewidgets.h"
#include "components.h"
#include "services/chatservice.h"
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QLabel>
#include <QBuffer>
#include <QImageReader>
#include <QVBoxLayout>
#include <QPushButton>
#include <QUuid>
namespace Messager {
CallManager::CallManager(ChatService* service,QWidget* owner):QObject(owner),m_service(service),m_owner(owner){
    setObjectName("callManager");m_timer.setInterval(40);connect(&m_timer,&QTimer::timeout,this,&CallManager::packet);
    m_timeout.setInterval(30000);m_timeout.setSingleShot(true);connect(&m_timeout,&QTimer::timeout,this,[this]{end();});
    connect(service,&ChatService::socialResult,this,&CallManager::receive);connect(service,&ChatService::sessionChanged,this,[this]{if(!m_service->online())end(false);});
}
CallManager::~CallManager(){end(false);}
void CallManager::send(const QString& action){m_service->socialAction({{"op","call"},{"action",action},{"call_id",m_id},{"to",m_peer},{"video",m_video}});}
void CallManager::invite(const QString& peer,bool video){if(!m_id.isEmpty()||!m_service->online())return;m_peer=peer;m_video=video;m_id=QUuid::createUuid().toString(QUuid::WithoutBraces);open(false);send("invite");}
void CallManager::open(bool incoming){
    auto* window=new QWidget(m_owner,Qt::Window);m_window=window;window->setAttribute(Qt::WA_DeleteOnClose);window->setWindowTitle((m_video?"视频通话 · ":"语音通话 · ")+m_peer);window->resize(420,380);auto* layout=new QVBoxLayout(window);
    layout->addWidget(label(m_peer,22,nullptr,true));m_status=label(incoming?"对方请求通话，请选择接听或挂断":"正在呼叫对方…",13,"muted");m_status->setWordWrap(true);layout->addWidget(m_status);
    m_remote=new QLabel;m_remote->setMinimumSize(320,180);m_remote->setAlignment(Qt::AlignCenter);m_remote->setText(m_video?"等待对方画面":"语音通话");layout->addWidget(m_remote,1);
    m_accept=button("接听","primary");m_accept->setObjectName("acceptCall");m_accept->setVisible(incoming);layout->addWidget(m_accept);connect(m_accept,&QPushButton::clicked,this,[this]{send("accept");activate();});
    auto* mute=button("麦克风静音");layout->addWidget(mute);connect(mute,&QPushButton::clicked,this,[this,mute]{m_muted=!m_muted;mute->setText(m_muted?"取消静音":"麦克风静音");});
    auto* hangup=button("挂断");hangup->setObjectName("endCall");layout->addWidget(hangup);connect(hangup,&QPushButton::clicked,this,[this]{end();});
    connect(window,&QObject::destroyed,this,[this]{if(!m_id.isEmpty())end();});window->show();m_timeout.start();
}
void CallManager::activate(){
    if(m_active||!m_window)return;m_active=true;m_sequence=0;m_accept->hide();m_status->setText("通话已连接 · TLS 中转 · PCM 16kHz / 单声道");m_timeout.start(120000);
    QAudioFormat format;format.setSampleRate(16000);format.setChannelCount(1);format.setSampleFormat(QAudioFormat::Int16);
    const auto input=QMediaDevices::defaultAudioInput(),output=QMediaDevices::defaultAudioOutput();
    if(!input.isNull()&&input.isFormatSupported(format)){m_input=new QAudioSource(input,format,this);m_input->setBufferSize(6400);m_capture=m_input->start();}else m_status->setText("通话已连接 · 当前没有支持 16kHz PCM 的麦克风");
    if(!output.isNull()&&output.isFormatSupported(format)){m_output=new QAudioSink(output,format,this);m_output->setBufferSize(6400);m_playback=m_output->start();}
    if(m_video){const auto device=QMediaDevices::defaultVideoInput();if(!device.isNull()){m_camera=new QCamera(device,this);m_session=new QMediaCaptureSession(this);m_sink=new QVideoSink(this);m_session->setCamera(m_camera);m_session->setVideoSink(m_sink);connect(m_sink,&QVideoSink::videoFrameChanged,this,[this](const QVideoFrame& frame){m_frame=frame.toImage().scaled(160,120,Qt::KeepAspectRatio,Qt::SmoothTransformation);});m_camera->start();}else m_status->setText("通话已连接 · 当前没有可用摄像头");}
    m_timer.start();
}
void CallManager::packet(){
    if(!m_active)return;QJsonObject p{{"op","call"},{"action","media"},{"call_id",m_id},{"to",m_peer},{"sequence",qint64(++m_sequence)}};
    if(m_capture){const auto audio=m_capture->readAll();if(!m_muted)p["audio"]=QString::fromLatin1(audio.right(2560).toBase64());}
    if(m_video&&!m_frame.isNull()&&m_sequence%3==0){QByteArray jpeg;QBuffer buffer(&jpeg);buffer.open(QIODevice::WriteOnly);m_frame.save(&buffer,"JPEG",45);if(jpeg.size()<=16*1024)p["image"]=QString::fromLatin1(jpeg.toBase64());}
    m_service->socialAction(p);
}
void CallManager::receive(const QJsonObject& packet){
    if(packet["op"]!="call")return;
    const auto action=packet["action"].toString(),id=packet["call_id"].toString();
    if(action=="invite"&&m_id.isEmpty()&&packet["ok"].toBool()){m_id=id;m_peer=packet["from"].toString();m_video=packet["video"].toBool();open(true);return;}
    if(id!=m_id)return;if(!packet["ok"].toBool()){if(m_status)m_status->setText("通话失败："+packet["error"].toString());m_timeout.start(4000);return;}
    if(action=="accept"){activate();return;}if(action=="end"||action=="reject"){end(false);return;}
    if(action!="media"||!m_active)return;m_timeout.start(120000);
    const auto encoded=packet["audio"].toString();if(encoded.size()<=4096&&m_playback){const auto bytes=QByteArray::fromBase64(encoded.toLatin1());if(bytes.size()<=m_output->bytesFree())m_playback->write(bytes);}
    const auto image=packet["image"].toString();if(image.size()<=24*1024){QImage frame;const auto bytes=QByteArray::fromBase64(image.toLatin1());QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,"JPEG");const auto size=reader.size();if(size.isValid()&&size.width()<=160&&size.height()<=120){frame=reader.read();if(!frame.isNull())m_remote->setPixmap(QPixmap::fromImage(frame).scaled(320,240,Qt::KeepAspectRatio));}}
}
void CallManager::end(bool notify){
    if(notify&&!m_id.isEmpty()&&m_service->online())send("end");m_id.clear();m_active=false;m_timer.stop();m_timeout.stop();
    if(m_input){m_input->stop();delete m_input;m_input=nullptr;}if(m_output){m_output->stop();delete m_output;m_output=nullptr;}m_capture=m_playback=nullptr;
    if(m_camera){m_camera->stop();delete m_camera;m_camera=nullptr;}delete m_session;m_session=nullptr;delete m_sink;m_sink=nullptr;m_frame={};
    if(m_window){auto* window=m_window.data();m_window=nullptr;window->hide();window->deleteLater();}m_status=nullptr;m_remote=nullptr;m_accept=nullptr;
}
}
