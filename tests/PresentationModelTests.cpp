#include "TestFramework.hpp"

#include "AppNotification.hpp"
#include "interface/IBasicGuiCustomization.hpp"
#include "interface/IDataSender.hpp"

#include <variant>

TEST_CASE(LogicalSizeRepresentsDefaultWithoutGuiTypes)
{
    const LogicalSize default_size;
    const LogicalSize explicit_size{320, 200};

    EXPECT_TRUE(default_size.IsDefault());
    EXPECT_FALSE(explicit_size.IsDefault());
    EXPECT_EQ(explicit_size.width, 320);
    EXPECT_EQ(explicit_size.height, 200);
}

TEST_CASE(DataEntryBaseCopyPreservesRequestAndResponse)
{
    std::uint8_t request[] = {1, 2};
    std::uint8_t response[] = {3, 4, 5};
    const DataEntryBase original(request, std::size(request), response, std::size(response));
    const DataEntryBase copy = original;

    EXPECT_EQ(copy.data, original.data);
    EXPECT_EQ(copy.response, original.response);
}

TEST_CASE(AppNotificationCarriesATypeCheckedPayload)
{
    AppNotification notification = BackupCompletedNotification{
        1000, 2, 128, 1, std::filesystem::path("backup")};

    EXPECT_TRUE(std::holds_alternative<BackupCompletedNotification>(notification));
    const auto& completed = std::get<BackupCompletedNotification>(notification);
    EXPECT_EQ(completed.file_count, std::size_t{2});
    EXPECT_EQ(completed.bytes_copied, std::size_t{128});
}
