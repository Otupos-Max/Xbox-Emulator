
#include "XenonCPU.hpp"
#include <stdexcept>

XenonCPU::XenonCPU() {
    reset();
}

void XenonCPU::reset() {
    for (auto& core : cores) {
        core = {};
    }

    initialized = true;
}

void XenonCPU::start() {
    if (!initialized) reset();

    for (auto& core : cores) {
        core.running = true;
    }
}

void XenonCPU::stop() {
    for (auto& core : cores) {
        core.running = false;
    }
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
