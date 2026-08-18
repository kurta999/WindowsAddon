#include "TestFramework.hpp"

#include "automation/CommandTextResolver.hpp"

TEST_CASE(CommandTextResolverLeavesUnknownCommandsUnchanged)
{
    const CommandTextResolver resolver;
    EXPECT_EQ(resolver.Resolve("Unknown", "echo value"), std::string("echo value"));
}

TEST_CASE(CommandTextResolverCanBeExtendedWithoutChangingCommand)
{
    CommandTextResolver resolver;
    resolver.Register("Prefix", [](std::string_view command) {
        return "wrapped:" + std::string(command);
    });

    EXPECT_EQ(resolver.Resolve("Prefix", "payload"), std::string("wrapped:payload"));
}
