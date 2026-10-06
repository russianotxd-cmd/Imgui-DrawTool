#pragma once

#include <chrono>
#include <thread>

namespace drawtool {

// Holds a loop to a target frame rate with low jitter: sleeps for most of the
// remaining frame, then spins the last stretch (OS sleep granularity is ~1 ms
// on Linux and up to 15.6 ms on Windows without timeBeginPeriod(1)).
//
// Turn vsync OFF (glfwSwapInterval(0) / Present(0, ...)) or the swap chain
// will cap you at the monitor refresh rate before this ever matters.
class FrameLimiter {
public:
    using Clock = std::chrono::steady_clock;

    // fps <= 0 disables limiting.
    explicit FrameLimiter(double fps = 300.0) { SetTarget(fps); }

    void SetTarget(double fps) {
        fps_ = fps;
        period_ = fps > 0.0 ? std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps))
                            : Clock::duration::zero();
        next_ = Clock::now() + period_;
    }
    double Target() const { return fps_; }

    // How close to the deadline we stop sleeping and start spinning.
    void SetSpinWindow(std::chrono::microseconds w) { spin_ = w; }

    // Call once per frame, after presenting.
    void Wait() {
        if (period_ == Clock::duration::zero())
            return;
        const auto now = Clock::now();
        if (next_ > now) {
            if (next_ - now > spin_)
                std::this_thread::sleep_until(next_ - spin_);
            while (Clock::now() < next_) {
                // spin
            }
            next_ += period_;
        } else {
            // Running behind: don't try to "catch up" with a burst of frames.
            next_ = now + period_;
        }
    }

private:
    double fps_ = 0.0;
    Clock::duration period_{};
    Clock::time_point next_{};
    std::chrono::microseconds spin_{1500};
};

}  // namespace drawtool
