
#include "PPCDecoder.hpp"

PPCInstruction PPCDecoder::decode(
    uint32_t instruction,
    uint32_t address
) {
    PPCInstruction result{};

    result.raw = instruction;
    result.address = address;

    const uint8_t primary = instruction >> 26;

    result.rd = (instruction >> 21) & 0x1F;
    result.ra = (instruction >> 16) & 0x1F;
    result.rb = (instruction >> 11) & 0x1F;

    result.immediate = static_cast<int16_t>(
        instruction & 0xFFFF
    );

    switch (primary) {
        case 14:
            result.opcode = PPCOpcode::Addi;
            break;

        case 24:
            result.opcode = PPCOpcode::Ori;
            break;

        case 15:
            result.opcode = PPCOpcode::Oris;
            break;

        case 18:
            result.opcode = PPCOpcode::Branch;
            break;

        case 24 + 1:
            result.opcode = PPCOpcode::Unknown;
            break;

        case 31: {
            const uint16_t extended = (instruction >> 1) & 0x3FF;

            if (extended == 266)
                result.opcode = PPCOpcode::Add;

            break;
        }

        default:
            break;
    }

    if (instruction == 0x60000000)
        result.opcode = PPCOpcode::Nop;

    return result;
}
