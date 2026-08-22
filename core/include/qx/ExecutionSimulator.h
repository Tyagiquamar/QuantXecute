#pragma once

#include "qx/Book.h"
#include "qx/ExecutionResult.h"

namespace qx {

enum class SizeMode : std::uint8_t {
    Notional,
    BaseQuantity,
};

struct OrderRequest {
    Side side = Side::Buy;
    SizeMode sizeMode = SizeMode::Notional;
    double size = 0.0;
    double takerFeeBps = 0.0;
};

class ExecutionSimulator {
public:
    ExecutionResult execute(const Book& book, const OrderRequest& request) const;
};

} // namespace qx
