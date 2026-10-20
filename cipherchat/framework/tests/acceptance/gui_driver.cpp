// Native Qt GUI acceptance driver. Only the test binary drives its own window.
#include "main_window.hpp"
#include "../ui/qt_client_page.hpp"
#include "../../src/snapshot_codec.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QTest>
#include <QScopedValueRollback>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    QApplication application(argc, argv); QCoreApplication::setApplicationName("CipherChat");
    const auto configPath = ccfw::codec::path(argv[1]); std::ifstream input(configPath); nlohmann::json config; input >> config;
    const auto directory = ccfw::codec::path(config.at("artifacts").get<std::string>()); std::filesystem::create_directories(directory);
    const bool sender = config.at("sending").get<bool>();
    const bool chatOnly = config.value("chat_only", false);
    const bool layoutChecks = config.value("layout_checks", false);
    const QString ownMessage = QString::fromUtf8(config.at("own_message").get<std::string>().c_str());
    const QString peerMessage = QString::fromUtf8(config.at("peer_message").get<std::string>().c_str());
    const QString peerName = QString::fromUtf8(config.at("peer").get<std::string>().c_str());
    ccfw::gui::WindowOptions options;
    options.username = config.at("username").get<std::string>(); options.dataDirectory = ccfw::codec::path(config.at("data").get<std::string>());
    if (config.contains("direct_enabled")) options.directEnabled = config.at("direct_enabled").get<bool>();
    options.accountServerId = config.value("account_scope", std::string{});
    options.connection = {config.value("host", "127.0.0.1"), config.at("port").get<std::uint16_t>(),
        ccfw::codec::path(config.at("ca").get<std::string>()), config.value("server_name", "localhost"), config.value("connect_timeout_ms", 3000u)};
    ccfw::gui::MainWindow window(options); window.move(sender ? 30 : 160, sender ? 30 : 100); window.show();
    if (!QTest::qWaitForWindowExposed(&window, 5000)) return 3;
    ccfw::test::QtClientPage page(window);
    if (qEnvironmentVariable("CCFW_TEST_DESIGN") == "codex") page.useCodexDesign();
    if (qEnvironmentVariable("CCFW_TEST_APPEARANCE") == "dark") page.useDarkMode();
    bool conversationFilterVerified = false, timestampStable = false, timelineOrderVerified = false;
    bool defaultConnectionHidden = false, securityDetailsVerified = false;
    bool filesCollapsedByDefault = false, collapsedDuringTransfer = false, completedWhileCollapsed = false;
    QString fileId; qint64 fileTimestamp = 0;
    const auto afterMessage = QStringLiteral("文件已发出，接着聊下一件事。");
    int finalTransportPath = -1; bool p2pSeen = false, relaySeen = false;
    int stage = 0, heartbeatCount = 0, identityConfirmations = 0; bool identityConfirmed = false, progressSeen = false, sent = false, existingIdentityMatched = false;
    QElapsedTimer elapsed, pulse; elapsed.start(); pulse.start(); qint64 maxGap = 0, fileStart = 0, fileElapsed = 0;
    QTimer heartbeat; heartbeat.setInterval(10); QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] { ++heartbeatCount; maxGap = std::max(maxGap, pulse.restart()); }); heartbeat.start();
    auto screenshot = [&](QWidget& widget, const char* name) { if (!widget.grab().save(QString::fromStdWString((directory / name).wstring()))) throw std::runtime_error("screenshot save failed"); };
    bool driving = false;
    QTimer flow; flow.setInterval(40); QObject::connect(&flow, &QTimer::timeout, &window, [&] {
        if (driving) return; // QtTest input may run a nested event loop.
        QScopedValueRollback<bool> guard(driving, true);
        try {
            if (elapsed.elapsed() > config.value("workflow_timeout_ms", 45000)) throw std::runtime_error("GUI workflow timed out at stage " + std::to_string(stage));
            if (page.identityConfirmationPending()) {
                const auto expectedPath = ccfw::codec::path(config.at("peer_identity").get<std::string>());
                if (!std::filesystem::exists(expectedPath)) return;
                std::ifstream peer(expectedPath); const std::string expected{std::istreambuf_iterator<char>(peer), {}};
                if (layoutChecks) page.checkPeerDetailsInitiallyHidden();
                screenshot(page.peerSecuritySurface(), "identity-collapsed.png"); page.openPeerSecurity();
                const auto actual = page.peerFingerprint();
                if (actual != QString::fromUtf8(expected.c_str())) throw std::runtime_error("displayed peer fingerprint does not match other GUI identity");
                screenshot(page.peerSecuritySurface(), "identity.png"); page.confirmIdentity(); identityConfirmed = true; ++identityConfirmations;
                if (identityConfirmations > 1) throw std::runtime_error("unchanged verified identity reopened its confirmation dialog"); return;
            }
            switch (stage) {
            case 0:
                if (layoutChecks) { page.checkAccountLayout(); defaultConnectionHidden = true; }
                screenshot(window, "account-before.png");
                page.authenticate(QString::fromStdString(config.value("password", "password123")), config.value("register_new", true)); stage = 1; break;
            case 1:
                if (!page.online()) break;
                if (layoutChecks) page.checkAccountLayout();
                screenshot(window, "account-online.png");
                page.openOwnSecurity(); screenshot(window, "advanced-settings.png"); stage = 10; break;
            case 10: {
                if (page.connectionHost().toStdString() != options.connection.host)
                    throw std::runtime_error("opening connection settings changed the server");
                const auto own = page.ownFingerprint();
                if (config.contains("expected_own_identity")) {
                    std::ifstream previous(ccfw::codec::path(config.at("expected_own_identity").get<std::string>()));
                    const std::string expected{std::istreambuf_iterator<char>(previous), {}};
                    if (expected.empty() || own != QString::fromUtf8(expected.c_str())) throw std::runtime_error("existing identity changed after login");
                    existingIdentityMatched = true;
                }
                const auto identityPath = ccfw::codec::path(config.at("own_identity").get<std::string>());
                auto temporary = identityPath; temporary += ".tmp";
                std::ofstream identity(temporary); identity << own.toUtf8().constData(); identity.close();
                if (!identity) throw std::runtime_error("could not save own verification data");
                std::filesystem::rename(temporary, identityPath);
                screenshot(page.ownSecuritySurface(), "own-security-details.png"); page.closeOwnSecurity();
                if (layoutChecks) page.checkAccountLayout();
                securityDetailsVerified = true;
                page.selectConversation(peerName); stage = 2; break;
            }
            case 2:
                if (config.value("verify_timeline_on_login", false)) {
                    if (!page.timelineOrder(sender ? ownMessage : peerMessage, afterMessage)) break;
                    fileTimestamp = page.fileCard()->timestamp;
                    if (fileTimestamp != config.at("expected_file_timestamp").get<std::int64_t>()) throw std::runtime_error("restart changed the file timestamp");
                    timelineOrderVerified = timestampStable = true; screenshot(window, "timeline-restored.png"); stage = 6; break;
                }
                if (!std::filesystem::exists(ccfw::codec::path(config.at("peer_identity").get<std::string>()))) break;
                if (!sender && !page.hasMessage(peerMessage)) break;
                page.sendMessage(ownMessage); sent = true; stage = 3; break;
            case 3:
                if (!sent || !page.hasMessage(ownMessage) || !page.hasMessage(peerMessage)) break;
                if (!page.hasReceipt(ownMessage)) break;
                if (layoutChecks) { page.checkAttachmentsCollapsed(); filesCollapsedByDefault = true; }
                screenshot(window, "chat.png");
                if (chatOnly) { stage = 6; break; }
                page.showAttachments(true); stage = 13; break;
            case 13:
                fileStart = elapsed.elapsed();
                if (sender) {
                    const auto source = QString::fromUtf8(config.at("source").get<std::string>().c_str());
                    page.selectFile(source);
                    if (layoutChecks) {
                        page.showAttachments(false); page.showAttachments(true);
                        if (page.selectedFile() != source) throw std::runtime_error("collapsing cleared the selected file");
                    }
                    page.sendSelectedFile();
                }
                if (layoutChecks) { page.showAttachments(false); collapsedDuringTransfer = true; }
                stage = 4; break;
            case 4: {
                if (layoutChecks && page.attachmentsVisible()) throw std::runtime_error("progress unexpectedly reopened attachment tools");
                const auto card = page.fileCard();
                if (!card) break;
                const auto stamp = card->timestamp;
                if (fileId.isEmpty()) { fileId = card->id; fileTimestamp = stamp; }
                else { if (fileTimestamp != stamp || fileId != card->id) throw std::runtime_error("file progress changed timeline position"); timestampStable = true; }
                finalTransportPath = card->transportPath; p2pSeen |= finalTransportPath == 2; relaySeen |= finalTransportPath == 0 || finalTransportPath == 3;
                if (card->percent > 0 && card->percent < 100 && !progressSeen) {
                    if (!card->canResume || !card->canCancel)
                        throw std::runtime_error("file card actions unavailable");
                    screenshot(window, "file-progress.png"); progressSeen = true;
                    if (sender) page.sendMessage(afterMessage);
                }
                if (card->complete) {
                    fileElapsed = elapsed.elapsed() - fileStart; completedWhileCollapsed = collapsedDuringTransfer;
                    if (card->canResume || card->canCancel)
                        throw std::runtime_error("completed card still exposes active actions");
                    stage = 12;
                }
                break;
            }
            case 12:
                if (!page.hasMessage(afterMessage) || (sender && !page.hasReceipt(afterMessage))) break;
                if (!page.timelineOrder(sender ? ownMessage : peerMessage, afterMessage)) break;
                timelineOrderVerified = true; screenshot(window, "files-in-timeline.png");
                if (layoutChecks) window.resize(850, 640);
                stage = 8; break;
            case 8:
                if (layoutChecks) page.scrollFileIntoView();
                stage = 15; break;
            case 15: {
                if (layoutChecks) {
                    if (!page.timelineOrder(sender ? ownMessage : peerMessage, afterMessage)) throw std::runtime_error("minimum layout lost timeline entries");
                    page.checkFileFitsViewport(); screenshot(window, "timeline-minimum.png");
                }
                const auto pendingFile = directory / "pending-file-must-not-change-recipient.bin";
                { std::ofstream pending(pendingFile); pending << "file selection target proof"; }
                page.selectFile(QString::fromStdWString(pendingFile.wstring()));
                page.selectConversation(QStringLiteral("other_contact"));
                // New design preserves the explicitly captured recipient. The
                // selected attachment must never silently retarget this peer.
                if (page.fileCard() || page.selectedFile().isEmpty())
                    throw std::runtime_error("switching contact exposed another contact's card or lost selected attachment");
                if (!page.selectedFileRecipient().contains(peerName)) throw std::runtime_error("attachment recipient changed after selection");
                page.selectConversation(QString{});
                if (page.fileCard()) throw std::runtime_error("empty conversation exposed file card");
                page.selectConversation(peerName);
                const auto card = page.fileCard();
                if (!card || card->id != fileId || card->timestamp != fileTimestamp)
                    throw std::runtime_error("switching back lost file identity or time");
                conversationFilterVerified = true; stage = 14; break;
            }
            case 14:
                if (layoutChecks) { screenshot(window, "timeline-attachments-minimum.png"); page.showAttachments(false); window.resize(1020, 800); }
                page.showGroups();
                if (sender) page.createGroup(QStringLiteral("演示小组"), peerName);
                stage = 5; break;
            case 5:
                if (!page.hasGroup()) break;
                page.showGroups();
                if (sender) page.sendGroupMessage(QStringLiteral("群聊演示：每位成员分别加密。"));
                stage = 6; break;
            case 6:
                if (!chatOnly && !page.hasMessage(QStringLiteral("群聊演示：每位成员分别加密。"))) break;
                screenshot(window, chatOnly ? "chat-after-restart.png" : "group.png");
                {
                    std::ofstream done(configPath.string() + ".done.tmp"); done << nlohmann::json{{"identity_confirmed", identityConfirmed}, {"progress_seen", progressSeen},
                        {"identity_confirmations", identityConfirmations},
                        {"existing_identity_matched", existingIdentityMatched},
                        {"layout_checks", layoutChecks}, {"default_connection_hidden", defaultConnectionHidden}, {"security_details_verified", securityDetailsVerified},
                        {"files_collapsed_by_default", filesCollapsedByDefault}, {"completed_while_collapsed", completedWhileCollapsed},
                        {"conversation_filter_verified", conversationFilterVerified}, {"file_timestamp_stable", timestampStable}, {"timeline_order_verified", timelineOrderVerified}, {"file_timestamp", fileTimestamp},
                        {"file_transfer_ms", fileElapsed}, {"file_transport_path", finalTransportPath}, {"p2p_seen", p2pSeen}, {"relay_seen", relaySeen},
                        {"gui_heartbeats", heartbeatCount}, {"max_gui_gap_ms", maxGap}, {"native_window", static_cast<std::uint64_t>(window.winId())}, {"stage", stage}}.dump(2);
                    done.close(); if (!done) throw std::runtime_error("failed to save GUI evidence");
                    std::filesystem::rename(configPath.string() + ".done.tmp", configPath.string() + ".done");
                }
                stage = 7; break;
            case 7:
                if (std::filesystem::exists(configPath.string() + ".stop")) { flow.stop(); page.logout(); window.close(); }
                break;
            }
        } catch (const std::exception& error) {
            screenshot(window, "failure.png"); std::ofstream report(configPath.string() + ".error");
            report << error.what() << "\n" << page.notification().toUtf8().constData();
            const auto last=window.property("lastBusinessError").toMap();
            report << "\nBusiness error task=" << last.value("task").toString().toStdString() << " code=" << last.value("code").toInt(); report.close();
            flow.stop(); window.close(); application.exit(1);
        }
    }); flow.start(); return application.exec();
}
