#include "TestFramework.hpp"

#include "Measurement.hpp"

namespace
{
Measurement MakeMeasurement(float temperature, int co2)
{
    return Measurement(temperature, 40.0F, co2, 10.0F, 4, 6, 8, 1000.0F,
        10.0F, 20.0F, 30.0F, 100, 200, 2, "12:00:00");
}
}

TEST_CASE(MeasurementCopyPreservesSampleCountAndValue)
{
    const auto source = MakeMeasurement(20.0F, 400);
    const auto copy = source;

    EXPECT_EQ(copy.temp, 20.0F);
    EXPECT_EQ(copy.co2, 400);
    EXPECT_EQ(copy.cnt, std::uint16_t{1});
}

TEST_CASE(MeasurementFinalizationUsesTheActualNumberOfSamples)
{
    auto average = MakeMeasurement(20.0F, 400);
    average += MakeMeasurement(24.0F, 600);
    average.Finalize();

    EXPECT_EQ(average.temp, 22.0F);
    EXPECT_EQ(average.co2, 500);
    EXPECT_EQ(average.cnt, std::uint16_t{1});
}

TEST_CASE(EmptyMeasurementFinalizationIsSafe)
{
    Measurement empty;
    empty.Finalize();

    EXPECT_EQ(empty.cnt, std::uint16_t{0});
    EXPECT_EQ(empty.temp, 0.0F);
}
