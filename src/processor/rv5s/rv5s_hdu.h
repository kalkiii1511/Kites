/**
 * @file rv5s_hdu.h
 * @brief Header for the universal Hazard Detection Unit (HDU) logic.
 * * This unit checks for data hazards in the pipeline, supporting both
 * No-Forwarding (H_NF) and With-Forwarding (H_F) modes for GPR/FPR data.
 */
#pragma once

#include <cstdint>
// Assuming the path to pipeline_registers.h is correct relative to the HDU file
#include "processor/pipeline_registers.h"

namespace Kites
{
// Define stall count constants (useful for client code)
constexpr int STALL_NONE = 0;
constexpr int STALL_ONE_CYCLE = 1;
constexpr int STALL_TWO_CYCLES = 2;

/**
 * @brief Detailed result of one hazard-detection pass. The GUI uses the extra
 * fields to explain which instructions caused the stall, while the pipeline only
 * needs `stalls`.
 */
// Which encoded register fields an instruction actually reads. I-type
// instructions reuse the rs2 bit-field for immediate bits, so treating that
// field as a source register creates false hazards and bogus forwarding.
struct InstructionSources
{
    bool gpr_rs1{false};
    bool gpr_rs2{false};
    bool fpr_frs1{false};
    bool fpr_frs2{false};
    bool fpr_frs3{false};
};

InstructionSources instruction_sources(uint32_t instruction);

struct DataHazardReport
{
    int stalls{STALL_NONE};
    bool detected{false};
    bool loadUse{false};
    bool forwarded{false};
    uint64_t producerPc{INVALID_PC};
    uint64_t consumerPc{INVALID_PC};
    uint8_t reg{0};
    bool fpr{false};
};

/**
 * @brief Checks for data hazards between the instruction in ID/EX (source) and
 * the instruction in IF/ID (dependent).
 * * This function handles all GPR and FPR dependencies.
 * * @param if_id_reg The IF/ID register (holds the dependent instruction's data).
 * @param id_ex_reg The ID/EX register (holds the hazard-producing instruction's data).
 * @param is_forwarding_enabled Flag to determine the stall logic:
 * - true: Only stall for the Load-Use hazard (STALL_ONE_CYCLE).
 * - false: Stall for ALL dependencies (STALL_TWO_CYCLES).
 * @return The number of cycles the pipeline must stall (0, 1, or 2).
 */
int check_data_hazard(const IF_ID_Register &if_id_reg, const ID_EX_Register &id_ex_reg,
                      const EX_MEM_Register &ex_mem_reg, bool is_forwarding_enabled);

/**
 * @brief Same check as check_data_hazard(), but reports which register and
 * instructions formed the dependency.
 */
DataHazardReport detect_data_hazard(const IF_ID_Register &if_id_reg,
                                    const ID_EX_Register &id_ex_reg,
                                    const EX_MEM_Register &ex_mem_reg,
                                    bool is_forwarding_enabled);
}//namespace Kites
