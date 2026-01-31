import { Plugin } from "obsidian";
import { DecenzaSettings, DEFAULT_SETTINGS } from "./types";
import { DecenzaApi } from "./api";
import { DecenzaSettingTab } from "./settings";
import { registerShotChartBlock } from "./blocks/ShotChartBlock";
import { registerShotListBlock } from "./blocks/ShotListBlock";
import { ShotListView, VIEW_TYPE_SHOT_LIST } from "./views/ShotListView";
import { ShotPickerModal } from "./modals/ShotPickerModal";

export default class DecenzaPlugin extends Plugin {
    settings: DecenzaSettings = DEFAULT_SETTINGS;
    api: DecenzaApi = new DecenzaApi("");

    async onload(): Promise<void> {
        await this.loadSettings();
        this.api = new DecenzaApi(this.settings.serverUrl);

        this.addSettingTab(new DecenzaSettingTab(this.app, this));

        registerShotChartBlock(this);
        registerShotListBlock(this);

        this.registerView(
            VIEW_TYPE_SHOT_LIST,
            (leaf) => new ShotListView(leaf, this)
        );

        this.addCommand({
            id: "browse-shots",
            name: "Browse shots",
            callback: () => this.activateShotListView(),
        });

        this.addCommand({
            id: "insert-shot",
            name: "Insert shot",
            editorCallback: (editor) => {
                new ShotPickerModal(this.app, this, (shot) => {
                    const cursor = editor.getCursor();
                    editor.replaceRange(
                        `\n\`\`\`decenza-shot\n${shot.id}\n\`\`\`\n`,
                        cursor
                    );
                }).open();
            },
        });

        this.addRibbonIcon("coffee", "Decenza Shots", () => {
            this.activateShotListView();
        });
    }

    async activateShotListView(): Promise<void> {
        const existing = this.app.workspace.getLeavesOfType(VIEW_TYPE_SHOT_LIST);
        if (existing.length > 0) {
            this.app.workspace.revealLeaf(existing[0]);
            return;
        }
        const leaf = this.app.workspace.getRightLeaf(false);
        if (leaf) {
            await leaf.setViewState({ type: VIEW_TYPE_SHOT_LIST, active: true });
            this.app.workspace.revealLeaf(leaf);
        }
    }

    async loadSettings(): Promise<void> {
        this.settings = Object.assign({}, DEFAULT_SETTINGS, await this.loadData());
    }

    async saveSettings(): Promise<void> {
        await this.saveData(this.settings);
    }
}
