#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

// Loffee Labs Bean Base (loffeelabs.com) integration credentials. Split from
// Settings to keep settings.h's transitive-include footprint small, the same
// way every other Settings<Domain> is. Holds the per-user Bean Base API key
// used by BeanBaseClient for authenticated `GET /beans` searches.
//
// Each user supplies their own free key (1 req/3 s, 2,000 beans/day) from
// loffeelabs.com — there is no app-bundled key. The key is a sensitive
// credential: it is intentionally NOT exposed via the MCP settings tools
// (matching how the Visualizer password and AI API keys are handled) and
// must never be logged in plain text.
class SettingsBeanBase : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString beanBaseApiKey READ beanBaseApiKey WRITE setBeanBaseApiKey NOTIFY beanBaseApiKeyChanged)

public:
    explicit SettingsBeanBase(QObject* parent = nullptr);

    QString beanBaseApiKey() const;
    void setBeanBaseApiKey(const QString& key);

signals:
    void beanBaseApiKeyChanged();

private:
    mutable QSettings m_settings;
};
