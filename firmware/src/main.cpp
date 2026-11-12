// Hardware-free TCP transport for the portable controller logic.
// The networking scaffold is supplied so the exercise focuses on control,
// C# parsing, disconnection handling and engineering judgment.
#include "controller.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
static constexpr Socket INVALID_SOCKET_VALUE = INVALID_SOCKET;
static void close_socket(Socket s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <csignal>
using Socket = int;
static constexpr Socket INVALID_SOCKET_VALUE = -1;
static void close_socket(Socket s) { close(s); }
#endif

using Clock = std::chrono::steady_clock;
static const auto boot = Clock::now();
static std::uint64_t monotonic_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - boot).count());
}

static bool send_all(Socket s, const std::string& data) {
    std::size_t pos = 0;
    while (pos < data.size()) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        int sent = send(s, data.data() + pos,
                        static_cast<int>(data.size() - pos), flags);
        if (sent <= 0) return false;
        pos += static_cast<std::size_t>(sent);
    }
    return true;
}

static bool send_line(Socket s, const std::string& line) {
    return send_all(s, line + "\n");
}

static std::string format_status(std::uint64_t sequence, const ControllerStatus& s) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "STATUS," << sequence << ',' << std::fixed << std::setprecision(1)
        << s.temperature_c << ',' << (s.fan_on ? 1 : 0) << ','
        << (s.interlock_active ? 1 : 0) << ',' << s.fault;
    return out.str();
}

static bool parse_temperature(const std::string& input, double& value) {
    if (input.empty()) return false;
    char* end = nullptr;
    value = std::strtod(input.c_str(), &end);
    return end != input.c_str() && end && *end == '\0' && std::isfinite(value)
           && value >= 0.0 && value <= 100.0;
}

static bool handle_line(Socket client, const std::string& command,
                        Controller& controller, bool& fragmentation,
                        bool& malformed_pending) {
    if (command == "PING") {
        controller.OnHeartbeat(monotonic_ms());
        return send_line(client, "PONG");
    }
    if (command.rfind("TEMP,", 0) == 0) {
        double value = 0.0;
        if (!parse_temperature(command.substr(5), value)) {
            return send_line(client, "ERR,TEMP_RANGE");
        }
        controller.SetTemperature(value);
        return send_line(client, "ACK,TEMP");
    }
    if (command == "INTERLOCK,1" || command == "INTERLOCK,0") {
        controller.SetInterlock(command.back() == '1');
        return send_line(client, "ACK,INTERLOCK");
    }
    if (command == "INJECT,FRAGMENT") {
        fragmentation = !fragmentation;
        return send_line(client, fragmentation ? "ACK,FRAGMENT,1" : "ACK,FRAGMENT,0");
    }
    if (command == "INJECT,MALFORMED") {
        malformed_pending = true;
        return send_line(client, "ACK,MALFORMED");
    }
    if (command == "INJECT,DISCONNECT") {
        send_line(client, "ACK,DISCONNECT");
        return false;
    }
    return send_line(client, "ERR,UNKNOWN_COMMAND");
}

static void serve_client(Socket client) {
    Controller controller;
    bool fragmentation = false;
    bool malformed_pending = false;
    std::uint64_t sequence = 0;
    std::string pending;
    auto next_status = Clock::now();
    std::cout << "Client connected; controller reset to safe state" << std::endl;

    while (true) {
        controller.Tick(monotonic_ms());
        if (Clock::now() >= next_status) {
            std::string line;
            if (malformed_pending) {
                line = "STATUS,not-a-number,broken";
                malformed_pending = false;
            } else {
                line = format_status(++sequence, controller.Snapshot());
            }
            std::string payload = line + "\n";
            bool ok;
            if (fragmentation && payload.size() > 5) {
                std::size_t cut = payload.size() / 2;
                ok = send_all(client, payload.substr(0, cut));
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                ok = ok && send_all(client, payload.substr(cut));
            } else {
                ok = send_all(client, payload);
            }
            if (!ok) break;
            next_status = Clock::now() + std::chrono::milliseconds(250);
        }

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(client, &read_set);
        timeval wait_time = {0, 20000}; // 20 ms, keeping the control loop responsive.
#ifdef _WIN32
        int ready = select(0, &read_set, nullptr, nullptr, &wait_time);
#else
        int ready = select(client + 1, &read_set, nullptr, nullptr, &wait_time);
#endif
        if (ready < 0) break;
        if (ready == 0) continue;

        char bytes[512];
        int count = recv(client, bytes, static_cast<int>(sizeof(bytes)), 0);
        if (count <= 0) break;
        pending.append(bytes, static_cast<std::size_t>(count));
        if (pending.size() > 4096) {
            send_line(client, "ERR,LINE_TOO_LONG");
            break;
        }
        while (true) {
            auto newline = pending.find('\n');
            if (newline == std::string::npos) break;
            std::string command = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!command.empty() && command.back() == '\r') command.pop_back();
            if (!handle_line(client, command, controller, fragmentation, malformed_pending)) {
                std::cout << "Client disconnected by fault injection" << std::endl;
                return;
            }
        }
    }
    std::cout << "Client disconnected" << std::endl;
}

int main(int argc, char** argv) {
    int port = 5042;
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Port must be numeric" << std::endl;
            return 2;
        }
    }
    if (port < 1024 || port > 65535) {
        std::cerr << "Port must be between 1024 and 65535" << std::endl;
        return 2;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif

    Socket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET_VALUE) {
        std::cerr << "Could not create socket" << std::endl;
        return 1;
    }
    int option = 1;
#ifdef _WIN32
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&option), sizeof(option));
#else
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || listen(listener, 1) != 0) {
        std::cerr << "Cannot bind localhost:" << port << std::endl;
        close_socket(listener);
        return 1;
    }
    std::cout << "Virtual controller listening on 127.0.0.1:" << port << std::endl;
    std::cout << "Stop with Ctrl+C" << std::endl;
    for (;;) {
        Socket client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET_VALUE) break;
        serve_client(client);
        close_socket(client);
    }
    close_socket(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
