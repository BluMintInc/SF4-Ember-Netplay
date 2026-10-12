#pragma once
#include "ControllerView.hxx"
#include <cstdint>

namespace sf4e { namespace gamepads {
enum class HintContext { HiddenMainMenu, OpenMainMenu, Room, Fight, NativeMenu };
enum class HintKind { Connected, Adopted, Keyboard, Unmapped, Full, Reconnected, Disconnected };
struct PadHint { HintKind kind; std::string name; };
inline bool ShowPadHint(HintContext context, HintKind kind) {
    return context == HintContext::HiddenMainMenu || (context == HintContext::OpenMainMenu && kind != HintKind::Disconnected);
}
inline const char* PadHintKey(HintKind kind) {
    switch (kind) {
    case HintKind::Adopted: return "controller.connected_adopted";
    case HintKind::Keyboard: return "controller.connected_keyboard";
    case HintKind::Unmapped: return "controller.connected_unmapped";
    case HintKind::Full: return "controller.connected_full";
    case HintKind::Reconnected: return "controller.reconnected";
    case HintKind::Disconnected: return "controller.disconnected_named";
    default: return "controller.connected";
    }
}
// Identity, rather than SDL instance or slot, cancels a brief disconnect.
// Events mature after one second and are consumed even outside hint contexts.
class ControllerHints {
public:
    void Adopted(const Identity& identity, HintContext context, std::uint64_t now) {
        for (auto& event : pending_) if (!event.removed && Same(event.identity, identity)) {
            event.kind = HintKind::Adopted; event.context = context; return;
        }
        pending_.push_back({identity, HintKind::Adopted, now, false, true, context});
    }
    void Selected(const Identity& identity) {
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Event& event) { return Same(event.identity, identity) && !event.removed; }), pending_.end());
        for (auto& event : pending_) if (event.removed) event.announce = false;
        lostGameplay_.clear();
    }
    std::vector<PadHint> Update(const std::vector<PadView>& pads, bool keyboard, HintContext context, std::uint64_t now) {
        if (!seeded_) { previous_ = pads; seeded_ = true; return {}; }
        const auto find = [](const std::vector<PadView>& list, const Identity& identity) {
            return std::find_if(list.begin(), list.end(), [&](const PadView& pad) { return pad.state != RowState::Absent && Same(pad.identity, identity); });
        };
        for (const auto& old : previous_) if (old.state != RowState::Absent && find(pads, old.identity) == pads.end()) {
            const auto added = std::find_if(pending_.begin(), pending_.end(), [&](const Event& e) { return !e.removed && Same(e.identity, old.identity); });
            if (added != pending_.end()) pending_.erase(added);
            else {
                auto identity = old.identity; identity.name = old.name;
                pending_.push_back({std::move(identity), HintKind::Disconnected, now, true, old.gameplay, context});
                if (old.gameplay) lostGameplay_.push_back(old.identity);
            }
        }
        for (const auto& pad : pads) if (pad.state != RowState::Absent) {
            const auto old = find(previous_, pad.identity);
            const bool placed = old != previous_.end() && old->state == RowState::NotInUse && pad.state != RowState::NotInUse;
            if (old != previous_.end() && !placed) continue;
            const auto removed = std::find_if(pending_.begin(), pending_.end(), [&](const Event& e) { return e.removed && Same(e.identity, pad.identity) && now - e.since < 1000; });
            const auto lost = std::find_if(lostGameplay_.begin(), lostGameplay_.end(), [&](const Identity& id) { return Same(id, pad.identity); });
            if (removed != pending_.end()) {
                pending_.erase(removed);
                pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Event& e) {
                    return Same(e.identity, pad.identity);
                }), pending_.end());
            }
            else {
                const auto kind = placed ? HintKind::Connected : lost != lostGameplay_.end() && pad.gameplay ? HintKind::Reconnected : pad.state == RowState::Unmapped ? HintKind::Unmapped :
                    pad.state == RowState::NotInUse ? HintKind::Full : keyboard ? HintKind::Keyboard : HintKind::Connected;
                const bool adopted = std::any_of(pending_.begin(), pending_.end(), [&](const Event& e) { return e.kind == HintKind::Adopted && Same(e.identity, pad.identity); });
                if (!adopted) {
                    auto identity = pad.identity; identity.name = pad.name;
                    pending_.push_back({std::move(identity), kind, now, false, true, context});
                }
            }
            if (lost != lostGameplay_.end()) lostGameplay_.erase(lost);
        }
        previous_ = pads;
        std::vector<PadHint> hints;
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (now - it->since < 1000) { ++it; continue; }
            const auto kind = it->kind == HintKind::Keyboard && !keyboard ? HintKind::Connected : it->kind;
            if ((!it->removed || it->announce) && ShowPadHint(it->context, kind) && ShowPadHint(context, kind)) hints.push_back({kind, it->identity.name});
            it = pending_.erase(it);
        }
        return hints;
    }
private:
    struct Event { Identity identity; HintKind kind; std::uint64_t since; bool removed, announce; HintContext context; };
    bool seeded_ = false;
    std::vector<PadView> previous_;
    std::vector<Event> pending_;
    std::vector<Identity> lostGameplay_;
};
} }
