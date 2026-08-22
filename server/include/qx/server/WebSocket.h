#pragma once

#include <cstdint>
#include <string>

namespace qx::server {

bool webSocketSendText(int fd, const std::string& payload);

} // namespace qx::server
