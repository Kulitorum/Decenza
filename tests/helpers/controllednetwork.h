#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <cstring>

// Manual replies make retry budgets and out-of-order completions deterministic.
// No request is ever passed to the platform network backend.
class ControlledReply : public QNetworkReply {
public:
    ControlledReply(const QNetworkRequest& request, QObject* parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); open(ReadOnly | Unbuffered);
    }
    void complete(int status, const QByteArray& body, NetworkError error = NoError,
                  const QString& errorText = QStringLiteral("controlled network failure")) {
        bytes = body;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (error != NoError) setError(error, errorText);
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    void abort() override {
        if (!isFinished()) complete(0, {}, OperationCanceledError);
    }
    qint64 bytesAvailable() const override { return bytes.size() - offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* out, qint64 maxSize) override {
        const qint64 count = qMin(maxSize, bytes.size() - offset);
        if (count <= 0) return -1;
        std::memcpy(out, bytes.constData() + offset, size_t(count));
        offset += count;
        return count;
    }
private:
    QByteArray bytes;
    qint64 offset = 0;
};

class ControlledNetwork : public QNetworkAccessManager {
public:
    struct Request {
        QNetworkRequest request;
        Operation operation;
        QByteArray body;
        QPointer<ControlledReply> reply;
    };
    QList<Request> requests;
    void complete(qsizetype index, int status, const QByteArray& body,
                  QNetworkReply::NetworkError error = QNetworkReply::NoError,
                  const QString& text = QStringLiteral("controlled network failure")) {
        Q_ASSERT(index < requests.size() && requests[index].reply);
        requests[index].reply->complete(status, body, error, text);
    }
protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request, QIODevice* outgoing) override {
        auto* reply = new ControlledReply(request, this);
        requests.append({request, op, outgoing ? outgoing->readAll() : QByteArray(), reply});
        return reply;
    }
};
