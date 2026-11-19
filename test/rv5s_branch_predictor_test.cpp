// Tests for the 2-bit saturating-counter branch predictor and its pipeline integration.

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include "assembler/assembler.h"
#include "processor/rv5s/rv5s_branch_predictor.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "utils/utils.h"

using namespace Kites;

namespace
{

// ----------------------------
// Branch predictor unit tests
// ----------------------------

TEST(RV5SBranchPredictor, EmptyTablePredictsNotTaken)
{
    RV5SBranchPredictor predictor(BranchPredictorType::TwoBitBHT);
    const BranchPrediction prediction = predictor.Predict(0x1000);

    EXPECT_FALSE(prediction.predicted_taken);
    EXPECT_EQ(prediction.predicted_target, 0x1004u);
    EXPECT_FALSE(prediction.btb_hit);
    EXPECT_EQ(predictor.GetTotalPredictions(), 0u);
}

TEST(RV5SBranchPredictor, StaticNotTakenNeverPredictsTaken)
{
    RV5SBranchPredictor predictor(BranchPredictorType::StaticNotTaken);
    // Even with a taken branch recorded, the static baseline never redirects the fetch.
    predictor.Update(0x2000, /*actual_taken=*/true, 0x2010, /*is_unconditional=*/false,
                     /*btb_hit=*/false, /*mispredicted=*/false);
    const BranchPrediction prediction = predictor.Predict(0x2000);

    EXPECT_FALSE(prediction.predicted_taken);
    EXPECT_EQ(prediction.predicted_target, 0x2004u);
    EXPECT_FALSE(prediction.btb_hit);
    EXPECT_TRUE(predictor.GetActiveEntries().empty());
}

TEST(RV5SBranchPredictor, AllocatesBTBAndSaturatesCounter)
{
    RV5SBranchPredictor predictor(BranchPredictorType::TwoBitBHT);
    constexpr uint64_t kPc = 0x8000;
    constexpr uint64_t kTarget = 0x8020;

    predictor.Update(kPc, /*actual_taken=*/true, kTarget, /*is_unconditional=*/false,
                     /*btb_hit=*/false, /*mispredicted=*/true, nullptr);

    // A BTB entry now exists for the branch and predicts taken with the recorded target.
    BranchPrediction prediction = predictor.Predict(kPc);
    EXPECT_TRUE(prediction.btb_hit);
    EXPECT_TRUE(prediction.predicted_taken);
    EXPECT_EQ(prediction.predicted_target, kTarget);

    const auto entries = predictor.GetActiveEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].pc, kPc);
    EXPECT_EQ(entries[0].target, kTarget);
    EXPECT_EQ(entries[0].state, 2u); // 01 -> 10 on the first taken outcome
    EXPECT_FALSE(entries[0].is_unconditional);

    // Two more taken outcomes saturate at 11 and never wrap around.
    predictor.Update(kPc, true, kTarget, false, true, false);
    predictor.Update(kPc, true, kTarget, false, true, false);
    EXPECT_EQ(predictor.GetActiveEntries()[0].state, 3u);

    // 11 needs two not-taken outcomes before the direction flips back to not-taken.
    predictor.Update(kPc, false, kTarget, false, true, true);
    EXPECT_EQ(predictor.GetActiveEntries()[0].state, 2u);
    EXPECT_TRUE(predictor.Predict(kPc).predicted_taken);

    predictor.Update(kPc, false, kTarget, false, true, true);
    EXPECT_EQ(predictor.GetActiveEntries()[0].state, 1u);
    EXPECT_FALSE(predictor.Predict(kPc).predicted_taken);

    // Counter saturates at 00 as well.
    predictor.Update(kPc, false, kTarget, false, true, false);
    predictor.Update(kPc, false, kTarget, false, true, false);
    EXPECT_EQ(predictor.GetActiveEntries()[0].state, 0u);
}

TEST(RV5SBranchPredictor, UnconditionalEntryIgnoresCounter)
{
    RV5SBranchPredictor predictor(BranchPredictorType::TwoBitBHT);
    constexpr uint64_t kPc = 0x9000;
    constexpr uint64_t kTarget = 0x9100;

    predictor.Update(kPc, true, kTarget, /*is_unconditional=*/true, false, true);

    BranchPrediction prediction = predictor.Predict(kPc);
    EXPECT_TRUE(prediction.predicted_taken);
    EXPECT_EQ(prediction.predicted_target, kTarget);
    // Jumps do not consume the saturating counter; it stays at its reset value.
    EXPECT_EQ(predictor.GetActiveEntries()[0].state, 1u);
    EXPECT_TRUE(predictor.GetActiveEntries()[0].is_unconditional);
}

TEST(RV5SBranchPredictor, RollbackAndReapplyRestoreState)
{
    RV5SBranchPredictor predictor(BranchPredictorType::TwoBitBHT);
    constexpr uint64_t kPc = 0xA000;
    constexpr uint64_t kTarget = 0xA010;

    BranchPredictorDelta delta;
    predictor.Update(kPc, true, kTarget, false, false, true, &delta);
    ASSERT_TRUE(delta.recorded);
    ASSERT_EQ(predictor.GetTotalPredictions(), 1u);
    ASSERT_EQ(predictor.GetMispredictions(), 1u);

    predictor.Rollback(delta);
    EXPECT_EQ(predictor.GetTotalPredictions(), 0u);
    EXPECT_EQ(predictor.GetMispredictions(), 0u);
    EXPECT_TRUE(predictor.GetActiveEntries().empty());

    predictor.Reapply(delta);
    EXPECT_EQ(predictor.GetTotalPredictions(), 1u);
    EXPECT_EQ(predictor.GetMispredictions(), 1u);
    ASSERT_EQ(predictor.GetActiveEntries().size(), 1u);
    EXPECT_EQ(predictor.GetActiveEntries()[0].state, 2u);
}

TEST(RV5SBranchPredictor, StatsAndAccuracy)
{
    RV5SBranchPredictor predictor(BranchPredictorType::TwoBitBHT);
    EXPECT_DOUBLE_EQ(predictor.GetAccuracy(), 100.0); // No branches: vacuously perfect

    predictor.Update(0x100, true, 0x110, false, false, true);
    predictor.Update(0x100, true, 0x110, false, true, false);
    predictor.Update(0x100, true, 0x110, false, true, false);
    predictor.Update(0x100, true, 0x110, false, true, false);

    EXPECT_EQ(predictor.GetTotalPredictions(), 4u);
    EXPECT_EQ(predictor.GetBTBHits(), 3u);
    EXPECT_EQ(predictor.GetMispredictions(), 1u);
    EXPECT_DOUBLE_EQ(predictor.GetAccuracy(), 75.0);
}

// ----------------------------
// Pipeline integration tests
// ----------------------------

std::string loopProgram()
{
    // Four branch executions: three taken loop-back iterations plus one not-taken exit.
    return "li x1, 4\n"
           "li x2, 0\n"
           "loop:\n"
           "addi x2, x2, 1\n"
           "addi x1, x1, -1\n"
           "addi x0, x0, 0\n"
           "addi x0, x0, 0\n"
           "addi x0, x0, 0\n"
           "bne x1, x0, loop\n"
           "addi x3, x2, 100\n";
}

template <typename VmType>
std::unique_ptr<VmType> runPredictedProgram(const std::string &source)
{
    setupVmStateDirectory();

    auto vm = std::make_unique<VmType>();
    vm->SetBranchPredictorType(BranchPredictorType::TwoBitBHT);

    std::istringstream stream(source);
    vm->LoadProgram(assemble(stream));
    vm->DebugRun();
    return vm;
}

template <typename VmType>
std::unique_ptr<VmType> runPredictedSource(BranchPredictorType type)
{
    setupVmStateDirectory();

    auto vm = std::make_unique<VmType>();
    vm->SetBranchPredictorType(type);

    std::istringstream stream(loopProgram());
    AssembledProgram program = assemble(stream);
    vm->LoadProgram(program);
    vm->DebugRun();
    return vm;
}

TEST(RV5SBranchPredictorPipeline, DefaultIsStaticNotTaken)
{
    setupVmStateDirectory();
    RV5StageProcessorHF vm;
    EXPECT_EQ(vm.GetBranchPredictorType(), BranchPredictorType::StaticNotTaken);
    EXPECT_FALSE(vm.UsesBranchPrediction());
}

TEST(RV5SBranchPredictorPipeline, HazardVariantsSupportPrediction)
{
    RV5StageProcessorHF vm;
    vm.SetBranchPredictorType(BranchPredictorType::TwoBitBHT);
    EXPECT_EQ(vm.GetBranchPredictorType(), BranchPredictorType::TwoBitBHT);
    EXPECT_TRUE(vm.UsesBranchPrediction());
}

TEST(RV5SBranchPredictorPipeline, LoopComputesCorrectResultWithPrediction)
{
    auto vm = runPredictedSource<RV5StageProcessorHF>(BranchPredictorType::TwoBitBHT);

    EXPECT_EQ(vm->registers_.ReadGpr(2), 4u);
    EXPECT_EQ(vm->registers_.ReadGpr(3), 104u);

    // Four executed branches: three taken loop-backs and the not-taken exit.
    EXPECT_EQ(vm->GetBranchPredictions(), 4u);
    // The cold start misses, the middle iterations hit, and the exit is a miss.
    EXPECT_EQ(vm->GetBranchPredictor().GetMispredictions(), 2u);
    EXPECT_EQ(vm->GetBTBHits(), 3u);
    EXPECT_NEAR(vm->GetBranchAccuracy(), 50.0, 0.001);
    EXPECT_EQ(vm->GetBranchPredictorEntries().size(), 1u);
}

TEST(RV5SBranchPredictorPipeline, PredictionBeatsStaticTimingWithoutChangingResults)
{
    auto staticVm = runPredictedSource<RV5StageProcessorHF>(BranchPredictorType::StaticNotTaken);
    auto predictedVm = runPredictedSource<RV5StageProcessorHF>(BranchPredictorType::TwoBitBHT);

    // Same architectural state ...
    EXPECT_EQ(staticVm->registers_.ReadGpr(2), predictedVm->registers_.ReadGpr(2));
    EXPECT_EQ(staticVm->registers_.ReadGpr(3), predictedVm->registers_.ReadGpr(3));
    // ... but warm branch prediction removes bubbles.
    EXPECT_LT(predictedVm->cycle_s_, staticVm->cycle_s_);
    EXPECT_EQ(staticVm->GetBranchPredictions(), 0u); // static baseline keeps no stats
}

TEST(RV5SBranchPredictorPipeline, HNFVariantAlsoSupportsPrediction)
{
    auto vm = runPredictedSource<RV5StageProcessorHNF>(BranchPredictorType::TwoBitBHT);

    EXPECT_EQ(vm->registers_.ReadGpr(2), 4u);
    EXPECT_EQ(vm->registers_.ReadGpr(3), 104u);
    EXPECT_EQ(vm->GetBranchPredictions(), 4u);
    EXPECT_EQ(vm->GetBranchPredictor().GetMispredictions(), 2u);
}

TEST(RV5SBranchPredictorPipeline, NoForwardingVariantsStayStatic)
{
    // Branches can only be checked in MEM without forwarding, so these variants must
    // keep the static baseline instead of redirecting on a prediction they can't verify.
    RV5StageProcessorNHF nhf;
    nhf.SetBranchPredictorType(BranchPredictorType::TwoBitBHT);
    EXPECT_EQ(nhf.GetBranchPredictorType(), BranchPredictorType::StaticNotTaken);
    EXPECT_FALSE(nhf.UsesBranchPrediction());

    RV5StageProcessorNHNF nhnf;
    nhnf.SetBranchPredictorType(BranchPredictorType::TwoBitBHT);
    EXPECT_EQ(nhnf.GetBranchPredictorType(), BranchPredictorType::StaticNotTaken);
    EXPECT_FALSE(nhnf.UsesBranchPrediction());

    auto nhfVm = runPredictedSource<RV5StageProcessorNHF>(BranchPredictorType::TwoBitBHT);
    EXPECT_EQ(nhfVm->registers_.ReadGpr(2), 4u);
    EXPECT_EQ(nhfVm->registers_.ReadGpr(3), 104u);

    auto nhnfVm = runPredictedSource<RV5StageProcessorNHNF>(BranchPredictorType::TwoBitBHT);
    EXPECT_EQ(nhnfVm->registers_.ReadGpr(2), 4u);
    EXPECT_EQ(nhnfVm->registers_.ReadGpr(3), 104u);
}

TEST(RV5SBranchPredictorPipeline, PredictsThroughHazardStalls)
{
    // The branch reads the register produced by the immediately preceding addi, so the
    // hazard unit must stall. The prediction latched with the fetch has to survive it.
    setupVmStateDirectory();
    const std::string source = "li x1, 3\n"
                               "loop:\n"
                               "addi x1, x1, -1\n"
                               "bne x1, x0, loop\n"
                               "addi x2, x1, 42\n";

    auto hf = runPredictedProgram<RV5StageProcessorHF>(source);
    EXPECT_EQ(hf->registers_.ReadGpr(1), 0u);
    EXPECT_EQ(hf->registers_.ReadGpr(2), 42u);
    EXPECT_EQ(hf->GetBranchPredictions(), 3u);
    EXPECT_EQ(hf->GetBranchPredictor().GetMispredictions(), 2u);

    auto hnf = runPredictedProgram<RV5StageProcessorHNF>(source);
    EXPECT_EQ(hnf->registers_.ReadGpr(1), 0u);
    EXPECT_EQ(hnf->registers_.ReadGpr(2), 42u);
    EXPECT_EQ(hnf->GetBranchPredictions(), 3u);
    EXPECT_EQ(hnf->GetBranchPredictor().GetMispredictions(), 2u);
}

TEST(RV5SBranchPredictorPipeline, UndoRedoRestoresPredictorState)
{
    auto vm = runPredictedSource<RV5StageProcessorHF>(BranchPredictorType::StaticNotTaken);
    vm->Reset();
    vm->SetBranchPredictorType(BranchPredictorType::TwoBitBHT);

    std::istringstream stream(loopProgram());
    vm->LoadProgram(assemble(stream));

    // Step until the first branch has trained the predictor.
    unsigned int predictorUpdates = 0;
    for (int i = 0; i < 200 && vm->GetBranchPredictions() == 0; ++i)
    {
        vm->Step();
    }
    predictorUpdates = static_cast<unsigned int>(vm->GetBranchPredictions());
    ASSERT_EQ(predictorUpdates, 1u);

    const uint64_t mispredictionsAfterFirstBranch =
        vm->GetBranchPredictor().GetMispredictions();
    const auto entriesAfterFirstBranch = vm->GetBranchPredictorEntries();
    ASSERT_EQ(entriesAfterFirstBranch.size(), 1u);

    vm->Undo();
    EXPECT_EQ(vm->GetBranchPredictions(), 0u);
    EXPECT_EQ(vm->GetBranchPredictor().GetMispredictions(), 0u);
    EXPECT_TRUE(vm->GetBranchPredictorEntries().empty());

    vm->Redo();
    EXPECT_EQ(vm->GetBranchPredictions(), 1u);
    EXPECT_EQ(vm->GetBranchPredictor().GetMispredictions(), mispredictionsAfterFirstBranch);
    const auto entriesAfterRedo = vm->GetBranchPredictorEntries();
    ASSERT_EQ(entriesAfterRedo.size(), 1u);
    EXPECT_EQ(entriesAfterRedo[0].pc, entriesAfterFirstBranch[0].pc);
    EXPECT_EQ(entriesAfterRedo[0].state, entriesAfterFirstBranch[0].state);
}

} // namespace
