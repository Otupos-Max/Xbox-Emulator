
#include "XenonCPU.hpp"
#include <stdexcept>

XenonCPU::XenonCPU() {
    reset();
}

void XenonCPU::reset() {
    for (auto& core : cores) {
        core.running = false;

        for (auto& thread : core.threads) {
            thread.reset();
        }
    }

    initialized = true;
}

void XenonCPU::start() {
    if (!initialized) reset();

    for (auto& core : cores) {
        core.running = true;

        for (auto& thread : core.threads) {
            thread.getState().running = true;
        }
    }
}

void XenonCPU::stop() {
    for (auto& core : cores) {
        core.running = false;

        for (auto& thread : core.threads) {
            thread.getState().running = false;
        }
    }
}

void XenonCPU::executeThread(
    int coreIndex,
    int threadIndex,
    const PPCInstruction& instruction
) {
    if (coreIndex < 0 || coreIndex >= CORE_COUNT ||
        threadIndex < 0 || threadIndex >= THREADS_PER_CORE) {
        throw std::out_of_range("Invalid Xenon thread");
    }

    Core& core = cores[coreIndex];

    if (!core.running) return;

    core.threads[threadIndex].execute(instruction);
}

XenonCPU::Core& XenonCPU::getCore(int index) {
    if (index < 0 || index >= CORE_COUNT)
        throw std::out_of_range("Invalid Xenon core");

    return cores[index];
}

const XenonCPU::Core& XenonCPU::getCore(int index) const {
    if (index < 0 || index >= CORE_COUNT)
        throw std::out_of_range("Invalid Xenon core");

    return cores[index];
}
