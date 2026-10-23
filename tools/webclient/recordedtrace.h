#pragma once

#include "rendering/fastlinerenderer.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <array>

// Recorded rendering fixture only; no simulator, machine logic, or persistence.
class RecordedTrace : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool playing READ playing NOTIFY changed FINAL)
    Q_PROPERTY(double elapsed READ elapsed NOTIFY changed FINAL)
    Q_PROPERTY(double duration READ duration CONSTANT FINAL)
    Q_PROPERTY(int sampleCount READ sampleCount CONSTANT FINAL)
    Q_PROPERTY(QString error READ error CONSTANT FINAL)
    Q_PROPERTY(double frameP95Ms READ frameP95Ms NOTIFY changed FINAL)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList pressureGoalPoints READ pressureGoalPoints NOTIFY samplesChanged FINAL)
    Q_PROPERTY(QVariantList flowGoalPoints READ flowGoalPoints NOTIFY samplesChanged FINAL)
    Q_PROPERTY(QVariantList temperatureGoalPoints READ temperatureGoalPoints NOTIFY samplesChanged FINAL)
public:
    explicit RecordedTrace(QObject* parent = nullptr);
    bool playing() const { return m_timer.isActive(); }
    double elapsed() const { return m_elapsed; }
    double duration() const { return m_samples.isEmpty() ? 0 : m_samples.last().time; }
    int sampleCount() const { return m_samples.size(); }
    QString error() const { return m_error; }
    double frameP95Ms() const { return m_frameP95; }
    int frameCount() const { return m_frameIntervals.size(); }
    QVariantList pressureGoalPoints() const { return m_goals[0]; }
    QVariantList flowGoalPoints() const { return m_goals[1]; }
    QVariantList temperatureGoalPoints() const { return m_goals[2]; }
    Q_INVOKABLE void attach(FastLineRenderer* pressure, FastLineRenderer* flow,
                           FastLineRenderer* temperature, FastLineRenderer* weight,
                           FastLineRenderer* weightFlow, FastLineRenderer* mix);
    Q_INVOKABLE void start();
    Q_INVOKABLE void showAll();
    Q_INVOKABLE void framePresented();
signals:
    void changed();
    void samplesChanged();
private:
    struct Sample { double time; std::array<double, 6> values; std::array<double, 3> goals; };
    void advance();
    void appendUntil(double time);
    void clear();
    QVector<Sample> m_samples;
    std::array<QVariantList, 3> m_goals;
    std::array<QPointer<FastLineRenderer>, 6> m_renderers;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QVector<double> m_frameIntervals;
    qint64 m_lastFrameNs = -1;
    int m_next = 0;
    double m_elapsed = 0;
    double m_frameP95 = 0;
    QString m_error;
};
