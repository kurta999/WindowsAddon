#pragma once

#include <mutex>

template<class T>
class CSingleton
{
public:
    CSingleton(const CSingleton&) = delete;
    CSingleton& operator=(const CSingleton&) = delete;
    CSingleton(CSingleton&&) = delete;
    CSingleton& operator=(CSingleton&&) = delete;

    [[nodiscard]] static T* Get()
    {
        std::scoped_lock lock(InstanceMutex());
        auto& instance = Instance();
        if(!instance)
            instance = new T;
        return instance;
    }

    // Returns the current instance without creating one. Teardown code should
    // use this to avoid resurrecting services after the composition root has
    // destroyed them.
    [[nodiscard]] static T* TryGet() noexcept
    {
        std::scoped_lock lock(InstanceMutex());
        return Instance();
    }

    // Legacy singletons are explicitly released by the application composition
    // root. Unlike std::once_flag, this implementation can be constructed again
    // after shutdown, which is important for repeatable application/test cycles.
    static void Destroy()
    {
        T* instance = nullptr;
        {
            std::scoped_lock lock(InstanceMutex());
            instance = Instance();
            Instance() = nullptr;
        }
        delete instance;
    }

protected:
    CSingleton() = default;
    virtual ~CSingleton() = default;

private:
    [[nodiscard]] static T*& Instance()
    {
        static T* instance = nullptr;
        return instance;
    }

    [[nodiscard]] static std::mutex& InstanceMutex()
    {
        static std::mutex mutex;
        return mutex;
    }
};
