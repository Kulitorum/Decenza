#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include "accountlink.h"

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class SettingsDecent;

// The user's decentespresso.com account, linked the way de1app and Decaid do it:
// the password is sent once to login_test, which answers with an encrypted
// password; only that is stored, and every later call sends it as HTTP Basic.
// All Decent API traffic authenticates through applyAuth(), the one place a
// future OAuth bearer token would go.
class DecentAccount : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("DecentAccount is created in C++ and reached via MainController")

    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString email READ email NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)

public:
    enum class State { NotLinked, Linked, NeedsSignIn };
    Q_ENUM(State)

    static constexpr const char* kBaseUrl = "https://decentespresso.com";

    DecentAccount(QNetworkAccessManager* network, SettingsDecent* settings, QObject* parent = nullptr);

    State state() const;
    QString email() const;
    bool busy() const { return m_linkReply != nullptr; }

    // The password is used for this one request and never stored.
    Q_INVOKABLE void link(const QString& email, const QString& password);
    Q_INVOKABLE void unlink();
    // Opens the account's machine page in the browser, signed in through a
    // single-use authenticated redirect; falls back to the plain page.
    Q_INVOKABLE void openAccountInBrowser();

    // Sets the Basic Authorization header. Returns false when no account is linked.
    bool applyAuth(QNetworkRequest& request) const;
    // Any authenticated call that gets HTTP 401 reports it here.
    void reportAuthFailure();

signals:
    void stateChanged();
    void busyChanged();
    void linkFinished(AccountLink::Error error);

private:
    void onLinkFinished();

    QNetworkAccessManager* m_network;
    SettingsDecent* m_settings;
    QPointer<QNetworkReply> m_linkReply;
    QString m_pendingEmail;
};
