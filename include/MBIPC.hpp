// MBIPC.hpp
#pragma once
#include <string>
#include <thread>
#include <atomic>

namespace MB
{
    struct IPCServer
    {
        void Start();
        void Stop();

    private:
        void Loop();
        std::thread _thr;
        std::atomic<bool> _running{ false };
    };

    IPCServer& GetIPC();
}
