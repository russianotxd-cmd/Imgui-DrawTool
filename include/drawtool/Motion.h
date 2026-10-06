#pragma once

#include <cmath>
#include <memory>
#include <utility>

#include "drawtool/Types.h"

namespace drawtool {

struct MotionContext {
    float dt = 0.0f;  // seconds since last update
    Rect bounds;      // area boxes live in (usually the display)
};

// A motion module advances every box in a layer once per frame. One virtual
// call per layer per frame -- the per-box loop inside is plain, branch-light
// SoA code the compiler can vectorize. Write your own by deriving from this.
class IMotion {
public:
    virtual ~IMotion() = default;
    virtual void Update(BoxSpan boxes, const MotionContext& ctx) = 0;
};

// pos += vel * dt. Boxes are free to leave the screen (they get culled).
class LinearMotion final : public IMotion {
public:
    void Update(BoxSpan b, const MotionContext& ctx) override {
        const float dt = ctx.dt;
        for (uint32_t i = 0; i < b.count; ++i) {
            b.x[i] += b.vx[i] * dt;
            b.y[i] += b.vy[i] * dt;
        }
    }
};

// Integrates velocity and reflects off the bounds edges.
class BounceMotion final : public IMotion {
public:
    void Update(BoxSpan b, const MotionContext& ctx) override {
        const float dt = ctx.dt;
        const Rect r = ctx.bounds;
        for (uint32_t i = 0; i < b.count; ++i) {
            Axis(b.x[i], b.vx[i], b.w[i], r.x0, r.x1, dt);
            Axis(b.y[i], b.vy[i], b.h[i], r.y0, r.y1, dt);
        }
    }

private:
    static inline void Axis(float& p, float& v, float size, float lo, float hi, float dt) {
        const float maxP = hi - size > lo ? hi - size : lo;
        p += v * dt;
        if (p < lo) {
            p = lo + (lo - p);
            v = std::fabs(v);
        } else if (p > maxP) {
            p = maxP - (p - maxP);
            v = -std::fabs(v);
        }
        // Huge dt or box larger than bounds: keep it inside.
        p = p < lo ? lo : (p > maxP ? maxP : p);
    }
};

// Integrates velocity and wraps around to the opposite edge.
class WrapMotion final : public IMotion {
public:
    void Update(BoxSpan b, const MotionContext& ctx) override {
        const float dt = ctx.dt;
        const Rect r = ctx.bounds;
        for (uint32_t i = 0; i < b.count; ++i) {
            Axis(b.x[i], b.vx[i], b.w[i], r.x0, r.x1, dt);
            Axis(b.y[i], b.vy[i], b.h[i], r.y0, r.y1, dt);
        }
    }

private:
    static inline void Axis(float& p, float v, float size, float lo, float hi, float dt) {
        const float span = (hi - lo) + size;
        if (span <= 0.0f) return;
        p += v * dt;
        // Box fully past one edge re-enters from the other.
        if (p > hi) p -= span * std::floor((p - hi) / span + 1.0f);
        else if (p + size < lo) p += span * std::floor((lo - (p + size)) / span + 1.0f);
    }
};

// Frame-rate independent exponential smoothing toward the per-box target
// (BoxStore::SetTarget). Use this when positions come from somewhere slower
// than your render loop (game state at 64 Hz, a socket, a tracker...) and you
// want silky motion at 300 FPS instead of boxes stepping at the data rate.
class SmoothFollowMotion final : public IMotion {
public:
    // rate: how quickly the gap closes, 1/s. ~15-30 feels tight, ~5 floaty.
    explicit SmoothFollowMotion(float rate = 20.0f) : rate_(rate) {}

    void SetRate(float rate) { rate_ = rate; }
    float Rate() const { return rate_; }

    void Update(BoxSpan b, const MotionContext& ctx) override {
        const float a = 1.0f - std::exp(-rate_ * ctx.dt);
        for (uint32_t i = 0; i < b.count; ++i) {
            b.x[i] += (b.tx[i] - b.x[i]) * a;
            b.y[i] += (b.ty[i] - b.y[i]) * a;
            b.w[i] += (b.tw[i] - b.w[i]) * a;
            b.h[i] += (b.th[i] - b.h[i]) * a;
        }
    }

private:
    float rate_;
};

// Wrap any callable `void(BoxSpan, const MotionContext&)` as a motion module.
template <class F>
class LambdaMotion final : public IMotion {
public:
    explicit LambdaMotion(F fn) : fn_(std::move(fn)) {}
    void Update(BoxSpan b, const MotionContext& ctx) override { fn_(b, ctx); }

private:
    F fn_;
};

template <class F>
std::unique_ptr<IMotion> MakeLambdaMotion(F fn) {
    return std::make_unique<LambdaMotion<F>>(std::move(fn));
}

}  // namespace drawtool
