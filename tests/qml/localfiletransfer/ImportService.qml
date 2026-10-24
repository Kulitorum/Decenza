pragma Singleton

import QtQml
import Decenza 1.0

QtObject {
    property int nextId: 0
    property var calls: []

    signal operationCompleted(string requestId, string operation, bool success,
                              var result, string reason)

    function execute(operation: string, args: var): string {
        const requestId = "operation-" + (++nextId)
        calls = calls.concat([{requestId: requestId, operation: operation,
                               args: LocalFileSelection.roundTrip(args)}])
        return requestId
    }
}
