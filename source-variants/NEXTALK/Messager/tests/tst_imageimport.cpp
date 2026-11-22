#include "services/imageimport.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
using namespace Messager;
class ImageImportTest:public QObject {
    Q_OBJECT
private slots:
    void webpAndMalformedFiles() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("avatar.webp");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromBase64("UklGRh4AAABXRUJQVlA4TBEAAAAvE8ACAAdQvC7UvP+BiOh/AAA="));file.close();
        QString error;const auto image=importImage(path,&error);QVERIFY2(!image.isNull(),qPrintable(error));QCOMPARE(image.size(),QSize(20,12));
        QVERIFY(!imageAsPng(path).isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write("not an image");file.close();QVERIFY(importImage(path).isNull());
    }
};
QTEST_MAIN(ImageImportTest)
#include "tst_imageimport.moc"
