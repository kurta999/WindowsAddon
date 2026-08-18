#include "TcpMessageParser.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto* chars = reinterpret_cast<const char*>(data);
    const std::string_view message = size == 0
        ? std::string_view{}
        : std::string_view(chars, size);
    (void)tcp_message::Parse(message);

    const auto buffered_size = std::min(size, tcp_message::MaxMessageSize);
    std::vector<char> buffer(buffered_size);
    if(buffered_size != 0)
        std::copy_n(chars, buffered_size, buffer.begin());
    const auto bounded = tcp_message::BoundedMessage(buffer, size);
    if(bounded.size() > buffer.size())
        __builtin_trap();

    return 0;
}
