#include "settings_decent.h"

namespace {
const QString kEmailKey = QStringLiteral("decent/email");
const QString kCryptPwKey = QStringLiteral("decent/cryptpw");
const QString kNeedsSignInKey = QStringLiteral("decent/needsSignIn");
const QString kEnabledKey = QStringLiteral("decent/enabled");
const QString kMachinesKey = QStringLiteral("decent/machines");
const QString kChosenMachineKey = QStringLiteral("decent/chosenMachine");
}

SettingsDecent::SettingsDecent(QObject* parent)
    : QObject(parent)
{
    connect(this, &SettingsDecent::accountChanged, this, &SettingsDecent::activeChanged);
    connect(this, &SettingsDecent::enabledChanged, this, &SettingsDecent::activeChanged);
}

bool SettingsDecent::active() const {
    return enabled() && linked() && !needsSignIn();
}

QString SettingsDecent::email() const {
    return m_settings.value(kEmailKey, QString()).toString();
}

QString SettingsDecent::encryptedPassword() const {
    return m_settings.value(kCryptPwKey, QString()).toString();
}

bool SettingsDecent::linked() const {
    return !email().isEmpty() && !encryptedPassword().isEmpty();
}

bool SettingsDecent::needsSignIn() const {
    return m_settings.value(kNeedsSignInKey, false).toBool();
}

void SettingsDecent::setAccount(const QString& email, const QString& encryptedPassword) {
    m_settings.setValue(kEmailKey, email);
    m_settings.setValue(kCryptPwKey, encryptedPassword);
    m_settings.remove(kNeedsSignInKey);
    clearMachines();
    emit accountChanged();
}

void SettingsDecent::clearAccount() {
    m_settings.remove(kEmailKey);
    m_settings.remove(kCryptPwKey);
    m_settings.remove(kNeedsSignInKey);
    clearMachines();
    emit accountChanged();
}

void SettingsDecent::clearMachines() {
    if (!m_settings.contains(kMachinesKey) && !m_settings.contains(kChosenMachineKey)) return;
    m_settings.remove(kMachinesKey);
    m_settings.remove(kChosenMachineKey);
    emit machinesChanged();
}

QStringList SettingsDecent::registeredMachines() const {
    return m_settings.value(kMachinesKey).toStringList();
}

void SettingsDecent::setRegisteredMachines(const QStringList& lines) {
    m_settings.setValue(kMachinesKey, lines);
    emit machinesChanged();
}

QString SettingsDecent::chosenMachine() const {
    return m_settings.value(kChosenMachineKey).toString();
}

void SettingsDecent::setChosenMachine(const QString& serial) {
    if (chosenMachine() == serial) return;
    m_settings.setValue(kChosenMachineKey, serial);
    emit machinesChanged();
}

void SettingsDecent::setNeedsSignIn(bool needsSignIn) {
    if (needsSignIn && !linked()) return;
    if (this->needsSignIn() != needsSignIn) {
        m_settings.setValue(kNeedsSignInKey, needsSignIn);
        emit accountChanged();
    }
}

bool SettingsDecent::enabled() const {
    return m_settings.value(kEnabledKey, false).toBool();
}

void SettingsDecent::setEnabled(bool enabled) {
    if (this->enabled() != enabled) {
        m_settings.setValue(kEnabledKey, enabled);
        emit enabledChanged();
    }
}
