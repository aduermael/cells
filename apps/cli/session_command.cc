// Session client + daemon: long-running collab peer for non-interactive agents.

#include "session_command.h"

#include <csignal>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "core/cells/id.h"
#include "core/cells/luau_sandbox.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"
#include "core/net/include/SyncClient.h"

#include "output_spill.h"
#include "session_protocol.h"
#include "session_store.h"

namespace cells::cli {
namespace {

std::atomic<bool> g_daemon_shutdown{false};

void daemon_signal_handler(int /*sig*/) {
    g_daemon_shutdown = true;
}

// ---------------------------------------------------------------------------
// Unix socket helpers
// ---------------------------------------------------------------------------

int create_listen_socket(const std::string& path) {
    ::unlink(path.c_str());
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return -1;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 16) != 0) {
        ::close(fd);
        ::unlink(path.c_str());
        return -1;
    }
    // non-blocking accept
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

int connect_socket(const std::string& path, int timeout_ms = 5000) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return -1;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    // non-blocking connect with poll
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        ::close(fd);
        return -1;
    }
    if (rc != 0) {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        rc = ::poll(&pfd, 1, timeout_ms);
        if (rc <= 0) {
            ::close(fd);
            return -1;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            ::close(fd);
            return -1;
        }
    }
    // back to blocking for request/response simplicity
    ::fcntl(fd, F_SETFL, flags);
    return fd;
}

bool write_all(int fd, const std::string& data) {
    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    return true;
}

// Read one line (up to max_bytes). Returns empty on EOF/error.
std::string read_line(int fd, std::size_t max_bytes = 8 * 1024 * 1024) {
    std::string out;
    char buf[4096];
    while (out.size() < max_bytes) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return {};
        }
        if (n == 0) {
            break;
        }
        out.append(buf, static_cast<std::size_t>(n));
        auto nl = out.find('\n');
        if (nl != std::string::npos) {
            out.resize(nl);  // drop newline and anything after for one-shot
            return out;
        }
    }
    return out;
}

std::string read_file_contents(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Send one request and read one JSON response line.
SessionResponse rpc(const std::string& socket_path, const std::string& request_line) {
    SessionResponse fail;
    fail.ok = false;
    int fd = connect_socket(socket_path);
    if (fd < 0) {
        fail.error = "Cannot connect to session socket (is the session running?)";
        return fail;
    }
    std::string msg = request_line;
    if (msg.empty() || msg.back() != '\n') {
        msg.push_back('\n');
    }
    if (!write_all(fd, msg)) {
        ::close(fd);
        fail.error = "Failed to write to session socket";
        return fail;
    }
    std::string line = read_line(fd);
    ::close(fd);
    if (line.empty()) {
        fail.error = "Empty response from session";
        return fail;
    }
    // Minimal decode into SessionResponse for client use
    SessionResponse r;
    auto ok = json_get_bool(line, "ok");
    r.ok = ok.value_or(false);
    r.error = json_get_string(line, "error");
    r.output = json_get_string(line, "output");
    r.id = json_get_string(line, "id");
    r.url = json_get_string(line, "url");
    r.room = json_get_string(line, "room");
    r.state = json_get_string(line, "state");
    r.name = json_get_string(line, "name");
    if (auto p = json_get_number(line, "peers")) {
        r.peers = static_cast<int>(*p);
    }
    if (auto idle = json_get_number(line, "idle_minutes")) {
        r.idle_minutes = *idle;
    }
    r.pong = json_get_bool(line, "pong").value_or(false);
    r.stopped = json_get_bool(line, "stopped").value_or(false);
    return r;
}

// ---------------------------------------------------------------------------
// Daemon: SyncClient + IPC
// ---------------------------------------------------------------------------

class SessionDaemon : public net::SyncClientDelegate {
public:
    SessionDaemon(SessionMeta meta, std::string root, std::string socket_path, RoomTarget target)
        : meta_(std::move(meta)),
          root_(std::move(root)),
          socket_path_(std::move(socket_path)),
          target_(std::move(target)) {
        touch();
    }

    int run() {
        workbook_ = std::make_unique<Workbook>();
        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        workbook_->addSheet(std::move(sheet));

        net::SyncClientConfig config;
        config.signaling_url = target_.signaling_ws;
        sync_ = std::make_unique<net::SyncClient>(workbook_.get(), config);
        sync_->setDelegate(this);
        sync_->startSync(target_.room_id);
        sync_->setLocalName(meta_.name.empty() ? "CLI Agent" : meta_.name);

        listen_fd_ = create_listen_socket(socket_path_);
        if (listen_fd_ < 0) {
            std::cerr << "Error: cannot create session socket: " << socket_path_ << "\n";
            return 1;
        }

        // Update pid in meta (daemon pid)
        meta_.pid = static_cast<std::int64_t>(::getpid());
        write_session_meta(root_, meta_);
        write_session_pid(root_, meta_.id, meta_.pid);

        std::signal(SIGINT, daemon_signal_handler);
        std::signal(SIGTERM, daemon_signal_handler);
        // Ignore SIGPIPE from closed clients
        std::signal(SIGPIPE, SIG_IGN);

        while (!g_daemon_shutdown && !stop_requested_) {
            pump_network();
            accept_clients();
            service_clients();
            if (idle_expired(last_activity_ms_, now_mono_ms(), meta_.idle_minutes)) {
                push_event({"state", "idle timeout; stopping session", {}, {}, {}, {}});
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Notify watchers
        SessionResponse end;
        end.ok = true;
        end.watch_end = true;
        std::string end_line = encode_session_response(end) + "\n";
        for (auto& c : clients_) {
            if (c.watching) {
                write_all(c.fd, end_line);
            }
            ::close(c.fd);
        }
        clients_.clear();

        if (sync_) {
            sync_->stopSync();
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        ::unlink(socket_path_.c_str());
        remove_session_dir(root_, meta_.id);
        return 0;
    }

    // SyncClientDelegate
    void syncClientStateDidChange(net::SyncClient& /*client*/,
                                  net::SyncClientState state) override {
        SessionEvent e;
        e.type = "state";
        e.message = net::syncClientStateToString(state);
        push_event(std::move(e));
    }

    void syncClientPeerDidChange(net::SyncClient& /*client*/, const net::PeerInfo& peer) override {
        SessionEvent e;
        e.type = "peer";
        e.peer_id = peer.id;
        e.message = peer.is_connected ? "connected" : "updated";
        if (peer.is_synced) {
            e.message += " (synced)";
        }
        push_event(std::move(e));
    }

    void syncClientPeerDidDisconnect(net::SyncClient& /*client*/,
                                     const std::string& peer_id) override {
        SessionEvent e;
        e.type = "peer_left";
        e.peer_id = peer_id;
        e.message = "left";
        push_event(std::move(e));
    }

    void syncClientDataDidChange(net::SyncClient& /*client*/) override {
        SessionEvent e;
        e.type = "data";
        e.message = "workbook changed";
        push_event(std::move(e));
    }

    void syncClientDidError(net::SyncClient& /*client*/, const std::string& error) override {
        SessionEvent e;
        e.type = "error";
        e.message = error;
        push_event(std::move(e));
    }

    void syncClientDidReceiveOperations(net::SyncClient& /*client*/, const std::string& peer_id,
                                        const std::vector<Operation>& operations) override {
        for (const auto& op : operations) {
            SessionEvent e;
            e.type = "op";
            e.peer_id = peer_id;
            e.op_type = opTypeToString(op.type);
            e.target = op.target_id.toString();
            if (op.payload.size() <= 80) {
                e.payload = op.payload;
            } else {
                e.payload = op.payload.substr(0, 77) + "...";
            }
            e.message = e.op_type;
            push_event(std::move(e));
        }
    }

private:
    struct Client {
        int fd = -1;
        std::string buf;
        bool watching = false;
        bool close_after_write = false;
    };

    static std::int64_t now_mono_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    void touch() { last_activity_ms_ = now_mono_ms(); }

    void pump_network() {
        if (!sync_) {
            return;
        }
        sync_->processOutgoing();
        sync_->processPresenceUpdates();
    }

    void push_event(SessionEvent e) {
        std::string line = encode_session_response([&] {
                               SessionResponse r;
                               r.ok = true;
                               r.event = std::move(e);
                               return r;
                           }()) +
                           "\n";
        for (auto& c : clients_) {
            if (c.watching) {
                if (!write_all(c.fd, line)) {
                    c.close_after_write = true;
                }
            }
        }
        // Cap recent events for late joiners (optional ring — skip for KISS)
        (void)line;
    }

    void accept_clients() {
        while (true) {
            int cfd = ::accept(listen_fd_, nullptr, nullptr);
            if (cfd < 0) {
                break;
            }
            Client c;
            c.fd = cfd;
            clients_.push_back(std::move(c));
        }
    }

    void service_clients() {
        pollfd pfds[64];
        // Only poll a batch; simple single-threaded
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < clients_.size() && indices.size() < 64; ++i) {
            if (clients_[i].fd >= 0 && !clients_[i].watching) {
                indices.push_back(i);
            }
        }
        // For watchers we only write; still need to detect disconnect via POLLIN/POLLHUP
        for (std::size_t i = 0; i < clients_.size() && indices.size() < 64; ++i) {
            if (clients_[i].fd >= 0 && clients_[i].watching) {
                indices.push_back(i);
            }
        }

        if (indices.empty()) {
            return;
        }

        nfds_t n = 0;
        for (std::size_t idx : indices) {
            pfds[n].fd = clients_[idx].fd;
            pfds[n].events = POLLIN | POLLHUP | POLLERR;
            pfds[n].revents = 0;
            ++n;
        }
        int pr = ::poll(pfds, n, 0);
        if (pr <= 0) {
            // still handle close_after_write
            cleanup_clients();
            return;
        }

        for (nfds_t j = 0; j < n; ++j) {
            Client& c = clients_[indices[j]];
            if (pfds[j].revents & (POLLHUP | POLLERR)) {
                c.close_after_write = true;
                continue;
            }
            if (!(pfds[j].revents & POLLIN)) {
                continue;
            }
            char buf[4096];
            ssize_t nr = ::read(c.fd, buf, sizeof(buf));
            if (nr <= 0) {
                c.close_after_write = true;
                continue;
            }
            if (c.watching) {
                // any data from watcher ends watch
                c.close_after_write = true;
                continue;
            }
            c.buf.append(buf, static_cast<std::size_t>(nr));
            auto nl = c.buf.find('\n');
            if (nl == std::string::npos) {
                if (c.buf.size() > 8 * 1024 * 1024) {
                    c.close_after_write = true;
                }
                continue;
            }
            std::string line = c.buf.substr(0, nl);
            c.buf.erase(0, nl + 1);
            handle_request(c, line);
        }
        cleanup_clients();
    }

    void handle_request(Client& c, const std::string& line) {
        auto req_opt = parse_session_request(line);
        SessionResponse resp;
        if (!req_opt) {
            resp.ok = false;
            resp.error = "Invalid request JSON";
            write_all(c.fd, encode_session_response(resp) + "\n");
            c.close_after_write = true;
            return;
        }
        const SessionRequest& req = *req_opt;

        // Activity: all ops except pure watch stream refresh idle
        if (req.op != SessionOp::kUnknown) {
            touch();
        }

        switch (req.op) {
            case SessionOp::kPing:
                resp.ok = true;
                resp.pong = true;
                write_all(c.fd, encode_session_response(resp) + "\n");
                c.close_after_write = true;
                break;
            case SessionOp::kTouch:
                resp.ok = true;
                write_all(c.fd, encode_session_response(resp) + "\n");
                c.close_after_write = true;
                break;
            case SessionOp::kStatus:
                resp = make_status();
                write_all(c.fd, encode_session_response(resp) + "\n");
                c.close_after_write = true;
                break;
            case SessionOp::kStop:
                resp.ok = true;
                resp.stopped = true;
                write_all(c.fd, encode_session_response(resp) + "\n");
                c.close_after_write = true;
                stop_requested_ = true;
                break;
            case SessionOp::kExec:
                resp = run_exec(req);
                write_all(c.fd, encode_session_response(resp) + "\n");
                c.close_after_write = true;
                break;
            case SessionOp::kWatch:
                c.watching = true;
                // send initial status event so client sees a line immediately
                {
                    SessionEvent e;
                    e.type = "watch_start";
                    e.message = "watching " + meta_.id;
                    SessionResponse r;
                    r.ok = true;
                    r.event = e;
                    write_all(c.fd, encode_session_response(r) + "\n");
                }
                break;
            case SessionOp::kUnknown:
            default:
                resp.ok = false;
                resp.error = "Unknown op";
                write_all(c.fd, encode_session_response(resp) + "\n");
                c.close_after_write = true;
                break;
        }
    }

    SessionResponse make_status() const {
        SessionResponse r;
        r.ok = true;
        r.id = meta_.id;
        r.url = meta_.url;
        r.room = meta_.room;
        r.name = meta_.name;
        r.idle_minutes = meta_.idle_minutes;
        r.last_activity_ms = last_activity_ms_;
        if (sync_) {
            r.state = net::syncClientStateToString(sync_->getState());
            r.peers = static_cast<int>(sync_->getPeerCount());
        } else {
            r.state = "OFFLINE";
        }
        return r;
    }

    SessionResponse run_exec(const SessionRequest& req) {
        SessionResponse r;
        std::string script = req.code;
        if (script.empty() && !req.script_path.empty()) {
            script = read_file_contents(req.script_path);
            if (script.empty()) {
                r.ok = false;
                r.error = "Could not read script: " + req.script_path;
                return r;
            }
        }
        if (script.empty()) {
            r.ok = false;
            r.error = "No script code provided";
            return r;
        }

        LuauSandbox sandbox;
        Sheet* sheet = workbook_->sheets.empty() ? nullptr : workbook_->sheets[0].get();
        sandbox.setContext(workbook_.get(), sheet);
        ScriptResult result = sandbox.execute(script);
        r.output = result.output;
        if (!result.success) {
            r.ok = false;
            r.error = "Script error: " + result.error;
            return r;
        }
        // Propagate local CRDT ops to peers
        if (sync_) {
            sync_->broadcastOperations();
            sync_->processOutgoing();
        }
        r.ok = true;
        return r;
    }

    void cleanup_clients() {
        for (auto it = clients_.begin(); it != clients_.end();) {
            if (it->close_after_write || it->fd < 0) {
                if (it->fd >= 0) {
                    ::close(it->fd);
                }
                it = clients_.erase(it);
            } else {
                ++it;
            }
        }
    }

    SessionMeta meta_;
    std::string root_;
    std::string socket_path_;
    RoomTarget target_;
    std::unique_ptr<Workbook> workbook_;
    std::unique_ptr<net::SyncClient> sync_;
    int listen_fd_ = -1;
    std::vector<Client> clients_;
    std::int64_t last_activity_ms_ = 0;
    bool stop_requested_ = false;
};

// ---------------------------------------------------------------------------
// Client commands
// ---------------------------------------------------------------------------

std::string resolve_root(const SessionCliOptions& opts) {
    if (!opts.root_dir.empty()) {
        return opts.root_dir;
    }
    return session_root_dir();
}

// ---------------------------------------------------------------------------
// Pure-JSON stdout contract for all session client commands:
// - one JSON value (object/array) per response, or JSONL for watch
// - large payloads spill to /tmp with a small pointer object on stdout
// - errors are JSON {"ok":false,"error":"..."} on stdout (exit 1); no prose
// ---------------------------------------------------------------------------

void emit_json_stdout(std::string_view json) {
    SpillResult spill = maybe_spill_output(json);
    std::cout << spill.stdout_text;
    if (!spill.stdout_text.empty() && spill.stdout_text.back() != '\n') {
        std::cout << "\n";
    }
}

int emit_json_error(std::string_view error, std::string_view id = {}) {
    std::ostringstream o;
    o << "{\"ok\":false,\"error\":\"" << json_escape(error) << "\"";
    if (!id.empty()) {
        o << ",\"id\":\"" << json_escape(id) << "\"";
    }
    o << "}";
    emit_json_stdout(o.str());
    return 1;
}

int cmd_start(const SessionCliOptions& opts) {
    RoomTarget target = parse_room_target(opts.url);
    if (!target.ok) {
        return emit_json_error(target.error);
    }

    std::string root = resolve_root(opts);
    std::string id = generate_session_id();
    if (!create_session_dir(root, id)) {
        return emit_json_error("cannot create session directory under " + root);
    }

    SessionMeta meta;
    meta.id = id;
    meta.url = target.url;
    meta.room = target.room_id;
    meta.name = opts.name.empty() ? "CLI Agent" : opts.name;
    meta.idle_minutes = opts.idle_minutes;
    meta.started_at_ms = now_unix_ms();
    meta.pid = 0;
    if (!write_session_meta(root, meta)) {
        remove_session_dir(root, id);
        return emit_json_error("cannot write session meta", id);
    }

    std::string sock = session_socket_path(root, id);

    // Fork daemon process
    pid_t pid = ::fork();
    if (pid < 0) {
        remove_session_dir(root, id);
        return emit_json_error("fork failed");
    }
    if (pid == 0) {
        // Child: become session leader and run daemon
        ::setsid();
        // Redirect stdio to /dev/null (keep stderr for rare fatal logs? — silence for daemon)
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            ::dup2(devnull, STDOUT_FILENO);
            // leave stderr for debugging if CELLS_SESSION_DEBUG
            if (std::getenv("CELLS_SESSION_DEBUG") == nullptr) {
                ::dup2(devnull, STDERR_FILENO);
            }
            if (devnull > 2) {
                ::close(devnull);
            }
        }
        g_daemon_shutdown = false;
        SessionDaemon daemon(meta, root, sock, target);
        int code = daemon.run();
        _exit(code);
    }

    // Parent: wait until socket accepts connections
    meta.pid = static_cast<std::int64_t>(pid);
    write_session_meta(root, meta);
    write_session_pid(root, id, meta.pid);

    const int max_wait_ms = 10000;
    int waited = 0;
    bool ready = false;
    while (waited < max_wait_ms) {
        if (!process_alive(meta.pid)) {
            remove_session_dir(root, id);
            return emit_json_error("session daemon exited during startup", id);
        }
        int fd = connect_socket(sock, 200);
        if (fd >= 0) {
            // ping
            write_all(fd, "{\"op\":\"ping\"}\n");
            std::string line = read_line(fd);
            ::close(fd);
            if (!line.empty() && json_get_bool(line, "ok").value_or(false)) {
                ready = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }

    if (!ready) {
        ::kill(pid, SIGTERM);
        remove_session_dir(root, id);
        return emit_json_error("session daemon did not become ready", id);
    }

    std::ostringstream o;
    o << "{"
      << "\"ok\":true,"
      << "\"id\":\"" << json_escape(id) << "\","
      << "\"url\":\"" << json_escape(target.url) << "\","
      << "\"room\":\"" << json_escape(target.room_id) << "\","
      << "\"name\":\"" << json_escape(meta.name) << "\","
      << "\"idle_minutes\":" << meta.idle_minutes << ","
      << "\"pid\":" << meta.pid << "}";
    emit_json_stdout(o.str());
    return 0;
}

int cmd_list(const SessionCliOptions& opts) {
    std::string root = resolve_root(opts);
    auto sessions = list_sessions(root, true);
    std::ostringstream o;
    o << "[";
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        const auto& e = sessions[i];
        if (i) {
            o << ",";
        }
        o << "{"
          << "\"id\":\"" << json_escape(e.meta.id) << "\","
          << "\"url\":\"" << json_escape(e.meta.url) << "\","
          << "\"room\":\"" << json_escape(e.meta.room) << "\","
          << "\"name\":\"" << json_escape(e.meta.name) << "\","
          << "\"idle_minutes\":" << e.meta.idle_minutes << ","
          << "\"pid\":" << e.meta.pid << ","
          << "\"alive\":" << (e.alive ? "true" : "false") << "}";
    }
    o << "]";
    emit_json_stdout(o.str());
    return 0;
}

int cmd_stop(const SessionCliOptions& opts) {
    std::string root = resolve_root(opts);
    auto meta = read_session_meta(root, opts.session_id);
    if (!meta) {
        return emit_json_error("session not found", opts.session_id);
    }
    std::string sock = session_socket_path(root, opts.session_id);
    SessionResponse r = rpc(sock, "{\"op\":\"stop\"}");
    if (!r.ok && process_alive(meta->pid)) {
        // Force kill if RPC failed
        ::kill(static_cast<pid_t>(meta->pid), SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (process_alive(meta->pid)) {
            ::kill(static_cast<pid_t>(meta->pid), SIGKILL);
        }
        remove_session_dir(root, opts.session_id);
        emit_json_stdout("{\"ok\":true,\"id\":\"" + json_escape(opts.session_id) +
                         "\",\"stopped\":true}");
        return 0;
    }
    if (!r.ok) {
        remove_session_dir(root, opts.session_id);
        emit_json_stdout("{\"ok\":true,\"id\":\"" + json_escape(opts.session_id) +
                         "\",\"stopped\":true,\"note\":\"cleaned stale\"}");
        return 0;
    }
    // Wait briefly for cleanup
    for (int i = 0; i < 50 && process_alive(meta->pid); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    emit_json_stdout("{\"ok\":true,\"id\":\"" + json_escape(opts.session_id) +
                     "\",\"stopped\":true}");
    return 0;
}

int cmd_status(const SessionCliOptions& opts) {
    std::string root = resolve_root(opts);
    auto meta = read_session_meta(root, opts.session_id);
    if (!meta) {
        return emit_json_error("session not found", opts.session_id);
    }
    std::string sock = session_socket_path(root, opts.session_id);
    SessionResponse r = rpc(sock, "{\"op\":\"status\"}");
    if (!r.ok) {
        return emit_json_error(r.error.empty() ? "status failed" : r.error, opts.session_id);
    }
    emit_json_stdout(encode_session_response(r));
    return 0;
}

int cmd_exec(const SessionCliOptions& opts) {
    std::string root = resolve_root(opts);
    auto meta = read_session_meta(root, opts.session_id);
    if (!meta) {
        return emit_json_error("session not found", opts.session_id);
    }
    std::string sock = session_socket_path(root, opts.session_id);

    std::ostringstream req;
    req << "{\"op\":\"exec\"";
    if (!opts.script_inline.empty()) {
        req << ",\"code\":\"" << json_escape(opts.script_inline) << "\"";
    }
    if (!opts.script_file.empty()) {
        // Prefer absolute path so daemon can open it
        std::string path = opts.script_file;
        if (!path.empty() && path[0] != '/') {
            char cwd[4096];
            if (::getcwd(cwd, sizeof(cwd))) {
                path = std::string(cwd) + "/" + path;
            }
        }
        req << ",\"script\":\"" << json_escape(path) << "\"";
    }
    req << "}";

    SessionResponse r = rpc(sock, req.str());
    // Always wrap as JSON (script print text lives in "output")
    std::ostringstream o;
    o << "{\"ok\":" << (r.ok ? "true" : "false") << ",\"id\":\"" << json_escape(opts.session_id)
      << "\"";
    if (!r.ok) {
        o << ",\"error\":\"" << json_escape(r.error.empty() ? "exec failed" : r.error) << "\"";
    }
    if (!r.output.empty()) {
        o << ",\"output\":\"" << json_escape(r.output) << "\"";
    }
    o << "}";
    emit_json_stdout(o.str());
    return r.ok ? 0 : 1;
}

int cmd_watch(const SessionCliOptions& opts) {
    std::string root = resolve_root(opts);
    auto meta = read_session_meta(root, opts.session_id);
    if (!meta) {
        return emit_json_error("session not found", opts.session_id);
    }
    std::string sock = session_socket_path(root, opts.session_id);
    int fd = connect_socket(sock);
    if (fd < 0) {
        return emit_json_error("cannot connect to session", opts.session_id);
    }
    if (!write_all(fd, "{\"op\":\"watch\"}\n")) {
        ::close(fd);
        return emit_json_error("failed to start watch", opts.session_id);
    }

    // JSONL stream: one JSON object per line until duration/watch_end/disconnect
    auto start = std::chrono::steady_clock::now();
    double max_sec = opts.watch_duration_sec;
    std::string buf;
    char tmp[4096];

    while (true) {
        if (max_sec > 0) {
            auto elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (elapsed >= max_sec) {
                break;
            }
        }
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int timeout = 200;
        if (max_sec > 0) {
            auto elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            double left = max_sec - elapsed;
            if (left <= 0) {
                break;
            }
            timeout = static_cast<int>(std::min(left * 1000.0, 200.0));
            if (timeout < 1) {
                timeout = 1;
            }
        }
        int pr = ::poll(&pfd, 1, timeout);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (pr == 0) {
            continue;
        }
        ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n <= 0) {
            break;
        }
        buf.append(tmp, static_cast<std::size_t>(n));
        std::size_t pos = 0;
        while (true) {
            auto nl = buf.find('\n', pos);
            if (nl == std::string::npos) {
                buf.erase(0, pos);
                break;
            }
            std::string line = buf.substr(pos, nl - pos);
            pos = nl + 1;
            if (line.empty()) {
                continue;
            }
            // JSONL: emit line as-is (already JSON). Large single events spill.
            emit_json_stdout(line);
            // End if watch_end
            if (json_get_bool(line, "watch_end").value_or(false)) {
                ::close(fd);
                return 0;
            }
        }
    }
    ::close(fd);
    return 0;
}

int cmd_daemon(const SessionCliOptions& opts) {
    // Direct daemon entry (for testing / re-exec). Not the normal path (fork from start).
    RoomTarget target = parse_room_target(opts.url);
    if (!target.ok) {
        return emit_json_error(target.error);
    }
    std::string root = resolve_root(opts);
    SessionMeta meta;
    meta.id = opts.session_id;
    meta.url = target.url;
    meta.room = target.room_id;
    meta.name = opts.name;
    meta.idle_minutes = opts.idle_minutes;
    meta.started_at_ms = now_unix_ms();
    meta.pid = ::getpid();
    create_session_dir(root, meta.id);
    write_session_meta(root, meta);
    std::string sock =
        opts.socket_path.empty() ? session_socket_path(root, meta.id) : opts.socket_path;
    SessionDaemon daemon(meta, root, sock, target);
    return daemon.run();
}

}  // namespace

void print_session_help(const char* program_name) {
    // Usage text is for humans via `cells --help`; session subcommands themselves
    // never print prose on stdout/stderr — only JSON / JSONL.
    (void)program_name;
}

int run_session_command(const SessionCliOptions& opts) {
    switch (opts.kind) {
        case SessionCommandKind::kStart:
            return cmd_start(opts);
        case SessionCommandKind::kList:
            return cmd_list(opts);
        case SessionCommandKind::kStop:
            return cmd_stop(opts);
        case SessionCommandKind::kExec:
            return cmd_exec(opts);
        case SessionCommandKind::kWatch:
            return cmd_watch(opts);
        case SessionCommandKind::kStatus:
            return cmd_status(opts);
        case SessionCommandKind::kDaemon:
            return cmd_daemon(opts);
        case SessionCommandKind::kNone:
        default: {
            std::cout << "{\"ok\":false,\"error\":\"missing session subcommand "
                         "(start|list|stop|exec|watch|status)\"}\n";
            return 1;
        }
    }
}

}  // namespace cells::cli
