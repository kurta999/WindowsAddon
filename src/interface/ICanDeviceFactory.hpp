#pragma once

#include <memory>

#include "CanTransportModels.hpp"

class ICanDevice;

// Factory Method: centralizes selection of the wire-protocol strategy so the
// transport remains closed to new adapter implementations.
class ICanDeviceFactory
{
public:
    virtual ~ICanDeviceFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<ICanDevice> Create(CanDeviceType type) = 0;
};
