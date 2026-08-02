// Thread-agnostic worker flags for reconnectable WinHTTP WebSocket workers.
// Pure C++ (no WinHTTP) so lifecycle rules can be unit-tested on any host.

#ifndef CELLS_NET_WINDOWS_WS_WORKER_STATE_H_
#define CELLS_NET_WINDOWS_WS_WORKER_STATE_H_

#include <cstdint>

#include <atomic>

namespace cells::net {

// Tracks whether a WS worker thread is active and whether close was requested.
// Contract:
// - tryBeginConnect() succeeds only when no worker is active; clears closing.
// - requestClose() signals the worker to exit.
// - onWorkerFinished() must run when the worker thread exits (or after join).
// - After onWorkerFinished(), tryBeginConnect() may start a new attempt.
class WsWorkerState {
public:
    // Start a new connect attempt. Returns false if a worker is still active.
    bool tryBeginConnect() {
        if (started_.load()) {
            return false;
        }
        closing_.store(false);
        started_.store(true);
        return true;
    }

    void requestClose() { closing_.store(true); }

    [[nodiscard]] bool isClosing() const { return closing_.load(); }
    [[nodiscard]] bool isStarted() const { return started_.load(); }

    // Call when the worker thread has fully exited (after join, or at end of thread).
    void onWorkerFinished() { started_.store(false); }

    // Notification for an abandoned attempt that never reached onOpen.
    enum class AbandonNotify : std::uint8_t { Close, Error };

    [[nodiscard]] static AbandonNotify abandonNotify(bool closing) {
        return closing ? AbandonNotify::Close : AbandonNotify::Error;
    }

private:
    std::atomic<bool> started_{false};
    std::atomic<bool> closing_{false};
};

}  // namespace cells::net

#endif  // CELLS_NET_WINDOWS_WS_WORKER_STATE_H_
