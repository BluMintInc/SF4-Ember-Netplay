#pragma once
#include "ButtonLayout.hxx"
#include "PadBindingRecords.hxx"

namespace sf4e { namespace gamepads {
inline bool ReadSlot(const PadBindingRecords& records, unsigned slot, ButtonLayout& layout) {
    const auto* table = records.FightTable(slot);
    if (!table) return false;
    for (unsigned i = 0; i < layout.size(); ++i) layout[i] = table[LayoutBits[i]];
    return true;
}
inline bool WriteSlot(const PadBindingRecords& records, unsigned slot, const ButtonLayout& layout) {
    auto* table = records.FightTable(slot);
    if (!table || slot >= 4) return false;
    for (unsigned i = 0; i < layout.size(); ++i) table[LayoutBits[i]] = layout[i];
    return true;
}
struct LayoutPlacement { Identity identity; ButtonLayout buttons; std::uint32_t instance = 0; };
struct LayoutOwner { Identity identity; ButtonLayout buttons, gameCopy; std::uint32_t instance = 0; };
using LayoutOwners = std::array<std::optional<LayoutOwner>, 4>;
using LayoutPlacements = std::array<std::optional<LayoutPlacement>, 4>;
enum class LayoutSource { Layout, GameCopy };
struct SlotWrite { unsigned slot; LayoutSource source; bool copyFirst; };
inline std::vector<SlotWrite> SlotWritePlan(const LayoutOwners& owners, const LayoutPlacements& placements,
    const std::array<bool, 4>& occupied) {
    std::vector<SlotWrite> writes;
    for (unsigned slot = 0; slot < 4; ++slot) {
        if (!occupied[slot]) continue;
        const auto& owner = owners[slot]; const auto& next = placements[slot];
        if (next && (!owner || !Same(owner->identity, next->identity) || owner->buttons != next->buttons || owner->instance != next->instance))
            writes.push_back({slot, LayoutSource::Layout, !owner});
        else if (!next && owner) writes.push_back({slot, LayoutSource::GameCopy, false});
    }
    return writes;
}
// Transactional bounds check: a bad vector cannot partially apply a plan.
inline bool ApplySlotWrites(LayoutOwners& owners, const LayoutPlacements& placements,
    const std::vector<SlotWrite>& writes, const PadBindingRecords& records) {
    for (const auto& write : writes) if (write.slot >= 4 || !records.FightTable(write.slot)) return false;
    for (const auto& write : writes) {
        auto& owner = owners[write.slot];
        if (write.source == LayoutSource::GameCopy) { WriteSlot(records, write.slot, owner->gameCopy); owner.reset(); }
        else {
            ButtonLayout copy;
            if (owner) copy = owner->gameCopy;
            else ReadSlot(records, write.slot, copy);
            const auto& next = *placements[write.slot];
            WriteSlot(records, write.slot, next.buttons);
            owner = LayoutOwner{next.identity, next.buttons, copy, next.instance};
        }
    }
    return true;
}
inline bool RestoreGameLayouts(LayoutOwners& owners, const PadBindingRecords& records) {
    std::vector<SlotWrite> writes;
    for (unsigned slot = 0; slot < owners.size(); ++slot)
        if (owners[slot]) writes.push_back({slot, LayoutSource::GameCopy, false});
    return ApplySlotWrites(owners, {}, writes, records);
}
// The slot holds a table the game wrote: keep it as the table to restore, and
// write the owner's layout back over it.
inline void Reclaim(LayoutOwner& owner, const PadBindingRecords& records, unsigned slot) {
    ReadSlot(records, slot, owner.gameCopy);
    WriteSlot(records, slot, owner.buttons);
}
class ButtonScreenSwap {
public:
    bool Open(const LayoutOwners& owners, const PadBindingRecords& records) {
        for (unsigned slot = 0; slot < 4; ++slot) if (owners[slot] && !records.FightTable(slot)) return false;
        for (unsigned slot = 0; slot < 4; ++slot) if (owners[slot]) WriteSlot(records, slot, owners[slot]->gameCopy);
        open_ = true;
        return true;
    }
    bool Close(LayoutOwners& owners, const PadBindingRecords& records) {
        if (!open_) return false;
        for (unsigned slot = 0; slot < 4; ++slot) if (owners[slot] && !records.FightTable(slot)) return false;
        for (unsigned slot = 0; slot < 4; ++slot) if (owners[slot]) Reclaim(*owners[slot], records, slot);
        open_ = false;
        return true;
    }
    bool IsOpen() const { return open_; }
private:
    bool open_ = false;
};
// The game loads its own button config into the binding records a few seconds
// after they exist (the .ctrlsav loader), over any layout Ember wrote there.
// An owned slot whose ten entries no longer equal its owner's layout holds that
// fresh table, so it becomes the slot's gameCopy and the layout is written back.
// Returns the slots reclaimed. While the game's button screen is open the slots
// hold gameCopy on purpose (ButtonScreenSwap), so nothing is reclaimed.
inline std::vector<unsigned> ReclaimReloadedSlots(LayoutOwners& owners, const PadBindingRecords& records,
    const ButtonScreenSwap& screen) {
    std::vector<unsigned> reclaimed;
    if (screen.IsOpen()) return reclaimed;
    for (unsigned slot = 0; slot < owners.size(); ++slot) {
        ButtonLayout actual;
        if (!owners[slot] || !ReadSlot(records, slot, actual) || actual == owners[slot]->buttons) continue;
        Reclaim(*owners[slot], records, slot);
        reclaimed.push_back(slot);
    }
    return reclaimed;
}
} }
