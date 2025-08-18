#pragma once
#include <atomic>
namespace MB {
    struct State {
        std::atomic_bool upscaler{ false };
        std::atomic<float> traffic{ 1.0f };
        static State& I() { static State s; return s; }
    };
}
