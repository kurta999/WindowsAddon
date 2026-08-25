#pragma once

#include <filesystem>

// !\brief The starter Cmds.xml the application writes when it finds none.
//
// This was CmdExecutor::WriteDefaultCommandsFile, a static member holding a
// forty-four line XML literal - and XmlCommandLoader called it. The loader is
// declared above CmdExecutor and CmdExecutor creates the loader, so that call
// closed a cycle between the two for the sake of one piece of file content that
// belongs to neither.
namespace default_commands
{
// !\brief Write the starter file to `path`.
// !\return False when the file could not be opened for writing.
[[nodiscard]] bool Write(const std::filesystem::path& path);
}
