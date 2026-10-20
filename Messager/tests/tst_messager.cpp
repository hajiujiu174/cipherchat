#include "services/localfiles.h"
#include "services/mockservice.h"
#include "ui/groupmemberpicker.h"
#include "ui/mainwindow.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLayout>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>
using namespace Messager;
class MessagerTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    std::unique_ptr<MockService> service;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<MainWindow> window;
    bool sendsSettled(const QString& id) const {
        for (const auto& message : service->messages(id))
            if (message.state == SendState::Sending)
                return false;
        return true;
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("MessagerTests");
        QCoreApplication::setApplicationName("UI");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        qApp->setStyle(QStyleFactory::create("Fusion"));
        Theme::instance().apply();
    }
    void init() {
        Theme::instance().setSkin(Theme::DefaultSkin);
        Theme::instance().setMode(Theme::Light);
        Theme::instance().setFontSize(14);
        service = std::make_unique<MockService>();
        controller = std::make_unique<AppController>(service.get());
        window = std::make_unique<MainWindow>(controller.get());
        window->show();
        QTest::qWait(40);
    }
    void cleanup() {
        window.reset();
        controller.reset();
        service.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void windowSizesAndDetails() {
        QCOMPARE(window->size(), QSize(1280, 800));
        QCOMPARE(window->minimumSize(), QSize(960, 640));
        auto* split = window->findChild<QSplitter*>("chatSplitter");
        QVERIFY(qAbs(split->sizes()[0] - 320) < 5);
        auto* chat = window->findChild<QWidget*>("chatArea");
        int width = chat->width();
        window->setDetailsVisible(true);
        QVERIFY(window->detailsVisible());
        QCOMPARE(chat->width(), width);
        window->resize(960, 640);
        QTest::qWait(40);
        QVERIFY(chat->width() >= 520);
        QVERIFY(split->sizes()[0] >= 260);
        auto* panel = window->findChild<QWidget*>("conversationDetails");
        QVERIFY(chat->rect().contains(panel->geometry()));
        window->setDetailsVisible(false);
    }
    void navigationAndThemes() {
        for (int i = 0; i < 5; ++i) {
            QTest::mouseClick(window->findChild<QPushButton*>("nav_" + QString::number(i)), Qt::LeftButton);
            QCOMPARE(window->findChild<QStackedWidget*>("pages")->currentIndex(), i);
        }
        QTest::mouseClick(window->findChild<QPushButton*>("theme_2"), Qt::LeftButton);
        QVERIFY(Theme::instance().dark());
        QCOMPARE(Theme::instance().mode(), Theme::Dark);
        QTest::mouseClick(window->findChild<QPushButton*>("theme_0"), Qt::LeftButton);
        QCOMPARE(Theme::instance().mode(), Theme::System);
        auto* font = window->findChild<QComboBox*>("fontSize");
        font->setCurrentIndex(4);
        QCOMPARE(qApp->font().pixelSize(), 17);
    }
    void groupMemberDropdown() {
        GroupMemberPicker picker;
        picker.resize(420, 44);
        const QVector<Contact> contacts = {{"self", true, true},   {"alice", true, true},
                                           {"bob", false, true},   {"stranger", true, false},
                                           {"alice", false, true}, {{}, false, true}};
        picker.setContacts(contacts, "self");
        picker.show();
        QTest::qWait(40);
        QCOMPARE(picker.findChildren<QCheckBox*>().size(), 2);
        QVERIFY(picker.selectedMembers().isEmpty());
        QSignalSpy changed(&picker, &GroupMemberPicker::selectionChanged);
        QTest::mouseClick(&picker, Qt::LeftButton);
        auto* popup = picker.findChild<QMenu*>("groupMembersPopup");
        QTRY_VERIFY(popup->isVisible());
        auto* alice = picker.findChild<QCheckBox*>("groupMember_alice");
        auto* bob = picker.findChild<QCheckBox*>("groupMember_bob");
        QVERIFY(alice && bob);
        QTest::mouseClick(alice, Qt::LeftButton, Qt::NoModifier, QPoint(10, alice->height() / 2));
        QVERIFY(popup->isVisible());
        QTest::mouseClick(bob, Qt::LeftButton, Qt::NoModifier, QPoint(10, bob->height() / 2));
        QCOMPARE(picker.selectedMembers(), QStringList({"alice", "bob"}));
        QCOMPARE(changed.count(), 2);
        QVERIFY(popup->isVisible());
        QVERIFY(picker.text().contains("2"));
        auto* search = picker.findChild<QLineEdit*>("groupMemberSearch");
        search->setText("BOB");
        QVERIFY(alice->isHidden());
        QVERIFY(bob->isVisible());
        QCOMPARE(picker.selectedMembers().size(), 2);
        search->setText("no-match");
        QVERIFY(picker.findChild<QLabel*>("groupMemberEmpty")->isVisible());
        search->clear();
        // Periodic contact events must retain the actual controls, selection and focus.
        picker.setContacts(contacts, "self");
        QCOMPARE(picker.findChild<QCheckBox*>("groupMember_alice"), alice);
        QCOMPARE(picker.selectedMembers().size(), 2);
        const QString screenshots = qEnvironmentVariable("MESSAGER_EVIDENCE_DIR");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            popup->grab().save(screenshots + "/group-members-light.png");
            Theme::instance().setMode(Theme::Dark);
            QTest::qWait(40);
            popup->grab().save(screenshots + "/group-members-dark.png");
            Theme::instance().setMode(Theme::Light);
        }
        QTest::mouseClick(picker.findChild<QPushButton*>("groupMemberDone"), Qt::LeftButton);
        QVERIFY(!popup->isVisible());
        QTest::mouseClick(&picker, Qt::LeftButton);
        QTRY_VERIFY(popup->isVisible());
        QVERIFY(alice->isChecked() && bob->isChecked());
        QTest::keyClick(search, Qt::Key_Escape);
        QTRY_VERIFY(!popup->isVisible());
        picker.setContacts({{"alice", false, true}}, "self");
        QCOMPARE(picker.selectedMembers(), QStringList({"alice"}));
        picker.setContacts({}, "self");
        QVERIFY(picker.selectedMembers().isEmpty());
        QTest::mouseClick(&picker, Qt::LeftButton);
        QTRY_VERIFY(popup->isVisible());
        QVERIFY(picker.findChild<QLabel*>("groupMemberEmpty")->text().contains("暂无好友"));
        popup->hide();
    }
    void pencilSkinKeepsDraftAndReadingPosition() {
        controller->selectConversation("design");
        for(int i=0;i<24;++i)service->send("design",QString("Pencil history %1 彩铅手帐与聊天记录").arg(i));
        QTRY_VERIFY_WITH_TIMEOUT(sendsSettled("design"),5000);
        auto* edit=window->findChild<MessageEditor*>("messageInput");
        edit->setPlainText("切换皮肤后保留的草稿\n第二行");
        auto cursor=edit->textCursor();cursor.setPosition(2);cursor.setPosition(7,QTextCursor::KeepAnchor);edit->setTextCursor(cursor);
        const auto selectedText=edit->textCursor().selectedText();
        auto* scroll=window->findChild<QScrollArea*>("messageScroll");auto* bar=scroll->verticalScrollBar();
        QVERIFY(bar->maximum()>500);bar->setSliderPosition(bar->maximum()/2);bar->triggerAction(QAbstractSlider::SliderMove);
        auto visibleId=[scroll,bar] {
            for(int i=0;i<scroll->widget()->layout()->count();++i) {
                auto* row=scroll->widget()->layout()->itemAt(i)->widget();
                if(row&&row->property("messageId").isValid()&&row->geometry().bottom()>=bar->value())return row->property("messageId").toString();
            }
            return QString{};
        };
        const auto anchor=visibleId();QVERIFY(!anchor.isEmpty());
        const auto count=service->messages("design").size();
        window->showPage(4);auto* skin=window->findChild<QComboBox*>("appearanceSkin");QVERIFY(skin);
        skin->setCurrentIndex(1);QCOMPARE(Theme::instance().skin(),Theme::PencilSkin);
        QCOMPARE(QSettings().value("appearance/skin").toString(),QString("pencil"));
        window->showPage(0);QTest::qWait(100);
        QCOMPARE(controller->selectedId(),QString("design"));QCOMPARE(service->messages("design").size(),count);
        QCOMPARE(edit->toPlainText(),QString("切换皮肤后保留的草稿\n第二行"));QCOMPARE(edit->textCursor().selectedText(),selectedText);
        QCOMPARE(visibleId(),anchor);
        for(auto mode:{Theme::Light,Theme::Dark}) {
            Theme::instance().setMode(mode);QTest::qWait(80);
            const auto& c=Theme::instance().colors();
            QVERIFY(c.text.lightness()!=c.window.lightness());
            const auto image=window->grab().toImage();QVERIFY(!image.isNull());
            const auto evidence=qEnvironmentVariable("MESSAGER_EVIDENCE_DIR");
            if(!evidence.isEmpty()) {QDir().mkpath(evidence);QVERIFY(image.save(evidence+(mode==Theme::Light?"/pencil-chat-light.png":"/pencil-chat-dark.png")));}
            for(auto* bubble:scroll->findChildren<MessageBubble*>())
                for(auto* body:bubble->findChildren<QLabel*>())
                    if(body->property("bubbleOutgoing").toBool())QCOMPARE(body->palette().color(QPalette::WindowText),c.text);
            if(!evidence.isEmpty()) {
                window->showPage(4);QTest::qWait(40);
                QVERIFY(window->grab().save(evidence+(mode==Theme::Light?"/pencil-settings-light.png":"/pencil-settings-dark.png")));
                skin->showPopup();QTest::qWait(40);
                QVERIFY(skin->view()->window()->grab().save(evidence+(mode==Theme::Light?"/pencil-skin-menu-light.png":"/pencil-skin-menu-dark.png")));
                skin->hidePopup();window->showPage(0);
            }
        }
        window->showPage(4);skin->setCurrentIndex(0);QCOMPARE(Theme::instance().skin(),Theme::DefaultSkin);
        QCOMPARE(QSettings().value("appearance/skin").toString(),QString("default"));
        QCOMPARE(edit->toPlainText(),QString("切换皮肤后保留的草稿\n第二行"));
    }
    void groupMemberLimit() {
        GroupMemberPicker picker;
        QVector<Contact> contacts;
        for (int i = 0; i < 70; ++i)
            contacts.append({QString("friend_%1").arg(i, 2, 10, QChar('0')), false, true});
        picker.setContacts(contacts, "self");
        const auto checks = picker.findChildren<QCheckBox*>();
        for (int i = 0; i < 63; ++i)
            checks[i]->setChecked(true);
        QCOMPARE(picker.selectedMembers().size(), 63);
        QVERIFY(!checks[63]->isEnabled());
        checks[0]->setChecked(false);
        QVERIFY(checks[63]->isEnabled());
        checks[63]->setChecked(true);
        QCOMPARE(picker.selectedMembers().size(), 63);
    }
    void sendAndMultiline() {
        auto* edit = window->findChild<MessageEditor*>("messageInput");
        auto before = service->messages("design").size();
        edit->setFocus();
        QTest::keyClicks(edit, "Hello");
        QTest::keyClick(edit, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(edit, "world");
        QCOMPARE(service->messages("design").size(), before);
        QCOMPARE(edit->toPlainText(), QString("Hello\nworld"));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(service->messages("design").size(), before + 1);
        QVERIFY(edit->toPlainText().isEmpty());
        QCOMPARE(service->messages("design").last().state, SendState::Sending);
        QTRY_COMPARE_WITH_TIMEOUT(service->messages("design").last().state, SendState::Stored, 1500);
        const int count = service->messages("design").size();
        edit->setPlainText(" \n ");
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(service->messages("design").size(), count);
    }
    void failedMessageRetry() {
        controller->sendText("failure scenario", true);
        const auto id = service->messages("design").last().id;
        QTRY_COMPARE_WITH_TIMEOUT(service->messages("design").last().state, SendState::Failed, 1500);
        auto* retry = window->findChild<QPushButton*>("retry_" + id);
        QVERIFY(retry);
        QTest::mouseClick(retry, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(service->messages("design").last().state, SendState::Stored, 1500);
    }
    void searchAndUnreadSelection() {
        auto* search = window->findChild<QLineEdit*>("conversationSearch");
        auto* list = window->findChild<QListView*>("conversationList");
        search->setText("不存在的会话");
        QCOMPARE(list->model()->rowCount(), 0);
        search->setText("林予");
        QCOMPARE(list->model()->rowCount(), 2);
        search->clear();
        QTest::mouseClick(window->findChild<QPushButton*>("filter_1"), Qt::LeftButton);
        QCOMPARE(list->model()->rowCount(), 2);
        const auto index = list->model()->index(0, 0);
        const QString id = index.data(ConversationModel::Id).toString();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualRect(index).center());
        QCOMPARE(controller->selectedId(), id);
        QCOMPARE(service->conversation(id).unread, 0);
        QCOMPARE(list->model()->rowCount(), 1);
    }
    void contactsAndDrafts() {
        auto* edit = window->findChild<MessageEditor*>("messageInput");
        edit->setPlainText("设计群草稿");
        window->showPage(1);
        QTest::mouseClick(window->findChild<QPushButton*>("contact_lin"), Qt::LeftButton);
        QCOMPARE(controller->selectedId(), QString("lin"));
        QCOMPARE(window->findChild<QStackedWidget*>("pages")->currentIndex(), 0);
        QVERIFY(edit->toPlainText().isEmpty());
        edit->setPlainText("给林予的草稿");
        controller->selectConversation("design");
        QCOMPARE(edit->toPlainText(), QString("设计群草稿"));
        controller->selectConversation("lin");
        QCOMPARE(edit->toPlainText(), QString("给林予的草稿"));
    }
    void incomingMessage() {
        const int before = service->messages("design").size();
        QTest::mouseClick(window->findChild<QPushButton*>("simulateMessage"), Qt::LeftButton);
        QCOMPARE(service->messages("design").size(), before + 1);
        QVERIFY(!service->messages("design").last().outgoing);
        QCOMPARE(service->conversation("design").unread, 0);
    }
    void adaptiveComposer() {
        auto* edit = window->findChild<MessageEditor*>("messageInput");
        edit->setPlainText("one\ntwo\nthree\nfour\nfive\nsix");
        QTest::qWait(30);
        QVERIFY(edit->height() > 52);
        QVERIFY(edit->height() <= 132);
        edit->clear();
        QCOMPARE(edit->height(), 52);
    }
    void transfersLifecycle() {
        service->transferAction("t1", "pause");
        auto progress = service->transfers()[0].progress;
        service->tickTransfers();
        QCOMPARE(service->transfers()[0].progress, progress);
        service->transferAction("t1", "resume");
        service->tickTransfers();
        QVERIFY(service->transfers()[0].progress > progress);
        service->transferAction("t1", "cancel");
        QCOMPARE(service->transfers()[0].state, TransferState::Cancelled);
        service->transferAction("t1", "retry");
        QCOMPARE(service->transfers()[0].progress, 0);
        for (int i = 0; i < 50; ++i)
            service->tickTransfers();
        QCOMPARE(service->transfers()[0].state, TransferState::Completed);
        window->showPage(2);
        auto* retry = window->findChild<QPushButton*>("transferAction_t3");
        QVERIFY(retry);
        QTest::mouseClick(retry, Qt::LeftButton);
        QCOMPARE(service->transfers()[2].state, TransferState::Running);
    }
    void fileMetadataOnly() {
        QTemporaryDir dir;
        QString path = dir.filePath("sample.txt");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("local mock attachment");
        f.close();
        auto original = f.size();
        controller->attachFile(path);
        QCOMPARE(service->transfers().first().name, QString("sample.txt"));
        QVERIFY(service->messages("design").last().file);
        QCOMPARE(QFileInfo(path).size(), original);
    }
    void fileLocationUsesExactMessageAndClearsSearch() {
        service->addTransfer("same-name.zip", 2048, "design");
        const auto first = service->transfers().first();
        for (int i = 0; i < 24; ++i)
            service->send("design", QString("History between file transfers %1").arg(i));
        service->addTransfer("same-name.zip", 2048, "design");
        const auto second = service->transfers().first();
        for (int i = 0; i < 12; ++i)
            service->send("design", QString("Tail after file transfers %1").arg(i));
        QTRY_VERIFY_WITH_TIMEOUT(sendsSettled("design"), 5000);
        QVERIFY(first.messageId != second.messageId);
        controller->selectConversation("lin");
        auto* search = window->findChild<QLineEdit*>("messageSearch");
        search->setText("filter that hides all file messages");
        window->showPage(2);
        auto* locate = window->findChild<QPushButton*>("transferLocate_" + first.id);
        QVERIFY(locate && locate->isEnabled());
        QTest::mouseClick(locate, Qt::LeftButton);
        QCOMPARE(controller->selectedId(), QString("design"));
        QCOMPARE(window->findChild<QStackedWidget*>("pages")->currentIndex(), 0);
        QVERIFY(search->text().isEmpty());
        auto* scroll = window->findChild<QScrollArea*>("messageScroll");
        auto checkVisible = [&](const QString& messageId) {
            for (auto* row : scroll->widget()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
                if (row->property("messageId").toString() == messageId) {
                    auto* bubble = row->findChild<MessageBubble*>();
                    const QRect bounds(row->mapTo(scroll->viewport(), QPoint()), row->size());
                    return bubble && bubble->property("locatedMessage").toBool() &&
                           scroll->viewport()->rect().contains(bounds);
                }
            return false;
        };
        QVERIFY(checkVisible(first.messageId));
        service->simulateIncoming("design");
        QTest::qWait(150);
        QVERIFY(checkVisible(first.messageId));
        QVERIFY(scroll->verticalScrollBar()->value() < scroll->verticalScrollBar()->maximum());
        window->showPage(2);
        locate = window->findChild<QPushButton*>("transferLocate_" + second.id);
        QVERIFY(locate && locate->isEnabled());
        QTest::mouseClick(locate, Qt::LeftButton);
        QVERIFY(checkVisible(second.messageId));
        window->resize(960, 640);
        QTest::qWait(100);
        QVERIFY(checkVisible(second.messageId));
    }
    void fileActionButtonsAndLocalPathAvailability() {
        QTemporaryDir directory;
        const QString path = directory.filePath("文件, with spaces.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Local file location fixture");
        file.close();
        Transfer transfer;
        transfer.id = "layout-fixture";
        transfer.name = "文件名称很长时仍应能显示操作按钮-with-a-long-file-name.txt";
        transfer.conversation = "design";
        transfer.messageId = "file:layout-fixture";
        transfer.path = path;
        transfer.simulated = false;
        transfer.state = TransferState::Completed;
        TransferCard card(transfer);
        card.resize(800, 210);
        card.show();
        QTest::qWait(30);
        auto* locate = card.findChild<QPushButton*>("transferLocate_" + transfer.id);
        auto* reveal = card.findChild<QPushButton*>("transferReveal_" + transfer.id);
        QVERIFY(locate && reveal);
        QSignalSpy actions(&card, &TransferCard::action);
        for (const auto state : {TransferState::Running, TransferState::Paused, TransferState::Completed,
                                 TransferState::Failed, TransferState::Cancelled}) {
            transfer.state = state;
            card.refresh(transfer);
            QTest::qWait(10);
            QVERIFY(locate->isVisible() && locate->isEnabled());
            QVERIFY(reveal->isVisible() && reveal->isEnabled());
            const QRect locateBounds(locate->mapTo(&card, QPoint()), locate->size());
            const QRect revealBounds(reveal->mapTo(&card, QPoint()), reveal->size());
            QVERIFY(card.rect().contains(locateBounds));
            QVERIFY(card.rect().contains(revealBounds));
            QVERIFY(!locateBounds.intersects(revealBounds));
        }
        QTest::mouseClick(reveal, Qt::LeftButton);
        QCOMPARE(actions.count(), 1);
        QCOMPARE(actions.first().at(1).toString(), QString("reveal"));
        QCOMPARE(localFileLocation(path).selectedFile, path);
        const QString missingFile = directory.filePath("not-saved-yet.bin");
        transfer.path = missingFile;
        card.refresh(transfer);
        QVERIFY(reveal->isEnabled());
        QVERIFY(localFileLocation(missingFile).selectedFile.isEmpty());
        QCOMPARE(localFileLocation(missingFile).directory, directory.path());
        transfer.path = directory.filePath("missing-directory/file.bin");
        card.refresh(transfer);
        QVERIFY(!reveal->isEnabled());
        QVERIFY(!revealLocalFile(transfer.path));
        transfer.path = path;
        transfer.simulated = true;
        card.refresh(transfer);
        QVERIFY(!reveal->isEnabled());
    }
    void minimumSizeAllPages() {
        window->resize(960, 640);
        for (int i = 0; i < 5; ++i) {
            window->showPage(i);
            QTest::qWait(25);
            QCOMPARE(window->size(), QSize(960, 640));
            auto* pages = window->findChild<QStackedWidget*>("pages");
            QVERIFY(pages->currentWidget()->width() <= 892);
        }
    }
    void resizeKeepsLatestMessageVisible() {
        auto* edit = window->findChild<MessageEditor*>("messageInput");
        edit->setPlainText("tail marker");
        QTest::keyClick(edit, Qt::Key_Return);
        QTest::qWait(150);
        auto* scroll = window->findChild<QScrollArea*>("messageScroll");
        QCOMPARE(scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum());
        window->resize(960, 640);
        QTest::qWait(180);
        QCOMPARE(scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum());
    }
    void backgroundRefreshKeepsTranscript() {
        for (int i = 0; i < 24; ++i)
            service->send(
                "design",
                QString("History fixture %1: a message to read without moving the viewport.").arg(i));
        QTRY_VERIFY_WITH_TIMEOUT(sendsSettled("design"), 5000);
        auto* scroll = window->findChild<QScrollArea*>("messageScroll");
        auto* bar = scroll->verticalScrollBar();
        QVERIFY(bar->maximum() > 500);
        bar->setSliderPosition(bar->maximum() / 2);
        bar->triggerAction(QAbstractSlider::SliderMove);
        const int readingPosition = bar->value();
        QPointer<QWidget> transcript = scroll->widget();
        QSignalSpy movements(bar, &QScrollBar::valueChanged);
        for (int i = 0; i < 12; ++i) {
            emit service->conversationsChanged();
            emit service->messagesChanged("design");
            QTest::qWait(15);
        }
        QCOMPARE(scroll->widget(), transcript.data());
        QCOMPARE(bar->value(), readingPosition);
        QCOMPARE(movements.count(), 0);
    }
    void newMessagesKeepReadingPositionWithoutTopFlash() {
        for (int i = 0; i < 24; ++i)
            service->send("design", QString("History fixture %1").arg(i));
        QTRY_VERIFY_WITH_TIMEOUT(sendsSettled("design"), 5000);
        auto* scroll = window->findChild<QScrollArea*>("messageScroll");
        auto* bar = scroll->verticalScrollBar();
        bar->setSliderPosition(bar->maximum() / 2);
        bar->triggerAction(QAbstractSlider::SliderMove);
        const int readingPosition = bar->value();
        QVERIFY(readingPosition > 200);
        QSignalSpy movements(bar, &QScrollBar::valueChanged);
        service->simulateIncoming("design");
        QCOMPARE(bar->value(), readingPosition);
        QTest::qWait(150);
        QCOMPARE(bar->value(), readingPosition);
        for (const auto& movement : movements)
            QVERIFY2(movement.first().toInt() > 0, "Transcript temporarily jumped to the top");
    }
    void newMessagesFollowTailWithoutTopFlash() {
        for (int i = 0; i < 24; ++i)
            service->send("design", QString("History fixture %1").arg(i));
        QTRY_VERIFY_WITH_TIMEOUT(sendsSettled("design"), 5000);
        auto* scroll = window->findChild<QScrollArea*>("messageScroll");
        auto* bar = scroll->verticalScrollBar();
        QCOMPARE(bar->value(), bar->maximum());
        QSignalSpy movements(bar, &QScrollBar::valueChanged);
        for (int i = 0; i < 4; ++i)
            service->simulateIncoming("design");
        QCOMPARE(bar->value(), bar->maximum());
        QTest::qWait(150);
        QCOMPARE(bar->value(), bar->maximum());
        for (const auto& movement : movements)
            QVERIFY2(movement.first().toInt() > 0, "Transcript temporarily jumped to the top");
    }
};
QTEST_MAIN(MessagerTest)
#include "tst_messager.moc"
