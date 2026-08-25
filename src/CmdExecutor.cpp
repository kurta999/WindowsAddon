#include "pch_core.hpp"
#include "CmdExecutor.hpp"
#include "CommandParams.hpp"
#include "DefaultCommandsFile.hpp"
#include "utils/XmlDocument.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

void Command::Execute(ICommandRunner& command_runner, const ICommandTextResolver& text_resolver)
{
    std::string cmd_to_execute = text_resolver.Resolve(m_name, HandleParameters());
    if(!command_runner.Start(cmd_to_execute, IsConsoleHidden()))
        LOG(LogLevel::Error, "Failed to start command: {}", cmd_to_execute);
}

void CmdExecutor::ExecuteByName(const std::string& page_name, const std::string& cmd_name)
{
    auto page_it = std::ranges::find(m_CommandPageNames, page_name);
    if(page_it != m_CommandPageNames.end())
    {
        ptrdiff_t id = page_it - m_CommandPageNames.begin();

        for(auto& col : m_Commands[id])
        {
            for(auto& x : col)
            {
                std::visit([this, &cmd_name](auto& c)
                    {
                        using T = std::decay_t<decltype(c)>;
                        if constexpr(std::is_same_v<T, std::shared_ptr<Command>>)
                        {
                            if(c->GetName() == cmd_name)
                            {
                                c->Execute(m_CommandRunner, m_CommandTextResolver);
                            }
                        }
                        else if constexpr(std::is_same_v<T, Separator>)
                        {

                        }
                        else
                            static_assert(always_false_v<T>, "XmlCommandLoader::Save Bad visitor!");
                    }, x);

            }
        }
    }
}

/* The three walks below share command_params::Next. Each keeps its own idea of
   where to resume, because that is what differs between them: substituting a
   value moves past what it wrote, collecting a default moves past the closing
   marker, and writing a default back stays inside the placeholder it edited. */

std::string Command::HandleParameters()
{
    std::string new_cmd{ m_cmd };
    size_t param_count = 0;
    size_t pos = 1;

    while(pos < m_cmd.length() - 1)
    {
        const auto found = command_params::Next(new_cmd, pos);
        if(!found)
            break;

        const std::string fallback = new_cmd.substr(found->value_begin, found->value_length());
        const std::string& value = m_params.size() <= param_count ? fallback : m_params[param_count];

        new_cmd.erase(found->begin, found->end() - found->begin);
        new_cmd.insert(found->begin, value);

        pos = found->begin + value.length();
        ++param_count;
    }
    return new_cmd;
}

void Command::LoadParametersFromString()
{
    size_t pos = 1;

    while(pos < m_cmd.length() - 1)
    {
        const auto found = command_params::Next(m_cmd, pos);
        if(!found)
            break;

        m_params.push_back(m_cmd.substr(found->value_begin, found->value_length()));
        pos = found->value_end;
    }
}

void Command::SaveParametersToString()
{
    std::string new_cmd{ m_cmd };
    size_t param_count = 0;
    size_t pos = 1;

    while(pos < m_cmd.length() - 1)
    {
        const auto found = command_params::Next(new_cmd, pos);
        if(!found)
            break;

        /* A command whose text has more placeholders than the entry has values
           keeps the rest of its defaults. */
        if(m_params.size() <= param_count)
            break;

        const size_t old_length = found->value_length();
        new_cmd.erase(found->value_begin, old_length);
        new_cmd.insert(found->value_begin, m_params[param_count]);

        pos = found->value_begin + old_length;
        ++param_count;
    }
    m_cmd = new_cmd;
}

XmlCommandLoader::XmlCommandLoader(ICmdHelper* mediator)
{
    m_Mediator = mediator;
}

namespace
{
/* Every optional attribute a <Cmd> can carry, in both spellings the file
   format grew: as a child element (<Name>x</Name>) and as an attribute
   (name="x"). The loader declared eleven boost::optional locals and then read
   all eleven twice, once per spelling, so adding a twelfth meant three edits
   in two shapes and forgetting one of them was silent. */
enum class CmdField : std::size_t
{
    Name, Icon, Hidden, Color, BackgroundColor, Bold, FontFace, Scale,
    UseSizer, AddToPrevSizer, MinSize, Count
};

constexpr std::size_t kCmdFieldCount = static_cast<std::size_t>(CmdField::Count);

struct CmdFieldSpelling
{
    std::string_view element;
    std::string_view attribute;
};

constexpr std::array<CmdFieldSpelling, kCmdFieldCount> kCmdFields{{
    { "Name",            "<xmlattr>.name" },
    { "Icon",            "<xmlattr>.icon" },
    { "Hidden",          "<xmlattr>.hidden" },
    { "Color",           "<xmlattr>.color" },
    { "BackgroundColor", "<xmlattr>.bg_color" },
    { "Bold",            "<xmlattr>.bold" },
    { "FontFace",        "<xmlattr>.font_face" },
    { "Scale",           "<xmlattr>.scale" },
    { "UseSizer",        "<xmlattr>.use_sizer" },
    { "AddToPrevSizer",  "<xmlattr>.add_to_prev_sizer" },
    { "MinSize",         "<xmlattr>.min_size" },
}};

using CmdFields = std::array<boost::optional<std::string>, kCmdFieldCount>;

[[nodiscard]] const boost::optional<std::string>& Field(const CmdFields& fields, CmdField which)
{
    return fields[static_cast<std::size_t>(which)];
}

// !\brief The field's value, or `fallback` when the <Cmd> did not carry it.
template <typename T, typename F>
[[nodiscard]] T FieldOr(const CmdFields& fields, CmdField which, T fallback, F&& convert)
{
    const auto& value = Field(fields, which);
    return value.has_value() ? convert(*value) : fallback;
}
}

bool XmlCommandLoader::Load(const std::filesystem::path& path, CommandStorage& storage, CommandPageNames& names, CommandPageIcons& icons)
{
    /* Checked against the file being loaded, not against the default one: a
       reload from anywhere else used to create Cmds.xml as a side effect and
       then still fail on the file it was actually asked for. */
    if(!std::filesystem::exists(path))
    {
        if(path != COMMAND_FILE_PATH)
        {
            LOG(LogLevel::Error, "Command file '{}' does not exist", path.generic_string());
            return false;
        }
        if(!default_commands::Write(path))
            return false;
        LOG(LogLevel::Normal, "Default {} is missing, creating one", COMMAND_FILE_PATH);
    }

    auto document = utils::xml::Load(path);
    if(!document)
        return false;

    boost::property_tree::ptree& pt = *document;
    try
    {

        storage.clear();
        const uint8_t declared_pages = pt.get_child("Commands").get_child("Pages").get_value<uint8_t>();
        if(m_Mediator)
            m_Mediator->OnPreReload(declared_pages);
        for(uint8_t p = 1; p <= declared_pages; p++)
        {
            auto pages_child = pt.get_child("Commands").get_child_optional(std::format("Page_{}", p));
            if(!pages_child.has_value())
            {
                LOG(LogLevel::Warning, "No child found with 'Page_{}', loading has been aborted!", p);
                break;
            }

            std::string page_name = pages_child->get<std::string>("<xmlattr>.name");
            names.push_back(std::move(page_name));
            /* Optional, not required: an absent icon means the same as an empty
               one. Demanding it made the default file this class writes
               unreadable by the very next load. */
            std::string page_icon = pages_child->get<std::string>("<xmlattr>.icon", "");

            if(page_icon.empty())
                page_icon = "wxART_HARDDISK"; // Do not let page icon empty, set it to default
            icons.push_back(std::move(page_icon));

            const uint8_t declared_cols = pages_child->get_child("Columns").get_value<uint8_t>();
            if(m_Mediator)
                m_Mediator->OnPreReloadColumns(p, declared_cols);

            std::vector<std::vector<CommandTypes>> temp_cmds_per_page;
            for(uint8_t i = 1; i <= declared_cols; i++)
            {
                auto col_child = pages_child->get_child_optional(std::format("Col_{}", i));
                if(!col_child.has_value())
                {
                    LOG(LogLevel::Normal, "No child found with 'Col_{}' within 'Page_{}', loading of this page has been aborted!", i, p);
                    break;
                }

                std::vector<CommandTypes> temp_cmds;
                for(const boost::property_tree::ptree::value_type& v : pages_child->get_child(std::format("Col_{}", i)))
                {
                    if(v.first == "Cmd")
                    {
                        std::string cmd;
                        CmdFields fields;

                        /* A <Cmd> carrying an <Execute> child spells its
                           attributes as child elements; one without spells them
                           as XML attributes. Same eleven fields either way. */
                        if(v.second.get_child_optional("Execute").has_value())
                        {
                            cmd = v.second.get_child("Execute").get_value<std::string>();
                            for(std::size_t i = 0; i < kCmdFieldCount; ++i)
                                utils::xml::ReadChildIfexists<std::string>(v,
                                    std::string(kCmdFields[i].element), fields[i]);
                        }
                        else
                        {
                            cmd = v.second.get_value<std::string>();
                            for(std::size_t i = 0; i < kCmdFieldCount; ++i)
                                fields[i] = v.second.get_optional<std::string>(
                                    std::string(kCmdFields[i].attribute));
                        }

                        if(cmd.empty())
                        {
                            const auto& name = Field(fields, CmdField::Name);
                            LOG(LogLevel::Warning, "Empty cmd for command: {}",
                                name.has_value() ? *name : "Unknown");
                        }

                        LogicalSize minimum_size;
                        if(const auto& min_size = Field(fields, CmdField::MinSize))
                        {
                            if(sscanf(min_size->c_str(), "%d,%d", &minimum_size.width, &minimum_size.height) != 2)
                                LOG(LogLevel::Error, "Invalid format for MinSize");
                        }

                        const auto text = [](const std::string& s) { return s; };
                        const auto flag = [](const std::string& s) { return utils::stob(s); };
                        const auto colour = [](const std::string& s) { return utils::ColorStringToInt(s); };

                        std::shared_ptr<Command> command = std::make_shared<Command>(
                            FieldOr<std::string>(fields, CmdField::Name, "", text),
                            cmd,
                            FieldOr<std::string>(fields, CmdField::Icon, "", text),
                            FieldOr<bool>(fields, CmdField::Hidden, false, flag),
                            FieldOr<uint32_t>(fields, CmdField::Color, 0, colour),
                            FieldOr<uint32_t>(fields, CmdField::BackgroundColor, 0xFFFFFF, colour),
                            FieldOr<bool>(fields, CmdField::Bold, false, flag),
                            FieldOr<std::string>(fields, CmdField::FontFace, "", text),
                            /* std::stof threw out of the XML loader on a
                               hand-edited Scale; an unreadable one now falls
                               back to the same 1.0 as an absent one. */
                            FieldOr<float>(fields, CmdField::Scale, 1.0f,
                                [](const std::string& s) { return utils::ParseOr<float>(s, 1.0f); }),
                            minimum_size,
                            FieldOr<bool>(fields, CmdField::UseSizer, false, flag),
                            FieldOr<bool>(fields, CmdField::AddToPrevSizer, false, flag));

                        temp_cmds.push_back(command);

                        //DBG("loading command page: %d, col: %d\n", p, i);
                        if(m_Mediator)
                            m_Mediator->OnCommandLoaded(p, i, command);
                    }
                    else if(v.first == "Separator")
                    {
                        int width = v.second.get_value<int>();
                        temp_cmds.push_back(Separator(width));

                        if(m_Mediator)
                            m_Mediator->OnCommandLoaded(p, i, Separator(width));
                    }
                }
                temp_cmds_per_page.push_back(std::move(temp_cmds));
            }
            storage.push_back(std::move(temp_cmds_per_page));

            if(m_Mediator)
                m_Mediator->OnPostReload(p, declared_cols, names, icons);
        }
    }
    catch(const std::exception& e)
    {
        /* utils::xml::Load already reported anything that was not well-formed,
           so what reaches here is a document that parsed but does not contain
           what this loader expects - a missing element or an unreadable value. */
        LOG(LogLevel::Error, "Malformed {}: {}", path.generic_string(), e.what());
        return false;
    }
    return true;
}

bool XmlCommandLoader::Save(const std::filesystem::path& path, CommandStorage& storage, CommandPageNames& names, CommandPageIcons& icons) const
{
    boost::property_tree::ptree pt;
    auto& root_node = pt.add_child("Commands", boost::property_tree::ptree{});
    root_node.put("Pages", std::to_string(storage.size()));

    for(uint8_t page_cnt = 1; auto& page : storage)
    {
        auto& page_node = root_node.add_child(std::format("Page_{}", page_cnt), boost::property_tree::ptree{});
        page_node.put("<xmlattr>.name", names[page_cnt - 1]);
        page_node.put("<xmlattr>.icon", icons[page_cnt - 1]);
        page_node.put("Columns", std::to_string(page.size()));
        for(uint8_t cnt = 1; auto& col : page)
        {
            auto& col_node = page_node.add_child(std::format("Col_{}", cnt), boost::property_tree::ptree{});
            for(auto& i : col)
            {
                std::visit([this, &col_node](auto& c)
                    {
                        using T = std::decay_t<decltype(c)>;
                        if constexpr(std::is_same_v<T, std::shared_ptr<Command>>)
                        {
                            c->SaveParametersToString();
                            auto& cmd_node = col_node.add_child("Cmd", boost::property_tree::ptree{});
                            if(c->GetName() != c->GetCmd() && !c->GetName().empty())
                                cmd_node.add("Name", c->GetName());

                            cmd_node.add("Execute", c->GetCmd());
                            if(!c->GetIcon().empty())
                                cmd_node.add("Icon", c->GetIcon());
                            if(c->IsConsoleHidden())
                                cmd_node.add("Hidden", true);
                            /* Written alongside the background: leaving it out
                               made every command black again on the next load. */
                            cmd_node.add("Color", utils::ColorIntToString(c->GetColor()));
                            cmd_node.add("BackgroundColor", utils::ColorIntToString(c->GetBackgroundColor()));
                            cmd_node.add("Bold", c->IsBold());
                            cmd_node.add("FontFace", c->GetFontFace());
                            cmd_node.add("Scale", c->GetScale());

                            if(c->IsUsingSizer())
                                cmd_node.add("UseSizer", true);
                            if(c->IsAddToPrevSizer())
                                cmd_node.add("AddToPrevSizer", true);
                            if(!c->GetMinSize().IsDefault())
                                cmd_node.add("MinSize", std::format("{},{}", c->GetMinSize().width, c->GetMinSize().height));
                        }
                        else if constexpr(std::is_same_v<T, Separator>)
                        {
                            col_node.add("Separator", c.width);
                        }
                        else
                            static_assert(always_false_v<T>, "XmlCommandLoader::Save Bad visitor!");
                    }, i);
            }
            cnt++;
        }
        page_cnt++;
    }

    return utils::xml::Save(path, pt);
}

void CmdExecutor::Init()
{

}

void CmdExecutor::SetMediator(ICmdHelper* mediator)
{
    m_CmdMediator = mediator;
}

void CmdExecutor::AddCommand(uint8_t page_number, uint8_t col_number, Command cmd)
{
    if(AddItem(page_number, col_number, std::make_shared<Command>(std::move(cmd))))
    {
        if(m_CmdMediator)
            m_CmdMediator->OnCommandLoaded(page_number, col_number,
                m_Commands[page_number - 1][col_number - 1].back());
    }
}

void CmdExecutor::RotateCommand([[maybe_unused]] uint8_t page_number, [[maybe_unused]] uint8_t col_number,
    [[maybe_unused]] Command& cmd, [[maybe_unused]] uint8_t direction)
{
    /* Not implemented: reordering a command inside its column has no storage
       representation yet. The operation is declared by ICmdExecutor, so it stays
       as an explicit no-op rather than being silently absent. */
}

void CmdExecutor::AddSeparator(uint8_t page_number, uint8_t col_number, Separator sep)
{
    if(AddItem(page_number, col_number, sep) && m_CmdMediator)
        m_CmdMediator->OnCommandLoaded(page_number, col_number,
            m_Commands[page_number - 1][col_number - 1].back());
}

void CmdExecutor::Execute(Command& command)
{
    command.Execute(m_CommandRunner, m_CommandTextResolver);
}

bool CmdExecutor::AddItem(uint8_t page_number, uint8_t col_number, CommandTypes item)
{
    if(page_number > 0 && page_number <= m_Commands.size() &&
        col_number > 0 && col_number <= m_Commands[page_number - 1].size())
    {
        m_Commands[page_number - 1][col_number - 1].push_back(std::move(item));
        return true;
    }
    return false;
}

namespace
{
/* The five methods below indexed their vectors without checking, so a page
   index past the end was undefined behaviour rather than a refusal. AddItem
   above has always checked; these now match it. */
[[nodiscard]] bool IsPage(const CommandStorage& storage, uint8_t page_index)
{
    if(page_index < storage.size())
        return true;
    LOG(LogLevel::Warning, "No page at index {}; there are {}", page_index, storage.size());
    return false;
}

[[nodiscard]] bool IsInsertPosition(std::size_t size, uint8_t dest_index)
{
    /* One past the end is a legal place to insert. */
    if(dest_index <= size)
        return true;
    LOG(LogLevel::Warning, "Cannot insert at {}; the range holds {}", dest_index, size);
    return false;
}
}

void CmdExecutor::AddCol(uint8_t page_index, uint8_t dest_index)
{
    if(!IsPage(m_Commands, page_index) ||
        !IsInsertPosition(m_Commands[page_index].size(), dest_index))
        return;

    std::vector<CommandTypes> temp_cmds_per_page;
    m_Commands[page_index].insert(m_Commands[page_index].begin() + dest_index, temp_cmds_per_page);
}

void CmdExecutor::DeleteCol(uint8_t page_index, uint8_t dest_index)
{
    if(!IsPage(m_Commands, page_index) || dest_index >= m_Commands[page_index].size())
        return;

    m_Commands[page_index].erase(m_Commands[page_index].begin() + dest_index);
}

void CmdExecutor::AddPage([[maybe_unused]] uint8_t page_index, uint8_t dest_index)  /* a new page starts empty, so the source page is not read */
{
    if(!IsInsertPosition(m_Commands.size(), dest_index))
        return;

    std::vector<std::vector<CommandTypes>> temp_cmds_per_page;

    std::vector<CommandTypes> cmd_types;
    cmd_types.push_back(std::make_shared<Command>("New cmd, empty", "& ping 127.0.0.1 -n 3 > nul", "", false, 0x33FF33, 0xFFFFFF, false, "", 2.0f));
    temp_cmds_per_page.push_back(std::move(cmd_types));

    m_Commands.insert(m_Commands.begin() + dest_index, std::move(temp_cmds_per_page));
    m_CommandPageNames.insert(m_CommandPageNames.begin() + dest_index, "New Page");
    m_CommandPageIcons.insert(m_CommandPageIcons.begin() + dest_index, "wxART_HARDDISK");
}

void CmdExecutor::CopyPage(uint8_t page_index, uint8_t dest_index)
{
    if(!IsPage(m_Commands, page_index) || !IsInsertPosition(m_Commands.size(), dest_index))
        return;

    std::vector<std::vector<CommandTypes>> temp_cmds_per_page;

    for(auto& cmd : m_Commands[page_index])
    {
        std::vector<CommandTypes> cmd_types;
        for(auto& col_cmd : cmd)
        {
            std::visit([this, &cmd_types](auto& c)
                {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr(std::is_same_v<T, std::shared_ptr<Command>>)
                    {
                        cmd_types.push_back(std::make_shared<Command>(*c));
                    }
                    else if constexpr(std::is_same_v<T, Separator>)
                    {
                        cmd_types.push_back(c);
                    }
                    else
                        static_assert(always_false_v<T>, "CmdExecutor::CopyPage Bad visitor!");
                }, col_cmd);
        }
        temp_cmds_per_page.push_back(std::move(cmd_types));
    }
    m_Commands.insert(m_Commands.begin() + dest_index, std::move(temp_cmds_per_page));
    m_CommandPageNames.insert(m_CommandPageNames.begin() + dest_index, m_CommandPageNames[page_index]);
    m_CommandPageIcons.insert(m_CommandPageIcons.begin() + dest_index, m_CommandPageIcons[page_index]);
}

void CmdExecutor::DeletePage(uint8_t page_index)
{
    if(!IsPage(m_Commands, page_index))
        return;

    m_Commands.erase(m_Commands.begin() + page_index);
    m_CommandPageNames.erase(m_CommandPageNames.begin() + page_index);
    m_CommandPageIcons.erase(m_CommandPageIcons.begin() + page_index);
}

bool CmdExecutor::ReloadCommandsFromFile(const char* path)
{
    XmlCommandLoader loader(m_CmdMediator);
    return loader.Load(path, m_Commands, m_CommandPageNames, m_CommandPageIcons);
}

bool CmdExecutor::Save(const char* path)
{
    XmlCommandLoader loader(m_CmdMediator);
    return loader.Save(path, m_Commands, m_CommandPageNames, m_CommandPageIcons);
}

bool CmdExecutor::SaveToTempAndReload()
{
    const char* tempfile = "temp_cmds.xml";
    bool ret = Save(tempfile);
    if(ret)
        ret = ReloadCommandsFromFile(tempfile);
    if(std::filesystem::exists(tempfile))  /* Just to make sure */
        std::filesystem::remove(tempfile);
    return ret;
}

uint8_t CmdExecutor::GetColumns() const
{
    return m_Cols;
}

CommandStorage& CmdExecutor::GetCommands()
{
    return m_Commands;
}

CommandPageNames& CmdExecutor::GetPageNames()
{
    return m_CommandPageNames;
}

CommandPageIcons& CmdExecutor::GetPageIcons()
{
    return m_CommandPageIcons;
}

