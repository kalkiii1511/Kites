#pragma once
#include "processor/processor_constants.h"
#include "processor/processor_types.h"
#include "utils/to_index.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Kites
{
// The reason a pipelined processor had to alter its normal flow this cycle.
enum class HazardKind
{
    None,
    Data,      // register dependency detected by the hazard detection unit
    LoadUse,   // load followed immediately by a dependent instruction
    Control,   // branch/jump changed the fetch stream
};

// Per-cycle hazard information attached to ProcessorState. The GUI/web API uses this
// to explain stalls and forwarding instead of only exposing a stall counter.
struct HazardReport
{
    HazardKind kind{HazardKind::None};
    bool stalled{false};
    bool forwarded{false};
    int stallCycles{0};
    // Instruction that produces the value (INVALID_PC when unknown).
    uint64_t sourcePc{INVALID_PC};
    // Instruction that consumes the value (INVALID_PC when unknown).
    uint64_t consumerPc{INVALID_PC};
    uint8_t registerIndex{0};
    bool isFpr{false};

    bool active() const { return kind != HazardKind::None; }

    void reset() { *this = HazardReport{}; }
};

struct ProcessorState
{
    std::vector<uint64_t> programCounters;
    uint64_t lastExecutedPC{INVALID_PC};
    HazardReport hazard;

    void reset()
    {
        programCounters.assign(programCounters.size(), 0);
        lastExecutedPC = INVALID_PC;
        hazard.reset();
    }
    // for further extension we can add more fields here
    // tho beware if this struct is too big we lose on performance
};

} // namespace Kites
