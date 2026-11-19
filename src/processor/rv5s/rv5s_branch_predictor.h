#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Kites
{

enum class BranchPredictorType
{
    TwoBitBHT,     //2-bit saturating counter BHT + 64-entry BTB
    StaticNotTaken  // Baseline fallback: always predict PC + 4 (MEM-stage resolution)
};

struct BranchPrediction
{
    bool predicted_taken = false;
    uint64_t predicted_target = 0;
    bool btb_hit = false;
};

struct BTBEntry
{
    bool valid = false;
    uint64_t tag = 0;
    uint64_t target = 0;
    bool is_unconditional = false;
};

struct BTBInspectorRow
{
    size_t index = 0;
    uint64_t pc = 0;
    uint64_t target = 0;
    uint8_t state = 1; // 0..3
    bool is_unconditional = false;
};

struct BranchPredictorDelta
{
    bool recorded = false;
    bool table_updated = false;
    size_t index = 0;
    BTBEntry old_btb_entry{};
    BTBEntry new_btb_entry{};
    uint8_t old_bht_state = 1;
    uint8_t new_bht_state = 1;
    bool was_btb_hit = false;
    bool was_mispredicted = false;
};

class RV5SBranchPredictor
{
  public:
    static constexpr size_t kTableSize = 64; // Indexed by PC[7:2]

    explicit RV5SBranchPredictor(BranchPredictorType type = BranchPredictorType::TwoBitBHT)
        : type_(type)
    {
        Reset();
    }

    void SetType(BranchPredictorType type)
    {
        type_ = type;
        Reset();
    }

    BranchPredictorType GetType() const { return type_; }

    void Reset()
    {
        btb_.fill(BTBEntry{});
        bht_.fill(1); // 01 = Weakly Not Taken
        total_branches_ = 0;
        mispredictions_ = 0;
        btb_hits_ = 0;
    }

    // Queried during IF (pipeline_fetch)
    BranchPrediction Predict(uint64_t pc) const
    {
        BranchPrediction pred{};
        if (type_ == BranchPredictorType::StaticNotTaken)
        {
            pred.predicted_taken = false;
            pred.predicted_target = pc + 4;
            return pred;
        }

        const size_t idx = Index(pc);
        const BTBEntry &entry = btb_[idx];
        const bool hit = entry.valid && (entry.tag == pc);
        pred.btb_hit = hit;

        if (!hit)
        {
            pred.predicted_taken = false;
            pred.predicted_target = pc + 4;
            return pred;
        }

        if (entry.is_unconditional)
        {
            pred.predicted_taken = true;
            pred.predicted_target = entry.target;
            return pred;
        }

        // 2-bit saturating counter: predict Taken for states 2 (10) and 3 (11)
        const bool take = (bht_[idx] >= 2);
        pred.predicted_taken = take;
        pred.predicted_target = take ? entry.target : (pc + 4);
        return pred;
    }

    // Called during EX (pipeline_execute) when branch condition and target resolve.
    // The static baseline keeps no table and no statistics: the pipeline reports its
    // mispredictions through branch_mispredictions_ instead.
    void Update(uint64_t branch_pc, bool actual_taken, uint64_t actual_target,
                bool is_unconditional, bool btb_hit, bool mispredicted,
                BranchPredictorDelta *delta = nullptr)
    {
        if (type_ == BranchPredictorType::StaticNotTaken)
        {
            return;
        }

        ++total_branches_;
        if (btb_hit) ++btb_hits_;
        if (mispredicted) ++mispredictions_;

        if (delta)
        {
            delta->recorded = true;
            delta->was_btb_hit = btb_hit;
            delta->was_mispredicted = mispredicted;
            delta->table_updated = false;
        }

        const size_t idx = Index(branch_pc);
        const BTBEntry old_btb = btb_[idx];
        const uint8_t old_state = bht_[idx];

        // Allocate BTB entry when branch/jump is taken or refresh an existing matching entry
        if (actual_taken || (old_btb.valid && old_btb.tag == branch_pc))
        {
            btb_[idx] = BTBEntry{true, branch_pc, actual_target, is_unconditional};
        }

        // Saturating 2-bit counter transition:
        // Taken:     0 -> 1 -> 2 -> 3 (saturates at 3)
        // Not Taken: 3 -> 2 -> 1 -> 0 (saturates at 0)
        uint8_t new_state = old_state;
        if (!is_unconditional)
        {
            if (actual_taken)
            {
                new_state = (old_state < 3) ? static_cast<uint8_t>(old_state + 1) : 3;
            }
            else
            {
                new_state = (old_state > 0) ? static_cast<uint8_t>(old_state - 1) : 0;
            }
            bht_[idx] = new_state;
        }

        if (delta)
        {
            delta->table_updated = true;
            delta->index = idx;
            delta->old_btb_entry = old_btb;
            delta->new_btb_entry = btb_[idx];
            delta->old_bht_state = old_state;
            delta->new_bht_state = new_state;
        }
    }

    void Rollback(const BranchPredictorDelta &delta)
    {
        if (!delta.recorded) return;
        if (total_branches_ > 0) --total_branches_;
        if (delta.was_btb_hit && btb_hits_ > 0) --btb_hits_;
        if (delta.was_mispredicted && mispredictions_ > 0) --mispredictions_;
        if (delta.table_updated)
        {
            btb_[delta.index] = delta.old_btb_entry;
            bht_[delta.index] = delta.old_bht_state;
        }
    }

    void Reapply(const BranchPredictorDelta &delta)
    {
        if (!delta.recorded) return;
        ++total_branches_;
        if (delta.was_btb_hit) ++btb_hits_;
        if (delta.was_mispredicted) ++mispredictions_;
        if (delta.table_updated)
        {
            btb_[delta.index] = delta.new_btb_entry;
            bht_[delta.index] = delta.new_bht_state;
        }
    }

    uint64_t GetTotalPredictions() const { return total_branches_; }
    uint64_t GetMispredictions() const { return mispredictions_; }
    uint64_t GetBTBHits() const { return btb_hits_; }

    double GetAccuracy() const
    {
        if (total_branches_ == 0) return 100.0;
        const uint64_t correct =
            total_branches_ > mispredictions_ ? (total_branches_ - mispredictions_) : 0;
        return (static_cast<double>(correct) * 100.0) / static_cast<double>(total_branches_);
    }

    std::vector<BTBInspectorRow> GetActiveEntries() const
    {
        std::vector<BTBInspectorRow> rows;
        for (size_t i = 0; i < kTableSize; ++i)
        {
            if (btb_[i].valid)
            {
                rows.push_back(BTBInspectorRow{
                    i, btb_[i].tag, btb_[i].target, bht_[i], btb_[i].is_unconditional});
            }
        }
        return rows;
    }

  private:
    static constexpr size_t Index(uint64_t pc) { return (pc >> 2) & (kTableSize - 1); }

    BranchPredictorType type_;
    std::array<BTBEntry, kTableSize> btb_{};
    std::array<uint8_t, kTableSize> bht_{};
    uint64_t total_branches_ = 0;
    uint64_t mispredictions_ = 0;
    uint64_t btb_hits_ = 0;
};

}