#include "TestFramework.hpp"

#include "automation/ScriptCommandResolver.hpp"
#include "automation/ScriptLaunchService.hpp"

#include <string>

namespace
{
class FakeFileSystem final : public IFileSystem
{
public:
    bool Exists(const std::filesystem::path& path) const noexcept override
    {
        last_path = path;
        return exists;
    }

    bool exists = true;
    mutable std::filesystem::path last_path;
};

class FakeCommandRunner final : public ICommandRunner
{
public:
    bool Start(std::string_view command, bool hidden) override
    {
        last_command = command;
        last_hidden = hidden;
        ++start_count;
        return succeeds;
    }

    bool succeeds = true;
    bool last_hidden = true;
    int start_count = 0;
    std::string last_command;
};
}

TEST_CASE(ScriptLaunchServiceUsesInjectedFileSystemAndRunner)
{
    FakeFileSystem file_system;
    FakeCommandRunner runner;
    ScriptCommandResolver resolver;
    ScriptLaunchService service(runner, file_system, resolver);
    const std::filesystem::path script = "folder/my script.py";

    EXPECT_EQ(service.Launch(script), ScriptLaunchResult::Started);
    EXPECT_EQ(file_system.last_path, script);
    EXPECT_EQ(runner.start_count, 1);
    EXPECT_FALSE(runner.last_hidden);
#ifdef _WIN32
    const std::string interpreter = "python";
#else
    const std::string interpreter = "python3";
#endif
    EXPECT_EQ(runner.last_command, interpreter + " \"" + script.string() + "\"");
}

TEST_CASE(ScriptLaunchServiceRejectsMissingAndUnsupportedFilesWithoutStartingProcess)
{
    FakeFileSystem file_system;
    FakeCommandRunner runner;
    ScriptCommandResolver resolver;
    ScriptLaunchService service(runner, file_system, resolver);

    file_system.exists = false;
    EXPECT_EQ(service.Launch("missing.js"), ScriptLaunchResult::FileNotFound);
    EXPECT_EQ(runner.start_count, 0);

    file_system.exists = true;
    EXPECT_EQ(service.Launch("notes.txt"), ScriptLaunchResult::UnsupportedType);
    EXPECT_EQ(runner.start_count, 0);
}

TEST_CASE(ScriptLaunchServiceReportsProcessStartFailure)
{
    FakeFileSystem file_system;
    FakeCommandRunner runner;
    runner.succeeds = false;
    ScriptCommandResolver resolver;
    ScriptLaunchService service(runner, file_system, resolver);

    EXPECT_EQ(service.Launch("tool.js"), ScriptLaunchResult::StartFailed);
    EXPECT_EQ(runner.start_count, 1);
    EXPECT_EQ(runner.last_command, std::string("node \"tool.js\""));
}

TEST_CASE(ScriptLaunchServiceAcceptsAnInjectedInterpreterRegistry)
{
    FakeFileSystem file_system;
    FakeCommandRunner runner;
    ScriptCommandResolver resolver({{".rb", "ruby"}});
    ScriptLaunchService service(runner, file_system, resolver);

    EXPECT_EQ(service.Launch("tools/report.RB"), ScriptLaunchResult::Started);
    EXPECT_EQ(runner.last_command, std::string("ruby \"tools/report.RB\""));
    EXPECT_EQ(service.Launch("tools/report.py"), ScriptLaunchResult::UnsupportedType);
    EXPECT_EQ(runner.start_count, 1);
}
