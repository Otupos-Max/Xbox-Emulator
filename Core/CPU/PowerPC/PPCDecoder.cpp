#include "PPCDecoder.hpp"

PPCInstruction PPCDecoder::decode(uint32_t instruction, uint32_t address) {
    PPCInstruction result{};
    result.raw     = instruction;
    result.address = address;

    // Campos comuns (formato D e X — extraídos sempre, usados conforme o opcode).
    const uint8_t primary = static_cast<uint8_t>(instruction >> 26);

    result.rd        = static_cast<uint8_t>((instruction >> 21) & 0x1F);
    result.ra        = static_cast<uint8_t>((instruction >> 16) & 0x1F);
    result.rb        = static_cast<uint8_t>((instruction >> 11) & 0x1F);
    result.immediate = static_cast<int16_t>(instruction & 0xFFFF);

    switch (primary) {

        // ------------------------------------------------------------------ //
        // Aritmética inteira — formato D                                       //
        // ------------------------------------------------------------------ //

        case 14:   // addi  rD, rA, SIMM
            result.opcode = PPCOpcode::Addi;
            break;

        case 15:   // addis rD, rA, SIMM   (≠ oris — erro no original)
            result.opcode = PPCOpcode::Addis;
            break;

        // ------------------------------------------------------------------ //
        // Lógica imediata — formato D                                          //
        // ------------------------------------------------------------------ //

        case 24:   // ori   rA, rS, UIMM
            result.opcode = PPCOpcode::Ori;
            break;

        case 25:   // oris  rA, rS, UIMM   (original usava "24+1" com Unknown)
            result.opcode = PPCOpcode::Oris;
            break;

        // ------------------------------------------------------------------ //
        // Branch incondicional — formato I                                     //
        //                                                                      //
        // bits[31..6]  = opcode primário 18                                    //
        // bits[5..2]   = LI (campo de 24 bits, deslocado × 4 → offset 26-bit) //
        // bit[1]       = AA (0 = relativo ao PC, 1 = absoluto)                 //
        // bit[0]       = LK (0 = Branch, 1 = BranchLink — salva PC+4 em LR)   //
        // ------------------------------------------------------------------ //

        case 18: {
            const bool aa = (instruction & 0x2u) != 0;
            const bool lk = (instruction & 0x1u) != 0;

            // LI ocupa bits[2..25] — máscara 0x03FFFFFC exclui AA (bit1) e LK (bit0).
            // O valor já é múltiplo de 4 (alinhado), então é o offset direto.
            // Sign-extend 26→32 bits via shift aritmético.
            const int32_t li_raw = static_cast<int32_t>(instruction & 0x03FF'FFFCu);
            const int32_t li     = (li_raw << 6) >> 6;  // sign-extend 26→32 bits

            result.branch_absolute = aa;
            result.branch_target   = aa
                ? li                                        // endereço absoluto
                : static_cast<int32_t>(address) + li;      // PC-relativo

            result.opcode = lk ? PPCOpcode::BranchLink : PPCOpcode::Branch;
            break;
        }

        // ------------------------------------------------------------------ //
        // Opcode estendido 31 — formato X / XO                                 //
        // ------------------------------------------------------------------ //

        case 31: {
            // XO ocupa bits[1..10].
            const uint16_t xo = static_cast<uint16_t>((instruction >> 1) & 0x3FFu);

            switch (xo) {
                case 266:  // add rD, rA, rB   (OE=0)
                case 778:  // addo rD, rA, rB  (OE=1 — 266 | (1<<9))
                    result.opcode = PPCOpcode::Add;
                    break;

                // Adicionar outros XO aqui conforme o interpretador crescer:
                //   case  40: subf     case   8: subfc
                //   case 104: neg      case  10: addc
                //   ...

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }

    // nop canônico: ori r0, r0, 0 (0x60000000).
    // Já seria decodificado como Ori acima, mas marcamos explicitamente
    // para que o interpretador pule sem calcular nada.
    if (instruction == 0x6000'0000u)
        result.opcode = PPCOpcode::Nop;

    return result;
}
