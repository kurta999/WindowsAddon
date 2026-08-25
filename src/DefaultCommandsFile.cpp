#include "pch_core.hpp"
#include "DefaultCommandsFile.hpp"
#include "Logger.hpp"

#include <fstream>
#include <string_view>

bool default_commands::Write(const std::filesystem::path& path)
{
    /* The content is unchanged from CmdExecutor::WriteDefaultCommandsFile.
       The path is a parameter now rather than the COMMAND_FILE_PATH constant,
       so this knows what to write without knowing where it goes. */
    const std::string_view file_content = R"xml(<Commands>
  <Pages>2</Pages>
  <Page_1 name="Board" icon="wxART_HARDDISK">
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
  <Page_2 name="Linux VM" icon="wxART_HARDDISK">
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
    std::ofstream out(path, std::ofstream::binary);
    if(!out)
    {
        LOG(LogLevel::Error, "Failed to write default commands file {}!",
            path.generic_string());
        return false;
    }

    out.write(file_content.data(), static_cast<std::streamsize>(file_content.size()));
    return out.good();
}
