#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assembler/assembler.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "utils/utils.h"

namespace {

struct HazardScenario
{
    bool forwarding;
    size_t registerSlot;
    size_t separationNops;
};

std::vector<HazardScenario> hazardScenarios()
{
    std::vector<HazardScenario> scenarios;
    scenarios.reserve(128);
    for (bool forwarding : {false, true})
        for (size_t registerSlot = 0; registerSlot < 16; ++registerSlot)
            for (size_t nops = 0; nops < 4; ++nops)
                scenarios.push_back({forwarding, registerSlot, nops});
    return scenarios;
}

std::string hazardScenarioName(const ::testing::TestParamInfo<HazardScenario> &info)
{
    const auto &scenario = info.param;
    return std::string(scenario.forwarding ? "Forwarding" : "StallOnly") +
           "_Register" + std::to_string(scenario.registerSlot) +
           "_Gap" + std::to_string(scenario.separationNops);
}

template <typename VmType>
uint64_t runDependencyProgram(const HazardScenario &scenario)
{
    Kites::setupVmStateDirectory();
    const int source = 1 + static_cast<int>(scenario.registerSlot);
    const int firstDest = 1 + static_cast<int>((scenario.registerSlot + 7) % 31);
    const int secondDest = 1 + static_cast<int>((scenario.registerSlot + 13) % 31);

    std::string program = "addi x" + std::to_string(source) + ", x0, 7\n";
    for (size_t i = 0; i < scenario.separationNops; ++i)
        program += "nop\n";
    program += "add x" + std::to_string(firstDest) + ", x" + std::to_string(source) +
               ", x" + std::to_string(source) + "\n";
    program += "sub x" + std::to_string(secondDest) + ", x" +
               std::to_string(firstDest) + ", x" + std::to_string(source) + "\n";

    auto vm = std::make_unique<VmType>();
    std::istringstream sourceStream(program);
    vm->LoadProgram(Kites::assemble(sourceStream));
    vm->DebugRun();
    return vm->registers_.ReadGpr(static_cast<size_t>(secondDest));
}

class PipelineHazardForwardingMatrixTest
    : public ::testing::TestWithParam<HazardScenario>
{
};

TEST_P(PipelineHazardForwardingMatrixTest, ResolvesDependentIntegerInstructions)
{
    const auto scenario = GetParam();
    const uint64_t result = scenario.forwarding
        ? runDependencyProgram<Kites::RV5StageProcessorHF>(scenario)
        : runDependencyProgram<Kites::RV5StageProcessorHNF>(scenario);
    EXPECT_EQ(result, 7u);
}

INSTANTIATE_TEST_SUITE_P(
    RegisterAndDependencyDistanceMatrix,
    PipelineHazardForwardingMatrixTest,
    ::testing::ValuesIn(hazardScenarios()),
    hazardScenarioName);

} // namespace
