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

// Descomente quando o decoder estiver disponível:
// #include "Core/CPU/PowerPC/PPCDecoder.hpp"

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
