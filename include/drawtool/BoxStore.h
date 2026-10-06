#pragma once

#include <cstdint>
#include <vector>

#include "drawtool/Types.h"

namespace drawtool {

// Stable reference to a box. Survives other boxes being added/removed; becomes
// invalid (IsValid() == false) once its own box is removed.
struct BoxHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;

    bool IsNull() const { return slot == UINT32_MAX; }
    friend bool operator==(BoxHandle a, BoxHandle b) { return a.slot == b.slot && a.generation == b.generation; }
    friend bool operator!=(BoxHandle a, BoxHandle b) { return !(a == b); }
};

struct BoxDesc {
    ImVec2 pos{0.0f, 0.0f};    // top-left, pixels
    ImVec2 size{10.0f, 10.0f};
    ImVec2 vel{0.0f, 0.0f};    // px/s
    ImU32 color = IM_COL32_WHITE;
    uint64_t user = 0;         // free for your own id / pointer
};

// Packed structure-of-arrays box storage with O(1) add/remove and stable
// handles (slot map). Columns stay contiguous so per-frame update and render
// loops are linear, cache friendly and auto-vectorizable. Removal swaps the
// last box into the hole, so dense indices are NOT stable -- keep handles.
class BoxStore {
public:
    static constexpr uint32_t kInvalidIndex = UINT32_MAX;

    BoxHandle Add(const BoxDesc& desc);
    bool Remove(BoxHandle handle);
    void Clear();
    void Reserve(uint32_t capacity);

    bool IsValid(BoxHandle handle) const { return IndexOf(handle) != kInvalidIndex; }
    uint32_t IndexOf(BoxHandle handle) const;
    BoxHandle HandleAt(uint32_t index) const;
    uint32_t Size() const { return static_cast<uint32_t>(x_.size()); }
    bool Empty() const { return x_.empty(); }

    // Per-box setters by handle. Return false for stale handles.
    bool SetPosition(BoxHandle h, ImVec2 pos);
    bool SetSize(BoxHandle h, ImVec2 size);
    bool SetVelocity(BoxHandle h, ImVec2 vel);
    bool SetColor(BoxHandle h, ImU32 color);
    // Target used by SmoothFollowMotion. Feed positions here at whatever rate
    // your data arrives (e.g. 30-60 Hz) and the boxes glide at render rate.
    bool SetTarget(BoxHandle h, ImVec2 pos, ImVec2 size);
    // Teleport: sets position and target so follow motion does not glide.
    bool Snap(BoxHandle h, ImVec2 pos, ImVec2 size);

    BoxView View() const;
    BoxSpan Span();

    // Direct column access for bulk work (index range [0, Size())).
    float* X() { return x_.data(); }
    float* Y() { return y_.data(); }
    float* W() { return w_.data(); }
    float* H() { return h_.data(); }
    ImU32* Colors() { return color_.data(); }
    uint64_t* User() { return user_.data(); }

private:
    struct Slot {
        uint32_t dense = kInvalidIndex;
        uint32_t generation = 0;
    };

    template <class F>
    void ForEachColumn(F&& f) {
        f(x_); f(y_); f(w_); f(h_);
        f(vx_); f(vy_);
        f(tx_); f(ty_); f(tw_); f(th_);
        f(color_); f(user_);
        f(denseToSlot_);
    }

    std::vector<float> x_, y_, w_, h_;
    std::vector<float> vx_, vy_;
    std::vector<float> tx_, ty_, tw_, th_;
    std::vector<ImU32> color_;
    std::vector<uint64_t> user_;
    std::vector<uint32_t> denseToSlot_;

    std::vector<Slot> slots_;
    std::vector<uint32_t> freeSlots_;
};

}  // namespace drawtool
