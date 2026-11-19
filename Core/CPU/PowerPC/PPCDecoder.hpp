
#pragma once

#include <cstdint>

enum class PPCOpcode : uint8_t {
    Unknown,
    Addi,
    Add,
    Ori,
    Oris,
    Branch,
    BranchLink,
    Nop
};

struct PPCInstruction {
    PPCOpcode opcode = PPCOpcode::Unknown;

    uint8_t rd = 0;
    uint8_t ra = 0;
    uint8_t rb = 0;

    int16_t immediate = 0;
    uint32_t address = 0;
    uint32_t raw = 0;
};

class PPCDecoder {
public:
    static PPCInstruction decode(
        uint32_t instruction,
        uint32_t address
    );
};

