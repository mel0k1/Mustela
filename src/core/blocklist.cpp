#include "blocklist.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace mustela {

namespace {

const char* const kDefaultDomains[] = {
    "33across.com",
    "adcolony.com",
    "adform.net",
    "adnxs.com",
    "ads-twitter.com",
    "adsafeprotected.com",
    "adservice.google.com",
    "adsrvr.org",
    "amazon-adsystem.com",
    "amplitude.com",
    "analytics.google.com",
    "analytics.tiktok.com",
    "ads.tiktok.com",
    "bidswitch.net",
    "casalemedia.com",
    "chartbeat.com",
    "criteo.com",
    "criteo.net",
    "doubleclick.net",
    "doubleverify.com",
    "fullstory.com",
    "google-analytics.com",
    "googleadservices.com",
    "googlesyndication.com",
    "googletagmanager.com",
    "googletagservices.com",
    "heap.io",
    "heapanalytics.com",
    "hotjar.com",
    "inmobi.com",
    "inspectlet.com",
    "kissmetrics.io",
    "luckyorange.com",
    "media.net",
    "mgid.com",
    "mixpanel.com",
    "moatads.com",
    "mouseflow.com",
    "nr-data.net",
    "openx.net",
    "optimizely.com",
    "outbrain.com",
    "pubmatic.com",
    "quantcount.com",
    "quantserve.com",
    "revcontent.com",
    "rubiconproject.com",
    "scorecardresearch.com",
    "segment.com",
    "segment.io",
    "sharethrough.com",
    "smartadserver.com",
    "smartlook.com",
    "statcounter.com",
    "static.ads-twitter.com",
    "taboola.com",
    "teads.tv",
    "tr.snapchat.com",
    "yieldmo.com",
    "fpjs.io"
};

bool valid_domain(const std::string& domain) {
    if (domain.empty() || domain.front() == '.' || domain.back() == '.') return false;
    if (domain.front() == '-' || domain.back() == '-') return false;
    if (domain.find("..") != std::string::npos) return false;
    for (char c : domain) {
        unsigned char ch = static_cast<unsigned char>(c);
        bool ok = ch == '.' || ch == '-' || (ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'z');
        if (!ok) return false;
    }
    return true;
}

}

std::string normalize_host(const std::string& host) {
    size_t begin = host.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = host.find_last_not_of(" \t\r\n");
    std::string text;
    text.reserve(end - begin + 1);
    for (size_t i = begin; i <= end; ++i) {
        text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(host[i]))));
    }
    while (!text.empty() && text.front() == '.') text.erase(text.begin());
    while (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

bool host_matches_domain(const std::string& host, const std::string& domain) {
    std::string h = normalize_host(host);
    std::string d = normalize_host(domain);
    if (h.empty() || d.empty()) return false;
    if (h == d) return true;
    if (h.size() > d.size() && h.compare(h.size() - d.size() - 1, d.size() + 1, "." + d) == 0) {
        return true;
    }
    return false;
}

void Blocklist::add(const std::string& domain) {
    std::string clean = normalize_host(domain);
    if (!valid_domain(clean)) return;
    auto it = std::lower_bound(domains_.begin(), domains_.end(), clean);
    if (it != domains_.end() && *it == clean) return;
    domains_.insert(it, clean);
}

void Blocklist::clear() {
    domains_.clear();
}

bool Blocklist::hit(const std::string& host) const {
    std::string clean = normalize_host(host);
    for (const std::string& domain : domains_) {
        if (host_matches_domain(clean, domain)) return true;
    }
    return false;
}

Blocklist::Blocklist() {
    for (const char* domain : kDefaultDomains) {
        add(domain);
    }
}

Blocklist default_blocklist(const std::string& extra_file) {
    Blocklist list;
    if (!extra_file.empty()) {
        std::ifstream in(extra_file);
        std::string line;
        while (std::getline(in, line)) {
            size_t begin = line.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r\n");
            std::string value = line.substr(begin, end - begin + 1);
            if (value.empty() || value.front() == '#') continue;
            list.add(value);
        }
    }
    return list;
}

std::string blocklist_rules_json(const std::vector<std::string>& domains) {
    std::string json;
    for (const std::string& raw : domains) {
        std::string domain = normalize_host(raw);
        if (!valid_domain(domain)) continue;
        if (!json.empty()) json += ",";
        json += "{\"trigger\":{\"url-filter\":\".*\",\"if-domain\":[\"";
        json += domain;
        json += "\",\"*.";
        json += domain;
        json += "\"]},\"action\":{\"type\":\"block\"}}";
    }
    if (json.empty()) return "";
    return "[" + json + "]";
}

}
