// src/MBIPC.cpp

#include "MBIPC.hpp"
#include "MBOps.hpp"         // for MB::Ops::I().Dispatch(op, args)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>            // ConvertStringSecurityDescriptorToSecurityDescriptorW

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>

#ifndef MB_IPC_BUFSZ
#define MB_IPC_BUFSZ  65536u
#endif

namespace
{
    constexpr wchar_t kPipeName[] = LR"(\\.\pipe\MirrorBladeBridge)";

    // Build a permissive-but-sane security descriptor (Users/SYSTEM/Admins RW, Low Integrity)
    // SDDL: D:(A;;GRGW;;;BU)(A;;GRGW;;;SY)(A;;GRGW;;;BA)S:(ML;;NW;;;LW)
    // If this fails, fall back to default security.
    SECURITY_ATTRIBUTES* MakePipeSA()
    {
        static SECURITY_ATTRIBUTES sa{};
        static PSECURITY_DESCRIPTOR psd = nullptr;

        if (psd)
        {
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = FALSE;
            sa.lpSecurityDescriptor = psd;
            return &sa;
        }

        LPCWSTR sddl = L"D:(A;;GRGW;;;BU)(A;;GRGW;;;SY)(A;;GRGW;;;BA)S:(ML;;NW;;;LW)";
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &psd, nullptr))
        {
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = FALSE;
            sa.lpSecurityDescriptor = psd;
            return &sa;
        }
        return nullptr; // default security
    }

    // Read one complete message from a MESSAGE-type pipe (handles ERROR_MORE_DATA).
    // Returns false on disconnect/error.
    bool PipeReadMessage(HANDLE h, std::string& out)
    {
        out.clear();
        DWORD read = 0;
        std::vector<char> buf(MB_IPC_BUFSZ);

        for (;;)
        {
            BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
            if (!ok)
            {
                DWORD err = GetLastError();
                if (err == ERROR_MORE_DATA)
                {
                    // Append the partial chunk we did receive, then continue.
                    out.append(buf.data(), buf.data() + read);
                    continue;
                }
                return false; // disconnected or real error
            }
            // ok == TRUE: we have a (possibly final) chunk
            out.append(buf.data(), buf.data() + read);
            break;
        }
        return true;
    }

    // Write entire string; loops until all bytes written or failure.
    bool PipeWriteAll(HANDLE h, const std::string& s)
    {
        const char* p = s.data();
        size_t remaining = s.size();
        while (remaining > 0)
        {
            DWORD wrote = 0;
            BOOL ok = WriteFile(h, p, static_cast<DWORD>(remaining), &wrote, nullptr);
            if (!ok) return false;
            p += wrote;
            remaining -= wrote;
        }
        return true;
    }

    // Handle a single request JSON → reply JSON via MB::Ops
    json HandleRequestJSON(const json& in)
    {
        // Strict contract: { "op": string, "args": object }
        const std::string op = in.value("op", "");
        const json args = in.value("args", json::object());

        // Dispatch to your op table; expected to return a json object/primitive.
        json reply;
        try
        {
            reply = MB::Ops::I().Dispatch(op, args);
        }
        catch (const std::exception& e)
        {
            reply = json{ {"ok", false}, {"error", e.what()}, {"op", op} };
        }
        catch (...)
        {
            reply = json{ {"ok", false}, {"error", "unknown error"}, {"op", op} };
        }

        // If the op implementation didn't include an "ok", make a default wrapper.
        if (!reply.is_object() || !reply.contains("ok"))
        {
            reply = json{ {"ok", true}, {"result", reply}, {"op", op} };
        }
        return reply;
    }
} // anonymous

namespace MB
{
    // Singleton instance (defined here; declaration in MBIPC.hpp)
    static IPCServer g_server;
    IPCServer& GetIPC() { return g_server; }

    void IPCServer::Start()
    {
        if (_running.exchange(true)) return;
        _thr = std::thread([this] { Loop(); });
    }

    void IPCServer::Stop()
    {
        if (!_running.exchange(false)) return;

        // Poke the server so ConnectNamedPipe/ReadFile unblocks
        HANDLE h = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

        if (_thr.joinable()) _thr.join();
    }

    void IPCServer::Loop()
    {
        const DWORD kOutSz = MB_IPC_BUFSZ;
        const DWORD kInSz = MB_IPC_BUFSZ;

        while (_running.load())
        {
            SECURITY_ATTRIBUTES* sa = MakePipeSA();

            HANDLE pipe = CreateNamedPipeW(
                kPipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1,                // one client at a time is fine for now
                kOutSz,
                kInSz,
                0,                // default timeout
                sa                // permissive security (or nullptr fallback)
            );

            if (pipe == INVALID_HANDLE_VALUE)
            {
                // Backoff briefly if creation fails
                Sleep(250);
                continue;
            }

            if (!ConnectNamedPipe(pipe, nullptr))
            {
                DWORD err = GetLastError();
                if (err != ERROR_PIPE_CONNECTED)
                {
                    CloseHandle(pipe);
                    Sleep(50);
                    continue;
                }
            }

            // Session loop: process messages until client disconnects
            for (;;)
            {
                std::string reqStr;
                if (!PipeReadMessage(pipe, reqStr))
                    break; // disconnect

                json reply;
                try
                {
                    json in = json::parse(reqStr, /*cb*/nullptr, /*allow_exceptions*/true);
                    reply = HandleRequestJSON(in);
                }
                catch (const std::exception& e)
                {
                    reply = json{ {"ok", false}, {"error", e.what()} };
                }
                catch (...)
                {
                    reply = json{ {"ok", false}, {"error", "bad json"} };
                }

                const std::string out = reply.dump();
                if (!PipeWriteAll(pipe, out))
                    break; // client write failure → end session
            }

            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }
}
