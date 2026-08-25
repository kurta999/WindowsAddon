#include "pch_core.hpp"
#include "CanDeviceFactory.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

#include "CanDeviceFactory.hpp"
#include "CanDeviceLawicel.hpp"
#include "CanDeviceStm32.hpp"

std::unique_ptr<ICanDevice> CanDeviceFactory::Create(CanDeviceType type)
{
    switch(type)
    {
        case CanDeviceType::STM32:
            return std::make_unique<CanDeviceStm32>();
        case CanDeviceType::LAWICEL:
            return std::make_unique<CanDeviceLawicel>();
    }

    throw std::invalid_argument("Unsupported CAN device type");
}
