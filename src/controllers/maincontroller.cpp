#include "maincontroller.h"
#include "../core/settings.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../models/shotdatamodel.h"
#include <QDir>
#include <QStandardPaths>

MainController::MainController(Settings* settings, DE1Device* device,
                               MachineState* machineState, ShotDataModel* shotDataModel,
                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_device(device)
    , m_machineState(machineState)
    , m_shotDataModel(shotDataModel)
{
    // Connect to shot sample updates
    if (m_device) {
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &MainController::onShotSampleReceived);
    }

    // Connect to machine state events
    if (m_machineState) {
        connect(m_machineState, &MachineState::shotStarted,
                this, &MainController::onShotStarted);
        connect(m_machineState, &MachineState::shotEnded,
                this, &MainController::onShotEnded);
    }

    // Load initial profile
    refreshProfiles();
    if (m_settings) {
        loadProfile(m_settings->currentProfile());
    } else {
        loadDefaultProfile();
    }
}

QString MainController::currentProfileName() const {
    return m_currentProfile.title();
}

double MainController::targetWeight() const {
    return m_currentProfile.targetWeight();
}

void MainController::setTargetWeight(double weight) {
    if (m_currentProfile.targetWeight() != weight) {
        m_currentProfile.setTargetWeight(weight);
        if (m_machineState) {
            m_machineState->setTargetWeight(weight);
        }
        emit targetWeightChanged();
    }
}

QVariantList MainController::availableProfiles() const {
    QVariantList result;
    for (const QString& name : m_availableProfiles) {
        result.append(name);
    }
    return result;
}

void MainController::loadProfile(const QString& profileName) {
    QString path = profilesPath() + "/" + profileName + ".json";

    if (QFile::exists(path)) {
        m_currentProfile = Profile::loadFromFile(path);
    } else {
        // Try built-in profiles
        path = ":/profiles/" + profileName + ".json";
        if (QFile::exists(path)) {
            m_currentProfile = Profile::loadFromFile(path);
        } else {
            loadDefaultProfile();
        }
    }

    if (m_settings) {
        m_settings->setCurrentProfile(profileName);
    }

    if (m_machineState) {
        m_machineState->setTargetWeight(m_currentProfile.targetWeight());
    }

    emit currentProfileChanged();
    emit targetWeightChanged();
}

void MainController::refreshProfiles() {
    m_availableProfiles.clear();

    // Scan profiles directory
    QDir profileDir(profilesPath());
    QStringList filters;
    filters << "*.json";

    QStringList files = profileDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        m_availableProfiles.append(file.left(file.length() - 5));  // Remove .json
    }

    // Add built-in profiles
    QDir builtInDir(":/profiles");
    files = builtInDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        if (!m_availableProfiles.contains(name)) {
            m_availableProfiles.append(name);
        }
    }

    emit profilesChanged();
}

void MainController::uploadCurrentProfile() {
    if (m_device && m_device->isConnected()) {
        m_device->uploadProfile(m_currentProfile);
    }
}

void MainController::onShotStarted() {
    m_shotStartTime = 0;
    if (m_shotDataModel) {
        m_shotDataModel->clear();
    }
}

void MainController::onShotEnded() {
    // Could save shot history here
}

void MainController::onShotSampleReceived(const ShotSample& sample) {
    if (!m_shotDataModel) return;

    if (m_shotStartTime == 0) {
        m_shotStartTime = sample.timer;
    }

    double time = sample.timer - m_shotStartTime;
    m_shotDataModel->addSample(time, sample.groupPressure,
                               sample.groupFlow, sample.headTemp);
}

void MainController::onWeightChanged(double weight) {
    if (!m_shotDataModel || !m_machineState) return;

    double time = m_machineState->shotTime();
    // Flow rate would come from scale device
    m_shotDataModel->addWeightSample(time, weight, 0);
}

void MainController::loadDefaultProfile() {
    m_currentProfile = Profile();
    m_currentProfile.setTitle("Default");
    m_currentProfile.setTargetWeight(36.0);

    // Create a simple default profile
    ProfileFrame preinfusion;
    preinfusion.name = "Preinfusion";
    preinfusion.pump = "pressure";
    preinfusion.pressure = 4.0;
    preinfusion.temperature = 93.0;
    preinfusion.seconds = 10.0;
    preinfusion.exitIf = true;
    preinfusion.exitType = "pressure_over";
    preinfusion.exitPressureOver = 3.0;

    ProfileFrame extraction;
    extraction.name = "Extraction";
    extraction.pump = "pressure";
    extraction.pressure = 9.0;
    extraction.temperature = 93.0;
    extraction.seconds = 30.0;

    m_currentProfile.addStep(preinfusion);
    m_currentProfile.addStep(extraction);
    m_currentProfile.setPreinfuseFrameCount(1);
}

QString MainController::profilesPath() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles";

    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return path;
}
