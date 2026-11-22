// Smoke test do PPCDecoder.
//
// Este arquivo existe para dois propósitos:
//   1) Servir de template para novos testes (padrão de organização).
//   2) Validar a infraestrutura de teste (CMake + GTest + CI) de ponta a ponta.
//
// Os casos abaixo são propositalmente triviais. Substitua-os por testes
// reais do PPCDecoder assim que a API estiver estável.
//
// -------------------------------------------------------------------------
// Instruções de referência (IBM PowerPC 2.02, big-endian):
//   add  r3, r4, r5  -> 0x7C 64 2A 14
//   blr              -> 0x4E 80 00 20
//   nop              -> 0x60 00 00 00  (ori r0, r0, 0)
// -------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <cstdint>

#include "Core/CPU/PowerPC/PPCDecoder.hpp"
#include "Core/CPU/PowerPC/PPCInterpreter.hpp"
#include "Core/Memory/MemoryManager.hpp"

TEST(PPCDecoderSmoke, InfrastructureBuilds)
{
    // Este teste apenas valida que a infraestrutura de teste compila e roda.
    // Se este teste falhar, o problema NÃO é do emulador — é do ambiente.
    EXPECT_TRUE(true);
}

TEST(PPCDecoderSmoke, InstructionWordEncodesExpectedOpcode)
{
    // Encoding de "add r3, r4, r5" (XO-form, opcode primário 31, XO 266).
    // opcode 31 = 0b11111 (bits 0-5, big-endian)
    constexpr std::uint32_t kAdd = 0x7C642A14u;

    // Extrai o opcode primário (top 6 bits do word, big-endian).
    const std::uint32_t primary_opcode = kAdd >> 26;

    EXPECT_EQ(primary_opcode, 31u);
}

TEST(PPCDecoderSmoke, BranchLinkRegisterIsBranchFamily)
{
    // "blr" = 0x4E800020, opcode primário 19 (XL-form, branch to LR).
    constexpr std::uint32_t kBlr = 0x4E800020u;

    const std::uint32_t primary_opcode = kBlr >> 26;
    EXPECT_EQ(primary_opcode, 19u);
}

TEST(PPCDecoder, ResolvesRelativeAndAbsoluteBranchTargets)
{
    const auto relative = PPCDecoder::decode(0x48000009u, 0x100u);
    EXPECT_EQ(relative.opcode, PPCOpcode::BranchLink);
    EXPECT_EQ(relative.branch_target, 0x108);

    const auto backward = PPCDecoder::decode(0x4BFFFFFCu, 0x100u);
    EXPECT_EQ(backward.opcode, PPCOpcode::Branch);
    EXPECT_EQ(backward.branch_target, 0xFC);

    const auto absolute = PPCDecoder::decode(0x48000102u, 0x100u);
    EXPECT_EQ(absolute.branch_target, 0x100);
}

TEST(PPCInterpreter, FetchesAndExecutesBigEndianInstructions)
{
    xboxemu::memory::MemoryManager memory;
    xboxemu::memory::MemoryManager::Config config;
    config.ram_size = 0x1000;
    ASSERT_TRUE(memory.initialize(config));
    memory.write_u32(0x100, 0x38600005u); // addi r3, r0, 5

    PPCInterpreter interpreter;
    auto& state = interpreter.getState();
    state.pc = 0x100;
    state.running = true;

    ASSERT_TRUE(interpreter.step(memory));
    EXPECT_EQ(state.gpr[3], 5u);
    EXPECT_EQ(state.pc, 0x104u);
}

TEST(PPCInterpreter, BranchLinkSetsReturnAddressAndBranchesRelativeToPc)
{
    xboxemu::memory::MemoryManager memory;
    xboxemu::memory::MemoryManager::Config config;
    config.ram_size = 0x1000;
    ASSERT_TRUE(memory.initialize(config));
    memory.write_u32(0x100, 0x48000009u); // bl +8

    PPCInterpreter interpreter;
    auto& state = interpreter.getState();
    state.pc = 0x100;
    state.running = true;

    ASSERT_TRUE(interpreter.step(memory));
    EXPECT_EQ(state.lr, 0x104u);
    EXPECT_EQ(state.pc, 0x108u);
}
