import { ItemView, WorkspaceLeaf } from "obsidian";
import type DecenzaPlugin from "../main";
import { ShotSummary } from "../types";

export const VIEW_TYPE_SHOT_LIST = "decenza-shot-list";

export class ShotListView extends ItemView {
    plugin: DecenzaPlugin;
    private shots: ShotSummary[] = [];
    private searchQuery = "";

    constructor(leaf: WorkspaceLeaf, plugin: DecenzaPlugin) {
        super(leaf);
        this.plugin = plugin;
    }

    getViewType(): string {
        return VIEW_TYPE_SHOT_LIST;
    }

    getDisplayText(): string {
        return "Decenza Shots";
    }

    getIcon(): string {
        return "coffee";
    }

    async onOpen(): Promise<void> {
        this.buildUI();
        await this.refresh();
    }

    private buildUI(): void {
        const container = this.containerEl.children[1] as HTMLElement;
        container.empty();
        container.addClass("decenza-sidebar");

        const searchRow = container.createDiv({ cls: "decenza-search-row" });
        const searchInput = searchRow.createEl("input", {
            type: "text",
            placeholder: "Filter shots...",
            cls: "decenza-search-input",
        });
        searchInput.addEventListener("input", () => {
            this.searchQuery = searchInput.value;
            this.renderShotList();
        });

        const refreshBtn = searchRow.createEl("button", {
            text: "Refresh",
            cls: "decenza-refresh-btn",
        });
        refreshBtn.addEventListener("click", () => this.refresh());

        container.createDiv({ cls: "decenza-shot-list-container" });
    }

    async refresh(): Promise<void> {
        if (!this.plugin.settings.serverUrl) {
            this.showMessage("Server URL not configured. Go to Settings.");
            return;
        }
        try {
            this.shots = await this.plugin.api.getShots();
            this.renderShotList();
        } catch (err) {
            this.showMessage(`Connection failed: ${err}`);
        }
    }

    private showMessage(text: string): void {
        const container = this.containerEl.children[1] as HTMLElement;
        const listEl = container.querySelector(
            ".decenza-shot-list-container"
        ) as HTMLElement | null;
        if (!listEl) return;
        listEl.empty();
        listEl.createEl("p", { text, cls: "decenza-empty" });
    }

    private renderShotList(): void {
        const container = this.containerEl.children[1] as HTMLElement;
        const listEl = container.querySelector(
            ".decenza-shot-list-container"
        ) as HTMLElement | null;
        if (!listEl) return;
        listEl.empty();

        let filtered = this.shots;
        if (this.searchQuery) {
            const q = this.searchQuery.toLowerCase();
            filtered = this.shots.filter(
                (s) =>
                    s.profileName.toLowerCase().includes(q) ||
                    s.beanBrand.toLowerCase().includes(q) ||
                    s.beanType.toLowerCase().includes(q) ||
                    s.dateTime.includes(q) ||
                    s.grinderSetting.toLowerCase().includes(q)
            );
        }

        if (filtered.length === 0) {
            listEl.createEl("p", {
                text: this.searchQuery
                    ? "No shots match filter."
                    : "No shots found.",
                cls: "decenza-empty",
            });
            return;
        }

        for (const shot of filtered) {
            const item = listEl.createDiv({ cls: "decenza-shot-item" });
            const ratio =
                shot.doseWeight > 0
                    ? (shot.finalWeight / shot.doseWeight).toFixed(1)
                    : "-";
            const bean =
                [shot.beanBrand, shot.beanType].filter(Boolean).join(" ") ||
                "-";

            item.createEl("div", {
                text: shot.dateTime,
                cls: "decenza-shot-date",
            });
            item.createEl("div", {
                text: shot.profileName,
                cls: "decenza-shot-profile",
            });
            item.createEl("div", {
                text: `${bean} | 1:${ratio} | ${shot.doseWeight.toFixed(1)}g \u2192 ${shot.finalWeight.toFixed(1)}g`,
                cls: "decenza-shot-detail",
            });

            item.addEventListener("click", () => {
                const editor =
                    this.app.workspace.activeEditor?.editor;
                if (editor) {
                    const cursor = editor.getCursor();
                    editor.replaceRange(
                        `\n\`\`\`decenza-shot\n${shot.id}\n\`\`\`\n`,
                        cursor
                    );
                }
            });
        }
    }
}
