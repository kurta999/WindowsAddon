#pragma once

#include <string>
#include <cstdint>
#include <span>

class ICanResultPanel
{
public:
    virtual ~ICanResultPanel() = default;

    virtual void AddToLog(std::string str) = 0;
    virtual void OnTxFrameUpdated(std::uint32_t frame_id, std::span<const std::uint8_t> data) = 0;
};
