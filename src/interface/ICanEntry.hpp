#pragma once

#include <vector>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <string>
#include <memory>

class CanTxEntry;
class CanMap;

using CanMapping = std::map<uint32_t, std::map<uint8_t, std::unique_ptr<CanMap>>>;

struct CanFrameMetadata
{
    std::string name;
    uint8_t     size      = 0;
    char        direction = 'T';
};
using CanFrameMetadataMap = std::map<uint32_t, CanFrameMetadata>;

class ICanEntryLoader
{
public:
    virtual ~ICanEntryLoader() = default;
    virtual bool Load(const std::filesystem::path& path, std::vector<std::unique_ptr<CanTxEntry>>& e) = 0;
    virtual bool Save(const std::filesystem::path& path, std::vector<std::unique_ptr<CanTxEntry>>& e) const = 0;
};

class ICanRxEntryLoader
{
public:
    virtual ~ICanRxEntryLoader() = default;
    virtual bool Load(const std::filesystem::path& path, std::unordered_map<uint32_t, std::string>& e, std::unordered_map<uint32_t, uint8_t>& loglevels) = 0;
    virtual bool Save(const std::filesystem::path& path, std::unordered_map<uint32_t, std::string>& e, std::unordered_map<uint32_t, uint8_t>& loglevels) const = 0;
};

class ICanMappingLoader
{
public:
    virtual ~ICanMappingLoader() = default;
    virtual bool Load(const std::filesystem::path& path, CanMapping& mapping, CanFrameMetadataMap& metadata) = 0;
    virtual bool Save(const std::filesystem::path& path, CanMapping& mapping, const CanFrameMetadataMap& metadata) const = 0;
};
