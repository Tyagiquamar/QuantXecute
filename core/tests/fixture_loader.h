#pragma once

#include <cstdint>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "qx/MarketEvent.h"

namespace qx::test {

inline std::vector<MarketEvent> loadEventsJsonl(const std::string& path)
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open fixture: " + path);
    }

    std::vector<MarketEvent> events;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const nlohmann::json j = nlohmann::json::parse(line);

        MarketEvent event;
        event.type = j.at("type").get<std::string>() == "snapshot" ? EventType::Snapshot : EventType::Delta;
        event.sequence = j.at("sequence").get<std::uint64_t>();
        event.timestampNs = j.at("timestamp_ns").get<std::int64_t>();

        for (const auto& side : { "bids", "asks" }) {
            auto& levels = side[0] == 'b' ? event.bids : event.asks;
            for (const auto& entry : j.at(side)) {
                const auto price = std::stod(entry.at("price").get<std::string>());
                const auto size = std::stod(entry.at("size").get<std::string>());
                levels.push_back(Level { price, size });
            }
        }

        if (j.contains("checksum")) {
            event.checksum = j.at("checksum").get<std::uint32_t>();
        }

        events.push_back(std::move(event));
    }
    return events;
}

} // namespace qx::test
