import {
    Chart,
    LineController,
    LineElement,
    PointElement,
    LinearScale,
    Tooltip,
    Legend,
    Filler,
    type ChartConfiguration,
    type Plugin,
} from "chart.js";
import type { ShotDetail, Point } from "../types";

Chart.register(
    LineController,
    LineElement,
    PointElement,
    LinearScale,
    Tooltip,
    Legend,
    Filler
);

export interface ChartOptions {
    charts?: string[];
}

const COLORS = {
    pressure: "#18c37e",
    pressureBg: "rgba(24, 195, 126, 0.1)",
    pressureGoal: "#69fdb3",
    flow: "#4e85f4",
    flowBg: "rgba(78, 133, 244, 0.1)",
    flowGoal: "#7aaaff",
    weight: "#a2693d",
    weightBg: "rgba(162, 105, 61, 0.1)",
    temperature: "#e73249",
    temperatureBg: "rgba(231, 50, 73, 0.1)",
};

function insertNullGaps(
    points: Point[]
): ({ x: number; y: number } | { x: number; y: null })[] {
    if (!points || points.length === 0) return [];
    const result: ({ x: number; y: number } | { x: number; y: null })[] = [];
    let lastX = -999;
    for (const pt of points) {
        if (lastX >= 0 && pt.x - lastX > 0.5) {
            result.push({ x: (lastX + pt.x) / 2, y: null });
        }
        result.push(pt);
        lastX = pt.x;
    }
    return result;
}

function makePhaseMarkerPlugin(shot: ShotDetail): Plugin {
    return {
        id: "phaseMarkers",
        afterDraw(chart: Chart) {
            if (!shot.phases || shot.phases.length === 0) return;
            const ctx = chart.ctx;
            const xScale = chart.scales["x"];
            const yScale = chart.scales["y"];
            if (!xScale || !yScale) return;
            const top = yScale.top;
            const bottom = yScale.bottom;

            ctx.save();
            for (const marker of shot.phases) {
                const x = xScale.getPixelForValue(marker.time);
                if (x < xScale.left || x > xScale.right) continue;

                ctx.beginPath();
                ctx.setLineDash([3, 3]);
                ctx.strokeStyle =
                    marker.label === "End"
                        ? "#FF6B6B"
                        : "rgba(150, 150, 150, 0.5)";
                ctx.lineWidth = 1;
                ctx.moveTo(x, top);
                ctx.lineTo(x, bottom);
                ctx.stroke();
                ctx.setLineDash([]);

                let suffix = "";
                if (marker.transitionReason === "weight") suffix = " [W]";
                else if (marker.transitionReason === "pressure") suffix = " [P]";
                else if (marker.transitionReason === "flow") suffix = " [F]";
                else if (marker.transitionReason === "time") suffix = " [T]";
                const text = marker.label + suffix;

                ctx.save();
                ctx.translate(x + 4, top + 10);
                ctx.rotate(-Math.PI / 2);
                ctx.font =
                    (marker.label === "End" ? "bold " : "") + "11px sans-serif";
                ctx.fillStyle =
                    marker.label === "End"
                        ? "#FF6B6B"
                        : "rgba(150, 150, 150, 0.8)";
                ctx.textAlign = "right";
                ctx.fillText(text, 0, 0);
                ctx.restore();
            }
            ctx.restore();
        },
    };
}

export function renderShotChart(
    canvas: HTMLCanvasElement,
    shot: ShotDetail,
    options: ChartOptions = {}
): Chart {
    const enabled = options.charts || ["pressure", "flow", "weight"];
    const datasets: ChartConfiguration<"line">["data"]["datasets"] = [];

    if (enabled.includes("pressure")) {
        datasets.push({
            label: "Pressure",
            data: shot.pressure,
            borderColor: COLORS.pressure,
            backgroundColor: COLORS.pressureBg,
            borderWidth: 2,
            pointRadius: 0,
            tension: 0.3,
            yAxisID: "y",
        });
        if (shot.pressureGoal?.length > 0) {
            datasets.push({
                label: "Pressure Goal",
                data: insertNullGaps(shot.pressureGoal) as any,
                borderColor: COLORS.pressureGoal,
                borderWidth: 1,
                borderDash: [5, 5],
                pointRadius: 0,
                tension: 0.1,
                yAxisID: "y",
                spanGaps: false,
            });
        }
    }

    if (enabled.includes("flow")) {
        datasets.push({
            label: "Flow",
            data: shot.flow,
            borderColor: COLORS.flow,
            backgroundColor: COLORS.flowBg,
            borderWidth: 2,
            pointRadius: 0,
            tension: 0.3,
            yAxisID: "y",
        });
        if (shot.flowGoal?.length > 0) {
            datasets.push({
                label: "Flow Goal",
                data: insertNullGaps(shot.flowGoal) as any,
                borderColor: COLORS.flowGoal,
                borderWidth: 1,
                borderDash: [5, 5],
                pointRadius: 0,
                tension: 0.1,
                yAxisID: "y",
                spanGaps: false,
            });
        }
    }

    if (enabled.includes("weight")) {
        datasets.push({
            label: "Yield",
            data: shot.weight,
            borderColor: COLORS.weight,
            backgroundColor: COLORS.weightBg,
            borderWidth: 2,
            pointRadius: 0,
            tension: 0.3,
            yAxisID: "y2",
        });
    }

    if (enabled.includes("temperature")) {
        datasets.push({
            label: "Temp",
            data: shot.temperature,
            borderColor: COLORS.temperature,
            backgroundColor: COLORS.temperatureBg,
            borderWidth: 2,
            pointRadius: 0,
            tension: 0.3,
            yAxisID: "y3",
        });
    }

    return new Chart(canvas, {
        type: "line",
        plugins: [makePhaseMarkerPlugin(shot)],
        data: { datasets },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            interaction: {
                mode: "nearest",
                axis: "x",
                intersect: false,
            },
            plugins: {
                legend: {
                    display: true,
                    position: "top",
                    labels: {
                        usePointStyle: true,
                        boxWidth: 8,
                        filter: (item) =>
                            !item.text.includes("Goal"),
                    },
                },
                tooltip: {
                    mode: "nearest",
                    axis: "x",
                    intersect: false,
                    callbacks: {
                        label(ctx) {
                            let unit = "";
                            const label = ctx.dataset.label || "";
                            if (label.includes("Pressure")) unit = " bar";
                            else if (label.includes("Flow")) unit = " ml/s";
                            else if (label.includes("Yield")) unit = " g";
                            else if (label.includes("Temp")) unit = " \u00b0C";
                            const val =
                                typeof ctx.parsed.y === "number"
                                    ? ctx.parsed.y.toFixed(1)
                                    : "-";
                            return `${label}: ${val}${unit}`;
                        },
                        title(items) {
                            if (items.length === 0) return "";
                            return `${items[0].parsed.x.toFixed(1)}s`;
                        },
                    },
                },
            },
            scales: {
                x: {
                    type: "linear",
                    title: { display: true, text: "Time (s)" },
                },
                y: {
                    type: "linear",
                    position: "left",
                    title: { display: true, text: "Pressure / Flow" },
                    min: 0,
                    max: 12,
                },
                y2: {
                    type: "linear",
                    position: "right",
                    title: { display: true, text: "Yield (g)" },
                    min: 0,
                    grid: { display: false },
                },
                y3: {
                    type: "linear",
                    position: "right",
                    title: { display: false },
                    min: 80,
                    max: 100,
                    display: false,
                },
            },
        },
    });
}
