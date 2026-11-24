#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "processor/cache/cache.h"
#include "processor/cache/cacheconfig.h"
#include "processor/main_memory.h"

namespace {

using Kites::AllocationPolicy;
using Kites::Cache;
using Kites::CacheConfig;
using Kites::MainMemory;
using Kites::ReplacementPolicy;
using Kites::WritePolicy;

enum class Workload
{
    AddressDecomposition,
    RepeatedReadLocality,
    StoreCoherenceAndFlush,
    SetConflictEviction
};

struct CacheScenario
{
    CacheConfig config;
    Workload workload;
};

const char *writePolicyName(WritePolicy policy)
{
    return policy == WritePolicy::WriteThrough ? "WT" : "WB";
}

const char *allocationPolicyName(AllocationPolicy policy)
{
    return policy == AllocationPolicy::WriteAllocate ? "WA" : "NWA";
}

const char *replacementPolicyName(ReplacementPolicy policy)
{
    return policy == ReplacementPolicy::LRU ? "LRU" : "FIFO";
}

const char *workloadName(Workload workload)
{
    switch (workload)
    {
    case Workload::AddressDecomposition: return "AddressDecomposition";
    case Workload::RepeatedReadLocality: return "RepeatedReadLocality";
    case Workload::StoreCoherenceAndFlush: return "StoreCoherenceAndFlush";
    case Workload::SetConflictEviction: return "SetConflictEviction";
    }
    return "Unknown";
}

std::vector<CacheScenario> buildCacheScenarios()
{
    constexpr size_t setCounts[] = {1, 4};
    constexpr size_t lineSizes[] = {4, 8, 16, 32};
    constexpr size_t wayCounts[] = {1, 2, 4};
    constexpr WritePolicy writePolicies[] = {WritePolicy::WriteThrough, WritePolicy::WriteBack};
    constexpr AllocationPolicy allocationPolicies[] = {
        AllocationPolicy::WriteAllocate, AllocationPolicy::NoWriteAllocate};
    constexpr ReplacementPolicy replacementPolicies[] = {
        ReplacementPolicy::LRU, ReplacementPolicy::FIFO};
    constexpr Workload workloads[] = {
        Workload::AddressDecomposition,
        Workload::RepeatedReadLocality,
        Workload::StoreCoherenceAndFlush,
        Workload::SetConflictEviction};

    std::vector<CacheScenario> scenarios;
    scenarios.reserve(768);
    for (size_t sets : setCounts)
    for (size_t lineSize : lineSizes)
    for (size_t ways : wayCounts)
    for (WritePolicy writePolicy : writePolicies)
    for (AllocationPolicy allocationPolicy : allocationPolicies)
    for (ReplacementPolicy replacementPolicy : replacementPolicies)
    {
        const CacheConfig config{sets, lineSize, ways, writePolicy,
                                 allocationPolicy, replacementPolicy};
        for (Workload workload : workloads)
            scenarios.push_back({config, workload});
    }
    return scenarios;
}

std::string scenarioName(const ::testing::TestParamInfo<CacheScenario> &info)
{
    const auto &config = info.param.config;
    std::ostringstream name;
    name << "S" << config.lineCount
         << "_L" << config.lineSizeInBytes
         << "_W" << config.wayCount
         << "_" << writePolicyName(config.writePolicy)
         << "_" << allocationPolicyName(config.allocationPolicy)
         << "_" << replacementPolicyName(config.replacementPolicy)
         << "_" << workloadName(info.param.workload);
    return name.str();
}

class CacheConfigurationMatrixTest : public ::testing::TestWithParam<CacheScenario>
{
};

TEST_P(CacheConfigurationMatrixTest, PreservesConfiguredBehavior)
{
    const CacheScenario scenario = GetParam();
    const CacheConfig &config = scenario.config;
    MainMemory memory;
    Cache cache(memory, config.lineCount, config.lineSizeInBytes, config.wayCount,
                 config.writePolicy, config.allocationPolicy, config.replacementPolicy);

    constexpr uint64_t base = 0x200;

    switch (scenario.workload)
    {
    case Workload::AddressDecomposition:
    {
        const uint64_t address = 0x12345;
        const size_t offsetBits = static_cast<size_t>(std::countr_zero(config.lineSizeInBytes));
        const size_t setBits = static_cast<size_t>(std::countr_zero(config.lineCount));
        EXPECT_EQ(cache.getOffset(address), address & (config.lineSizeInBytes - 1));
        EXPECT_EQ(cache.getSetIndex(address),
                  (address >> offsetBits) & (config.lineCount - 1));
        EXPECT_EQ(cache.getTag(address), address >> (offsetBits + setBits));
        break;
    }
    case Workload::RepeatedReadLocality:
    {
        memory.writeWord(base, 0xA1B2C3D4);
        EXPECT_EQ(cache.readWord(base), 0xA1B2C3D4u);
        EXPECT_EQ(cache.readWord(base), 0xA1B2C3D4u);
        EXPECT_EQ(cache.readWord(base), 0xA1B2C3D4u);
        EXPECT_EQ(cache.getMissCount(), 1u);
        EXPECT_EQ(cache.getHitCount(), 2u);
        break;
    }
    case Workload::StoreCoherenceAndFlush:
    {
        constexpr uint32_t value = 0x5A6B7C8D;
        cache.writeWord(base, value);
        EXPECT_EQ(cache.getMissCount(), 1u);

        const bool retainedInCache = config.allocationPolicy == AllocationPolicy::WriteAllocate;
        if (config.writePolicy == WritePolicy::WriteThrough || !retainedInCache)
            EXPECT_EQ(memory.readWord(base), value);
        else
            EXPECT_EQ(memory.readWord(base), 0u);

        cache.flush();
        EXPECT_EQ(memory.readWord(base), value);
        break;
    }
    case Workload::SetConflictEviction:
    {
        const uint64_t setStride = config.lineCount * config.lineSizeInBytes;
        for (size_t i = 0; i < config.wayCount + 1; ++i)
        {
            const uint64_t address = base + i * setStride;
            memory.writeWord(address, static_cast<uint32_t>(0x1000 + i));
            EXPECT_EQ(cache.readWord(address), static_cast<uint32_t>(0x1000 + i));
        }
        EXPECT_EQ(cache.getMissCount(), config.wayCount + 1);

        // All blocks map to one set; after filling it, the first inserted block
        // must have been evicted under both LRU and FIFO.
        EXPECT_EQ(cache.readWord(base), 0x1000u);
        EXPECT_EQ(cache.getMissCount(), config.wayCount + 2);
        break;
    }
    }
}

INSTANTIATE_TEST_SUITE_P(
    GeometryPolicyAndWorkloadMatrix,
    CacheConfigurationMatrixTest,
    ::testing::ValuesIn(buildCacheScenarios()),
    scenarioName);

} // namespace
