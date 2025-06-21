#include "ui/OutputPanel.h"
#include "imgui.h"

void DrawOutputPanel(const OutputMetrics& m) {
    ImGui::Begin("Output Metrics");
    ImGui::Text("Expected Slippage: %.4f", m.slippage);
    ImGui::Text("Expected Fees: %.4f", m.fees);
    ImGui::Text("Market Impact: %.4f", m.marketImpact);
    ImGui::Text("Net Cost: %.4f", m.netCost);
    ImGui::Text("Maker/Taker Ratio: %.2f%%", m.makerTakerRatio * 100.0f);
    ImGui::Text("Internal Latency: %.2f ms", m.latencyMs);
    ImGui::End();
}