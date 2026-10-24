#pragma once

#include "remote/remoteservice.h"
#include "webclient/remotesessionbackend.h"

#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>

class ProbeService : public RemoteService
{
    Q_OBJECT
    QML_ELEMENT
public:
    using RemoteService::RemoteService;

    void setNode(QRemoteObjectNode* value) override
    {
        if (node == value) return;
        if (node && !value) ++detachments;
        node = value;
        if (value) ++attachments;
    }

    QPointer<QRemoteObjectNode> node;
    int attachments = 0;
    int detachments = 0;
};

// The production service base and session backend are compiled into
// decenza_webclienttestlib. Register their existing metaobjects in this test's
// QML module through foreign wrappers rather than compiling their headers into
// the test target a second time.
struct RemoteServiceTestForeign
{
    Q_GADGET
    QML_FOREIGN(RemoteService)
    QML_ANONYMOUS
};

struct RemoteSessionBackendTestForeign
{
    Q_GADGET
    QML_FOREIGN(RemoteSessionBackend)
    QML_NAMED_ELEMENT(RemoteSessionBackend)
};
