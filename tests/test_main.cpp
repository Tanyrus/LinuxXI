#include "test.h"

#include <exception>

int main()
{
    int failed = 0;
    for (const test::Case& c : test::Registry())
    {
        try
        {
            c.fn();
            std::printf("PASS %s\n", c.name);
        }
        catch (const test::Failure& f)
        {
            ++failed;
            std::printf("FAIL %s\n     %s\n", c.name, f.message.c_str());
        }
        catch (const std::exception& e)
        {
            ++failed;
            std::printf("FAIL %s\n     threw %s\n", c.name, e.what());
        }
        catch (...)
        {
            ++failed;
            std::printf("FAIL %s\n     threw something that is not a std::exception\n", c.name);
        }
        std::fflush(stdout);
    }
    std::printf("\n%zu tests, %d failed\n", test::Registry().size(), failed);
    return failed == 0 ? 0 : 1;
}
