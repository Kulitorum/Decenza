#pragma once

#include "../network/steameditrequest.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class MachineState;
class SettingsBrew;

class SteamPageEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by MainController")
    Q_PROPERTY(QString editorRevision READ editorRevision NOTIFY editorRevisionChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged FINAL)

public:
    explicit SteamPageEditor(SettingsBrew* brew, MachineState* machineState,
                             QObject* parent = nullptr);

    QString editorRevision() const;
    bool busy() const { return m_busy; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE bool setPresetDuration(const QString& requestId, const QString& expectedRevision,
                                       int index, const QString& expectedName, int durationSec);
    Q_INVOKABLE bool setPresetFlow(const QString& requestId, const QString& expectedRevision,
                                   int index, const QString& expectedName, int flow);
    Q_INVOKABLE bool setPresetTemperatureC(const QString& requestId, const QString& expectedRevision,
                                           int index, const QString& expectedName, double temperatureC);
    Q_INVOKABLE bool renamePreset(const QString& requestId, const QString& expectedRevision,
                                  int index, const QString& expectedName, const QString& newName);
    Q_INVOKABLE bool createPreset(const QString& requestId, const QString& expectedRevision,
                                  const QString& name);
    Q_INVOKABLE bool deletePreset(const QString& requestId, const QString& expectedRevision,
                                  int index, const QString& expectedName);
    Q_INVOKABLE bool movePreset(const QString& requestId, const QString& expectedRevision,
                                int fromIndex, const QString& expectedFromName,
                                int toIndex, const QString& expectedToName);
    Q_INVOKABLE bool setPitcherWeight(const QString& requestId, const QString& expectedRevision,
                                      int index, const QString& expectedName, double weightG);
    Q_INVOKABLE bool storeCurrentPitcherWeight(const QString& requestId, const QString& expectedRevision,
                                               int index, const QString& expectedName);
    Q_INVOKABLE bool clearPitcherWeight(const QString& requestId, const QString& expectedRevision,
                                        int index, const QString& expectedName);
    Q_INVOKABLE bool setMilkAutoCaptureEnabled(const QString& requestId, const QString& expectedRevision,
                                               bool enabled);
    Q_INVOKABLE bool setSteamSecondsPerGram(const QString& requestId, const QString& expectedRevision,
                                            double value);
    Q_INVOKABLE bool calibrateFromLastSteam(const QString& requestId, const QString& expectedRevision);

    // ShotServer validates the wire request and calls the same host operation
    // path used by the native QML facade.
    bool dispatch(const SteamEditRequest::Command& command, const QString& expectedRevision,
                  const QString& requestId);

signals:
    void editorRevisionChanged();
    void busyChanged();
    void errorMessageChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                           bool success, const QString& message, const QVariantMap& result,
                           const QString& newRevision);
    void steamFlowCommitRequested(int flow);
    void steamTemperatureCommitRequested(double temperatureC);

private:
    bool submit(const QString& operation, const QJsonObject& fields,
                const QString& requestId, const QString& expectedRevision);
    void finish(const QString& requestId, const QString& operation,
                bool success, const QString& message = {}, const QVariantMap& result = {});
    void setBusy(bool busy);
    void setErrorMessage(const QString& message);

    SettingsBrew* m_brew = nullptr;
    MachineState* m_machineState = nullptr;
    bool m_busy = false;
    QString m_errorMessage;
};
