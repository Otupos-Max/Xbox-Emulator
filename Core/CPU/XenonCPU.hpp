
#pragma once

#include <cstdint>
#include "PowerPC/PPCInterpreter.hpp"

class XenonCPU {
public:
    static constexpr int CORE_COUNT = 3;
    static constexpr int THREADS_PER_CORE = 2;

    struct Core {
        PPCInterpreter threads[THREADS_PER_CORE];
        bool running = false;
    };

    XenonCPU();

    void reset();
    void start();
    void stop();

    void executeThread(int coreIndex, int threadIndex,
                       const PPCInstruction& instruction);

    Core& getCore(int index);
    const Core& getCore(int index) const;

private:
    Core cores[CORE_COUNT];
    bool initialized = false;
};
