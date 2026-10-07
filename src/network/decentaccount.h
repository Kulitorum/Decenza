#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include "accountlink.h"

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class SettingsDecent;

// The user's decentespresso.com account, linked the way de1app and Decaid do it:
// the password is sent once to login_test, which answers with an encrypted
// password; only that is stored, and every later call sends it as HTTP Basic
// through applyAuth(), the one place a future OAuth bearer token would go.
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
    // The page a signed-in owner's machines and shots live on.
    static constexpr const char* kAccountPath = "/support/espressomachine";
    // Without one, Qt waits forever on a stalled connection (no default timeout,
    // qnetworkrequest.cpp), leaving Connect stuck "busy". decentespresso.com took
    // 33-38 s to answer login_test and its home page on 2026-10-04 (from a Mac).
    static constexpr int kTransferTimeoutMs = 60000;

    DecentAccount(QNetworkAccessManager* network, SettingsDecent* settings, QObject* parent = nullptr);

    State state() const;
    QString email() const;
    // Uploads switched on, the account linked and signed in.
    bool uploadsActive() const;
    bool busy() const { return m_linkReply != nullptr; }

    // The password is used for this one request and never stored. Always ends
    // with linkFinished, Cancelled if unlink() interrupts it.
    Q_INVOKABLE void link(const QString& email, const QString& password);
    Q_INVOKABLE void unlink();
    // Opens the account's machine page in the browser, signed in through a
    // single-use authenticated redirect; falls back to the plain page.
    Q_INVOKABLE void openAccountInBrowser();

    // Sets the Basic Authorization header. Returns false unless the account is
    // Linked (not when the server has refused the stored credentials).
    bool applyAuth(QNetworkRequest& request) const;
    // Any authenticated call that gets HTTP 401 reports it here.
    void reportAuthFailure();

    // Older DE1s report serial 0. Their shots are filed under a DE1 from the
    // account's machine list, read at sign-in, as Decaid's
    // LegacyDe1IdentityResolver does: the one the user chose, the account's only
    // DE1, or the only one of the machine's model (DE1Device::machineModel()).
    // Otherwise `serial` is empty and `choices` lists the DE1s to choose from.
    struct UnreportedSerial {
        QString serial;
        QStringList choices;
        QStringList labels;  // "serial · SKU" (the serial alone without one), for the user
    };
    static UnreportedSerial resolveUnreportedSerial(const QStringList& machines, int machineModel,
                                                    const QString& chosen);
    // The SKU's model as DE1Device::machineModel() numbers it; 0 when it is not a DE1.
    static int skuModel(const QString& sku);
    QString serialForUnreportedMachine(int machineModel) const;
    // The connected DE1 reports no serial. Logs what it will be filed under and,
    // once per app run and sign-in, asks the user (machineChoiceNeeded) if that is not settled.
    void machineReportsNoSerial(int machineModel);
    Q_INVOKABLE void chooseMachine(const QString& serial);

signals:
    void stateChanged();
    void busyChanged();
    void linkFinished(AccountLink::Error error);
    // The machine list or the user's choice changed.
    void machinesChanged();
    void machineChoiceNeeded(const QStringList& serials, const QStringList& labels);

private:
    void onLinkFinished();
    void fetchMachines();
    void onMachinesFinished(QNetworkReply* reply);

    QNetworkAccessManager* m_network;
    SettingsDecent* m_settings;
    QPointer<QNetworkReply> m_linkReply;
    QPointer<QNetworkReply> m_machinesReply;
    QElapsedTimer m_machinesTimer;
    bool m_unreportedSerialNoted = false;
    QElapsedTimer m_linkTimer;
    QString m_pendingEmail;
};
