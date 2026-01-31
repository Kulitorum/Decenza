import { MarkdownPostProcessorContext } from "obsidian";
import type DecenzaPlugin from "../main";
import { ShotsCodeBlockConfig, ShotSummary } from "../types";

export function registerShotListBlock(plugin: DecenzaPlugin): void {
    plugin.registerMarkdownCodeBlockProcessor(
        "decenza-shots",
        async (
            source: string,
            el: HTMLElement,
            ctx: MarkdownPostProcessorContext
        ) => {
            const config = parseConfig(source.trim());

            if (!plugin.settings.serverUrl) {
                el.createEl("p", {
                    text: "Decenza server URL not configured. Go to Settings \u2192 Decenza DE1.",
                    cls: "decenza-error",
                });
                return;
            }

            const loading = el.createEl("p", {
                text: "Loading shots...",
                cls: "decenza-loading",
            });

            try {
                let shots = await plugin.api.getShots();

                if (config.filter) {
                    const q = config.filter.toLowerCase();
                    shots = shots.filter(
                        (s) =>
                            s.profileName.toLowerCase().includes(q) ||
                            s.beanBrand.toLowerCase().includes(q) ||
                            s.beanType.toLowerCase().includes(q) ||
                            s.grinderSetting.toLowerCase().includes(q)
                    );
                }

                const limit = config.limit || 10;
                shots = shots.slice(0, limit);

                loading.remove();

                if (shots.length === 0) {
                    el.createEl("p", {
                        text: "No shots found.",
                        cls: "decenza-empty",
                    });
                    return;
                }

                renderShotsTable(el, shots);
            } catch (err) {
                loading.setText(`Error loading shots: ${err}`);
                loading.addClass("decenza-error");
            }
        }
    );
}

function parseConfig(source: string): ShotsCodeBlockConfig {
    if (!source) return {};
    try {
        return JSON.parse(source) as ShotsCodeBlockConfig;
    } catch {
        return { filter: source };
    }
}

function renderShotsTable(el: HTMLElement, shots: ShotSummary[]): void {
    const table = el.createEl("table", { cls: "decenza-shots-table" });
    const thead = table.createEl("thead");
    const headerRow = thead.createEl("tr");
    const columns = [
        "Date",
        "Profile",
        "Bean",
        "Dose / Yield",
        "Ratio",
        "Rating",
        "Grinder",
    ];
    for (const col of columns) {
        headerRow.createEl("th", { text: col });
    }

    const tbody = table.createEl("tbody");
    for (const shot of shots) {
        const row = tbody.createEl("tr");
        const ratio =
            shot.doseWeight > 0
                ? (shot.finalWeight / shot.doseWeight).toFixed(1)
                : "-";
        const stars = Math.round(shot.enjoyment / 20);
        const bean =
            [shot.beanBrand, shot.beanType].filter(Boolean).join(" ") || "-";

        row.createEl("td", { text: shot.dateTime });
        row.createEl("td", { text: shot.profileName });
        row.createEl("td", { text: bean });
        row.createEl("td", {
            text: `${shot.doseWeight.toFixed(1)}g \u2192 ${shot.finalWeight.toFixed(1)}g`,
        });
        row.createEl("td", { text: `1:${ratio}` });
        row.createEl("td", {
            text:
                shot.enjoyment > 0
                    ? "\u2605".repeat(stars) + "\u2606".repeat(5 - stars)
                    : "-",
        });
        row.createEl("td", { text: shot.grinderSetting || "-" });
    }
}
