/**
 * @file hazard_detection_unit.cpp
 * @brief Logic for the Hazard Detection Unit (HDU).
 * * This function determines if a data hazard exists between instructions in
 * the ID/EX and IF/ID pipeline registers, and calculates the number of stalls
 * (bubbles) needed based on the simulator's mode (Forwarding ON/OFF).
 * * NOTE: This version includes and uses the full structs from pipeline_registers.h.
 */
#include <algorithm>
#include <cstdint>
#include <iostream>

#include "common/instructions.h"
#include "processor/pipeline_registers.h"
#include "processor/rv5s/rv5s_hdu.h"

namespace Kites
{
InstructionSources instruction_sources(uint32_t instruction)
{
    InstructionSources sources;
    const uint32_t opcode = instruction & 0x7F;
    const bool isFloat = instruction_set::isFInstruction(instruction) ||
                         instruction_set::isDInstruction(instruction);

    if (!isFloat)
    {
        switch (opcode)
        {
        case 0x33: // R-type
        case 0x3B: // R-type (32-bit)
        case 0x23: // Store
        case 0x63: // Branch
            sources.gpr_rs1 = true;
            sources.gpr_rs2 = true;
            break;
        case 0x13: // I-type ALU
        case 0x1B: // I-type ALU (32-bit)
        case 0x03: // Load
        case 0x67: // JALR
            sources.gpr_rs1 = true;
            break;
        case 0x6F: // JAL
        case 0x37: // LUI
        case 0x17: // AUIPC
        case 0x73: // ECALL / CSR (not modelled as a data dependency)
            break;
        default:
            // Unknown integer encoding: stay conservative.
            sources.gpr_rs1 = true;
            sources.gpr_rs2 = true;
            break;
        }
        return sources;
    }

    switch (opcode)
    {
    case 0x07: // FLW / FLD: integer base register
        sources.gpr_rs1 = true;
        break;
    case 0x27: // FSW / FSD: integer base + FP data register
        sources.gpr_rs1 = true;
        sources.fpr_frs2 = true;
        break;
    case 0x43: // FMADD
    case 0x47: // FMSUB
    case 0x4B: // FNMSUB
    case 0x4F: // FNMADD
        sources.fpr_frs1 = true;
        sources.fpr_frs2 = true;
        sources.fpr_frs3 = true;
        break;
    case 0x53: // FP arithmetic / conversion
        sources.fpr_frs1 = true;
        sources.fpr_frs2 = true;
        break;
    default:
        sources.fpr_frs1 = true;
        break;
    }
    return sources;
}

namespace
{
struct SourceMatch
{
    bool matched{false};
    uint8_t reg{0};
};

SourceMatch matchGpr(uint8_t rd, bool writeEnable, bool useRs1, uint8_t ifIdRs1, bool useRs2,
                     uint8_t ifIdRs2)
{
    SourceMatch match;
    if (!writeEnable || rd == 0)
    {
        return match;
    }
    if (useRs1 && rd == ifIdRs1)
    {
        match.matched = true;
        match.reg = rd;
    }
    else if (useRs2 && rd == ifIdRs2)
    {
        match.matched = true;
        match.reg = rd;
    }
    return match;
}

SourceMatch matchFpr(uint8_t frd, bool writeEnable, bool useFrs1, uint8_t ifIdFrs1,
                     bool useFrs2, uint8_t ifIdFrs2, bool useFrs3, uint8_t ifIdFrs3)
{
    SourceMatch match;
    if (!writeEnable)
    {
        return match;
    }
    if ((useFrs1 && frd == ifIdFrs1) || (useFrs2 && frd == ifIdFrs2) ||
        (useFrs3 && frd == ifIdFrs3))
    {
        match.matched = true;
        match.reg = frd;
    }
    return match;
}
} // namespace

DataHazardReport detect_data_hazard(const IF_ID_Register &if_id_reg,
                                    const ID_EX_Register &id_ex_reg,
                                    const EX_MEM_Register &ex_mem_reg,
                                    bool is_forwarding_enabled)
{
    DataHazardReport report;
    report.consumerPc = if_id_reg.pc;

    // --- 1. Extract Source Register Indices from IF/ID Instruction (Dependent) ---
    const uint32_t instruction = if_id_reg.instruction;

    // GPR Source Registers (rs1/rs2 fields)
    const uint8_t if_id_rs1 = (instruction >> 15) & 0x1F;
    const uint8_t if_id_rs2 = (instruction >> 20) & 0x1F;

    // FPR Source Registers (frs1/frs2/frs3 fields)
    // Note: F-type instructions reuse the rs1/rs2 bit-fields for frs1/frs2
    const uint8_t if_id_frs1 = (instruction >> 15) & 0x1F;
    const uint8_t if_id_frs2 = (instruction >> 20) & 0x1F;
    const uint8_t if_id_frs3 = (instruction >> 27) & 0x1F; // For FMA (R4 format)
    const InstructionSources consumer = instruction_sources(instruction);

    // --- 2. Check for GPR Hazard (Source writes GPR, Dependent reads GPR) ---
    // Two-cycle hazard: current EX/MEM instruction writes a GPR the IF/ID instruction reads
    const SourceMatch gpr_two = matchGpr(ex_mem_reg.rd, ex_mem_reg.reg_write, consumer.gpr_rs1,
                                         if_id_rs1, consumer.gpr_rs2, if_id_rs2);
    // One-cycle hazard: previous EX/MEM instruction (now further down) writes a GPR the IF/ID
    // instruction reads
    const SourceMatch gpr_one =
        matchGpr(ex_mem_reg.prev_rd, ex_mem_reg.prev_reg_write, consumer.gpr_rs1, if_id_rs1,
                 consumer.gpr_rs2, if_id_rs2);

    // --- 3. Check for FPR Hazard (Source writes FPR, Dependent reads FPR) ---
    // Note: FPR f0 is NOT hardwired to zero (unlike GPR x0), so no frd != 0 check needed.
    const SourceMatch fpr_two =
        matchFpr(ex_mem_reg.frd, ex_mem_reg.freg_write, consumer.fpr_frs1, if_id_frs1,
                 consumer.fpr_frs2, if_id_frs2, consumer.fpr_frs3, if_id_frs3);
    const SourceMatch fpr_one =
        matchFpr(ex_mem_reg.prev_frd, ex_mem_reg.prev_freg_write, consumer.fpr_frs1, if_id_frs1,
                 consumer.fpr_frs2, if_id_frs2, consumer.fpr_frs3, if_id_frs3);

    const bool any_hazard_two_cycle = gpr_two.matched || fpr_two.matched;
    const bool any_hazard_one_cycle = gpr_one.matched || fpr_one.matched;

    if (is_forwarding_enabled)
    {
        // --- Forwarding ON: Detects Load-Use only (1 Stall) ---
        // Load-Use hazard occurs if the producing instruction (ID/EX) is a Load
        // AND the dependent instruction reads that destination register.
        if (id_ex_reg.mem_read)
        {
            const SourceMatch gpr_load_use =
                matchGpr(id_ex_reg.rd, id_ex_reg.reg_write, consumer.gpr_rs1, if_id_rs1,
                         consumer.gpr_rs2, if_id_rs2);
            const SourceMatch fpr_load_use =
                matchFpr(id_ex_reg.frd, id_ex_reg.freg_write, consumer.fpr_frs1, if_id_frs1,
                         consumer.fpr_frs2, if_id_frs2, consumer.fpr_frs3, if_id_frs3);

            if (gpr_load_use.matched || fpr_load_use.matched)
            {
                report.stalls = STALL_ONE_CYCLE;
                report.detected = true;
                report.loadUse = true;
                report.producerPc = id_ex_reg.pc;
                report.reg = gpr_load_use.matched ? gpr_load_use.reg : fpr_load_use.reg;
                report.fpr = !gpr_load_use.matched && fpr_load_use.matched;
                return report;
            }
        }

        // All other hazards (R-R, F-F, R-Store, F-Store) are solved by the forwarding unit.
        return report;
    }

    // --- No forwarding: all RAW dependencies stall until WB writes the register ---
    if (any_hazard_two_cycle)
    {
        report.stalls = STALL_TWO_CYCLES;
        report.detected = true;
        report.producerPc = ex_mem_reg.pc;
        report.reg = gpr_two.matched ? gpr_two.reg : fpr_two.reg;
        report.fpr = !gpr_two.matched && fpr_two.matched;
        return report;
    }
    if (any_hazard_one_cycle)
    {
        report.stalls = STALL_ONE_CYCLE;
        report.detected = true;
        report.producerPc = ex_mem_reg.prev_pc != INVALID_PC ? ex_mem_reg.prev_pc
                                                             : ex_mem_reg.pc;
        report.reg = gpr_one.matched ? gpr_one.reg : fpr_one.reg;
        report.fpr = !gpr_one.matched && fpr_one.matched;
        return report;
    }

    return report;
}

int check_data_hazard(const IF_ID_Register &if_id_reg, const ID_EX_Register &id_ex_reg,
                      const EX_MEM_Register &ex_mem_reg, bool is_forwarding_enabled)
{
    return detect_data_hazard(if_id_reg, id_ex_reg, ex_mem_reg, is_forwarding_enabled).stalls;
}
}//namespace Kites
