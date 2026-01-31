import { requestUrl } from "obsidian";
import { ShotSummary, ShotDetail } from "./types";

export class DecenzaApi {
    private baseUrl: string;

    constructor(baseUrl: string) {
        this.baseUrl = baseUrl.replace(/\/+$/, "");
    }

    setBaseUrl(url: string): void {
        this.baseUrl = url.replace(/\/+$/, "");
    }

    async testConnection(): Promise<boolean> {
        try {
            const response = await requestUrl({
                url: `${this.baseUrl}/api/shots`,
                method: "GET",
            });
            return response.status === 200;
        } catch {
            return false;
        }
    }

    async getShots(): Promise<ShotSummary[]> {
        const response = await requestUrl({
            url: `${this.baseUrl}/api/shots`,
            method: "GET",
        });
        return response.json as ShotSummary[];
    }

    async getShot(id: number): Promise<ShotDetail> {
        const response = await requestUrl({
            url: `${this.baseUrl}/api/shot/${id}`,
            method: "GET",
        });
        return response.json as ShotDetail;
    }
}
