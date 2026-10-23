// Explicit operator utility. All account and message mutations use CipherChatService.
#include "services/cipherchatservice.h"
#include "ui/mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStyleFactory>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <wincrypt.h>
#endif

using namespace Messager;
namespace {
QJsonObject readJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(("Cannot read " + path).toStdString());
    QJsonParseError error;
    auto value = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !value.isObject())
        throw std::runtime_error("Invalid JSON input");
    return value.object();
}
void writeBytes(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Cannot save operator state");
}
void log(const QString& value) {
    std::cout << value.toUtf8().constData() << std::endl;
}
QByteArray protectedBytes(const QByteArray& input, bool seal) {
#ifdef Q_OS_WIN
    DATA_BLOB source{DWORD(input.size()), reinterpret_cast<BYTE*>(const_cast<char*>(input.constData()))};
    DATA_BLOB result{};
    const bool ok = seal ? CryptProtectData(&source, L"CipherChat demo actor credentials", nullptr, nullptr,
                                            nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)
                         : CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr,
                                              CRYPTPROTECT_UI_FORBIDDEN, &result);
    if (!ok)
        throw std::runtime_error("Windows credential protection failed");
    QByteArray bytes(reinterpret_cast<char*>(result.pbData), int(result.cbData));
    SecureZeroMemory(result.pbData, result.cbData);
    LocalFree(result.pbData);
    return bytes;
#else
    Q_UNUSED(input);
    Q_UNUSED(seal);
    throw std::runtime_error("This operator utility requires Windows credential protection");
#endif
}
QString digest(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot hash demo attachment");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

class Population : public QObject {
    using Client = std::shared_ptr<CipherChatService>;
    QJsonObject plan, state, credentials, base;
    QString work, targetName, dataRoot, password, fatal;
    QHash<QString, Client> clients;
    QHash<QString, QJsonObject> expectedIdentities;
    QHash<QString, QStringList> errors;
    Client target;
    void save() {
        writeBytes(work + "/state.json", QJsonDocument(state).toJson());
        writeBytes(work + "/actors.protected", protectedBytes(QJsonDocument(credentials).toJson(), true));
    }
    void wait(const std::function<bool()>& condition, const QString& description, int timeout = 45000) {
        QElapsedTimer timer;
        timer.start();
        while (!condition()) {
            if (!fatal.isEmpty())
                throw std::runtime_error(fatal.toStdString());
            if (timer.elapsed() > timeout)
                throw std::runtime_error(("Timeout: " + description).toStdString());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(20);
        }
    }
    void settle(int milliseconds = 150) {
        QElapsedTimer timer;
        timer.start();
        wait([&] { return timer.elapsed() >= milliseconds; }, "event settling", milliseconds + 3000);
    }
    Client makeClient(const QString& label, bool isTarget = false) {
        auto config = base;
        config["dataDirectory"] = isTarget ? dataRoot : work + "/actor-profiles";
        auto client = std::make_shared<CipherChatService>(config);
        clients[label] = client;
        connect(client.get(), &ChatService::operationError, this, [this, label](const QString& error) {
            errors[label].append(error);
            log("OPERATION " + label + ": " + error);
        });
        connect(client.get(), &ChatService::identityRequired, this,
                [this, weak = std::weak_ptr<CipherChatService>(client)](const QJsonObject& identity) {
                    const auto username = identity["username"].toString();
                    if (!expectedIdentities.contains(username))
                        return; // Never approve an unrelated existing contact.
                    const auto expected = expectedIdentities[username];
                    if (expected["signing"] != identity["signing"] || expected["dh"] != identity["dh"] ||
                        expected["fingerprint"] != identity["fingerprint"]) {
                        fatal = "Controlled actor identity mismatch: " + username;
                        return;
                    }
                    if (auto source = weak.lock())
                        source->confirmIdentity(identity);
                });
        return client;
    }
    bool authenticate(const Client& client, const QString& label, const QString& username,
                      const QString& secret, bool registration) {
        errors[label].clear();
        auto config = client->configuration();
        config["username"] = username;
        config["password"] = secret;
        config["registerNew"] = registration;
        client->authenticate(config);
        wait(
            [&] {
                return (client->online() && !client->ownIdentity().isEmpty()) || !errors[label].isEmpty();
            },
            "authentication " + label);
        if (!client->online())
            return false;
        expectedIdentities[username] = client->ownIdentity();
        return true;
    }
    Client actor(const QString& label) {
        auto record = credentials[label].toObject();
        bool fresh = record.isEmpty();
        if (fresh) {
            record = {{"username", label},
                      {"password", "Demo-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                       QUuid::createUuid().toString(QUuid::WithoutBraces)},
                      {"registered", false}};
            credentials[label] = record;
            save();
        }
        auto client = makeClient(label);
        if (!fresh &&
            authenticate(client, label, record["username"].toString(), record["password"].toString(), false))
            return client;
        if (record["registered"].toBool())
            throw std::runtime_error(("Cannot restore owned actor " + label).toStdString());
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (authenticate(client, label, record["username"].toString(), record["password"].toString(),
                             true)) {
                record["registered"] = true;
                credentials[label] = record;
                save();
                log("REGISTERED " + label + " -> " + client->username());
                return client;
            }
            const auto error = errors[label].join(' ');
            if (!error.contains("存在") && !error.contains("占用") &&
                !error.contains("registered", Qt::CaseInsensitive))
                throw std::runtime_error(("Actor registration failed: " + label + " " + error).toStdString());
            clients.remove(label);
            client.reset();
            record["username"] =
                label + "_演示_" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(4);
            credentials[label] = record;
            save();
            client = makeClient(label);
        }
        throw std::runtime_error("Cannot allocate a unique demo username");
    }
    QString groupId(const QString& key) const {
        return state["groups"].toObject()[key].toString();
    }
    QString conversation(const QJsonObject& message, const Client& source) const {
        if (message.contains("group"))
            return groupId(message["group"].toString());
        const auto other = source == target ? clients[message["to"].toString()] : target;
        return "peer:" + other->username();
    }
    bool hasMessage(const Client& client, const QString& id, const QString& sender,
                    const QString& text) const {
        for (const auto& message : client->messages(id))
            if (message.sender == sender && message.text == text)
                return true;
        return false;
    }
    void sendMessage(const QJsonObject& message, const QString& key, bool queued = false) {
        const auto label = message["from"].toString();
        const auto source = clients[label];
        if (!source)
            throw std::runtime_error("Unknown demo sender");
        const auto id = conversation(message, source);
        const auto text = message["text"].toString();
        auto done = state["messages"].toObject();
        if (done[key].toBool())
            return;
        if (!message.contains("group"))
            source->openDirectConversation(id.mid(5));
        if (!hasMessage(source, id, source->username(), text)) {
            if (source->send(id, text).isEmpty())
                throw std::runtime_error("Message was not queued");
        }
        wait(
            [&] {
                if (queued) {
                    for (const auto& m : source->messages(id))
                        if (m.text == text && m.outgoing && m.transportStatus == "服务器已接收")
                            return true;
                    return false;
                }
                if (message.contains("group")) {
                    const auto members = source->conversation(id).members;
                    for (const auto& username : members) {
                        for (const auto& c : clients)
                            if (c->username() == username && !hasMessage(c, id, source->username(), text))
                                return false;
                    }
                    return hasMessage(target, id, source->username(), text);
                }
                const auto receiver = source == target ? clients[message["to"].toString()] : target;
                return hasMessage(receiver, "peer:" + source->username(), source->username(), text);
            },
            "deliver " + key, 60000);
        done[key] = true;
        state["messages"] = done;
        save();
        log(QString("MESSAGE %1: %2").arg(key, label));
    }
    void createPdf() {
        const auto path = work + "/assets/Messager 交互说明.pdf";
        if (QFile::exists(path))
            return;
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(144);
        writer.setTitle("Messager 交互说明 · 演示资料");
        QPainter painter(&writer);
        painter.setPen(QColor("#6554d9"));
        painter.setFont(QFont("Microsoft YaHei", 24, QFont::Bold));
        painter.drawText(60, 90, "Messager 交互说明");
        painter.setPen(QColor("#242938"));
        painter.setFont(QFont("Microsoft YaHei", 12));
        painter.drawText(
            QRect(60, 135, writer.width() - 120, writer.height() - 180), Qt::TextWordWrap,
            "演示资料 · 内容为虚构示例，附件通过 CipherChat 正常发送。\n\n"
            "聊天：Enter 发送，Shift + Enter 换行；可搜索消息与展开会话详情。\n\n"
            "联系人：发送好友申请，对方同意后成为好友；临时会话只支持文字。\n\n"
            "群聊：填写名称，从好友下拉菜单中勾选成员；创建者自动加入。\n\n"
            "文件：在好友私聊中发送文件，文件页查看进度、打开文件、定位对话及打开所在文件夹。\n\n"
            "外观：支持跟随系统、浅色、深色和字号设置。\n\n"
            "本次附件包含真实 PDF、SVG、HTML 与 ZIP，文件尺寸和完成状态由实际传输生成。\n\n"
            "产品设计讨论 / Messager 开发组 / 周末出走计划");
    }
    void transfer(const QJsonObject& entry, int index) {
        const QString key = QString::number(index);
        auto done = state["files"].toObject();
        if (done[key].isObject())
            return;
        const auto source = clients[entry["from"].toString()];
        const auto receiver =
            entry.value("to").toString() == "self" ? target : clients[entry["to"].toString()];
        const auto path = work + "/assets/" + entry["name"].toString();
        const auto sha = digest(path);
        QSet<QString> existing;
        for (const auto& t : source->transfers())
            existing.insert(t.id);
        source->openDirectConversation(receiver->username());
        source->sendFile(path, "peer:" + receiver->username());
        QString transferId;
        wait(
            [&] {
                for (const auto& t : source->transfers())
                    if (!existing.contains(t.id) && t.name == entry["name"].toString()) {
                        transferId = t.id;
                        return true;
                    }
                return false;
            },
            "file submission");
        QString received;
        wait(
            [&] {
                for (const auto& t : receiver->transfers())
                    if (t.id == transferId && t.state == TransferState::Completed) {
                        received = t.path;
                        return true;
                    }
                return false;
            },
            "file completion", 180000);
        if (digest(received) != sha)
            throw std::runtime_error("Attachment hash mismatch");
        done[key] = QJsonObject{{"id", transferId},
                                {"name", entry["name"]},
                                {"sha256", sha},
                                {"bytes", QFileInfo(path).size()},
                                {"path", received}};
        state["files"] = done;
        save();
        log("FILE COMPLETE " + entry["name"].toString());
    }
    void preview() {
        AppController controller(target.get());
        MainWindow window(&controller);
        window.show();
        Theme::instance().setMode(Theme::Dark);
        controller.selectConversation(groupId("design"));
        window.showPage(0);
        settle(400);
        window.grab().save(work + "/account-chat.png");
        window.showPage(1);
        settle();
        window.grab().save(work + "/account-contacts.png");
        window.showPage(2);
        settle();
        window.grab().save(work + "/account-files.png");
        window.close();
    }

  public:
    Population(QJsonObject input, QJsonObject config, QString directory, QString root, QString secret)
        : plan(std::move(input)), base(std::move(config)), work(std::move(directory)),
          targetName(plan["target"].toString()), dataRoot(std::move(root)), password(std::move(secret)) {
        QDir().mkpath(work);
        if (QFile::exists(work + "/state.json"))
            state = readJson(work + "/state.json");
        if (QFile::exists(work + "/actors.protected")) {
            QFile file(work + "/actors.protected");
            if (!file.open(QIODevice::ReadOnly))
                throw std::runtime_error("Cannot open protected actor registry");
            credentials = QJsonDocument::fromJson(protectedBytes(file.readAll(), false)).object();
        }
        if (!state.isEmpty() && state["target"].toString() != targetName)
            throw std::runtime_error("Target mismatch in journal");
        state["target"] = targetName;
    }
    void run(bool apply) {
        target = makeClient("self", true);
        if (!authenticate(target, "self", targetName, password, false))
            throw std::runtime_error("Target account login failed");
        settle(800);
        log(QString("TARGET AUTHENTICATED: %1; contacts=%2 conversations=%3")
                .arg(targetName)
                .arg(target->contacts().size())
                .arg(target->conversations().size()));
        if (!apply) {
            target->disconnectAccount();
            settle(500);
            return;
        }
        if (!state.contains("before"))
            state["before"] = QJsonObject{{"contacts", target->contacts().size()},
                                          {"conversations", target->conversations().size()},
                                          {"transfers", target->transfers().size()}};
        for (const auto& value : plan["actors"].toArray())
            actor(value.toString());
        for (const auto& value : plan["actors"].toArray()) {
            const auto client = clients[value.toString()];
            if (target->isFriend(client->username()))
                continue;
            client->addFriend(targetName);
            wait(
                [&] {
                    return target->incomingRequests().contains(client->username()) ||
                           target->isFriend(client->username());
                },
                "friend request " + value.toString());
            if (!target->isFriend(client->username()))
                target->respondFriend(client->username(), true);
            wait([&] { return target->isFriend(client->username()) && client->isFriend(targetName); },
                 "friend acceptance " + value.toString());
            log("FRIEND " + client->username());
        }
        auto groups = state["groups"].toObject();
        for (const auto& value : plan["groups"].toArray()) {
            const auto entry = value.toObject();
            const auto key = entry["key"].toString();
            QStringList members{targetName};
            QStringList invited;
            for (const auto& item : entry["members"].toArray()) {
                invited << clients[item.toString()]->username();
                members << clients[item.toString()]->username();
            }
            members.sort();
            auto match = [&]() -> QString {
                for (const auto& c : target->conversations()) {
                    auto actual = c.members;
                    actual.sort();
                    if (c.group && c.name == entry["name"].toString() && actual == members)
                        return c.id;
                }
                return {};
            };
            if (match().isEmpty())
                target->createGroup(entry["name"].toString(), invited);
            wait([&] { return !match().isEmpty(); }, "group creation " + key);
            groups[key] = match();
            state["groups"] = groups;
            save();
            wait(
                [&] {
                    for (const auto& item : entry["members"].toArray())
                        if (clients[item.toString()]->conversation(match()).id.isEmpty())
                            return false;
                    return true;
                },
                "group membership propagation " + key);
            log("GROUP " + entry["name"].toString() + " " + match());
        }
        const auto messages = plan["messages"].toArray();
        for (int i = 0; i < messages.size(); ++i)
            sendMessage(messages[i].toObject(), QString::number(i));
        createPdf();
        const auto files = plan["files"].toArray();
        for (int i = 0; i < files.size(); ++i)
            transfer(files[i].toObject(), i);
        target->disconnectAccount();
        settle(500);
        clients.remove("self");
        target.reset();
        target = makeClient("self", true);
        if (!authenticate(target, "self", targetName, password, false))
            throw std::runtime_error("Target restart verification failed");
        wait(
            [&] {
                for (const auto& value : messages) {
                    const auto entry = value.toObject();
                    const auto source = clients[entry["from"].toString()];
                    QString id = entry.contains("group") ? groupId(entry["group"].toString())
                                 : source == target ? "peer:" + clients[entry["to"].toString()]->username()
                                                    : "peer:" + source->username();
                    if (!hasMessage(target, id, source->username(), entry["text"].toString()))
                        return false;
                }
                return true;
            },
            "persisted message verification");
        state["verifiedAfterRelogin"] = true;
        state["messageCount"] = messages.size();
        state["newActorCount"] = plan["actors"].toArray().size();
        state["groupCount"] = groups.size();
        state["completedFileCount"] = files.size();
        save();
        preview();
        target->disconnectAccount();
        settle(500);
        for (int i = 0; i < plan["queued"].toArray().size(); ++i)
            sendMessage(plan["queued"].toArray()[i].toObject(), "queued-" + QString::number(i), true);
        for (const auto& client : clients)
            client->disconnectAccount();
        settle(500);
        state["complete"] = true;
        state["queuedForNextLogin"] = plan["queued"].toArray().size();
        save();
        log("COMPLETE: registered actors, accepted friends, groups, delivered messages, verified files, "
            "relogin verified.");
    }
};
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName("MessagerDemoOperator");
    app.setApplicationName("DemoPopulation");
    app.setStyle(QStyleFactory::create("Fusion"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"plan", "Seed content JSON", "path"},
                       {"runtime", "Runtime directory", "path"},
                       {"work", "Scoped journal and actor directory", "path"},
                       {"data-root", "Existing target account root", "path"},
                       {"apply", "Create accounts and send the explicitly requested demo content"}});
    parser.process(app);
    try {
        auto config = readJson(parser.value("runtime") + "/connection.json");
        config["caFile"] = parser.value("runtime") + "/server-cert.pem";
        config["directEnabled"] = false;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, parser.value("work") + "/settings");
        Theme::instance().apply();
        log("PASSWORD_REQUIRED_ON_STDIN");
        std::string input;
        if (!std::getline(std::cin, input) || input.empty())
            throw std::runtime_error("Target password was not supplied through standard input");
        auto password = QString::fromUtf8(input.data(), qsizetype(input.size()));
        std::fill(input.begin(), input.end(), '\0');
        Population population(readJson(parser.value("plan")), config, parser.value("work"),
                              parser.value("data-root"), password);
        password.fill(QChar(0));
        population.run(parser.isSet("apply"));
        return 0;
    } catch (const std::exception& error) {
        log("STOPPED: " + QString::fromUtf8(error.what()));
        return 1;
    }
}
