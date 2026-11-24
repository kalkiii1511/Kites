#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assembler/assembler.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "processor/rvss/rvss_processor.h"
#include "utils/utils.h"

namespace {

enum class VmFlavor { RVSS, HF, HNF, NHF, NHNF };

struct IntegerInstructionScenario
{
    VmFlavor vm;
    const char *name;
    const char *instruction;
    uint64_t expected;
};

const char *vmName(VmFlavor vm)
{
    switch (vm)
    {
    case VmFlavor::RVSS: return "RVSS";
    case VmFlavor::HF: return "HazardForwarding";
    case VmFlavor::HNF: return "HazardNoForwarding";
    case VmFlavor::NHF: return "NoHazardForwarding";
    case VmFlavor::NHNF: return "NoHazardNoForwarding";
    }
    return "Unknown";
}

std::vector<IntegerInstructionScenario> integerScenarios()
{
    struct Operation { const char *name; const char *source; uint64_t result; };
    constexpr Operation operations[] = {
        {"Add",    "add x3, x1, x2", 23},
        {"Sub",    "sub x3, x1, x2", 17},
        {"And",    "and x3, x1, x2", 0},
        {"Or",     "or x3, x1, x2", 23},
        {"Xor",    "xor x3, x1, x2", 23},
        {"Sll",    "sll x3, x1, x2", 160},
        {"Srl",    "srl x3, x1, x2", 2},
        {"Sra",    "sra x3, x1, x2", 2},
        {"Slt",    "slt x3, x1, x2", 0},
        {"Sltu",   "sltu x3, x1, x2", 0},
        {"Addi",   "addi x3, x1, 5", 25},
        {"Andi",   "andi x3, x1, 7", 4},
        {"Ori",    "ori x3, x1, 7", 23},
        {"Xori",   "xori x3, x1, 7", 19},
        {"Slti",   "slti x3, x1, 21", 1},
        {"Sltiu",  "sltiu x3, x1, 21", 1},
        {"Mul",    "mul x3, x1, x2", 60},
        {"Div",    "div x3, x1, x2", 6},
        {"Rem",    "rem x3, x1, x2", 2},
        {"Slli",   "slli x3, x1, 2", 80},
    };
    constexpr VmFlavor vms[] = {
        VmFlavor::RVSS, VmFlavor::HF, VmFlavor::HNF, VmFlavor::NHF, VmFlavor::NHNF};

    std::vector<IntegerInstructionScenario> scenarios;
    scenarios.reserve(100);
    for (VmFlavor vm : vms)
        for (const auto &op : operations)
            scenarios.push_back({vm, op.name, op.source, op.result});
    return scenarios;
}

std::string integerScenarioName(
    const ::testing::TestParamInfo<IntegerInstructionScenario> &info)
{
    return std::string(vmName(info.param.vm)) + "_" + info.param.name;
}

template <typename VmType>
uint64_t runInstruction(const std::string &instruction)
{
    Kites::setupVmStateDirectory();
    auto vm = std::make_unique<VmType>();
    std::istringstream source("addi x1, x0, 20\n"
                              "addi x2, x0, 3\n"
                              "nop\nnop\nnop\n" + instruction + "\n");
    vm->LoadProgram(Kites::assemble(source));
    vm->DebugRun();
    return vm->registers_.ReadGpr(3);
}

uint64_t runForVm(VmFlavor flavor, const std::string &instruction)
{
    switch (flavor)
    {
    case VmFlavor::RVSS: return runInstruction<Kites::RVSSProcessor>(instruction);
    case VmFlavor::HF: return runInstruction<Kites::RV5StageProcessorHF>(instruction);
    case VmFlavor::HNF: return runInstruction<Kites::RV5StageProcessorHNF>(instruction);
    case VmFlavor::NHF: return runInstruction<Kites::RV5StageProcessorNHF>(instruction);
    case VmFlavor::NHNF: return runInstruction<Kites::RV5StageProcessorNHNF>(instruction);
    }
    return 0;
}

class IntegerInstructionVmMatrixTest
    : public ::testing::TestWithParam<IntegerInstructionScenario>
{
};

TEST_P(IntegerInstructionVmMatrixTest, ProducesExpectedRegisterResult)
{
    const auto scenario = GetParam();
    EXPECT_EQ(runForVm(scenario.vm, scenario.instruction), scenario.expected);
}

INSTANTIATE_TEST_SUITE_P(
    IntegerOperationsAcrossProcessorVariants,
    IntegerInstructionVmMatrixTest,
    ::testing::ValuesIn(integerScenarios()),
    integerScenarioName);

} // namespace
