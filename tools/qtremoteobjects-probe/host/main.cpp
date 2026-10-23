// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
#include "probesource.h"
#include "staticfileserver.h"
#include "websocketiodevice.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QPointer>
#include <QRemoteObjectHost>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketServer>

int main(int argc, char* argv[])
{
    QLoggingCategory::setFilterRules(QStringLiteral("qt.remoteobjects.debug=false"));
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qtro-probe-host"));

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption webRootOption(QStringLiteral("web-root"),
        QStringLiteral("Directory containing the built Wasm client files."),
        QStringLiteral("directory"), QStringLiteral("client-output"));
    parser.addOption(webRootOption);
    parser.process(app);

    StaticFileServer http(parser.value(webRootOption));
    if (!http.listen(8897)) {
        qCritical().noquote() << QStringLiteral("HTTP listener failed: %1").arg(http.errorString());
        return 1;
    }

    QWebSocketServer webSocketServer(QStringLiteral("QtRO Wasm Probe"),
                                     QWebSocketServer::NonSecureMode);
    if (!webSocketServer.listen(QHostAddress::LocalHost, 8898)) {
        qCritical().noquote() << QStringLiteral("WebSocket listener failed: %1")
                                     .arg(webSocketServer.errorString());
        return 1;
    }

    QRemoteObjectHost host;
    host.setHostUrl(QUrl(QStringLiteral("qtro-probe:///edge")),
                    QRemoteObjectHost::AllowExternalRegistration);
    QList<QPointer<QWebSocket>> clientSockets;
    ProbeSource source(&clientSockets);
    if (!host.enableRemoting<ProbeSourceSourceAPI>(&source)) {
        qCritical() << "Could not enable the probe source";
        return 1;
    }
    QObject::connect(&webSocketServer, &QWebSocketServer::newConnection, &host,
                     [&webSocketServer, &host, &clientSockets] {
        while (QWebSocket* socket = webSocketServer.nextPendingConnection()) {
            socket->setParent(&webSocketServer);
            clientSockets.append(socket);
            QObject::connect(socket, &QWebSocket::disconnected, &webSocketServer,
                             [&clientSockets, socket] {
                for (auto it = clientSockets.begin(); it != clientSockets.end();) {
                    if (!*it || it->data() == socket)
                        it = clientSockets.erase(it);
                    else
                        ++it;
                }
            });
            QObject::connect(socket, &QWebSocket::disconnected,
                             socket, &QObject::deleteLater);
            WebSocketIoDevice* device = new WebSocketIoDevice(socket);
            QObject::connect(socket, &QObject::destroyed,
                             device, &QObject::deleteLater);
            host.addHostSideConnection(device);
        }
    });

    qInfo().noquote() << "PROBE host ready http=http://127.0.0.1:8897/ ws=ws://127.0.0.1:8898";
    return app.exec();
}
