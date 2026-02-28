#include "usb/androidusbscalehelper.h"

#ifdef Q_OS_ANDROID

#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>

namespace {
const char* JAVA_CLASS = "io/github/kulitorum/decenza_de1/AndroidUsbScale";

QJniObject getContext()
{
    return QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
}
} // anonymous namespace

bool AndroidUsbScaleHelper::hasDevice()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) {
        qWarning() << "[USB] Scale JNI: context invalid";
        return false;
    }

    QJniEnvironment env;
    jboolean result = QJniObject::callStaticMethod<jboolean>(
        JAVA_CLASS, "hasDevice",
        "(Landroid/content/Context;)Z",
        ctx.object());

    if (env.checkAndClearExceptions()) {
        qWarning() << "[USB] Scale JNI: EXCEPTION in hasDevice — class" << JAVA_CLASS << "not found?";
        return false;
    }

    return result;
}

bool AndroidUsbScaleHelper::hasPermission()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) return false;

    return QJniObject::callStaticMethod<jboolean>(
        JAVA_CLASS, "hasPermission",
        "(Landroid/content/Context;)Z",
        ctx.object());
}

void AndroidUsbScaleHelper::requestPermission()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) return;

    QJniObject::callStaticMethod<void>(
        JAVA_CLASS, "requestPermission",
        "(Landroid/content/Context;)V",
        ctx.object());
}

QString AndroidUsbScaleHelper::deviceInfo()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) return {};

    QJniObject result = QJniObject::callStaticObjectMethod(
        JAVA_CLASS, "getDeviceInfo",
        "(Landroid/content/Context;)Ljava/lang/String;",
        ctx.object());

    return result.isValid() ? result.toString() : QString();
}

bool AndroidUsbScaleHelper::open()
{
    QJniObject ctx = getContext();
    if (!ctx.isValid()) return false;

    return QJniObject::callStaticMethod<jboolean>(
        JAVA_CLASS, "open",
        "(Landroid/content/Context;)Z",
        ctx.object());
}

void AndroidUsbScaleHelper::close()
{
    QJniObject::callStaticMethod<void>(JAVA_CLASS, "close", "()V");
}

bool AndroidUsbScaleHelper::isOpen()
{
    return QJniObject::callStaticMethod<jboolean>(JAVA_CLASS, "isOpen", "()Z");
}

int AndroidUsbScaleHelper::write(const QByteArray& data)
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

QByteArray AndroidUsbScaleHelper::readAvailable()
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

QString AndroidUsbScaleHelper::lastError()
{
    QJniObject result = QJniObject::callStaticObjectMethod(
        JAVA_CLASS, "getLastError", "()Ljava/lang/String;");

    return result.isValid() ? result.toString() : QStringLiteral("JNI error");
}

#else // !Q_OS_ANDROID — stub implementations

bool AndroidUsbScaleHelper::hasDevice()      { return false; }
bool AndroidUsbScaleHelper::hasPermission()  { return false; }
void AndroidUsbScaleHelper::requestPermission() {}
QString AndroidUsbScaleHelper::deviceInfo()  { return {}; }
bool AndroidUsbScaleHelper::open()           { return false; }
void AndroidUsbScaleHelper::close()          {}
bool AndroidUsbScaleHelper::isOpen()         { return false; }
int AndroidUsbScaleHelper::write(const QByteArray&) { return -1; }
QByteArray AndroidUsbScaleHelper::readAvailable()   { return {}; }
QString AndroidUsbScaleHelper::lastError()   { return QStringLiteral("Not Android"); }

#endif // Q_OS_ANDROID
