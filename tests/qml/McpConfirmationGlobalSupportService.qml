pragma Singleton
import QtQuick

QtObject {
    property bool ready: true
    property QtObject state: QtObject {
        property string crashSubmissionStatus: "prompt"
        property bool crashSubmitting: false
        property string crashIssueUrl: ""
        property string crashError: ""
    }
    property var requests: []
    property int requestSerial: 0
    signal operationCompleted(string requestId, string operation, bool success, var result, string reason)

    function execute(operation: string, args: var): string {
        const requestId = "crash-request-" + (++requestSerial)
        requests = requests.concat([{requestId: requestId, operation: operation, arguments: args}])
        return requestId
    }

    function resetForTest(): void {
        ready = true
        requests = []
        state.crashSubmissionStatus = "prompt"
        state.crashSubmitting = false
        state.crashIssueUrl = ""
        state.crashError = ""
    }
}
