#include <gtest/gtest.h>

#include "assembler/assembler.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "utils/utils.h"

using namespace Kites;

template <typename Vm>
static std::unique_ptr<Vm> runDoubleProgram(const std::string& filename)
{
    setupVmStateDirectory();
    auto vm = std::make_unique<Vm>();
    vm->LoadProgram(assemble(filename));
    vm->DebugRun();
    return vm;
}

template <typename Vm>
static void checkDoubleArithmeticAndMemory()
{
    auto vm = runDoubleProgram<Vm>("../examples/d_extension_nh_nf_test.s");
    EXPECT_EQ(vm->registers_.ReadGpr(10), 13);
    EXPECT_EQ(vm->registers_.ReadGpr(11), 1);
    EXPECT_EQ(vm->registers_.ReadGpr(12), 13);
}

template <typename Vm>
static void checkDoubleMemorySlots()
{
    auto vm = runDoubleProgram<Vm>("../examples/d_extension_memory_nh_nf_test.s");
    EXPECT_EQ(vm->registers_.ReadGpr(10), 5);
    EXPECT_EQ(vm->registers_.ReadGpr(11), 17);
    EXPECT_EQ(vm->registers_.ReadGpr(12), 1);
    EXPECT_EQ(vm->registers_.ReadGpr(13), 1);
    EXPECT_EQ(vm->registers_.ReadGpr(14), 17);
}

#define CHECK_D_PIPELINE_MODE(suite, vm_type) \
    TEST(suite, double_arithmetic_conversion_and_roundtrip) \
    { checkDoubleArithmeticAndMemory<vm_type>(); } \
    TEST(suite, double_memory_slots_and_overwrite) \
    { checkDoubleMemorySlots<vm_type>(); }

CHECK_D_PIPELINE_MODE(RV5_FP_NH_NF, RV5StageProcessorNHNF)
CHECK_D_PIPELINE_MODE(RV5_FP_NH_F, RV5StageProcessorNHF)
CHECK_D_PIPELINE_MODE(RV5_FP_H_NF, RV5StageProcessorHNF)
CHECK_D_PIPELINE_MODE(RV5_FP_H_F, RV5StageProcessorHF)

#undef CHECK_D_PIPELINE_MODE
