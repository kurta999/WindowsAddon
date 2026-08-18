#include "pch.hpp"

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

std::string Command::HandleParameters()
{
    constexpr size_t param_len = std::char_traits<char>::length("[({PARAM:");
    size_t param_count = 0;
    size_t pos = 1;

    std::string new_cmd{ m_cmd };
    while(pos < m_cmd.length() - 1)
    {
        size_t first_end = new_cmd.find("})]", pos + 3);
        size_t first_pos = new_cmd.substr(0, first_end).find("[({PARAM:", pos - 1);

        if(first_pos != std::string::npos && first_end != std::string::npos)
        {
            std::string ret = utils::extract_string(new_cmd, first_pos, first_end, param_len);
            DBG("ret: %s", ret.c_str());

            size_t to_erase = (first_end + 3) - first_pos;
            size_t replace_len = m_params.size() <= param_count ? ret.length() : m_params[param_count].length();

            new_cmd.erase(first_pos, to_erase);
            new_cmd.insert(first_pos, m_params.size() <= param_count ? ret : m_params[param_count]);

            pos = first_pos + replace_len;
            ++param_count;
        }
        else
        {
            break;
        }
    }
    return new_cmd;
}

void Command::LoadParametersFromString()
{
    constexpr size_t param_len = std::char_traits<char>::length("[({PARAM:");
    size_t param_count = 0;
    size_t pos = 1;

    std::string new_cmd{ m_cmd };
    while(pos < m_cmd.length() - 1)
    {
        size_t first_end = new_cmd.find("})]", pos + 3);
        size_t first_pos = new_cmd.substr(0, first_end).find("[({PARAM:", pos - 1);

        if(first_pos != std::string::npos && first_end != std::string::npos)
        {
            std::string ret = utils::extract_string(new_cmd, first_pos, first_end, param_len);
            m_params.push_back(ret);

            pos = first_end;
            ++param_count;
        }
        else
        {
            break;
        }
    }
}

void Command::SaveParametersToString()
{
    constexpr size_t param_len = std::char_traits<char>::length("[({PARAM:");
    size_t param_count = 0;
    size_t pos = 1;

    std::string new_cmd{ m_cmd };
    while(pos < m_cmd.length() - 1)
    {
        size_t first_end = new_cmd.find("})]", pos + 3);
        size_t first_pos = new_cmd.substr(0, first_end).find("[({PARAM:", pos - 1);

        if(first_pos != std::string::npos && first_end != std::string::npos)
        {
            std::string ret = new_cmd.substr(first_pos + param_len, (first_end - (first_pos + param_len)));
            DBG("ret: %s", ret.c_str());

            size_t to_erase = ret.length();

            if(m_params.size() <= param_count)
            {
                break;
            }

            new_cmd.erase(first_pos + param_len, to_erase);
            new_cmd.insert(first_pos + param_len, m_params[param_count]);

            pos = first_pos + param_len + to_erase;
            ++param_count;
        }
        else
        {
            break;
        }
    }
    m_cmd = new_cmd;
}

XmlCommandLoader::XmlCommandLoader(ICmdHelper* mediator)
{
    m_Mediator = mediator;
}

bool XmlCommandLoader::Load(const std::filesystem::path& path, CommandStorage& storage, CommandPageNames& names, CommandPageIcons& icons)
{
    if(!std::filesystem::exists(COMMAND_FILE_PATH))
    {
        CmdExecutor::WriteDefaultCommandsFile();
        LOG(LogLevel::Normal, "Default {} is missing, creating one", COMMAND_FILE_PATH);
    }

    bool ret = true;
    boost::property_tree::ptree pt;
    try
    {
        read_xml(path.generic_string(), pt);

        storage.clear();
        m_Pages = pt.get_child("Commands").get_child("Pages").get_value<uint8_t>();
        if(m_Mediator)
            m_Mediator->OnPreReload(m_Pages);
        for(uint8_t p = 1; p <= m_Pages; p++)
        {
            auto pages_child = pt.get_child("Commands").get_child_optional(std::format("Page_{}", p));
            if(!pages_child.has_value())
            {
                LOG(LogLevel::Warning, "No child found with 'Page_{}', loading has been aborted!", p);
                break;
            }

            std::string page_name = pages_child->get<std::string>("<xmlattr>.name");
            names.push_back(std::move(page_name));
            std::string page_icon = pages_child->get<std::string>("<xmlattr>.icon");

            if(page_icon.empty())
                page_icon = "wxART_HARDDISK"; // Do not let page icon empty, set it to default
            icons.push_back(std::move(page_icon));

            m_Cols = pages_child->get_child("Columns").get_value<uint8_t>();
            if(m_Mediator)
                m_Mediator->OnPreReloadColumns(p, m_Cols);

            std::vector<std::vector<CommandTypes>> temp_cmds_per_page;
            for(uint8_t i = 1; i <= m_Cols; i++)
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
                        boost::optional<std::string> name;
                        boost::optional<std::string> icon;
                        boost::optional<std::string> is_hidden;
                        boost::optional<std::string> color;
                        boost::optional<std::string> bg_color;
                        boost::optional<std::string> is_bold;
                        boost::optional<std::string> font_face;
                        boost::optional<std::string> scale;
                        boost::optional<std::string> use_sizer;
                        boost::optional<std::string> add_to_prev_sizer;
                        boost::optional<std::string> min_size;

                        auto is_name_present = v.second.get_child_optional("Execute");
                        if(is_name_present.has_value())
                        {
                            cmd = v.second.get_child("Execute").get_value<std::string>();

                            utils::xml::ReadChildIfexists<std::string>(v, "Name", name);
                            utils::xml::ReadChildIfexists<std::string>(v, "Icon", icon);
                            utils::xml::ReadChildIfexists<std::string>(v, "Hidden", is_hidden);
                            utils::xml::ReadChildIfexists<std::string>(v, "Color", color);
                            utils::xml::ReadChildIfexists<std::string>(v, "BackgroundColor", bg_color);
                            utils::xml::ReadChildIfexists<std::string>(v, "Bold", is_bold);
                            utils::xml::ReadChildIfexists<std::string>(v, "FontFace", font_face);
                            utils::xml::ReadChildIfexists<std::string>(v, "Scale", scale);
                            utils::xml::ReadChildIfexists<std::string>(v, "UseSizer", use_sizer);
                            utils::xml::ReadChildIfexists<std::string>(v, "AddToPrevSizer", add_to_prev_sizer);
                            utils::xml::ReadChildIfexists<std::string>(v, "MinSize", min_size);
                        }
                        else
                        {
                            cmd = v.second.get_value<std::string>();

                            name = v.second.get_optional<std::string>("<xmlattr>.name");
                            icon = v.second.get_optional<std::string>("<xmlattr>.icon");
                            is_hidden = v.second.get_optional<std::string>("<xmlattr>.hidden");
                            color = v.second.get_optional<std::string>("<xmlattr>.color");
                            bg_color = v.second.get_optional<std::string>("<xmlattr>.bg_color");
                            is_bold = v.second.get_optional<std::string>("<xmlattr>.bold");
                            font_face = v.second.get_optional<std::string>("<xmlattr>.font_face");
                            scale = v.second.get_optional<std::string>("<xmlattr>.scale");
                            use_sizer = v.second.get_optional<std::string>("<xmlattr>.use_sizer");
                            add_to_prev_sizer = v.second.get_optional<std::string>("<xmlattr>.add_to_prev_sizer");
                            min_size = v.second.get_optional<std::string>("<xmlattr>.min_size");
                        }

                        if(cmd.empty())
                        {
                            if(!name)
                                name = "Unknown";
                            LOG(LogLevel::Warning, "Empty cmd for command: {}", *name);
                        }

                        LogicalSize minimum_size;
                        if(min_size.has_value())
                        {
                            if(sscanf(min_size->c_str(), "%d,%d", &minimum_size.width, &minimum_size.height) != 2)
                                LOG(LogLevel::Error, "Invalid format for MinSize");
                        }

                        std::shared_ptr<Command> command = std::make_shared<Command>(
                            name.has_value() ? *name : "",
                            cmd,
                            icon.has_value() ? *icon : "",
                            is_hidden.has_value() ? utils::stob(*is_hidden) : false,
                            color.has_value() ? utils::ColorStringToInt(*color) : 0,
                            bg_color.has_value() ? utils::ColorStringToInt(*bg_color) : 0xFFFFFF,
                            is_bold.has_value() ? utils::stob(*is_bold) : false,
                            font_face.has_value() ? *font_face : "",
                            scale.has_value() ? std::stof(*scale) : 1.0f,
                            minimum_size,
                            use_sizer.has_value() ? utils::stob(*use_sizer) : false,
                            add_to_prev_sizer.has_value() ? utils::stob(*add_to_prev_sizer) : false);

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
                m_Mediator->OnPostReload(p, m_Cols, names, icons);
        }
    }
    catch(const boost::property_tree::xml_parser_error& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}, {}", e.filename(), e.what());
        ret = false;
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}", e.what());
        ret = false;
    }
    return ret;
}

bool XmlCommandLoader::Save(const std::filesystem::path& path, CommandStorage& storage, CommandPageNames& names, CommandPageIcons& icons) const
{
    bool ret = true;
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

    try
    {
        boost::property_tree::write_xml(path.generic_string(), pt, std::locale(),
            boost::property_tree::xml_writer_make_settings<boost::property_tree::ptree::key_type>('\t', 1));
    }
    catch(...)
    {
        ret = false;
    }
    return ret;
}

void CmdExecutor::Init()
{

}

void CmdExecutor::SetMediator(ICmdHelper* mediator)
{
    m_CmdMediator = mediator;
}

void CmdExecutor::AddCommand(uint8_t page, uint8_t col, Command cmd)
{
    if(AddItem(page, col, std::make_shared<Command>(std::move(cmd))))
    {
        if(m_CmdMediator)
            m_CmdMediator->OnCommandLoaded(page, col, m_Commands[page - 1][col - 1].back());
    }
}

void CmdExecutor::RotateCommand(uint8_t page, uint8_t col, Command& cmd, uint8_t direction)
{
//    if(m_Commands[page - 1][col - 1].back() == cmd)
}

void CmdExecutor::AddSeparator(uint8_t page, uint8_t col, Separator sep)
{
    if(AddItem(page, col, sep) && m_CmdMediator)
        m_CmdMediator->OnCommandLoaded(page, col, m_Commands[page - 1][col - 1].back());
}

void CmdExecutor::Execute(Command& command)
{
    command.Execute(m_CommandRunner, m_CommandTextResolver);
}

bool CmdExecutor::AddItem(uint8_t page, uint8_t col, CommandTypes item)
{
    if(page > 0 && page <= m_Commands.size() && col > 0 && col <= m_Commands[page - 1].size())
    {
        m_Commands[page - 1][col - 1].push_back(std::move(item));
        return true;
    }
    return false;
}

void CmdExecutor::AddCol(uint8_t page, uint8_t dest_index)
{
    std::vector<CommandTypes> temp_cmds_per_page;
    m_Commands[page].insert(m_Commands[page].begin() + dest_index, temp_cmds_per_page);
}

void CmdExecutor::DeleteCol(uint8_t page, uint8_t dest_index)
{
    m_Commands[page].erase(m_Commands[page].begin() + dest_index);
}

void CmdExecutor::AddPage(uint8_t page, uint8_t dest_index)
{
    std::vector<std::vector<CommandTypes>> temp_cmds_per_page;

    std::vector<CommandTypes> cmd_types;
    cmd_types.push_back(std::make_shared<Command>("New cmd, empty", "& ping 127.0.0.1 -n 3 > nul", "", false, 0x33FF33, 0xFFFFFF, false, "", 2.0f));
    temp_cmds_per_page.push_back(std::move(cmd_types));

    m_Commands.insert(m_Commands.begin() + dest_index, std::move(temp_cmds_per_page));
    m_CommandPageNames.insert(m_CommandPageNames.begin() + dest_index, "New Page");
    m_CommandPageIcons.insert(m_CommandPageIcons.begin() + dest_index, "wxART_HARDDISK");
}

void CmdExecutor::CopyPage(uint8_t page, uint8_t dest_index)
{
    std::vector<std::vector<CommandTypes>> temp_cmds_per_page;

    for(auto& cmd : m_Commands[page])
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
    m_CommandPageNames.insert(m_CommandPageNames.begin() + dest_index, m_CommandPageNames[page]);
    m_CommandPageIcons.insert(m_CommandPageIcons.begin() + dest_index, m_CommandPageIcons[page]);
}

void CmdExecutor::DeletePage(uint8_t page)
{
    m_Commands.erase(m_Commands.begin() + page);
    m_CommandPageNames.erase(m_CommandPageNames.begin() + page);
    m_CommandPageIcons.erase(m_CommandPageIcons.begin() + page);
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

void CmdExecutor::WriteDefaultCommandsFile()
{
    std::string_view file_content = R"xml(<Commands>
  <Pages>2</Pages>
  <Page_1 name="Board">
    <Columns>4</Columns>
    <Col_1>
      <Cmd>
        <Name>Directory</Name>
        <Execute>cd C:\ &amp; dir &amp; ping 127.0.0.1 -n [({PARAM:3})] > nul</Execute>
        <Color>0xFF0000</Color>
        <BackgroundColor>green</BackgroundColor>
        <Bold>true</Bold>
        <Scale>2.0</Scale>
      </Cmd>
      <Cmd>
        <Name>Set date</Name>
        <Execute>cd C:\ &amp; dir &amp; ping 127.0.0.1 -n 3 > nul</Execute>
        <Color>0xFF0000</Color>
        <BackgroundColor>green</BackgroundColor>
        <Bold>true</Bold>
        <Scale>2.0</Scale>
      </Cmd>
      <Cmd>cd ..</Cmd>
      <Separator>4</Separator>
      <Cmd>cd2 ..</Cmd>
    </Col_1>
    <Col_2>
      <Cmd>dir C:</Cmd>
      <Cmd>cd ../..</Cmd>
    </Col_2>
  </Page_1>
  <Page_2 name="Linux VM">
    <Columns>2</Columns>
    <Col_1>
      <Cmd>
        <Name>Print directory</Name>
        <Execute>cd C:\ &amp; dir &amp; ping 127.0.0.1 -n 3 > nul</Execute>
        <Color>0xFF0000</Color>
        <BackgroundColor>green</BackgroundColor>
        <Bold>true</Bold>
        <Scale>2.0</Scale>
      </Cmd>
    </Col_1>
  </Page_2>
</Commands>)xml";
    std::ofstream out(COMMAND_FILE_PATH, std::ofstream::binary);
    if(out)
        out.write(file_content.data(), static_cast<std::streamsize>(file_content.size()));
    else
        LOG(LogLevel::Error, "Failed to write default commands file!");
}
