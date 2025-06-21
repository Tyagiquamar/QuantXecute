

# Real-Time Trade Cost Estimator QuantXecute

## 📘 Contents

1. [Overview](#overview)
2. [Key Capabilities](#key-capabilities)
3. [System Architecture](#system-architecture)
4. [Module Descriptions](#module-descriptions)
5. [Tech Stack](#tech-stack)
6. [Installation Guide](#installation-guide)
7. [How to Use](#how-to-use)
8. [Known Limitations & Enhancements](#known-limitations--enhancements)
9. [Resources](#resources)

---

## 📌 Overview

This application is a **real-time analytics engine** for crypto futures on the OKX exchange, designed to estimate **comprehensive trade costs** using live orderbook data. It taps into OKX’s WebSocket Level 2 feed for the BTC-USDT-SWAP market and provides detailed cost breakdowns, including:

* Price slippage (regression-based)
* Fee estimation (tier-based)
* Market impact (via Almgren-Chriss modeling)
* Maker/taker likelihood
* Internal latency diagnostics

This tool helps traders optimize execution strategies and monitor cost components in fast-moving market environments.

---

## 🚀 Key Capabilities

* **Secure WebSocket Streaming:** Connects to OKX’s Level 2 orderbook at `wss://ws.gomarket-cpp.goquant.io/ws/l2-orderbook/okx/BTC-USDT-SWAP` using TLS encryption.
* **Real-Time Orderbook Parsing:** Continuously processes JSON-formatted data to maintain updated bid/ask depth.
* **Slippage Estimation:** Supports both linear and quantile regression models trained on historical snapshots.
* **Flexible Fee Modeling:** Fee computation reflects maker/taker dynamics and configurable tier structures.
* **Market Impact Analytics:** Employs a simplified Almgren-Chriss model to capture impact costs tied to size and speed of execution.
* **Aggregate Cost Evaluation:** Combines all components into a single cost estimate per trade simulation.
* **Execution Role Probability:** Predicts whether an order executes as maker or taker using logistic regression on market microstructure features.
* **Performance Metrics:** Tracks end-to-end system latency at sub-module levels for performance tuning.

---

## 🧱 System Architecture

```
+-----------------------------+
|        Web Interface        |
|  • Visualizes cost metrics  |
|  • Accepts simulation inputs|
+-------------------^---------+
                    |
          WebSocket Stream (L2 Orderbook)
                    |
+-------------------v----------------------------+
|         Backend Analytics Engine               |
| • WebSocket Client (Boost::Beast + SSL)        |
| • JSON Parsing (Boost::JSON)                   |
| • Slippage Estimation Models                   |
| • Almgren-Chriss Impact Calculator             |
| • Fee Calculator                               |
| • Logistic Classifier (Maker/Taker Prob.)      |
| • Latency Monitor                              |
+------------------------------------------------+
```

---

## 🔍 Module Descriptions

### 1. **Slippage Modeling**

Estimates expected price deviation due to trade execution volume:

**Model:**

```math
slippage(Q) = slope × Q + intercept
```

Where:

* `Q`: Trade quantity
* `slope`, `intercept`: Derived from linear regression on historical data

**Why:**

* Lightweight, interpretable
* Suitable for real-time processing
* Easily retrainable

### 2. **Fee Tier Computation**

Fees are based on trading tier levels:

```cpp
switch (tier) {
  case FeeTier::Tier1: return amountUSD * 0.001;
  case FeeTier::Tier2: return amountUSD * 0.0007;
  case FeeTier::Tier3: return amountUSD * 0.0005;
  default: return amountUSD * 0.001;
}
```

**Formula:**

```math
fee = amountUSD × tier_rate
```

### 3. **Market Impact Estimation**

Follows the Almgren-Chriss framework (simplified):

```math
impact = σ × sqrt(Q / T)
```

Where:

* `σ`: Market volatility
* `Q`: Order size
* `T`: Execution time window

Captures temporary market pressure from executing large trades rapidly.

### 4. **Net Trade Cost Calculation**

Sums all estimated costs:

```text
Total Cost = Slippage + Market Impact + Fees
```

This provides a consolidated execution cost estimate.

### 5. **Maker vs. Taker Prediction**

Uses a logistic regression model trained on:

* Orderbook imbalance
* Bid-ask spread
* Historical aggressor data

Estimates the probability that an order gets executed as a maker or taker.

### 6. **Latency Benchmarks**

Timestamps captured at key stages:

| Metric                | Target  | Description                             |
| --------------------- | ------- | --------------------------------------- |
| Data Processing Delay | < 5 ms  | From tick arrival to output computation |
| UI Refresh Latency    | < 10 ms | Rendering time via ImGui/OpenGL         |
| Simulation Loop Total | < 15 ms | Combined cost calculations              |

### 7. **WebSocket Streaming**

The client:

* Authenticates and connects securely via SSL
* Subscribes to BTC-USDT-SWAP L2 channel
* Parses and decodes depth updates in real-time

---

## 🛠️ Tech Stack

* **C++17/C++20**: Core system language
* **Boost Libraries**: For networking, WebSockets (Beast), JSON parsing
* **OpenSSL**: TLS encryption for secure communication
* **Statistical Models**: Custom implementations (OLS, quantile, logistic)
* **Build Tools**: `CMake`, `g++`, `clang`, or `make`

---

## ⚙️ Installation Guide

### 📋 Requirements

* Boost 1.75+ (with Beast and JSON)
* OpenSSL
* C++17-compliant compiler (g++ 9+ or clang 10+)
* CMake

### 🧱 Build Steps

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build .
```

### ▶️ Running the App

```bash
./TradeSimulator
```

---

## 🧪 How to Use

1. Launch the simulator to initiate a WebSocket connection with OKX.
2. Watch live updates of the BTC-USDT orderbook and cost analytics.
3. Optionally modify model coefficients or trade parameters in the code.
4. For frontend integration, connect via WebSocket or expose a REST interface.

---

## 🚧 Known Limitations & Enhancements

* **Model Accuracy:** Current regressions are basic and benefit from richer training datasets.
* **Single Instrument Support:** Extend beyond BTC-USDT-SWAP.
* **Resilience:** Improve handling of disconnects and corrupt packets.
* **Visualization:** Add a modern dashboard for interaction and visualization.
* **Speed Optimization:** Further profiling needed for high-frequency trading readiness.

---

## 📚 Resources

* Almgren, R., & Chriss, N. (2000). *Optimal execution of portfolio transactions*.
* [Boost C++ Libraries](https://www.boost.org/)
* [OKX WebSocket API Docs](https://www.okx.com/docs-v5/en/)

---

