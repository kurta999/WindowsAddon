#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

class PendingModbusRowUpdates
{
public:
    void Add(const std::vector<uint8_t>& rows)
    {
        std::scoped_lock lock(m_mutex);
        for(uint8_t row : rows)
            m_rows[row] = true;
    }

    std::vector<uint8_t> Take()
    {
        std::scoped_lock lock(m_mutex);
        std::vector<uint8_t> rows;
        for(size_t row = 0; row < m_rows.size(); ++row)
            if(m_rows[row])
            {
                rows.push_back(static_cast<uint8_t>(row));
                m_rows[row] = false;
            }
        return rows;
    }

    void Clear()
    {
        std::scoped_lock lock(m_mutex);
        m_rows.fill(false);
    }

private:
    std::mutex m_mutex;
    std::array<bool, 256> m_rows{};
};
