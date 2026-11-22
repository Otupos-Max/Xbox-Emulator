#pragma once

#include <cstdint>

// Opcodes reconhecidos pelo decoder. Adicione novos antes de Unknown.
enum class PPCOpcode : uint8_t {
    Unknown,

    // Aritmética inteira imediata (formato D)
    Addi,     // addi  rD, rA, SIMM   — opcode primário 14
    Addis,    // addis rD, rA, SIMM   — opcode primário 15

    // Aritmética inteira registrador (formato X, XO=266)
    Add,      // add   rD, rA, rB     — opcode 31 / XO 266

    // Lógica imediata (formato D)
    Ori,      // ori   rA, rS, UIMM   — opcode primário 24
    Oris,     // oris  rA, rS, UIMM   — opcode primário 25

    // Branch incondicional (formato I)
    Branch,      // b   target  (LK=0)
    BranchLink,  // bl  target  (LK=1) — salva PC+4 em LR

    // Pseudo-instrução
    Nop,      // ori r0, r0, 0  (0x60000000) — sem efeito
};

// Instrução decodificada. Os campos usados dependem do opcode:
//
//  Addi / Addis       → rd, ra, immediate
//  Add                → rd, ra, rb
//  Ori  / Oris        → rd (rS), ra (rD), immediate (UIMM)
//  Branch / BranchLink→ branch_target (endereço final), branch_absolute
//  Nop / Unknown      → (nenhum campo relevante)
struct PPCInstruction {
    PPCOpcode opcode = PPCOpcode::Unknown;

    uint8_t rd = 0;   // campo rD (bits 21-25)
    uint8_t ra = 0;   // campo rA (bits 16-20)
    uint8_t rb = 0;   // campo rB (bits 11-15)

    int16_t  immediate      = 0;      // SIMM/UIMM (bits 0-15)
    int64_t  branch_target  = 0;      // alvo do branch já resolvido (absoluto)
    bool     branch_absolute = false; // true = AA=1 (endereço absoluto no binário)

    uint32_t address = 0;  // endereço desta instrução no espaço de memória
    uint32_t raw     = 0;  // word original
};

class PPCDecoder {
public:
    // Decodifica `instruction` localizada em `address`.
    // Retorna PPCInstruction com opcode == Unknown se não reconhecida.
    static PPCInstruction decode(uint32_t instruction, uint32_t address);
};
