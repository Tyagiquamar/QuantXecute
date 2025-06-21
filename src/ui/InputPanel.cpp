#include "ui/InputPanel.h"
#include "imgui.h"

#include "ui/InputPanel.h"
#include "imgui.h"

void showInputPanel(InputParams& params) {
    ImGui::Begin("Input Parameters");

    // Exchange - For demo, fixed label or combo box
    ImGui::Text("Exchange: %s", params.exchange.c_str());

    // Spot Asset - text input or combo box
    ImGui::InputText("Spot Asset", &params.spotAsset[0], 64); // Simplified for example

    // Order Type - fixed market order for now
    ImGui::Text("Order Type: %s", params.orderType.c_str());

    // Trade Amount
    ImGui::InputFloat("Trade Amount (USD)", &params.tradeAmount);

    // Volatility
    ImGui::InputFloat("Volatility", &params.volatility);

    // Fee Rate
    ImGui::InputFloat("Fee Rate (%)", &params.feeRate);

    if (ImGui::Button("Simulate")) {
        params.shouldSimulate = true;
    }

    ImGui::End();
}