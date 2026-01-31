import { SuggestModal, App } from "obsidian";
import type DecenzaPlugin from "../main";
import { ShotSummary } from "../types";

export class ShotPickerModal extends SuggestModal<ShotSummary> {
    plugin: DecenzaPlugin;
    private shots: ShotSummary[] = [];
    private onChooseCallback: (shot: ShotSummary) => void;

    constructor(
        app: App,
        plugin: DecenzaPlugin,
        onChoose: (shot: ShotSummary) => void
    ) {
        super(app);
        this.plugin = plugin;
        this.onChooseCallback = onChoose;
        this.setPlaceholder("Search shots by profile, bean, date...");
        this.loadShots();
    }

    private async loadShots(): Promise<void> {
        try {
            this.shots = await this.plugin.api.getShots();
            // Force re-render of the suggestion list
            this.inputEl.dispatchEvent(new Event("input"));
        } catch {
            // Will show empty
        }
    }

    getSuggestions(query: string): ShotSummary[] {
        if (!query) return this.shots.slice(0, 50);
        const q = query.toLowerCase();
        return this.shots
            .filter(
                (s) =>
                    s.profileName.toLowerCase().includes(q) ||
                    s.beanBrand.toLowerCase().includes(q) ||
                    s.beanType.toLowerCase().includes(q) ||
                    s.dateTime.includes(q) ||
                    s.grinderSetting.toLowerCase().includes(q) ||
                    String(s.id).includes(q)
            )
            .slice(0, 50);
    }

    renderSuggestion(shot: ShotSummary, el: HTMLElement): void {
        const ratio =
            shot.doseWeight > 0
                ? (shot.finalWeight / shot.doseWeight).toFixed(1)
                : "-";
        const bean =
            [shot.beanBrand, shot.beanType].filter(Boolean).join(" ") || "";

        el.createEl("div", {
            text: `${shot.dateTime} \u2014 ${shot.profileName}`,
            cls: "decenza-suggest-title",
        });
        el.createEl("small", {
            text: `${bean} | 1:${ratio} | ${shot.doseWeight.toFixed(1)}g \u2192 ${shot.finalWeight.toFixed(1)}g | #${shot.id}`,
            cls: "decenza-suggest-detail",
        });
    }

    onChooseSuggestion(shot: ShotSummary): void {
        this.onChooseCallback(shot);
    }
}
