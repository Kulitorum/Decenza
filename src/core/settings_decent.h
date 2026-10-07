#pragma once

#include <QObject>
#include "appsettings.h"
#include <QString>
#include <QStringList>

// Decent account (decentespresso.com) link and upload switch.
//
// The encrypted password is the token login_test returns, not the user's
// password. It is deliberately not a Q_PROPERTY so QML cannot read it, and
// SettingsSerializer never exports or imports it. Only DecentAccount writes the
// account; QML reads its state from there.
class SettingsDecent : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)

public:
    explicit SettingsDecent(QObject* parent = nullptr);

    QString email() const;
    QString encryptedPassword() const;
    bool linked() const;
    bool needsSignIn() const;

    // Stores a freshly linked account and clears needsSignIn and the machines below.
    void setAccount(const QString& email, const QString& encryptedPassword);
    // Unlink: removes the email, the encrypted password, needsSignIn and the machines below.
    void clearAccount();
    void setNeedsSignIn(bool needsSignIn);

    // The Decent switch on the Shot Upload tab, off by default. When/what to
    // upload is SettingsUpload, shared with Visualizer.
    bool enabled() const;
    void setEnabled(bool enabled);
    // Switched on with a linked account that is not waiting to sign in again.
    bool active() const;

    // The account's espresso machines as /support/api/sn lists them ("serial sku"
    // lines), read at sign-in, and the one the user picked for a DE1 that reports
    // no serial number. Both belong to the account and go with it.
    QStringList registeredMachines() const;
    void setRegisteredMachines(const QStringList& lines);
    QString chosenMachine() const;
    void setChosenMachine(const QString& serial);

signals:
    void accountChanged();
    void enabledChanged();
    void activeChanged();
    void machinesChanged();

private:
    void clearMachines();

    mutable AppSettings m_settings;
};
