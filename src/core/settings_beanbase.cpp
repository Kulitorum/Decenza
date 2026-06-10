#include "settings_beanbase.h"

SettingsBeanBase::SettingsBeanBase(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
}

QString SettingsBeanBase::beanBaseApiKey() const {
    return m_settings.value("beanbase/apiKey", "").toString();
}

void SettingsBeanBase::setBeanBaseApiKey(const QString& key) {
    if (beanBaseApiKey() != key) {
        m_settings.setValue("beanbase/apiKey", key);
        emit beanBaseApiKeyChanged();
    }
}
