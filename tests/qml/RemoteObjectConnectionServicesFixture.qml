import QtQml
import DecenzaConnectionTests

QtObject {
    id: root
    property ProbeService firstProbe: ProbeService {}
    property ProbeService secondProbe: ProbeService {}
    property RemoteObjectConnection connection: RemoteObjectConnection {
        services: [root.firstProbe, root.secondProbe]
    }
}
