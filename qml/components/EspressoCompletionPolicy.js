.pragma library

function isOperationPhase(phase, phaseEnum) {
    return phase === phaseEnum.EspressoPreheating
            || phase === phaseEnum.Preinfusion
            || phase === phaseEnum.Pouring
}

function isHydratablePhase(phase, phaseEnum) {
    // Hydration is for a flow already in progress. Preheat can abort without
    // producing a shot and Ending is shared by other machine operations.
    return phase === phaseEnum.Preinfusion || phase === phaseEnum.Pouring
}

function canHydrate(ready, phase, operationId, phaseEnum) {
    return ready && isHydratablePhase(phase, phaseEnum)
            && Number.isSafeInteger(operationId) && operationId > 0
}

function isCurrentOperation(resultOperationId, currentOperationId) {
    return Number.isSafeInteger(resultOperationId) && resultOperationId > 0
            && resultOperationId === currentOperationId
}

function isCurrentReviewResult(shotId, resultOperationId, currentOperationId) {
    // Zero is the explicit save-failure sentinel. It still belongs to an
    // operation, but the coordinator must not navigate to it.
    return Number.isSafeInteger(shotId) && shotId >= 0
            && isCurrentOperation(resultOperationId, currentOperationId)
}
