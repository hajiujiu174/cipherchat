#pragma once
#include <QObject>
#include <QJsonObject>
#include <QHash>
#include <QElapsedTimer>
#include <QUdpSocket>
#include <QTimer>
namespace Messager {
// Authenticated movement transport with TLS-issued expiring HMAC keys and replay protection.
// TLS remains responsible for login, invitations and reliable control events.
class GameUdp final : public QObject {
    Q_OBJECT
public:
    // 构造游戏 UDP 中转客户端，初始化后续操作需要的状态或依赖。
    explicit GameUdp(QObject* parent=nullptr);
    // 验证短期票据，解析中转地址并绑定本机 UDP 套接字。
    void start(const QString& host,quint16 port,const QString& user,const QJsonObject& ticket);
    // 停止游戏 UDP 中转客户端。
    void stop();
    // 登记游戏 UDP 中转客户端。
    void advertise(const QString& peer,bool enabled);
    // 返回游戏 UDP 中转客户端的当前通道是否具备使用条件。
    bool capable()const{return !m_user.isEmpty();}
    // 判断本机绑定与指定对端的探测是否仍在有效时间内。
    bool ready(const QString& peer)const;
    // 仅在对端探测通过时发送压缩后的游戏状态包，否则允许调用方回退。
    bool send(const QString& peer,const QJsonObject& data);
    // 返回游戏 UDP 中转客户端使用的统计信息。
    QJsonObject stats()const;
signals:
    // 返回游戏 UDP 中转客户端使用的数据包。
    void packet(const QString& peer,const QJsonObject& data);
private:
    // 维持在中转上的绑定，并探测当前游戏对端是否可走 UDP。
    void heartbeat();/* 检查 UDP 来源、会话和签名，处理探测或把有效的实时游戏包交给控制器。 */ void read();/* 为 UDP 数据报附加会话、有效期、序号和签名，然后发给配置的中转。 */ bool write(QJsonObject data);
    QByteArray m_key;QString m_session;qint64 m_expiry=0;quint64 m_tx=0,m_rx=0,m_generation=0;
    QUdpSocket m_socket;QTimer m_timer;QElapsedTimer m_clock;
    QHostAddress m_host;quint16 m_port=0;QString m_user;
    struct Peer {qint64 pong=-10000,active=-10000;};
    QHash<QString,Peer> m_peers;
    qint64 m_bound=-10000;int m_sent=0,m_received=0,m_fallback=0;
};
}
