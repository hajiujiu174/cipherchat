#pragma once
#include <QObject>
#include <QJsonObject>
#include <QHash>
#include <QElapsedTimer>
#include <QUdpSocket>
#include <QTimer>
namespace Messager {
// Independent movement transport. No signatures, encryption or anti-cheat.
// TLS remains responsible for login, invitations and reliable control events.
class GameUdp final : public QObject {
    Q_OBJECT
public:
    explicit GameUdp(QObject* parent=nullptr);
    void start(const QString& host,quint16 port,const QString& user);
    void stop();
    void advertise(const QString& peer,bool enabled);
    bool capable()const{return !m_user.isEmpty();}
    bool ready(const QString& peer)const;
    bool send(const QString& peer,const QJsonObject& data);
    QJsonObject stats()const;
signals:
    void packet(const QString& peer,const QJsonObject& data);
private:
    void heartbeat();void read();bool write(QJsonObject data);
    QUdpSocket m_socket;QTimer m_timer;QElapsedTimer m_clock;
    QHostAddress m_host;quint16 m_port=0;QString m_user;
    struct Peer {qint64 pong=-10000,active=-10000;};
    QHash<QString,Peer> m_peers;
    qint64 m_bound=-10000;int m_sent=0,m_received=0,m_fallback=0;
};
}
