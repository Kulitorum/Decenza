pragma Singleton

import QtQml

QtObject {
    signal historyCompleted(string requestId, string operation, bool success,
                            var result, string reason)

    function requestHistory(requestId, operation, fields) {
        // The smoke test verifies that the production component can resolve
        // its host contract. It does not request or synthesize history data.
    }
}
