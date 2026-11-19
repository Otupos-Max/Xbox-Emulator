
#include "PPCInterpreter.hpp"

void PPCInterpreter::reset() {
    state = {};
}

void PPCInterpreter::execute(const PPCInstruction& instruction) {
    if (!state.running) return;

    const uint64_t nextPC = state.pc + 4;

    switch (instruction.opcode) {
        case PPCOpcode::Addi:
            state.gpr[instruction.rd] =
                (instruction.ra == 0 ? 0 : state.gpr[instruction.ra]) +
                instruction.immediate;
            break;

        case PPCOpcode::Add:
            state.gpr[instruction.rd] =
                state.gpr[instruction.ra] +
                state.gpr[instruction.rb];
            break;

        case PPCOpcode::Ori:
            state.gpr[instruction.ra] =
                state.gpr[instruction.rd] |
                static_cast<uint16_t>(instruction.immediate);
            break;

        case PPCOpcode::Oris:
            state.gpr[instruction.ra] =
                state.gpr[instruction.rd] |
                (static_cast<uint64_t>(
                    static_cast<uint16_t>(instruction.immediate)
                ) << 16);
            break;

        case PPCOpcode::Branch:
        case PPCOpcode::BranchLink:
            state.pc = instruction.raw & 0x03FFFFFC;
            return;

        case PPCOpcode::Nop:
        case PPCOpcode::Unknown:
            break;
    }

    state.pc = nextPC;
}

PPCInterpreter::State& PPCInterpreter::getState() {
    return state;
}

const PPCInterpreter::State& PPCInterpreter::getState() const {
    return state;
}
