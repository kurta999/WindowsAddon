#pragma once

class Measurement;

class ISensorObserver
{
public:
    virtual ~ISensorObserver() = default;
    virtual void OnMeasurementUpdated(const Measurement& m, size_t recv_count) = 0;
};
