#pragma once

#include <boost/circular_buffer.hpp>

#include "interface/ICanDeviceFactory.hpp"

class CanDeviceFactory final : public ICanDeviceFactory
{
public:
    explicit CanDeviceFactory(boost::circular_buffer<char>& receive_buffer)
        : m_ReceiveBuffer(receive_buffer)
    {
    }

    [[nodiscard]] std::unique_ptr<ICanDevice> Create(CanDeviceType type) override;

private:
    boost::circular_buffer<char>& m_ReceiveBuffer;
};
