#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xml_config
{
struct TxEntry
{
    std::uint32_t id{};
    std::vector<std::uint8_t> data;
    std::uint32_t period{};
    std::uint8_t log_level{};
    std::uint8_t favourite{};
    std::string comment;
    bool operator==(const TxEntry&) const = default;
};

struct RxEntry
{
    std::uint32_t id{};
    std::uint8_t log_level{};
    std::string comment;
    bool operator==(const RxEntry&) const = default;
};

struct DidEntry
{
    std::uint16_t id{};
    std::string type;
    std::string name;
    std::string min;
    std::string max;
    std::size_t length{};
    bool operator==(const DidEntry&) const = default;
};

struct AlarmEntry
{
    std::string name;
    std::string trigger;
    std::string trigger_key;
    std::string execute;
    bool show_dialog{};
    bool operator==(const AlarmEntry&) const = default;
};

struct MappingField
{
    std::uint8_t offset{};
    std::uint8_t length{};
    std::string type;
    std::string name;
    std::string description;
    bool operator==(const MappingField&) const = default;
};

struct FrameMapping
{
    std::uint32_t id{};
    std::string name;
    std::uint8_t size{};
    char direction{'T'};
    std::vector<MappingField> fields;
    bool operator==(const FrameMapping&) const = default;
};

[[nodiscard]] bool SaveTx(const std::filesystem::path& path, const std::vector<TxEntry>& entries);
[[nodiscard]] std::optional<std::vector<TxEntry>> LoadTx(const std::filesystem::path& path);
[[nodiscard]] bool SaveRx(const std::filesystem::path& path, const std::vector<RxEntry>& entries);
[[nodiscard]] std::optional<std::vector<RxEntry>> LoadRx(const std::filesystem::path& path);
[[nodiscard]] bool SaveDids(const std::filesystem::path& path, const std::vector<DidEntry>& entries);
[[nodiscard]] std::optional<std::vector<DidEntry>> LoadDids(const std::filesystem::path& path);
[[nodiscard]] bool SaveAlarms(const std::filesystem::path& path, const std::vector<AlarmEntry>& entries);
[[nodiscard]] std::optional<std::vector<AlarmEntry>> LoadAlarms(const std::filesystem::path& path);
[[nodiscard]] bool SaveMappings(const std::filesystem::path& path, const std::vector<FrameMapping>& entries);
[[nodiscard]] std::optional<std::vector<FrameMapping>> LoadMappings(const std::filesystem::path& path);
}
