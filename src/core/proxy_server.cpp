#include "proxy_server.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace mustela {

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle invalid_socket_value = INVALID_SOCKET;

bool socket_system_init() {
    static std::mutex mutex;
    static int count = 0;
    std::lock_guard<std::mutex> lock(mutex);
    if (count == 0) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    }
    ++count;
    return true;
}

void socket_system_cleanup() {
    static std::mutex mutex;
    static int count = 0;
    std::lock_guard<std::mutex> lock(mutex);
    if (count > 0 && --count == 0) WSACleanup();
}

void close_socket(SocketHandle handle) {
    if (handle != invalid_socket_value) closesocket(handle);
}

int socket_error() {
    return WSAGetLastError();
}

int select_ready(SocketHandle high, fd_set* readfds, fd_set* writefds, timeval* timeout) {
    (void)high;
    return select(0, readfds, writefds, nullptr, timeout);
}
#else
using SocketHandle = int;
constexpr SocketHandle invalid_socket_value = -1;

bool socket_system_init() {
    return true;
}

void socket_system_cleanup() {}

void close_socket(SocketHandle handle) {
    if (handle != invalid_socket_value) close(handle);
}

int socket_error() {
    return errno;
}

int select_ready(SocketHandle high, fd_set* readfds, fd_set* writefds, timeval* timeout) {
    return select(high + 1, readfds, writefds, nullptr, timeout);
}
#endif

constexpr size_t kMaxHeaderBytes = 64 * 1024;
constexpr size_t kMaxBodyBytes = 1024 * 1024;
constexpr size_t kMaxAnswerBytes = 64 * 1024;
constexpr int kIdleTimeoutSeconds = 120;
constexpr int kHeaderTimeoutSeconds = 15;
constexpr int kConnectTimeoutSeconds = 10;

void set_timeout(SocketHandle handle, int seconds) {
    timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
}

bool send_all(SocketHandle handle, const char* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        int chunk = static_cast<int>(std::min<size_t>(size - sent, 64 * 1024));
        int written = static_cast<int>(send(handle, data + sent, chunk, 0));
        if (written <= 0) return false;
        sent += static_cast<size_t>(written);
    }
    return true;
}

bool send_string(SocketHandle handle, const std::string& text) {
    return send_all(handle, text.data(), text.size());
}

struct ReceivedData {
    std::vector<char> buffer;
    size_t consumed = 0;
};

bool recv_more(SocketHandle handle, ReceivedData& data) {
    char chunk[8192];
    int got = static_cast<int>(recv(handle, chunk, static_cast<int>(sizeof(chunk)), 0));
    if (got <= 0) return false;
    data.buffer.insert(data.buffer.end(), chunk, chunk + got);
    if (data.buffer.size() > kMaxHeaderBytes + kMaxBodyBytes) return false;
    return true;
}

bool find_headers_end(ReceivedData& data, size_t& header_end) {
    if (data.buffer.size() < 4) return false;
    static const char needle[] = "\r\n\r\n";
    for (size_t i = data.consumed; i + 4 <= data.buffer.size(); ++i) {
        if (std::memcmp(data.buffer.data() + i, needle, 4) == 0) {
            header_end = i;
            return true;
        }
    }
    data.consumed = data.buffer.size() >= 4 ? data.buffer.size() - 3 : 0;
    return false;
}

std::string lowercase(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

struct RequestHead {
    std::string method;
    std::string target;
    std::string version;
    std::vector<std::string> headers;
};

bool parse_request_head(const std::vector<char>& data, size_t header_end, RequestHead& out) {
    std::string text(data.data(), header_end);
    size_t line_end = text.find("\r\n");
    if (line_end == std::string::npos) return false;
    std::string first = text.substr(0, line_end);
    size_t first_space = first.find(' ');
    size_t second_space = first.rfind(' ');
    if (first_space == std::string::npos || second_space == first_space) return false;
    out.method = first.substr(0, first_space);
    out.target = first.substr(first_space + 1, second_space - first_space - 1);
    out.version = first.substr(second_space + 1);
    if (out.method.empty() || out.target.empty()) return false;
    size_t cursor = line_end + 2;
    while (cursor < text.size()) {
        size_t end = text.find("\r\n", cursor);
        if (end == std::string::npos) {
            std::string line = text.substr(cursor);
            if (!line.empty()) out.headers.push_back(line);
            break;
        }
        out.headers.push_back(text.substr(cursor, end - cursor));
        cursor = end + 2;
    }
    return true;
}

std::string header_value(const RequestHead& head, const std::string& name) {
    std::string prefix = lowercase(name) + ":";
    for (const std::string& line : head.headers) {
        std::string clean = lowercase(line);
        if (clean.rfind(prefix, 0) == 0) {
            size_t begin = name.size() + 1;
            while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) ++begin;
            return line.substr(begin);
        }
    }
    return "";
}

bool has_header(const RequestHead& head, const std::string& name) {
    std::string prefix = lowercase(name) + ":";
    for (const std::string& line : head.headers) {
        if (lowercase(line).rfind(prefix, 0) == 0) return true;
    }
    return false;
}

bool split_authority(const std::string& authority, std::string& host, int& port, int fallback_port) {
    std::string clean = authority;
    if (clean.rfind("[", 0) == 0) {
        size_t close = clean.find(']');
        if (close == std::string::npos) return false;
        host = clean.substr(1, close - 1);
        if (close + 1 < clean.size()) {
            if (clean[close + 1] != ':') return false;
            port = std::atoi(clean.c_str() + close + 2);
        } else {
            port = fallback_port;
        }
    } else {
        size_t split = clean.rfind(':');
        if (split == std::string::npos) {
            host = clean;
            port = fallback_port;
        } else {
            host = clean.substr(0, split);
            port = std::atoi(clean.c_str() + split + 1);
        }
    }
    if (host.empty() || port <= 0 || port > 65535) return false;
    return true;
}

bool parse_http_target(const std::string& target, std::string& host, int& port, std::string& path) {
    if (target.rfind("http://", 0) != 0) return false;
    size_t start = 7;
    size_t slash = target.find('/', start);
    std::string authority = slash == std::string::npos ? target.substr(start) : target.substr(start, slash - start);
    path = slash == std::string::npos ? "/" : target.substr(slash);
    size_t userinfo = authority.rfind('@');
    if (userinfo != std::string::npos) authority = authority.substr(userinfo + 1);
    return split_authority(authority, host, port, 80);
}

bool connect_with_timeout(SocketHandle handle, const sockaddr* address, socklen_t length) {
#ifdef _WIN32
    u_long nonblocking = 1;
    ioctlsocket(handle, FIONBIO, &nonblocking);
#else
    int flags = fcntl(handle, F_GETFL, 0);
    if (flags < 0) return false;
    if (fcntl(handle, F_SETFL, flags | O_NONBLOCK) < 0) return false;
#endif
    bool connected = true;
    if (connect(handle, address, length) != 0) {
        int error = socket_error();
#ifdef _WIN32
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) connected = false;
#else
        if (error != EINPROGRESS) connected = false;
#endif
        if (connected) {
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(handle, &writefds);
            timeval timeout;
            timeout.tv_sec = kConnectTimeoutSeconds;
            timeout.tv_usec = 0;
            if (select_ready(handle, nullptr, &writefds, &timeout) != 1) {
                connected = false;
            } else {
                int so_error = 0;
                socklen_t so_length = sizeof(so_error);
                if (getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &so_length) != 0 ||
                    so_error != 0) {
                    connected = false;
                }
            }
        }
    }
#ifdef _WIN32
    u_long blocking = 0;
    ioctlsocket(handle, FIONBIO, &blocking);
#else
    fcntl(handle, F_SETFL, flags);
#endif
    return connected;
}

SocketHandle connect_to_endpoint(const std::string& host, int port, std::string& error) {
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    int result = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list);
    if (result != 0 || !list) {
        error = "resolve failed";
        return invalid_socket_value;
    }
    SocketHandle handle = invalid_socket_value;
    for (addrinfo* item = list; item; item = item->ai_next) {
        handle = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (handle == invalid_socket_value) continue;
        if (connect_with_timeout(handle, item->ai_addr, static_cast<socklen_t>(item->ai_addrlen))) break;
        close_socket(handle);
        handle = invalid_socket_value;
    }
    freeaddrinfo(list);
    if (handle == invalid_socket_value) error = "connect failed";
    return handle;
}

std::string basic_auth_value(const ProxyEndpoint& endpoint) {
    if (endpoint.user.empty() && endpoint.password.empty()) return "";
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string input = endpoint.user + ":" + endpoint.password;
    std::string out;
    size_t i = 0;
    for (; i + 2 < input.size(); i += 3) {
        unsigned value = (static_cast<unsigned char>(input[i]) << 16) | (static_cast<unsigned char>(input[i + 1]) << 8) |
            static_cast<unsigned char>(input[i + 2]);
        out += alphabet[(value >> 18) & 0x3F];
        out += alphabet[(value >> 12) & 0x3F];
        out += alphabet[(value >> 6) & 0x3F];
        out += alphabet[value & 0x3F];
    }
    if (i + 1 == input.size()) {
        unsigned value = static_cast<unsigned char>(input[i]) << 16;
        out += alphabet[(value >> 18) & 0x3F];
        out += alphabet[(value >> 12) & 0x3F];
        out += "==";
    } else if (i + 2 == input.size()) {
        unsigned value = (static_cast<unsigned char>(input[i]) << 16) | (static_cast<unsigned char>(input[i + 1]) << 8);
        out += alphabet[(value >> 18) & 0x3F];
        out += alphabet[(value >> 12) & 0x3F];
        out += alphabet[(value >> 6) & 0x3F];
        out += "=";
    }
    return "Basic " + out;
}

void send_simple_response(SocketHandle handle, const std::string& status) {
    send_string(handle, status + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
}

bool send_via_upstream(SocketHandle upstream, const std::string& authority, const ProxyEndpoint& endpoint) {
    std::string request = "CONNECT " + authority + " HTTP/1.1\r\nHost: " + authority + "\r\n";
    std::string auth = basic_auth_value(endpoint);
    if (!auth.empty()) request += "Proxy-Authorization: " + auth + "\r\n";
    request += "\r\n";
    if (!send_string(upstream, request)) return false;
    ReceivedData data;
    size_t header_end = 0;
    while (!find_headers_end(data, header_end)) {
        if (!recv_more(upstream, data)) return false;
    }
    std::string text(data.buffer.data(), header_end);
    size_t status_end = text.find("\r\n");
    std::string line = status_end == std::string::npos ? text : text.substr(0, status_end);
    return line.find(" 2") != std::string::npos;
}

void relay_two_way(SocketHandle a, SocketHandle b) {
    char buffer[16384];
    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(a, &readfds);
        FD_SET(b, &readfds);
        timeval timeout;
        timeout.tv_sec = kIdleTimeoutSeconds;
        timeout.tv_usec = 0;
        int ready = select_ready((std::max)(a, b), &readfds, nullptr, &timeout);
        if (ready <= 0) break;
        bool alive = true;
        if (FD_ISSET(a, &readfds)) {
            int got = static_cast<int>(recv(a, buffer, static_cast<int>(sizeof(buffer)), 0));
            if (got <= 0 || !send_all(b, buffer, static_cast<size_t>(got))) alive = false;
        }
        if (alive && FD_ISSET(b, &readfds)) {
            int got = static_cast<int>(recv(b, buffer, static_cast<int>(sizeof(buffer)), 0));
            if (got <= 0 || !send_all(a, buffer, static_cast<size_t>(got))) alive = false;
        }
        if (!alive) break;
    }
    close_socket(a);
    close_socket(b);
}

struct ProxyState {
    ProxyConfig config;
    Blocklist blocklist;
    std::unique_ptr<DohResolver> resolver;
    std::atomic<bool> running{true};
};

std::string resolve_host(ProxyState& state, const std::string& host) {
    if (host_is_ip_literal(host)) return host;
    if (state.resolver) {
        std::string ip = state.resolver->resolve(host);
        return ip;
    }
    return "";
}

SocketHandle open_tunnel(ProxyState& state, const std::string& host, int port, std::string& error) {
    if (state.config.has_upstream) {
        SocketHandle upstream = connect_to_endpoint(state.config.upstream.host, state.config.upstream.port, error);
        if (upstream == invalid_socket_value) return invalid_socket_value;
        set_timeout(upstream, kHeaderTimeoutSeconds);
        if (!send_via_upstream(upstream, host + ":" + std::to_string(port), state.config.upstream)) {
            close_socket(upstream);
            error = "upstream connect failed";
            return invalid_socket_value;
        }
        return upstream;
    }
    std::string host_or_ip = resolve_host(state, host);
    if (host_or_ip.empty()) {
        error = "resolve denied";
        return invalid_socket_value;
    }
    SocketHandle target = connect_to_endpoint(host_or_ip, port, error);
    if (target != invalid_socket_value) set_timeout(target, kHeaderTimeoutSeconds);
    return target;
}

bool forward_request(ProxyState& state, SocketHandle client, const RequestHead& head,
    const std::vector<char>& raw, size_t header_end, const std::string& host, int port, const std::string& path) {
    std::string error;
    size_t body_available = raw.size() - (header_end + 4);
    size_t body_needed = 0;
    std::string content_length = header_value(head, "Content-Length");
    if (!content_length.empty()) {
        long parsed = std::atol(content_length.c_str());
        if (parsed < 0) return false;
        body_needed = static_cast<size_t>(parsed);
    }
    bool chunked = lowercase(header_value(head, "Transfer-Encoding")).find("chunked") != std::string::npos;
    if (chunked || body_needed > kMaxBodyBytes || body_available > body_needed) return false;

    if (state.config.has_upstream) {
        SocketHandle upstream = connect_to_endpoint(state.config.upstream.host, state.config.upstream.port, error);
        if (upstream == invalid_socket_value) return false;
        set_timeout(upstream, kHeaderTimeoutSeconds);
        std::string auth = basic_auth_value(state.config.upstream);
        std::string request = head.method + " " + head.target + " HTTP/1.1\r\n";
        for (const std::string& line : head.headers) {
            std::string clean = lowercase(line);
            if (clean.rfind("proxy-", 0) == 0 || clean.rfind("connection:", 0) == 0 || clean.rfind("keep-alive:", 0) == 0) continue;
            request += line + "\r\n";
        }
        if (!has_header(head, "Host")) request += "Host: " + host + (port == 80 ? "" : ":" + std::to_string(port)) + "\r\n";
        request += "Connection: close\r\n";
        if (!auth.empty()) request += "Proxy-Authorization: " + auth + "\r\n";
        request += "\r\n";
        bool ok = send_string(upstream, request);
        if (ok && body_needed > 0) {
            ok = send_all(upstream, raw.data() + header_end + 4, body_available);
            size_t sent = body_available;
            char buffer[8192];
            while (ok && sent < body_needed) {
                int want = static_cast<int>(std::min<size_t>(sizeof(buffer), body_needed - sent));
                int got = static_cast<int>(recv(client, buffer, want, 0));
                if (got <= 0) {
                    ok = false;
                    break;
                }
                ok = send_all(upstream, buffer, static_cast<size_t>(got));
                sent += static_cast<size_t>(got);
            }
        }
        if (!ok) {
            close_socket(upstream);
            return false;
        }
        relay_two_way(client, upstream);
        return true;
    }

    std::string host_or_ip = resolve_host(state, host);
    if (host_or_ip.empty()) return false;
    SocketHandle target = connect_to_endpoint(host_or_ip, port, error);
    if (target == invalid_socket_value) return false;
    set_timeout(target, kHeaderTimeoutSeconds);
    std::string request = head.method + " " + path + " HTTP/1.1\r\n";
    for (const std::string& line : head.headers) {
        std::string clean = lowercase(line);
        if (clean.rfind("proxy-", 0) == 0 || clean.rfind("connection:", 0) == 0 || clean.rfind("keep-alive:", 0) == 0) continue;
        request += line + "\r\n";
    }
    if (!has_header(head, "Host")) request += "Host: " + host + (port == 80 ? "" : ":" + std::to_string(port)) + "\r\n";
    request += "Connection: close\r\n\r\n";
    bool ok = send_string(target, request);
    if (ok && body_needed > 0) {
        ok = send_all(target, raw.data() + header_end + 4, body_available);
        size_t sent = body_available;
        char buffer[8192];
        while (ok && sent < body_needed) {
            int want = static_cast<int>(std::min<size_t>(sizeof(buffer), body_needed - sent));
            int got = static_cast<int>(recv(client, buffer, want, 0));
            if (got <= 0) {
                ok = false;
                break;
            }
            ok = send_all(target, buffer, static_cast<size_t>(got));
            sent += static_cast<size_t>(got);
        }
    }
    if (!ok) {
        close_socket(target);
        return false;
    }
    relay_two_way(client, target);
    return true;
}

void handle_connection(std::shared_ptr<ProxyState> state, SocketHandle client) {
    set_timeout(client, kHeaderTimeoutSeconds);
    ReceivedData data;
    size_t header_end = 0;
    while (!find_headers_end(data, header_end)) {
        if (!recv_more(client, data)) {
            close_socket(client);
            return;
        }
        if (data.buffer.size() > kMaxHeaderBytes) {
            send_simple_response(client, "HTTP/1.1 431 Request Header Fields Too Large");
            close_socket(client);
            return;
        }
    }
    RequestHead head;
    if (!parse_request_head(data.buffer, header_end, head)) {
        send_simple_response(client, "HTTP/1.1 400 Bad Request");
        close_socket(client);
        return;
    }

    if (head.method == "CONNECT") {
        std::string host;
        int port = 0;
        if (!split_authority(head.target, host, port, 443)) {
            send_simple_response(client, "HTTP/1.1 400 Bad Request");
            close_socket(client);
            return;
        }
        if (state->blocklist.hit(host)) {
            send_simple_response(client, "HTTP/1.1 403 Forbidden");
            close_socket(client);
            return;
        }
        std::string error;
        SocketHandle remote = open_tunnel(*state, host, port, error);
        if (remote == invalid_socket_value) {
            send_simple_response(client, "HTTP/1.1 502 Bad Gateway");
            close_socket(client);
            return;
        }
        if (!send_string(client, "HTTP/1.1 200 Connection established\r\n\r\n")) {
            close_socket(remote);
            close_socket(client);
            return;
        }
        relay_two_way(client, remote);
        return;
    }

    std::string host;
    int port = 0;
    std::string path;
    if (!parse_http_target(head.target, host, port, path)) {
        send_simple_response(client, "HTTP/1.1 400 Bad Request");
        close_socket(client);
        return;
    }
    if (state->blocklist.hit(host)) {
        send_simple_response(client, "HTTP/1.1 403 Forbidden");
        close_socket(client);
        return;
    }
    if (!forward_request(*state, client, head, data.buffer, header_end, host, port, path)) {
        send_simple_response(client, "HTTP/1.1 502 Bad Gateway");
        close_socket(client);
        return;
    }
}

}

bool host_is_ip_literal(const std::string& host) {
    if (host.empty()) return false;
    if (host.front() == '[') return true;
    if (host.find(':') != std::string::npos) return true;
    return host.find_first_not_of("0123456789.") == std::string::npos;
}

bool parse_proxy_url(const std::string& url, ProxyEndpoint& out) {
    out = ProxyEndpoint();
    if (url.rfind("http://", 0) != 0) return false;
    std::string rest = url.substr(7);
    size_t slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    size_t userinfo = authority.rfind('@');
    if (userinfo != std::string::npos) {
        std::string credentials = authority.substr(0, userinfo);
        authority = authority.substr(userinfo + 1);
        size_t split = credentials.find(':');
        if (split == std::string::npos) {
            out.user = credentials;
        } else {
            out.user = credentials.substr(0, split);
            out.password = credentials.substr(split + 1);
        }
    }
    int port = 0;
    if (!split_authority(authority, out.host, port, 80)) return false;
    out.port = port;
    return true;
}

std::string proxy_url_from(const ProxyEndpoint& endpoint) {
    if (endpoint.host.empty()) return "";
    std::string credentials;
    if (!endpoint.user.empty() || !endpoint.password.empty()) {
        credentials = endpoint.user + ":" + endpoint.password + "@";
    }
    std::string authority = endpoint.host;
    if (authority.find(':') != std::string::npos && authority.front() != '[') authority = "[" + authority + "]";
    return "http://" + credentials + authority + ":" + std::to_string(endpoint.port);
}

DohResolver::DohResolver(DohTransport transport, std::string url) : transport_(std::move(transport)), url_(std::move(url)) {}

std::string DohResolver::resolve(const std::string& host) {
    if (!transport_ || url_.empty() || host.empty() || host_is_ip_literal(host)) return "";
    std::int64_t now = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(host);
        if (it != cache_.end() && it->second.second > now) return it->second.first;
    }
    std::vector<unsigned char> query = doh::build_query(host, 1);
    std::vector<unsigned char> answer;
    if (!transport_(url_, query, answer)) return "";
    if (answer.size() > kMaxAnswerBytes) return "";
    doh::Response response;
    if (!doh::parse_response(answer.data(), answer.size(), response)) return "";
    if (response.rcode != doh::Rcode::NoError) return "";
    std::string ip;
    unsigned ttl = 60;
    for (const doh::Answer& item : response.answers) {
        if (item.type == 1 && item.data.find_first_not_of("0123456789.") == std::string::npos) {
            ip = item.data;
            ttl = (std::max)(30u, (std::min)(600u, item.ttl));
            break;
        }
    }
    if (ip.empty()) return "";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[host] = {ip, now + static_cast<std::int64_t>(ttl) * 1000};
    }
    return ip;
}

struct ProxyServer::Impl {
    std::shared_ptr<ProxyState> state;
    std::thread accept_thread;
    std::atomic<int> port{0};
#ifdef _WIN32
    SOCKET listen_fd = INVALID_SOCKET;
#else
    int listen_fd = -1;
#endif
};

ProxyServer::ProxyServer() = default;

ProxyServer::~ProxyServer() {
    stop();
}

bool ProxyServer::start(const ProxyConfig& config, const Blocklist& blocklist, DohTransport transport) {
    stop();
    if (!socket_system_init()) return false;
    auto impl = std::make_unique<Impl>();
    impl->state = std::make_shared<ProxyState>();
    impl->state->config = config;
    impl->state->blocklist = blocklist;
    if (config.resolve_via_doh && !config.doh_url.empty() && transport) {
        impl->state->resolver = std::make_unique<DohResolver>(transport, config.doh_url);
    }
#ifdef _WIN32
    SocketHandle fd = socket(AF_INET, SOCK_STREAM, 0);
#else
    SocketHandle fd = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (fd == invalid_socket_value) {
        socket_system_cleanup();
        return false;
    }
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, 16) != 0) {
        close_socket(fd);
        socket_system_cleanup();
        return false;
    }
    sockaddr_in bound = {};
    socklen_t length = sizeof(bound);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
        close_socket(fd);
        socket_system_cleanup();
        return false;
    }
    impl->port = static_cast<int>(ntohs(bound.sin_port));
    std::shared_ptr<ProxyState> state = impl->state;
    SocketHandle listener = fd;
    impl->listen_fd = fd;
    impl->accept_thread = std::thread([state, listener]() {
        while (state->running) {
            SocketHandle client = accept(listener, nullptr, nullptr);
            if (client == invalid_socket_value) {
                if (!state->running) break;
                continue;
            }
            std::thread(handle_connection, state, client).detach();
        }
    });
    impl_ = std::move(impl);
    return true;
}

void ProxyServer::stop() {
    if (!impl_) return;
    if (impl_->state) impl_->state->running = false;
#ifdef _WIN32
    if (impl_->listen_fd != INVALID_SOCKET) {
        closesocket(impl_->listen_fd);
        impl_->listen_fd = INVALID_SOCKET;
    }
#else
    if (impl_->listen_fd >= 0) {
        shutdown(impl_->listen_fd, SHUT_RDWR);
        close(impl_->listen_fd);
        impl_->listen_fd = -1;
    }
#endif
    if (impl_->accept_thread.joinable()) impl_->accept_thread.join();
    impl_->port = 0;
    impl_.reset();
    socket_system_cleanup();
}

int ProxyServer::port() const {
    return impl_ ? impl_->port.load() : 0;
}

}
