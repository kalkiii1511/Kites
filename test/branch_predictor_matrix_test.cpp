#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "processor/rv5s/rv5s_branch_predictor.h"

namespace {

using Kites::BranchPredictorType;
using Kites::RV5SBranchPredictor;

struct BranchIndexScenario
{
    size_t index;
};

std::vector<BranchIndexScenario> allBranchIndexes()
{
    std::vector<BranchIndexScenario> scenarios;
    for (size_t index = 0; index < RV5SBranchPredictor::kTableSize; ++index)
        scenarios.push_back({index});
    return scenarios;
}

std::string branchIndexName(const ::testing::TestParamInfo<BranchIndexScenario> &info)
{
    return "Index" + std::to_string(info.param.index);
}

class BranchTargetBufferIndexTest : public ::testing::TestWithParam<BranchIndexScenario>
{
};

TEST_P(BranchTargetBufferIndexTest, LearnsAndReturnsTargetForEachIndex)
{
    RV5SBranchPredictor predictor(BranchPredictorType::TwoBitBHT);
    const uint64_t pc = 0x8000 + static_cast<uint64_t>(GetParam().index * 4);
    const uint64_t target = pc + 0x100;

    EXPECT_FALSE(predictor.Predict(pc).btb_hit);
    predictor.Update(pc, true, target, false, false, true);
    const auto prediction = predictor.Predict(pc);
    EXPECT_TRUE(prediction.btb_hit);
    EXPECT_TRUE(prediction.predicted_taken);
    EXPECT_EQ(prediction.predicted_target, target);
}

INSTANTIATE_TEST_SUITE_P(
    AllBTBIndexes,
    BranchTargetBufferIndexTest,
    ::testing::ValuesIn(allBranchIndexes()),
    branchIndexName);

struct CounterTransitionScenario
{
    size_t pcSlot;
    uint8_t initialState;
    bool taken;
};

std::vector<CounterTransitionScenario> counterTransitions()
{
    std::vector<CounterTransitionScenario> scenarios;
    for (size_t pcSlot = 0; pcSlot < 16; ++pcSlot)
        for (uint8_t state = 0; state < 4; ++state)
            for (bool taken : {false, true})
                scenarios.push_back({pcSlot, state, taken});
    return scenarios;
}

std::string transitionName(const ::testing::TestParamInfo<CounterTransitionScenario> &info)
{
    const auto &scenario = info.param;
    return "PC" + std::to_string(scenario.pcSlot) +
           "_State" + std::to_string(scenario.initialState) +
           (scenario.taken ? "_Taken" : "_NotTaken");
}

class SaturatingCounterTransitionTest
    : public ::testing::TestWithParam<CounterTransitionScenario>
{
};

TEST_P(SaturatingCounterTransitionTest, MovesOneStepAndSaturates)
{
    const auto scenario = GetParam();
    RV5SBranchPredictor predictor(BranchPredictorType::TwoBitBHT);
    const uint64_t pc = 0xA000 + static_cast<uint64_t>(scenario.pcSlot * 256);
    constexpr uint64_t target = 0xB000;

    // Start at the default weakly-not-taken state (01), then train to the
    // requested state while keeping a matching BTB entry for this PC.
    predictor.Update(pc, true, target, false, false, true); // 01 -> 10
    if (scenario.initialState <= 1)
    {
        predictor.Update(pc, false, target, false, true, true); // 10 -> 01
        if (scenario.initialState == 0)
            predictor.Update(pc, false, target, false, true, true); // 01 -> 00
    }
    else if (scenario.initialState == 3)
    {
        predictor.Update(pc, true, target, false, true, false); // 10 -> 11
    }

    const uint8_t expected = scenario.taken
        ? (scenario.initialState < 3 ? scenario.initialState + 1 : 3)
        : (scenario.initialState > 0 ? scenario.initialState - 1 : 0);
    predictor.Update(pc, scenario.taken, target, false, true, false);

    const auto entries = predictor.GetActiveEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].state, expected);
    const auto prediction = predictor.Predict(pc);
    EXPECT_EQ(prediction.predicted_taken, expected >= 2);
    EXPECT_EQ(prediction.predicted_target, expected >= 2 ? target : pc + 4);
}

INSTANTIATE_TEST_SUITE_P(
    CounterStatesAndOutcomes,
    SaturatingCounterTransitionTest,
    ::testing::ValuesIn(counterTransitions()),
    transitionName);

} // namespace
