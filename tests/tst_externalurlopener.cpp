#include "remote/externalurlopener.h"

#include <QtTest>

class tst_ExternalUrlOpener final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void acceptsOnlyCredentialFreeHttpUrls()
    {
        QVERIFY(ExternalUrlOpener::isSafeExternalUrl(QUrl(QStringLiteral("https://decentespresso.com/account"))));
        QVERIFY(ExternalUrlOpener::isSafeExternalUrl(QUrl(QStringLiteral("http://localhost:8080/help"))));
        QVERIFY(!ExternalUrlOpener::isSafeExternalUrl(QUrl(QStringLiteral("javascript:alert(1)"))));
        QVERIFY(!ExternalUrlOpener::isSafeExternalUrl(QUrl(QStringLiteral("file:///tmp/private"))));
        QVERIFY(!ExternalUrlOpener::isSafeExternalUrl(QUrl(QStringLiteral("https://user:secret@example.com/"))));
        QVERIFY(!ExternalUrlOpener::isSafeExternalUrl(QUrl(QStringLiteral("https:///missing-host"))));
    }

    void reservationIsSingleUseAndNativeFixtureDoesNotOpenAnything()
    {
        ExternalUrlOpener opener;
        const QString id = opener.reserve();
        QVERIFY(!id.isEmpty());
        opener.cancel(id);
        QVERIFY(!opener.openReserved(id, QStringLiteral("https://example.com")));
    }
};

QTEST_GUILESS_MAIN(tst_ExternalUrlOpener)
#include "tst_externalurlopener.moc"
