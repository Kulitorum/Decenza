import { App, PluginSettingTab, Setting, Notice } from "obsidian";
import type DecenzaPlugin from "./main";

export class DecenzaSettingTab extends PluginSettingTab {
    plugin: DecenzaPlugin;

    constructor(app: App, plugin: DecenzaPlugin) {
        super(app, plugin);
        this.plugin = plugin;
    }

    display(): void {
        const { containerEl } = this;
        containerEl.empty();

        containerEl.createEl("h2", { text: "Decenza DE1 Settings" });

        new Setting(containerEl)
            .setName("Server URL")
            .setDesc(
                "The Decenza server address. Enable Remote Access in the Decenza app first."
            )
            .addText((text) =>
                text
                    .setPlaceholder("http://192.168.1.x:8888")
                    .setValue(this.plugin.settings.serverUrl)
                    .onChange(async (value) => {
                        this.plugin.settings.serverUrl = value;
                        this.plugin.api.setBaseUrl(value);
                        await this.plugin.saveSettings();
                    })
            );

        new Setting(containerEl)
            .setName("Test connection")
            .setDesc("Verify the plugin can reach the Decenza server")
            .addButton((button) =>
                button.setButtonText("Test").onClick(async () => {
                    button.setButtonText("Testing...");
                    button.setDisabled(true);
                    const ok = await this.plugin.api.testConnection();
                    new Notice(
                        ok
                            ? "Connected to Decenza!"
                            : "Connection failed. Check URL and ensure Remote Access is enabled."
                    );
                    button.setButtonText("Test");
                    button.setDisabled(false);
                })
            );

        containerEl.createEl("p", {
            text: "Note: On iOS, Obsidian requires HTTPS URLs. Since Decenza uses HTTP on local network, this plugin works on desktop and Android only.",
            cls: "setting-item-description",
        });
    }
}
