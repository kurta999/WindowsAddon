#pragma once

#include <cstdint>
#include <string>
#include <utility>

// Domain value object shared by acquisition, persistence, and graph generation.
// It deliberately contains no GUI or database dependencies.
class Measurement
{
public:
    Measurement() = default;

    Measurement(float temperature, float humidity, int carbon_dioxide, float volatile_organic_compounds,
                int carbon_monoxide, int pm_2_5, int pm_10, float air_pressure,
                float red, float green, float blue, int illuminance, int color_temperature,
                int ultraviolet, std::string measurement_time)
        : temp(temperature), hum(humidity), voc(volatile_organic_compounds), pressure(air_pressure),
          r(red), g(green), b(blue), co2(carbon_dioxide), co(carbon_monoxide), pm25(pm_2_5),
          pm10(pm_10), lux(illuminance), cct(color_temperature), uv(ultraviolet),
          time(std::move(measurement_time)), cnt(1)
    {
    }

    Measurement& operator+=(const Measurement& rhs)
    {
        temp += rhs.temp;
        hum += rhs.hum;
        pressure += rhs.pressure;
        r += rhs.r;
        g += rhs.g;
        b += rhs.b;
        co2 += rhs.co2;
        voc += rhs.voc;
        co += rhs.co;
        pm25 += rhs.pm25;
        pm10 += rhs.pm10;
        lux += rhs.lux;
        cct += rhs.cct;
        uv += rhs.uv;
        cnt = static_cast<std::uint16_t>(cnt + rhs.cnt);
        return *this;
    }

    void Finalize()
    {
        if(cnt == 0)
            return;

        const auto sample_count = static_cast<float>(cnt);
        temp /= sample_count;
        hum /= sample_count;
        pressure /= sample_count;
        r /= sample_count;
        g /= sample_count;
        b /= sample_count;
        voc /= sample_count;
        co2 /= cnt;
        co /= cnt;
        pm25 /= cnt;
        pm10 /= cnt;
        lux /= cnt;
        cct /= cnt;
        uv /= cnt;
        cnt = 1;
    }

    float temp = 0.0F;
    float hum = 0.0F;
    float voc = 0.0F;
    float pressure = 0.0F;
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    int co2 = 0;
    int co = 0;
    int pm25 = 0;
    int pm10 = 0;
    int lux = 0;
    int cct = 0;
    int uv = 0;
    std::string time;
    std::uint16_t cnt = 0;
};
