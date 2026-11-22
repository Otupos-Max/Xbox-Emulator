
#pragma once

#include "PPCDecoder.hpp"
#include <cstdint>

namespace xboxemu::memory {
class MemoryManager;
}

class PPCInterpreter {
public:
    struct State {
        uint64_t gpr[32]{};
        uint64_t pc = 0;
        uint64_t lr = 0;
        uint64_t ctr = 0;
        uint32_t cr = 0;
        bool running = false;
    };

    void reset();
    void execute(const PPCInstruction& instruction);
    bool step(xboxemu::memory::MemoryManager& memory);

    State& getState();
    const State& getState() const;

private:
    State state;
};
