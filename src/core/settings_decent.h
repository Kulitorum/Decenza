#pragma once

#include <QObject>
#include "appsettings.h"
#include <QString>

// Decent account (decentespresso.com) link and upload switch.
//
// The encrypted password is the token login_test returns, not the user's
// password. It is deliberately not a Q_PROPERTY so QML cannot read it, and
// SettingsSerializer never exports or imports it.
class SettingsDecent : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString email READ email NOTIFY accountChanged FINAL)
    Q_PROPERTY(bool linked READ linked NOTIFY accountChanged FINAL)
    Q_PROPERTY(bool needsSignIn READ needsSignIn NOTIFY accountChanged FINAL)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)

public:
    explicit SettingsDecent(QObject* parent = nullptr);

    QString email() const;
    QString encryptedPassword() const;
    bool linked() const;
    bool needsSignIn() const;

    // Stores a freshly linked account and clears needsSignIn.
    void setAccount(const QString& email, const QString& encryptedPassword);
    // Unlink: removes the email, the encrypted password and needsSignIn.
    void clearAccount();
    void setNeedsSignIn(bool needsSignIn);

    // The Decent switch on the Shot Upload tab, off by default. When/what to
    // upload is SettingsUpload, shared with Visualizer.
    bool enabled() const;
    void setEnabled(bool enabled);
    // Switched on with a linked account that is not waiting to sign in again.
    bool active() const;

signals:
    void accountChanged();
    void enabledChanged();
    void activeChanged();

private:
    mutable AppSettings m_settings;
};
