import { MarkdownPostProcessorContext } from "obsidian";
import type DecenzaPlugin from "../main";
import { ShotCodeBlockConfig, ShotDetail } from "../types";
import { renderShotChart } from "../charts/ShotChart";

export function registerShotChartBlock(plugin: DecenzaPlugin): void {
    plugin.registerMarkdownCodeBlockProcessor(
        "decenza-shot",
        async (
            source: string,
            el: HTMLElement,
            ctx: MarkdownPostProcessorContext
        ) => {
            const config = parseConfig(source.trim());
            if (!config) {
                el.createEl("p", {
                    text: "Invalid config. Use a shot ID number or JSON like {\"id\": 42}.",
                    cls: "decenza-error",
                });
                return;
            }

            if (!plugin.settings.serverUrl) {
                el.createEl("p", {
                    text: "Decenza server URL not configured. Go to Settings \u2192 Decenza DE1.",
                    cls: "decenza-error",
                });
                return;
            }

            const loading = el.createEl("p", {
                text: "Loading shot data...",
                cls: "decenza-loading",
            });

            try {
                const shot = await plugin.api.getShot(config.id);
                if (!shot || !shot.id) {
                    loading.setText(`Shot #${config.id} not found.`);
                    loading.addClass("decenza-error");
                    return;
                }
                loading.remove();

                const header = el.createDiv({ cls: "decenza-shot-header" });
                renderMetadataHeader(header, shot);

                const chartContainer = el.createDiv({
                    cls: "decenza-chart-container",
                });
                const canvas = chartContainer.createEl("canvas");
                renderShotChart(canvas, shot, { charts: config.charts });
            } catch (err) {
                loading.setText(`Error loading shot: ${err}`);
                loading.addClass("decenza-error");
            }
        }
    );
}

function parseConfig(source: string): ShotCodeBlockConfig | null {
    const num = parseInt(source, 10);
    if (!isNaN(num) && num > 0 && String(num) === source) {
        return { id: num };
    }
    try {
        const obj = JSON.parse(source);
        if (obj.id && typeof obj.id === "number") {
            return { id: obj.id, charts: obj.charts };
        }
    } catch {
        /* not JSON */
    }
    return null;
}

function renderMetadataHeader(el: HTMLElement, shot: ShotDetail): void {
    const ratio =
        shot.doseWeight > 0
            ? (shot.finalWeight / shot.doseWeight).toFixed(1)
            : "-";
    const duration = shot.duration.toFixed(1);
    const stars = Math.round(shot.enjoyment / 20);

    const grid = el.createDiv({ cls: "decenza-metadata-grid" });
    const fields = [
        { label: "Date", value: shot.dateTime },
        { label: "Profile", value: shot.profileName },
        {
            label: "Bean",
            value:
                [shot.beanBrand, shot.beanType].filter(Boolean).join(" ") ||
                "-",
        },
        {
            label: "Grinder",
            value:
                [shot.grinderModel, shot.grinderSetting]
                    .filter(Boolean)
                    .join(" @ ") || "-",
        },
        {
            label: "Dose / Yield",
            value: `${shot.doseWeight.toFixed(1)}g \u2192 ${shot.finalWeight.toFixed(1)}g`,
        },
        { label: "Ratio", value: `1:${ratio}` },
        { label: "Time", value: `${duration}s` },
        {
            label: "Rating",
            value:
                shot.enjoyment > 0
                    ? "\u2605".repeat(stars) + "\u2606".repeat(5 - stars)
                    : "-",
        },
    ];

    for (const field of fields) {
        const item = grid.createDiv({ cls: "decenza-metadata-item" });
        item.createEl("span", {
            text: field.label,
            cls: "decenza-metadata-label",
        });
        item.createEl("span", {
            text: field.value,
            cls: "decenza-metadata-value",
        });
    }

    if (shot.espressoNotes) {
        const notes = el.createDiv({ cls: "decenza-shot-notes" });
        notes.createEl("span", {
            text: "Notes: ",
            cls: "decenza-metadata-label",
        });
        notes.createEl("span", { text: shot.espressoNotes });
    }
}
