
#include "PPCInterpreter.hpp"

#include "Core/Memory/MemoryManager.hpp"
#include <limits>

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
                static_cast<uint64_t>(static_cast<int64_t>(instruction.immediate));
            break;

        case PPCOpcode::Addis:
            state.gpr[instruction.rd] =
                (instruction.ra == 0 ? 0 : state.gpr[instruction.ra]) +
                static_cast<uint64_t>(static_cast<int64_t>(instruction.immediate) * 65536);
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
            state.pc = static_cast<uint64_t>(instruction.branch_target);
            return;

        case PPCOpcode::BranchLink:
            state.lr = nextPC;
            state.pc = static_cast<uint64_t>(instruction.branch_target);
            return;

        case PPCOpcode::Nop:
        case PPCOpcode::Unknown:
            break;
    }

    state.pc = nextPC;
}

bool PPCInterpreter::step(xboxemu::memory::MemoryManager& memory) {
    if (!state.running || state.pc > std::numeric_limits<uint32_t>::max() - 3 || (state.pc & 3u) != 0) {
        return false;
    }
    const uint32_t address = static_cast<uint32_t>(state.pc);
    if (!memory.is_ram(address) || !memory.is_ram(address + 3)) return false;
    const PPCInstruction instruction = PPCDecoder::decode(memory.read_u32(address), address);
    if (instruction.opcode == PPCOpcode::Unknown) return false;
    execute(instruction);
    return true;
}

PPCInterpreter::State& PPCInterpreter::getState() {
    return state;
}

const PPCInterpreter::State& PPCInterpreter::getState() const {
    return state;
}
