#include <QtTest>

#include <QDir>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "network/sessionbackgroundimage.h"

class tst_SessionBackgroundImage : public QObject
{
    Q_OBJECT

private:
    static QString writeImage(const QString& path, const QSize& size = QSize(32, 24),
                              const char* format = "PNG")
    {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(30, 80, 140, 128));
        if (!image.save(path, format))
            return {};
        return path;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void returnsSmallPngUnmodifiedAndPreservesAlpha()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString path = writeImage(root.filePath(QStringLiteral("background.png")));
        QVERIFY(!path.isEmpty());
        QFile originalFile(path);
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        const QByteArray original = originalFile.readAll();

        SessionBackgroundImage::Result result;
        QString error;
        QVERIFY2(SessionBackgroundImage::load(path, root.path(), &result, &error), qPrintable(error));
        QCOMPARE(result.contentType, QStringLiteral("image/png"));
        QCOMPARE(result.bytes, original);
        const QImage decoded = QImage::fromData(result.bytes);
        QVERIFY(!decoded.isNull());
        QVERIFY(decoded.hasAlphaChannel());
    }

    void rejectsMissingOutsideAndUnsupportedFiles()
    {
        QTemporaryDir root;
        QTemporaryDir outside;
        QVERIFY(root.isValid());
        QVERIFY(outside.isValid());
        SessionBackgroundImage::Result result;
        QString error;
        QVERIFY(!SessionBackgroundImage::load(root.filePath(QStringLiteral("missing.png")),
                                              root.path(), &result, &error));
        QVERIFY(!error.isEmpty());

        const QString outsidePath = writeImage(outside.filePath(QStringLiteral("outside.png")));
        QVERIFY(!outsidePath.isEmpty());
        QVERIFY(!SessionBackgroundImage::load(outsidePath, root.path(), &result, &error));

        const QString textPath = root.filePath(QStringLiteral("not-image.png"));
        QFile text(textPath);
        QVERIFY(text.open(QIODevice::WriteOnly));
        QCOMPARE(text.write("not a png"), qint64(9));
        text.close();
        QVERIFY(!SessionBackgroundImage::load(textPath, root.path(), &result, &error));
    }

    void rejectsSymlinkEscapingAllowedRoot()
    {
        QTemporaryDir root;
        QTemporaryDir outside;
        QVERIFY(root.isValid());
        QVERIFY(outside.isValid());
        const QString outsidePath = writeImage(outside.filePath(QStringLiteral("outside.png")));
        QVERIFY(!outsidePath.isEmpty());
        const QString linkPath = root.filePath(QStringLiteral("linked.png"));
        if (!QFile::link(outsidePath, linkPath))
            QSKIP("Filesystem does not permit creating a symlink for this test");

        SessionBackgroundImage::Result result;
        QString error;
        QVERIFY(!SessionBackgroundImage::load(linkPath, root.path(), &result, &error));
        QVERIFY(error.contains(QStringLiteral("unavailable")));
    }

    void rejectsSourceLargerThanBound()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString path = root.filePath(QStringLiteral("large.jpg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(SessionBackgroundImage::MaximumSourceBytes + 1));
        file.close();

        SessionBackgroundImage::Result result;
        QString error;
        QVERIFY(!SessionBackgroundImage::load(path, root.path(), &result, &error));
        QVERIFY(error.contains(QStringLiteral("size limit")));
    }

    void resizesLargeDimensionsAndBoundsOutput()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString path = writeImage(root.filePath(QStringLiteral("large.jpg")),
                                        QSize(3000, 1600), "JPEG");
        QVERIFY(!path.isEmpty());
        SessionBackgroundImage::Result result;
        QString error;
        QVERIFY2(SessionBackgroundImage::load(path, root.path(), &result, &error), qPrintable(error));
        QCOMPARE(result.contentType, QStringLiteral("image/jpeg"));
        QVERIFY(!result.bytes.isEmpty());
        QVERIFY(result.bytes.size() <= SessionBackgroundImage::MaximumOutputBytes);
        const QImage decoded = QImage::fromData(result.bytes);
        QVERIFY(!decoded.isNull());
        QVERIFY(decoded.width() <= SessionBackgroundImage::MaximumEdgePixels);
        QVERIFY(decoded.height() <= SessionBackgroundImage::MaximumEdgePixels);
    }
};

QTEST_GUILESS_MAIN(tst_SessionBackgroundImage)
#include "tst_sessionbackgroundimage.moc"
