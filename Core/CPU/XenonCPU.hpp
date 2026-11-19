
#pragma once

#include <cstdint>

class XenonCPU {
public:
    static constexpr int CORE_COUNT = 3;
    static constexpr int THREADS_PER_CORE = 2;

    struct Core {
        uint64_t gpr[32]{};
        uint64_t pc = 0;
        uint64_t lr = 0;
        uint64_t ctr = 0;
        uint32_t cr = 0;
        bool running = false;
    };

    XenonCPU();

    void reset();
    void start();
    void stop();

    Core& getCore(int index);
    const Core& getCore(int index) const;

private:
    Core cores[CORE_COUNT];
    bool initialized = false;
};

