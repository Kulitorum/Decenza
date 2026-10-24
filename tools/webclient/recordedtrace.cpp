#include "recordedtrace.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <algorithm>
#include <cmath>

RecordedTrace::RecordedTrace(QObject* parent) : QObject(parent)
{
    QFile fixture(QStringLiteral(":/fixtures/blooming_aborted.json"));
    if (!fixture.open(QIODevice::ReadOnly)) {
        m_error = fixture.errorString();
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(fixture.readAll(), &parseError);
    const auto root = document.object();
    const auto times = root.value("timeframe").toArray();
    const auto data = root.value("data").toObject();
    const std::array<QString, 6> keys = {
        "espresso_pressure", "espresso_flow", "espresso_temperature_basket",
        "espresso_weight", "espresso_flow_weight_raw", "espresso_temperature_mix"
    };
    std::array<QJsonArray, 6> values;
    const std::array<QJsonArray, 3> goals = {
        data.value("espresso_pressure_goal").toArray(),
        data.value("espresso_flow_goal").toArray(),
        data.value("espresso_temperature_goal").toArray()
    };
    for (const auto& goal : goals) {
        if (goal.size() < times.size()) {
            m_error = QStringLiteral("Recorded trace has fewer goals than timestamps");
            return;
        }
    }
    for (size_t j = 0; j < keys.size(); ++j) {
        values[j] = data.value(keys[j]).toArray();
        // This corpus file has one trailing value per column without a
        // corresponding timestamp. Replay only timestamped samples.
        if (values[j].size() < times.size()) {
            m_error = QStringLiteral("Recorded trace has fewer values than timestamps");
            return;
        }
    }
    if (parseError.error != QJsonParseError::NoError || times.isEmpty()) {
        m_error = QStringLiteral("Recorded trace is not valid JSON with samples");
        return;
    }
    auto number = [](const QJsonValue& value, bool& valid) {
        bool converted = value.isDouble();
        const double result = converted ? value.toDouble() : value.toString().toDouble(&converted);
        valid = valid && converted && std::isfinite(result);
        return result;
    };
    for (int i = 0; i < times.size(); ++i) {
        bool valid = true;
        Sample sample { number(times[i], valid), {}, {} };
        for (size_t j = 0; j < keys.size(); ++j)
            sample.values[j] = number(values[j][i], valid);
        for (size_t j = 0; j < goals.size(); ++j)
            sample.goals[j] = number(goals[j][i], valid);
        if (!valid || sample.time < 0 || (!m_samples.isEmpty() && sample.time < m_samples.last().time)) {
            m_samples.clear();
            m_error = QStringLiteral("Recorded trace contains invalid or unordered samples");
            return;
        }
        m_samples.append(sample);
    }
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &RecordedTrace::advance);
}

void RecordedTrace::attach(FastLineRenderer* pressure, FastLineRenderer* flow,
                           FastLineRenderer* temperature, FastLineRenderer* weight,
                           FastLineRenderer* weightFlow, FastLineRenderer* mix)
{
    m_renderers = {pressure, flow, temperature, weight, weightFlow, mix};
    showAll();
}

void RecordedTrace::clear()
{
    m_timer.stop();
    m_next = 0;
    m_elapsed = 0;
    for (auto& goal : m_goals) goal.clear();
    for (auto& renderer : m_renderers)
        if (renderer) renderer->clear();
}

void RecordedTrace::start()
{
    if (m_samples.isEmpty() ||
        std::any_of(m_renderers.begin(), m_renderers.end(), [](auto& r) { return !r; }))
        return;
    clear();
    m_frameIntervals.clear();
    m_frameP95 = 0;
    m_lastFrameNs = -1;
    m_clock.start();
    m_timer.start();
    advance();
}

void RecordedTrace::showAll()
{
    clear();
    m_elapsed = duration();
    appendUntil(m_elapsed);
    emit changed();
}

void RecordedTrace::appendUntil(double time)
{
    const int previous = m_next;
    while (m_next < m_samples.size() && m_samples[m_next].time <= time) {
        const auto& sample = m_samples[m_next++];
        for (size_t j = 0; j < m_renderers.size(); ++j)
            if (m_renderers[j]) m_renderers[j]->appendPoint(sample.time, sample.values[j]);
        for (size_t j = 0; j < m_goals.size(); ++j)
            m_goals[j].append(QVariant::fromValue(QPointF(sample.time, sample.goals[j])));
    }
    if (previous != m_next || m_next == 0)
        emit samplesChanged();
}

void RecordedTrace::advance()
{
    m_elapsed = std::min(duration(), m_clock.elapsed() / 1000.0);
    appendUntil(m_elapsed);
    // A recorded sample arrives only ~4 times/sec. Request continuous repaint
    // during this benchmark so idle gaps are not mislabeled as slow frames.
    if (m_renderers[0] && m_renderers[0]->window())
        m_renderers[0]->window()->update();
    if (m_elapsed >= duration()) {
        m_timer.stop();
        auto sorted = m_frameIntervals;
        std::sort(sorted.begin(), sorted.end());
        if (!sorted.isEmpty())
            m_frameP95 = sorted[std::max(0, int(std::ceil(sorted.size() * 0.95)) - 1)];
    }
    emit changed();
}

void RecordedTrace::framePresented()
{
    if (!playing()) return;
    const qint64 now = m_clock.nsecsElapsed();
    if (m_lastFrameNs >= 0)
        m_frameIntervals.append((now - m_lastFrameNs) / 1000000.0);
    m_lastFrameNs = now;
}
