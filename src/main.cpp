#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <iostream>
#pragma comment(lib, "Ws2_32.lib")  
#include "ui/InputPanel.h"
#include "ui/OutputPanel.h"

#include "models/InputParams.h"
#include "models/OutputMetrics.h"
#include "models/SimulationModel.h"
#include "models/WebSocketClient.h"

int main() {
    // Initialize GLFW
    if (!glfwInit()) return -1;

    // Create window with OpenGL context
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Trade Simulator", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui::StyleColorsDark();

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Initialize simulator components
    InputParams inputParams;
    OutputMetrics outputMetrics;
    SimulationModel model;
    WebSocketClient wsClient;

    wsClient.connect("BTC-USDT-SWAP");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        showInputPanel(inputParams);

        if (inputParams.shouldSimulate) {
            if (!wsClient.isOrderBookReady()) {
                std::cerr << "Order book not ready yet. Please wait...\n";
            }
            else {
                OrderBookData book = wsClient.getLatestOrderBook();
                if (book.bids.empty() || book.asks.empty()) {
                    std::cerr << "Order book is empty. Cannot simulate.\n";
                }
                else {
                    std::cout << "Running simulation...\n";
                    outputMetrics = model.runSimulation(inputParams, book);
                    std::cout << "Simulation done. Latency: " << outputMetrics.latencyMs << " ms\n";
                }
            }
            inputParams.shouldSimulate = false;
        }


        DrawOutputPanel(outputMetrics);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}