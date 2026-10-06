#include "drawtool/BoxStore.h"

namespace drawtool {

BoxHandle BoxStore::Add(const BoxDesc& d) {
    uint32_t slot;
    if (!freeSlots_.empty()) {
        slot = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        slot = static_cast<uint32_t>(slots_.size());
        slots_.push_back({});
    }

    const uint32_t dense = Size();
    slots_[slot].dense = dense;

    x_.push_back(d.pos.x);
    y_.push_back(d.pos.y);
    w_.push_back(d.size.x);
    h_.push_back(d.size.y);
    vx_.push_back(d.vel.x);
    vy_.push_back(d.vel.y);
    tx_.push_back(d.pos.x);
    ty_.push_back(d.pos.y);
    tw_.push_back(d.size.x);
    th_.push_back(d.size.y);
    color_.push_back(d.color);
    user_.push_back(d.user);
    denseToSlot_.push_back(slot);

    return {slot, slots_[slot].generation};
}

bool BoxStore::Remove(BoxHandle handle) {
    const uint32_t idx = IndexOf(handle);
    if (idx == kInvalidIndex)
        return false;

    const uint32_t last = Size() - 1;
    if (idx != last) {
        ForEachColumn([&](auto& col) { col[idx] = col[last]; });
        slots_[denseToSlot_[idx]].dense = idx;
    }
    ForEachColumn([](auto& col) { col.pop_back(); });

    Slot& s = slots_[handle.slot];
    s.dense = kInvalidIndex;
    ++s.generation;
    freeSlots_.push_back(handle.slot);
    return true;
}

void BoxStore::Clear() {
    ForEachColumn([](auto& col) { col.clear(); });
    freeSlots_.clear();
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        slots_[i].dense = kInvalidIndex;
        ++slots_[i].generation;
        freeSlots_.push_back(static_cast<uint32_t>(slots_.size()) - 1 - i);
    }
}

void BoxStore::Reserve(uint32_t capacity) {
    ForEachColumn([&](auto& col) { col.reserve(capacity); });
    slots_.reserve(capacity);
}

uint32_t BoxStore::IndexOf(BoxHandle handle) const {
    if (handle.slot >= slots_.size())
        return kInvalidIndex;
    const Slot& s = slots_[handle.slot];
    return s.generation == handle.generation ? s.dense : kInvalidIndex;
}

BoxHandle BoxStore::HandleAt(uint32_t index) const {
    if (index >= Size())
        return {};
    const uint32_t slot = denseToSlot_[index];
    return {slot, slots_[slot].generation};
}

bool BoxStore::SetPosition(BoxHandle h, ImVec2 pos) {
    const uint32_t i = IndexOf(h);
    if (i == kInvalidIndex) return false;
    x_[i] = pos.x;
    y_[i] = pos.y;
    return true;
}

bool BoxStore::SetSize(BoxHandle h, ImVec2 size) {
    const uint32_t i = IndexOf(h);
    if (i == kInvalidIndex) return false;
    w_[i] = size.x;
    h_[i] = size.y;
    return true;
}

bool BoxStore::SetVelocity(BoxHandle h, ImVec2 vel) {
    const uint32_t i = IndexOf(h);
    if (i == kInvalidIndex) return false;
    vx_[i] = vel.x;
    vy_[i] = vel.y;
    return true;
}

bool BoxStore::SetColor(BoxHandle h, ImU32 color) {
    const uint32_t i = IndexOf(h);
    if (i == kInvalidIndex) return false;
    color_[i] = color;
    return true;
}

bool BoxStore::SetTarget(BoxHandle h, ImVec2 pos, ImVec2 size) {
    const uint32_t i = IndexOf(h);
    if (i == kInvalidIndex) return false;
    tx_[i] = pos.x;
    ty_[i] = pos.y;
    tw_[i] = size.x;
    th_[i] = size.y;
    return true;
}

bool BoxStore::Snap(BoxHandle h, ImVec2 pos, ImVec2 size) {
    const uint32_t i = IndexOf(h);
    if (i == kInvalidIndex) return false;
    x_[i] = tx_[i] = pos.x;
    y_[i] = ty_[i] = pos.y;
    w_[i] = tw_[i] = size.x;
    h_[i] = th_[i] = size.y;
    return true;
}

BoxView BoxStore::View() const {
    BoxView v;
    v.x = x_.data();
    v.y = y_.data();
    v.w = w_.data();
    v.h = h_.data();
    v.color = color_.data();
    v.user = user_.data();
    v.count = Size();
    return v;
}

BoxSpan BoxStore::Span() {
    BoxSpan s;
    s.x = x_.data();
    s.y = y_.data();
    s.w = w_.data();
    s.h = h_.data();
    s.vx = vx_.data();
    s.vy = vy_.data();
    s.tx = tx_.data();
    s.ty = ty_.data();
    s.tw = tw_.data();
    s.th = th_.data();
    s.color = color_.data();
    s.count = Size();
    return s;
}

}  // namespace drawtool
