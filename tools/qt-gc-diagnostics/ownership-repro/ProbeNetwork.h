#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QQmlNetworkAccessManagerFactory>
#include <algorithm>
#include <cstring>

// Deliver real QML XMLHttpRequest callbacks without sockets or remote services.
class ProbeReply : public QNetworkReply {
public:
    ProbeReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        open(QIODevice::ReadOnly);
    }
    void abort() override {}
    void deliver(const QByteArray &body)
    {
        m_body = body;
        emit readyRead();
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto size = std::min(maximum, qint64(m_body.size() - m_offset));
        if (!size)
            return -1;
        std::memcpy(data, m_body.constData() + m_offset, size_t(size));
        m_offset += size;
        return size;
    }
private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class ProbeNetworkFactory : public QQmlNetworkAccessManagerFactory {
    class Manager : public QNetworkAccessManager {
    public:
        Manager(ProbeNetworkFactory *factory, QObject *parent)
            : QNetworkAccessManager(parent), factory(factory) {}
    protected:
        QNetworkReply *createRequest(Operation, const QNetworkRequest &request,
                                     QIODevice *) override
        {
            auto *reply = new ProbeReply(request, this);
            factory->reply = reply;
            return reply;
        }
    private:
        ProbeNetworkFactory *factory;
    };
public:
    QPointer<ProbeReply> reply;
    QNetworkAccessManager *create(QObject *parent) override { return new Manager(this, parent); }
};
