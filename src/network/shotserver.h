#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>

class ShotHistoryStorage;
class DE1Device;

struct PendingRequest {
    QByteArray data;
    qint64 contentLength = -1;
    int headerEnd = -1;
};

class ShotServer : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString url READ url NOTIFY urlChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)

public:
    explicit ShotServer(ShotHistoryStorage* storage, DE1Device* device, QObject* parent = nullptr);
    ~ShotServer();

    bool isRunning() const { return m_server && m_server->isListening(); }
    QString url() const;
    int port() const { return m_port; }
    void setPort(int port);

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();

signals:
    void runningChanged();
    void urlChanged();
    void portChanged();
    void clientConnected(const QString& address);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    void sendResponse(QTcpSocket* socket, int statusCode, const QString& contentType,
                      const QByteArray& body, const QByteArray& extraHeaders = QByteArray());
    void sendJson(QTcpSocket* socket, const QByteArray& json);
    void sendHtml(QTcpSocket* socket, const QString& html);
    void sendFile(QTcpSocket* socket, const QString& path, const QString& contentType);

    QString getLocalIpAddress() const;
    QString generateIndexPage() const;
    QString generateShotListPage() const;
    QString generateShotDetailPage(qint64 shotId) const;
    QString generateComparisonPage(const QList<qint64>& shotIds) const;
    QString generateDebugPage() const;
    QString generateUploadPage() const;
    void handleUpload(QTcpSocket* socket, const QByteArray& request);
    void installApk(const QString& apkPath);

    QTcpServer* m_server = nullptr;
    ShotHistoryStorage* m_storage = nullptr;
    DE1Device* m_device = nullptr;
    int m_port = 8888;
    QHash<QTcpSocket*, PendingRequest> m_pendingRequests;
};
