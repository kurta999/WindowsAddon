#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace utils
{
    // !\brief One enumerator and the name a configuration file writes it with.
    template <typename E>
    struct EnumName
    {
        E value{};
        std::string_view name;
    };

    // !\brief The name<->enumerator mapping for one enum, written once.
    //
    // Five enums carried this as two independent spellings: a std::map keyed by
    // name for reading a file, and a switch over the enum for writing it back.
    // Nothing tied the halves together, so a value could be readable and not
    // writable, and the register byte order and the comparison operators each
    // had a value that appeared in only one of the two.
    //
    // `fallback` is what an unknown name reads as - every one of the five
    // already had such a value, and every one of them spelled the check for it
    // differently.
    //
    // Dependency-free on purpose: the Modbus and CAN persistence headers are
    // compiled into test targets that link neither boost nor the logger.
    template <typename E, std::size_t N>
    struct EnumNameTable
    {
        E fallback{};
        std::array<EnumName<E>, N> names{};

        // !\brief The enumerator a file names, or `fallback`.
        [[nodiscard]] constexpr E FromName(std::string_view name) const
        {
            const auto it = std::find_if(names.begin(), names.end(),
                [name](const EnumName<E>& entry) { return entry.name == name; });
            return it != names.end() ? it->value : fallback;
        }

        // !\brief How an enumerator is written back out.
        //
        // A value with no row - which means one added to the enum and not to
        // the table - is written as the fallback's name, which is what the
        // switches these replace did with their default arm.
        [[nodiscard]] constexpr std::string_view NameOf(E value) const
        {
            const auto it = std::find_if(names.begin(), names.end(),
                [value](const EnumName<E>& entry) { return entry.value == value; });
            if(it != names.end())
                return it->name;

            const auto fallback_it = std::find_if(names.begin(), names.end(),
                [this](const EnumName<E>& entry) { return entry.value == fallback; });
            return fallback_it != names.end() ? fallback_it->name : std::string_view{};
        }

        // !\brief Every row, for the callers that offer the names as a choice.
        [[nodiscard]] constexpr std::span<const EnumName<E>> Entries() const
        {
            return std::span<const EnumName<E>>(names);
        }
    };
}
