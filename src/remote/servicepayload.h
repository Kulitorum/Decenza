#pragma once

#include <QDataStream>
#include <QIODevice>

#include <limits>

// Count the exact QDataStream representation without allocating a second
// serialized copy of a potentially large Qt Remote Objects response.
namespace ServicePayload {

class BoundedCountingDevice final : public QIODevice
{
public:
    explicit BoundedCountingDevice(qint64 maximumBytes)
        : m_maximumBytes(maximumBytes)
    {
        open(QIODevice::WriteOnly);
    }

    bool isSequential() const override { return true; }
    qint64 bytesWritten() const { return m_bytesWritten; }

protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char*, qint64 length) override
    {
        if (length < 0 || length > m_maximumBytes - m_bytesWritten)
            return -1;
        m_bytesWritten += length;
        return length;
    }

private:
    qint64 m_maximumBytes;
    qint64 m_bytesWritten = 0;
};

template<typename T>
qint64 serializedSize(const T& value,
                      qint64 maximumBytes = std::numeric_limits<qint64>::max(),
                      QDataStream::Version version = QDataStream::Qt_DefaultCompiledVersion)
{
    if (maximumBytes < 0)
        return -1;
    BoundedCountingDevice device(maximumBytes);
    QDataStream stream(&device);
    if (version != QDataStream::Qt_DefaultCompiledVersion)
        stream.setVersion(version);
    stream << value;
    return stream.status() == QDataStream::Ok ? device.bytesWritten() : -1;
}

template<typename T>
bool fits(const T& value, qint64 maximumBytes,
          QDataStream::Version version = QDataStream::Qt_DefaultCompiledVersion)
{
    return serializedSize(value, maximumBytes, version) >= 0;
}

} // namespace ServicePayload
