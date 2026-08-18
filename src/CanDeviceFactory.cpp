#include "pch.hpp"

#include "CanDeviceFactory.hpp"
#include "CanDeviceLawicel.hpp"
#include "CanDeviceStm32.hpp"

std::unique_ptr<ICanDevice> CanDeviceFactory::Create(CanDeviceType type)
{
    switch(type)
    {
        case CanDeviceType::STM32:
            return std::make_unique<CanDeviceStm32>(m_ReceiveBuffer);
        case CanDeviceType::LAWICEL:
            return std::make_unique<CanDeviceLawicel>(m_ReceiveBuffer);
    }

    throw std::invalid_argument("Unsupported CAN device type");
}
