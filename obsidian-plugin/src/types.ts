export interface DecenzaSettings {
    serverUrl: string;
}

export const DEFAULT_SETTINGS: DecenzaSettings = {
    serverUrl: "",
};

export interface Point {
    x: number;
    y: number;
}

export interface PhaseMarker {
    time: number;
    label: string;
    frameNumber: number;
    isFlowMode: boolean;
    transitionReason: string;
}

export interface ShotSummary {
    id: number;
    uuid: string;
    timestamp: number;
    profileName: string;
    duration: number;
    finalWeight: number;
    doseWeight: number;
    beanBrand: string;
    beanType: string;
    enjoyment: number;
    hasVisualizerUpload: boolean;
    grinderSetting: string;
    dateTime: string;
}

export interface ShotDetail extends ShotSummary {
    roastDate: string;
    roastLevel: string;
    grinderModel: string;
    drinkTds: number;
    drinkEy: number;
    espressoNotes: string;
    barista: string;
    visualizerId: string;
    visualizerUrl: string;
    debugLog: string;
    profileJson: string;
    temperatureOverride?: number;
    yieldOverride?: number;
    pressure: Point[];
    flow: Point[];
    temperature: Point[];
    temperatureMix: Point[];
    resistance: Point[];
    waterDispensed: Point[];
    pressureGoal: Point[];
    flowGoal: Point[];
    temperatureGoal: Point[];
    weight: Point[];
    phases: PhaseMarker[];
}

export interface ShotCodeBlockConfig {
    id: number;
    charts?: string[];
}

export interface ShotsCodeBlockConfig {
    limit?: number;
    filter?: string;
}
