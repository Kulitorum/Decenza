#pragma once

#include <QMetaType>
#include <QString>
#include <QVariant>

class MqttServicePolicy final
{
public:
    static bool supportedOperation(const QString& operation)
    {
        return operation == QLatin1String("setPreference")
            || operation == QLatin1String("connectToBroker")
            || operation == QLatin1String("disconnectFromBroker")
            || operation == QLatin1String("publishDiscovery")
            || operation == QLatin1String("newDeviceId");
    }

    static bool supportedPreference(const QString& name)
    {
        return name == QLatin1String("mqttEnabled") || name == QLatin1String("mqttBrokerHost")
            || name == QLatin1String("mqttBrokerPort") || name == QLatin1String("mqttUsername")
            || name == QLatin1String("mqttPassword") || name == QLatin1String("mqttBaseTopic")
            || name == QLatin1String("mqttPublishInterval") || name == QLatin1String("mqttRetainMessages")
            || name == QLatin1String("mqttHomeAssistantDiscovery") || name == QLatin1String("mqttClientId")
            || name == QLatin1String("mqttUseTls") || name == QLatin1String("mqttCaCertificate");
    }

    static bool isBoolPreference(const QString& name)
    {
        return name == QLatin1String("mqttEnabled") || name == QLatin1String("mqttRetainMessages")
            || name == QLatin1String("mqttHomeAssistantDiscovery") || name == QLatin1String("mqttUseTls");
    }

    static bool exactBool(const QVariant& value, bool* out)
    {
        if (value.metaType().id() != QMetaType::Bool) return false;
        *out = value.toBool();
        return true;
    }

    static bool exactString(const QVariant& value, int maxLength, QString* out)
    {
        if (value.metaType().id() != QMetaType::QString) return false;
        const QString text = value.toString();
        if (text.size() > maxLength) return false;
        *out = text;
        return true;
    }

    static bool boundedInt(const QVariant& value, int min, int max, int* out)
    {
        if (value.metaType().id() != QMetaType::Int) return false;
        const int v = value.toInt();
        if (v < min || v > max) return false;
        *out = v;
        return true;
    }

    static bool validPreferenceValue(const QString& name, const QVariant& value)
    {
        bool boolValue = false;
        int intValue = 0;
        QString stringValue;
        if (!supportedPreference(name)) return false;
        if (isBoolPreference(name)) return exactBool(value, &boolValue);
        if (name == QLatin1String("mqttBrokerPort"))
            return boundedInt(value, 1, 65535, &intValue);
        if (name == QLatin1String("mqttPublishInterval"))
            return boundedInt(value, 100, 5000, &intValue)
                && (intValue == 100 || intValue == 500 || intValue == 1000 || intValue == 5000);
        const int limit = name == QLatin1String("mqttCaCertificate") ? 65536
            : name == QLatin1String("mqttPassword") ? 4096 : 256;
        if (!exactString(value, limit, &stringValue)) return false;
        return name != QLatin1String("mqttClientId") || !stringValue.trimmed().isEmpty();
    }
};
