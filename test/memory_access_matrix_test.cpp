#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "processor/main_memory.h"

namespace {

using Kites::MainMemory;

enum class AccessWidth : size_t { Byte = 1, Half = 2, Word = 4, DoubleWord = 8 };

struct MemoryScenario
{
    AccessWidth width;
    size_t offset;
    size_t pattern;
};

const char *widthName(AccessWidth width)
{
    switch (width)
    {
    case AccessWidth::Byte: return "Byte";
    case AccessWidth::Half: return "Half";
    case AccessWidth::Word: return "Word";
    case AccessWidth::DoubleWord: return "DoubleWord";
    }
    return "Unknown";
}

std::vector<MemoryScenario> memoryScenarios()
{
    constexpr AccessWidth widths[] = {
        AccessWidth::Byte, AccessWidth::Half, AccessWidth::Word, AccessWidth::DoubleWord};
    std::vector<MemoryScenario> scenarios;
    scenarios.reserve(256);
    for (AccessWidth width : widths)
        for (size_t offset = 0; offset < 16; ++offset)
            for (size_t pattern = 0; pattern < 4; ++pattern)
                scenarios.push_back({width, offset, pattern});
    return scenarios;
}

uint64_t patternValue(AccessWidth width, size_t pattern)
{
    const size_t bits = static_cast<size_t>(width) * 8;
    const uint64_t mask = bits == 64 ? UINT64_MAX : ((uint64_t{1} << bits) - 1);
    switch (pattern)
    {
    case 0: return 0;
    case 1: return mask;
    case 2: return 0xAAAAAAAAAAAAAAAAULL & mask;
    case 3: return uint64_t{1} << (bits - 1);
    default: return 0;
    }
}

std::string memoryScenarioName(const ::testing::TestParamInfo<MemoryScenario> &info)
{
    std::ostringstream name;
    name << widthName(info.param.width)
         << "_Offset" << info.param.offset
         << "_Pattern" << info.param.pattern;
    return name.str();
}

class MemoryAccessMatrixTest : public ::testing::TestWithParam<MemoryScenario>
{
};

TEST_P(MemoryAccessMatrixTest, PreservesValueAndNeighborBytes)
{
    const MemoryScenario scenario = GetParam();
    MainMemory memory;
    constexpr uint64_t base = 0x400;
    const uint64_t address = base + scenario.offset;
    const size_t size = static_cast<size_t>(scenario.width);
    const uint64_t value = patternValue(scenario.width, scenario.pattern);
    constexpr uint8_t guard = 0xE7;

    memory.writeByte(address - 1, guard);
    memory.writeByte(address + size, guard);

    uint64_t actual = 0;
    switch (scenario.width)
    {
    case AccessWidth::Byte:
        memory.writeByte(address, static_cast<uint8_t>(value));
        actual = memory.readByte(address);
        break;
    case AccessWidth::Half:
        memory.writeHalfWord(address, static_cast<uint16_t>(value));
        actual = memory.readHalfWord(address);
        break;
    case AccessWidth::Word:
        memory.writeWord(address, static_cast<uint32_t>(value));
        actual = memory.readWord(address);
        break;
    case AccessWidth::DoubleWord:
        memory.writeDoubleWord(address, value);
        actual = memory.readDoubleWord(address);
        break;
    }

    EXPECT_EQ(actual, value);
    EXPECT_EQ(memory.readByte(address - 1), guard);
    EXPECT_EQ(memory.readByte(address + size), guard);
}

INSTANTIATE_TEST_SUITE_P(
    WidthOffsetAndDataPatternMatrix,
    MemoryAccessMatrixTest,
    ::testing::ValuesIn(memoryScenarios()),
    memoryScenarioName);

} // namespace
