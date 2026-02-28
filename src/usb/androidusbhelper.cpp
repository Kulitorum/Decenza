#include "usb/androidusbhelper.h"

#ifdef Q_OS_ANDROID

#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>

namespace {
const char* JAVA_CLASS = "io/github/kulitorum/decenza_de1/AndroidUsbSerial";

// Get Android application context via Qt's JNI bridge
QJniObject getContext()
{
    return QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
}
} // anonymous namespace

bool AndroidUsbHelper::hasDevice()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) return false;

    return QJniObject::callStaticMethod<jboolean>(
        JAVA_CLASS, "hasDevice",
        "(Landroid/content/Context;)Z",
        ctx.object());
}

bool AndroidUsbHelper::hasPermission()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) return false;

    return QJniObject::callStaticMethod<jboolean>(
        JAVA_CLASS, "hasPermission",
        "(Landroid/content/Context;)Z",
        ctx.object());
}

void AndroidUsbHelper::requestPermission()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) return;

    QJniObject::callStaticMethod<void>(
        JAVA_CLASS, "requestPermission",
        "(Landroid/content/Context;)V",
        ctx.object());
}

QString AndroidUsbHelper::deviceInfo()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) return {};

    QJniObject result = QJniObject::callStaticObjectMethod(
        JAVA_CLASS, "getDeviceInfo",
        "(Landroid/content/Context;)Ljava/lang/String;",
        ctx.object());

    return result.isValid() ? result.toString() : QString();
}

bool AndroidUsbHelper::open()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) return false;

    return QJniObject::callStaticMethod<jboolean>(
        JAVA_CLASS, "open",
        "(Landroid/content/Context;)Z",
        ctx.object());
}

void AndroidUsbHelper::close()
{
    QJniObject::callStaticMethod<void>(JAVA_CLASS, "close", "()V");
}

bool AndroidUsbHelper::isOpen()
{
    return QJniObject::callStaticMethod<jboolean>(JAVA_CLASS, "isOpen", "()Z");
}

int AndroidUsbHelper::write(const QByteArray& data)
{
    QJniEnvironment env;

    jbyteArray jarray = env->NewByteArray(data.size());
    if (!jarray) return -1;

    env->SetByteArrayRegion(jarray, 0, data.size(),
                            reinterpret_cast<const jbyte*>(data.constData()));

    jint result = QJniObject::callStaticMethod<jint>(
        JAVA_CLASS, "write", "([B)I", jarray);

    env->DeleteLocalRef(jarray);
    return static_cast<int>(result);
}

QByteArray AndroidUsbHelper::readAvailable()
{
    QJniEnvironment env;

    QJniObject result = QJniObject::callStaticObjectMethod(
        JAVA_CLASS, "readAvailable", "()[B");
    if (!result.isValid()) return {};

    jbyteArray jarray = result.object<jbyteArray>();
    if (!jarray) return {};

    jsize len = env->GetArrayLength(jarray);
    if (len <= 0) return {};

    QByteArray data(len, Qt::Uninitialized);
    env->GetByteArrayRegion(jarray, 0, len,
                            reinterpret_cast<jbyte*>(data.data()));
    return data;
}

QString AndroidUsbHelper::lastError()
{
    QJniObject result = QJniObject::callStaticObjectMethod(
        JAVA_CLASS, "getLastError", "()Ljava/lang/String;");

    return result.isValid() ? result.toString() : QStringLiteral("JNI error");
}

#else // !Q_OS_ANDROID — stub implementations

bool AndroidUsbHelper::hasDevice()      { return false; }
bool AndroidUsbHelper::hasPermission()  { return false; }
void AndroidUsbHelper::requestPermission() {}
QString AndroidUsbHelper::deviceInfo()  { return {}; }
bool AndroidUsbHelper::open()           { return false; }
void AndroidUsbHelper::close()          {}
bool AndroidUsbHelper::isOpen()         { return false; }
int AndroidUsbHelper::write(const QByteArray&) { return -1; }
QByteArray AndroidUsbHelper::readAvailable()   { return {}; }
QString AndroidUsbHelper::lastError()   { return QStringLiteral("Not Android"); }

#endif // Q_OS_ANDROID
