#include <QtGlobal>  // For Q_OS_ANDROID definition

#ifdef Q_OS_ANDROID

#include "androidscalebletransport.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJniEnvironment>
#include <QJniObject>
#include <jni.h>

// Helper macro that logs to both qDebug and emits signal for UI/file logging
#define BLE_LOG(msg) do { \
    QString _msg = QString("[BLE] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

// JNI callbacks - these are called from Java on the main thread
// The 'ptr' parameter is the AndroidScaleBleTransport* pointer

extern "C" {

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnConnected(
    JNIEnv* env, jobject obj, jlong ptr)
{
    Q_UNUSED(env)
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        transport->onConnected();
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnDisconnected(
    JNIEnv* env, jobject obj, jlong ptr)
{
    Q_UNUSED(env)
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        transport->onDisconnected();
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnServiceDiscovered(
    JNIEnv* env, jobject obj, jlong ptr, jstring serviceUuid)
{
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        const char* uuid = env->GetStringUTFChars(serviceUuid, nullptr);
        transport->onServiceDiscovered(QString::fromUtf8(uuid));
        env->ReleaseStringUTFChars(serviceUuid, uuid);
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnServicesDiscoveryFinished(
    JNIEnv* env, jobject obj, jlong ptr)
{
    Q_UNUSED(env)
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        transport->onServicesDiscoveryFinished();
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnCharacteristicDiscovered(
    JNIEnv* env, jobject obj, jlong ptr, jstring serviceUuid, jstring charUuid, jint properties)
{
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        const char* sUuid = env->GetStringUTFChars(serviceUuid, nullptr);
        const char* cUuid = env->GetStringUTFChars(charUuid, nullptr);
        transport->onCharacteristicDiscovered(QString::fromUtf8(sUuid),
                                              QString::fromUtf8(cUuid),
                                              static_cast<int>(properties));
        env->ReleaseStringUTFChars(serviceUuid, sUuid);
        env->ReleaseStringUTFChars(charUuid, cUuid);
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnCharacteristicsDiscoveryFinished(
    JNIEnv* env, jobject obj, jlong ptr, jstring serviceUuid)
{
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        const char* uuid = env->GetStringUTFChars(serviceUuid, nullptr);
        transport->onCharacteristicsDiscoveryFinished(QString::fromUtf8(uuid));
        env->ReleaseStringUTFChars(serviceUuid, uuid);
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnCharacteristicChanged(
    JNIEnv* env, jobject obj, jlong ptr, jstring charUuid, jbyteArray value)
{
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        const char* uuid = env->GetStringUTFChars(charUuid, nullptr);

        QByteArray data;
        if (value != nullptr) {
            jsize len = env->GetArrayLength(value);
            jbyte* bytes = env->GetByteArrayElements(value, nullptr);
            data = QByteArray(reinterpret_cast<const char*>(bytes), len);
            env->ReleaseByteArrayElements(value, bytes, JNI_ABORT);
        }

        transport->onCharacteristicChanged(QString::fromUtf8(uuid), data);
        env->ReleaseStringUTFChars(charUuid, uuid);
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnCharacteristicRead(
    JNIEnv* env, jobject obj, jlong ptr, jstring charUuid, jbyteArray value)
{
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        const char* uuid = env->GetStringUTFChars(charUuid, nullptr);

        QByteArray data;
        if (value != nullptr) {
            jsize len = env->GetArrayLength(value);
            jbyte* bytes = env->GetByteArrayElements(value, nullptr);
            data = QByteArray(reinterpret_cast<const char*>(bytes), len);
            env->ReleaseByteArrayElements(value, bytes, JNI_ABORT);
        }

        transport->onCharacteristicRead(QString::fromUtf8(uuid), data);
        env->ReleaseStringUTFChars(charUuid, uuid);
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnCharacteristicWritten(
    JNIEnv* env, jobject obj, jlong ptr, jstring charUuid)
{
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        const char* uuid = env->GetStringUTFChars(charUuid, nullptr);
        transport->onCharacteristicWritten(QString::fromUtf8(uuid));
        env->ReleaseStringUTFChars(charUuid, uuid);
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnNotificationsEnabled(
    JNIEnv* env, jobject obj, jlong ptr, jstring charUuid)
{
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        const char* uuid = env->GetStringUTFChars(charUuid, nullptr);
        transport->onNotificationsEnabled(QString::fromUtf8(uuid));
        env->ReleaseStringUTFChars(charUuid, uuid);
    }
}

JNIEXPORT void JNICALL
Java_io_github_kulitorum_decenza_1de1_ScaleBleManager_nativeOnError(
    JNIEnv* env, jobject obj, jlong ptr, jstring message)
{
    Q_UNUSED(obj)
    auto* transport = reinterpret_cast<AndroidScaleBleTransport*>(ptr);
    if (transport) {
        const char* msg = env->GetStringUTFChars(message, nullptr);
        transport->onError(QString::fromUtf8(msg));
        env->ReleaseStringUTFChars(message, msg);
    }
}

} // extern "C"


AndroidScaleBleTransport::AndroidScaleBleTransport(QObject* parent)
    : ScaleBleTransport(parent)
{
    // Note: Can't use BLE_LOG here as signals aren't connected yet
    qDebug() << "[BLE] AndroidScaleBleTransport: Creating instance";

    // Get the Android context via JNI (works on all Qt 6 versions)
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "context",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        qDebug() << "[BLE] context() failed, trying activity()";
        // Fallback: try getting activity instead
        context = QJniObject::callStaticObjectMethod(
            "org/qtproject/qt/android/QtNative",
            "activity",
            "()Landroid/app/Activity;");
    }

    if (!context.isValid()) {
        qWarning() << "[BLE] Failed to get Android context!";
        return;
    }

    qDebug() << "[BLE] Got Android context, creating ScaleBleManager";

    // Create the Java ScaleBleManager instance
    m_javaBleManager = QJniObject(
        "io/github/kulitorum/decenza_de1/ScaleBleManager",
        "(Landroid/content/Context;J)V",
        context.object<jobject>(),
        reinterpret_cast<jlong>(this));

    if (!m_javaBleManager.isValid()) {
        qWarning() << "[BLE] Failed to create ScaleBleManager Java object!";
    } else {
        qDebug() << "[BLE] ScaleBleManager created successfully";
    }
}

AndroidScaleBleTransport::~AndroidScaleBleTransport() {
    qDebug() << "[BLE] AndroidScaleBleTransport: Destroying instance";
    // Call release() on Java side to invalidate native pointer before destruction
    // This prevents any pending callbacks from calling back to this destroyed object
    if (m_javaBleManager.isValid()) {
        m_javaBleManager.callMethod<void>("release");
    }
    m_connected = false;
}

void AndroidScaleBleTransport::connectToDevice(const QString& address, const QString& name) {
    BLE_LOG(QString("connectToDevice: %1 at %2").arg(name, address));

    if (!m_javaBleManager.isValid()) {
        BLE_LOG("ERROR: Java BLE manager not initialized!");
        emit error("Java BLE manager not initialized");
        return;
    }

    QJniObject jAddress = QJniObject::fromString(address);
    BLE_LOG("Calling Java connectToDevice()");
    m_javaBleManager.callMethod<void>("connectToDevice",
                                       "(Ljava/lang/String;)V",
                                       jAddress.object<jstring>());
}

void AndroidScaleBleTransport::disconnectFromDevice() {
    if (m_javaBleManager.isValid()) {
        m_javaBleManager.callMethod<void>("disconnectDevice");
    }
    m_connected = false;
}

void AndroidScaleBleTransport::discoverServices() {
    BLE_LOG("discoverServices called");
    if (m_javaBleManager.isValid()) {
        BLE_LOG("Calling Java discoverServices()");
        m_javaBleManager.callMethod<void>("discoverServices");
    } else {
        BLE_LOG("ERROR: discoverServices - Java manager invalid!");
    }
}

void AndroidScaleBleTransport::discoverCharacteristics(const QBluetoothUuid& serviceUuid) {
    // On Android, characteristics are discovered together with services
    // Emit the finished signal immediately since they're already available
    BLE_LOG(QString("discoverCharacteristics called for %1").arg(serviceUuid.toString()));
    BLE_LOG("Emitting characteristicsDiscoveryFinished (Android discovers chars with services)");
    emit characteristicsDiscoveryFinished(serviceUuid);
}

void AndroidScaleBleTransport::enableNotifications(const QBluetoothUuid& serviceUuid,
                                                   const QBluetoothUuid& characteristicUuid) {
    BLE_LOG(QString("enableNotifications - Service: %1, Char: %2")
            .arg(serviceUuid.toString(), characteristicUuid.toString()));

    if (!m_javaBleManager.isValid()) {
        BLE_LOG("ERROR: enableNotifications - Java manager invalid!");
        emit error("Java BLE manager not initialized");
        return;
    }

    QJniObject jServiceUuid = QJniObject::fromString(serviceUuid.toString(QUuid::WithoutBraces));
    QJniObject jCharUuid = QJniObject::fromString(characteristicUuid.toString(QUuid::WithoutBraces));
    BLE_LOG("Calling Java enableNotifications()");

    m_javaBleManager.callMethod<void>("enableNotifications",
                                       "(Ljava/lang/String;Ljava/lang/String;)V",
                                       jServiceUuid.object<jstring>(),
                                       jCharUuid.object<jstring>());
}

void AndroidScaleBleTransport::writeCharacteristic(const QBluetoothUuid& serviceUuid,
                                                   const QBluetoothUuid& characteristicUuid,
                                                   const QByteArray& data,
                                                   WriteType writeType) {
    if (!m_javaBleManager.isValid()) {
        BLE_LOG("ERROR: writeCharacteristic - Java manager invalid!");
        emit error("Java BLE manager not initialized");
        return;
    }

    BLE_LOG(QString("writeCharacteristic: %1 data=%2 writeType=%3")
            .arg(characteristicUuid.toString(QUuid::WithoutBraces))
            .arg(QString(data.toHex(' ')))
            .arg(writeType == WriteType::WithResponse ? "WithResponse" : "NoResponse"));

    QJniObject jServiceUuid = QJniObject::fromString(serviceUuid.toString(QUuid::WithoutBraces));
    QJniObject jCharUuid = QJniObject::fromString(characteristicUuid.toString(QUuid::WithoutBraces));

    // Create Java byte array
    QJniEnvironment env;
    jbyteArray jData = env->NewByteArray(data.size());
    env->SetByteArrayRegion(jData, 0, data.size(),
                            reinterpret_cast<const jbyte*>(data.constData()));

    // Pass write type as int (matches BluetoothGattCharacteristic.WRITE_TYPE_* constants)
    jint jWriteType = static_cast<jint>(writeType);

    m_javaBleManager.callMethod<void>("writeCharacteristic",
                                       "(Ljava/lang/String;Ljava/lang/String;[BI)V",
                                       jServiceUuid.object<jstring>(),
                                       jCharUuid.object<jstring>(),
                                       jData,
                                       jWriteType);

    env->DeleteLocalRef(jData);
}

void AndroidScaleBleTransport::readCharacteristic(const QBluetoothUuid& serviceUuid,
                                                  const QBluetoothUuid& characteristicUuid) {
    if (!m_javaBleManager.isValid()) {
        emit error("Java BLE manager not initialized");
        return;
    }

    QJniObject jServiceUuid = QJniObject::fromString(serviceUuid.toString(QUuid::WithoutBraces));
    QJniObject jCharUuid = QJniObject::fromString(characteristicUuid.toString(QUuid::WithoutBraces));

    m_javaBleManager.callMethod<void>("readCharacteristic",
                                       "(Ljava/lang/String;Ljava/lang/String;)V",
                                       jServiceUuid.object<jstring>(),
                                       jCharUuid.object<jstring>());
}

bool AndroidScaleBleTransport::isConnected() const {
    return m_connected;
}

// JNI callback handlers

void AndroidScaleBleTransport::onConnected() {
    BLE_LOG("Connected!");
    m_connected = true;
    emit connected();
}

void AndroidScaleBleTransport::onDisconnected() {
    BLE_LOG("Disconnected");
    m_connected = false;
    emit disconnected();
}

void AndroidScaleBleTransport::onServiceDiscovered(const QString& serviceUuid) {
    BLE_LOG(QString("Service discovered: %1").arg(serviceUuid));
    emit serviceDiscovered(QBluetoothUuid(serviceUuid));
}

void AndroidScaleBleTransport::onServicesDiscoveryFinished() {
    BLE_LOG("Services discovery finished");
    emit servicesDiscoveryFinished();
}

void AndroidScaleBleTransport::onCharacteristicDiscovered(const QString& serviceUuid,
                                                          const QString& charUuid,
                                                          int properties) {
    BLE_LOG(QString("Characteristic discovered: %1 in service %2 (props: %3)")
            .arg(charUuid, serviceUuid).arg(properties));
    emit characteristicDiscovered(QBluetoothUuid(serviceUuid),
                                  QBluetoothUuid(charUuid),
                                  properties);
}

void AndroidScaleBleTransport::onCharacteristicsDiscoveryFinished(const QString& serviceUuid) {
    BLE_LOG(QString("Characteristics discovery finished for: %1").arg(serviceUuid));
    emit characteristicsDiscoveryFinished(QBluetoothUuid(serviceUuid));
}

void AndroidScaleBleTransport::onCharacteristicChanged(const QString& charUuid,
                                                       const QByteArray& value) {
    // Don't log every weight update - too noisy
    emit characteristicChanged(QBluetoothUuid(charUuid), value);
}

void AndroidScaleBleTransport::onCharacteristicRead(const QString& charUuid,
                                                    const QByteArray& value) {
    BLE_LOG(QString("Characteristic read: %1").arg(charUuid));
    emit characteristicRead(QBluetoothUuid(charUuid), value);
}

void AndroidScaleBleTransport::onCharacteristicWritten(const QString& charUuid) {
    BLE_LOG(QString("Characteristic written: %1").arg(charUuid));
    emit characteristicWritten(QBluetoothUuid(charUuid));
}

void AndroidScaleBleTransport::onNotificationsEnabled(const QString& charUuid) {
    BLE_LOG(QString("Notifications enabled for: %1").arg(charUuid));
    emit notificationsEnabled(QBluetoothUuid(charUuid));
}

void AndroidScaleBleTransport::onError(const QString& message) {
    BLE_LOG(QString("ERROR: %1").arg(message));
    emit error(message);
}

#endif // Q_OS_ANDROID
