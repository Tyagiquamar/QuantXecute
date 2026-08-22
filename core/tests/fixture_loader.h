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

        if (auto it = j.find("seqId"); it != j.end() && it->is_number_unsigned()) {
            event.sequence = it->get<std::uint64_t>();
            event.hasSequence = true;
            if (auto prev = j.find("prevSeqId"); prev != j.end() && prev->is_number_integer()) {
                event.prevSequence = prev->get<std::int64_t>();
            }
        } else {
            // Legacy dense-sequence fixtures without OKX metadata.
            event.sequence = j.at("sequence").get<std::uint64_t>();
            event.hasSequence = true;
        }

        event.timestampNs = j.value("timestamp_ns", static_cast<std::int64_t>(0));

        for (const auto& side : { "bids", "asks" }) {
            auto& levels = side[0] == 'b' ? event.bids : event.asks;
            for (const auto& entry : j.at(side)) {
                const auto price = std::stod(entry.at("price").get<std::string>());
                const auto size = std::stod(entry.at("size").get<std::string>());
                levels.push_back(Level { price, size });
            }
        }

        if (j.contains("checksum")) {
            event.checksum = j.at("checksum").get<std::int64_t>();
        }

        events.push_back(std::move(event));
    }
    return events;
}

} // namespace qx::test