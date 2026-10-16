#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "blocklist.hpp"
#include "doh.hpp"

namespace mustela {

struct ProxyEndpoint {
    std::string host;
    int port = 0;
    std::string user;
    std::string password;
};

bool parse_proxy_url(const std::string& url, ProxyEndpoint& out);
std::string proxy_url_from(const ProxyEndpoint& endpoint);
bool host_is_ip_literal(const std::string& host);

struct ProxyConfig {
    ProxyEndpoint upstream;
    bool has_upstream = false;
    bool resolve_via_doh = false;
    std::string doh_url;
};

using DohTransport = std::function<bool(const std::string& url, const std::vector<unsigned char>& query,
    std::vector<unsigned char>& answer)>;

class DohResolver {
public:
    DohResolver(DohTransport transport, std::string url);

    std::string resolve(const std::string& host);

private:
    DohTransport transport_;
    std::string url_;
    std::mutex mutex_;
    std::map<std::string, std::pair<std::string, std::int64_t>> cache_;
};

class ProxyServer {
public:
    ProxyServer();
    ~ProxyServer();
    ProxyServer(const ProxyServer&) = delete;
    ProxyServer& operator=(const ProxyServer&) = delete;

    bool start(const ProxyConfig& config, const Blocklist& blocklist, DohTransport transport = {});
    void stop();
    int port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
