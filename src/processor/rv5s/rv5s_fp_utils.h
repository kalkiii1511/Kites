#pragma once

#include <cstdint>

namespace Kites
{
namespace rv5s_fp
{
inline uint8_t opcode(uint32_t instruction) { return instruction & 0x7f; }
inline uint8_t funct7(uint32_t instruction) { return (instruction >> 25) & 0x7f; }

inline bool writesGpr(uint32_t instruction)
{
    if (opcode(instruction) != 0x53)
        return false;
    const uint8_t f7 = funct7(instruction);
    return f7 == 0x50 || f7 == 0x51 || f7 == 0x60 || f7 == 0x61 || f7 == 0x70 || f7 == 0x71;
}

inline bool writesFpr(uint32_t instruction)
{
    const uint8_t op = opcode(instruction);
    return op == 0x07 || op == 0x43 || op == 0x47 || op == 0x4b || op == 0x4f ||
           (op == 0x53 && !writesGpr(instruction));
}

inline bool usesGprRs1(uint32_t instruction)
{
    const uint8_t op = opcode(instruction);
    const uint8_t f7 = funct7(instruction);
    return op == 0x07 || op == 0x27 ||
           (op == 0x53 && (f7 == 0x68 || f7 == 0x69 || f7 == 0x78 || f7 == 0x79));
}
} // namespace rv5s_fp
} // namespace Kites
