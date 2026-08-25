#pragma once

#include "interface/ICanDeviceFactory.hpp"

// The devices decode the bytes they are handed, so the factory no longer has to
// wire them to the transport's receive buffer.
class CanDeviceFactory final : public ICanDeviceFactory
{
public:
    [[nodiscard]] std::unique_ptr<ICanDevice> Create(CanDeviceType type) override;
};
