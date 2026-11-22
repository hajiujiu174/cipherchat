#include "gui/update_controller.hpp"
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QElapsedTimer>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QtTest>

class RuntimePackageProbe : public QObject {
    Q_OBJECT
private slots:
    void installedPackageCanCheckRealLocalFeed() {
        const auto package = qEnvironmentVariable("NEXTALK_RUNTIME_AUDIT");
        QVERIFY2(!package.isEmpty(), "Set NEXTALK_RUNTIME_AUDIT to the real deployed version directory");
        QDialog window;
        QPushButton entry("软件更新", &window);
        ccfw::gui::UpdateController controller(&window, &entry, package, {}, true);
        auto* dialog = window.findChild<QDialog*>("updateDialog");
        QVERIFY(dialog);
        auto* check = dialog->findChild<QPushButton*>("checkUpdate");
        auto* status = dialog->findChild<QLabel*>("updateStatus");
        QVERIFY(check && status);
        QElapsedTimer timer;
        timer.start();
        while (!status->text().contains("已经是最新版本") && timer.elapsed() < 15000) QTest::qWait(20);
        QVERIFY2(status->text().contains("已经是最新版本"), qPrintable(status->text()));
        QVERIFY(check->isEnabled());
        QFile release(package + "/release.json");
        QVERIFY(release.open(QIODevice::ReadOnly));
        const auto version = QJsonDocument::fromJson(release.readAll()).object()["version"].toString();
        QVERIFY(!version.isEmpty());
        QVERIFY(dialog->findChild<QLabel*>("updateVersion")->text().contains(version));
        QTest::mouseClick(&entry, Qt::LeftButton);
        QVERIFY(dialog->isVisible());
        QTest::qWait(50);
        const auto output = qEnvironmentVariable("NEXTALK_RUNTIME_EVIDENCE");
        QVERIFY(!output.isEmpty());
        QDir().mkpath(output);
        QVERIFY(dialog->grab().save(output + "/installed-update-window.png"));
        QJsonArray formats;
        for (const auto& format : QImageReader::supportedImageFormats()) formats.append(QString::fromLatin1(format));
        QFile inventory(output + "/image-format-inventory.json");
        QVERIFY(inventory.open(QIODevice::WriteOnly));
        inventory.write(QJsonDocument(QJsonObject{{"formats", formats},
            {"webp_available", QImageReader::supportedImageFormats().contains("webp")}}).toJson());
    }
};
QTEST_MAIN(RuntimePackageProbe)
#include "tst_runtime_package.moc"
