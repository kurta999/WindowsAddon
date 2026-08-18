#include "TestFramework.hpp"

#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

namespace test_framework
{
namespace
{
struct TestCase
{
    std::string_view name;
    TestFunction function;
};

std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

std::size_t failure_count = 0;
}

Registrar::Registrar(const char* name, TestFunction function)
{
    Registry().push_back({name, function});
}

void ReportFailure(const char* file, int line, const char* expression)
{
    ++failure_count;
    std::cerr << file << ':' << line << ": failure: " << expression << '\n';
}

int RunAllTests()
{
    std::size_t failed_tests = 0;
    for(const auto& test : Registry())
    {
        const auto failures_before = failure_count;
        try
        {
            test.function();
        }
        catch(const std::exception& exception)
        {
            ++failure_count;
            std::cerr << test.name << ": unexpected exception: " << exception.what() << '\n';
        }
        catch(...)
        {
            ++failure_count;
            std::cerr << test.name << ": unexpected non-standard exception\n";
        }

        if(failure_count == failures_before)
            std::cout << "[PASS] " << test.name << '\n';
        else
        {
            ++failed_tests;
            std::cout << "[FAIL] " << test.name << '\n';
        }
    }

    std::cout << Registry().size() << " tests, " << failed_tests << " failed\n";
    return failed_tests == 0 ? 0 : 1;
}
}

int main()
{
    return test_framework::RunAllTests();
}
