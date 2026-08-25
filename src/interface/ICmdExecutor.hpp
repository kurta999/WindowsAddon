#pragma once

#include <string>
#include <vector>
#include <filesystem>

#include "ICmdHelper.hpp"

using CommandStorage = std::vector<std::vector<std::vector<CommandTypes>>>;

class ICommandLoader
{
public:
    virtual ~ICommandLoader() = default;

    virtual bool Load(const std::filesystem::path& path, CommandStorage& e, CommandPageNames& names, CommandPageIcons& icons) = 0;
    virtual bool Save(const std::filesystem::path& path, CommandStorage& e, CommandPageNames& names, CommandPageIcons& icons) const = 0;
};

class ICmdExecutor
{
public:
    virtual ~ICmdExecutor() = default;

    virtual void Init() = 0;
    virtual void SetMediator(ICmdHelper* mediator) = 0;
    /* Two conventions live here, and they used to be spelled the same way.
       The three item methods below take a 1-based page and column, matching
       the Page_1 / Col_1 element names in Cmds.xml. The five page and column
       methods below them take a 0-based index into the loaded vectors, which
       is what a wxNotebook selection already is. Calling one with the other's
       numbering is off by one, silently, and both were called `page`.

       Anything named _number is 1-based; anything named _index is 0-based. */

    // !\brief Append a command to a page and column, both 1-based.
    virtual void AddCommand(uint8_t page_number, uint8_t col_number, Command cmd) = 0;
    virtual void RotateCommand(uint8_t page_number, uint8_t col_number, Command& cmd, uint8_t direction) = 0;
    // !\brief Append a separator to a page and column, both 1-based.
    virtual void AddSeparator(uint8_t page_number, uint8_t col_number, Separator sep) = 0;

    // !\brief Insert an empty column into a 0-based page, at a 0-based position.
    virtual void AddCol(uint8_t page_index, uint8_t dest_index) = 0;
    // !\brief Remove a 0-based column from a 0-based page.
    virtual void DeleteCol(uint8_t page_index, uint8_t dest_index) = 0;
    // !\brief Insert a new page at a 0-based position.
    virtual void AddPage(uint8_t page_index, uint8_t dest_index) = 0;
    // !\brief Copy a 0-based page to a 0-based position.
    virtual void CopyPage(uint8_t page_index, uint8_t dest_index) = 0;
    // !\brief Remove a 0-based page.
    virtual void DeletePage(uint8_t page_index) = 0;
    virtual bool ReloadCommandsFromFile(const char* path) = 0;
    virtual bool Save(const char* path) = 0;
    virtual bool SaveToTempAndReload() = 0;
    virtual uint8_t GetColumns() const = 0;
    virtual CommandStorage& GetCommands() = 0;
    virtual CommandPageNames& GetPageNames() = 0;
    virtual CommandPageIcons& GetPageIcons() = 0;

    // !\brief Run a command by the names it carries in Cmds.xml.
    // Declared here because a macro action invokes it, and a macro action must
    // not have to know the concrete executor.
    virtual void ExecuteByName(const std::string& page_name, const std::string& cmd_name) = 0;
};
