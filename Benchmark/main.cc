#include "Instrumentor.hpp"
#include <iostream>

#define PROFILING 1
#ifdef  PROFILING
#define SCOPE_BENCHMARK(name) InstrumentationTimer timer##__LINE__(name)
#define FUNCTION_BENCHMARK() SCOPE_BENCHMARK(__FUNCTION__) // __FUNCSIG__
#else
#define SCOPE_BENCHMARK(name)
#endif

namespace
{
    int Test1() {
        FUNCTION_BENCHMARK();
        int cringe = 0;
        __noop(cringe);
        for (int i = 0; i < 1'000'000'000; i++)
            cringe += 1;
        return cringe;
    }

    auto Test2{ [] {
        FUNCTION_BENCHMARK();
        int cringe = 0;
        __noop(cringe);
        for (int i = 0; i < 1'000'000'000; i++)
            cringe -= 1;
        return cringe;
    } };

    void RunBenchmark() {
        SCOPE_BENCHMARK("Run benchmark");
        std::cout << "Run benchmark...";
        std::thread a([]() {Test1(); });
        auto res = Test2();
        a.join();
    }
};

int main() {
    Instrumentor::Get().BeginSession("Profile");
    RunBenchmark();
    Instrumentor::Get().FinishSession();
    return 0;
}
