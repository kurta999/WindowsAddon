#pragma once

#include <cstddef>

namespace test_framework
{
using TestFunction = void (*)();

class Registrar
{
public:
    Registrar(const char* name, TestFunction function);
};

void ReportFailure(const char* file, int line, const char* expression);
int RunAllTests();
}

#define WINDOWSHELPER_TEST_JOIN_IMPL(left, right) left##right
#define WINDOWSHELPER_TEST_JOIN(left, right) WINDOWSHELPER_TEST_JOIN_IMPL(left, right)

#define TEST_CASE(name) \
    static void WINDOWSHELPER_TEST_JOIN(WindowsHelperTest_, name)(); \
    static const test_framework::Registrar WINDOWSHELPER_TEST_JOIN(WindowsHelperRegistrar_, name)( \
        #name, &WINDOWSHELPER_TEST_JOIN(WindowsHelperTest_, name)); \
    static void WINDOWSHELPER_TEST_JOIN(WindowsHelperTest_, name)()

#define EXPECT_TRUE(expression) \
    do { \
        if(!(expression)) \
            test_framework::ReportFailure(__FILE__, __LINE__, "Expected true: " #expression); \
    } while(false)

#define EXPECT_FALSE(expression) \
    do { \
        if(expression) \
            test_framework::ReportFailure(__FILE__, __LINE__, "Expected false: " #expression); \
    } while(false)

#define EXPECT_EQ(actual, expected) \
    do { \
        if(!((actual) == (expected))) \
            test_framework::ReportFailure(__FILE__, __LINE__, \
                "Expected equality: " #actual " == " #expected); \
    } while(false)

#define EXPECT_LE(actual, expected) \
    do { \
        if(!((actual) <= (expected))) \
            test_framework::ReportFailure(__FILE__, __LINE__, \
                "Expected less than or equal: " #actual " <= " #expected); \
    } while(false)

#define EXPECT_THROW(statement, exception_type) \
    do { \
        bool windowshelper_caught_expected_exception = false; \
        try { statement; } \
        catch(const exception_type&) { windowshelper_caught_expected_exception = true; } \
        catch(...) {} \
        if(!windowshelper_caught_expected_exception) \
            test_framework::ReportFailure(__FILE__, __LINE__, \
                "Expected exception: " #exception_type " from " #statement); \
    } while(false)

#define ASSERT_TRUE(expression) \
    do { \
        if(!(expression)) { \
            test_framework::ReportFailure(__FILE__, __LINE__, "Expected true: " #expression); \
            return; \
        } \
    } while(false)

#define ASSERT_EQ(actual, expected) \
    do { \
        if(!((actual) == (expected))) { \
            test_framework::ReportFailure(__FILE__, __LINE__, \
                "Expected equality: " #actual " == " #expected); \
            return; \
        } \
    } while(false)
