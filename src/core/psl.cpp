#include "psl.hpp"

#include <cctype>
#include <set>
#include <vector>

namespace mustela {

namespace {

const char* const kExactRules[] = {
    "com", "org", "net", "edu", "gov", "mil", "int", "info", "biz", "name", "pro",
    "xyz", "online", "site", "top", "club", "app", "dev", "io", "ai", "me", "tv",
    "us", "uk", "de", "fr", "it", "es", "nl", "be", "pt", "pl", "cz", "sk", "at",
    "ch", "se", "no", "dk", "fi", "is", "ie", "eu", "ru", "su", "ua", "by", "kz",
    "gr", "hu", "ro", "bg", "hr", "rs", "si", "lt", "lv", "ee", "tr", "il", "ae",
    "sa", "in", "id", "th", "my", "sg", "ph", "vn", "kr", "jp", "cn", "hk", "tw",
    "mo", "au", "nz", "br", "mx", "ar", "cl", "pe", "ve", "uy", "py", "bo", "ec",
    "cr", "gt", "ni", "pa", "do", "cu", "za", "ng", "ke", "gh", "eg", "ma", "tn",
    "dz", "qa", "ck", "jm", "kh",
    "co.uk", "org.uk", "ac.uk", "gov.uk", "net.uk", "me.uk", "ltd.uk", "plc.uk",
    "nhs.uk", "police.uk", "sch.uk",
    "com.au", "net.au", "org.au", "edu.au", "gov.au", "asn.au", "id.au",
    "co.nz", "net.nz", "org.nz", "govt.nz", "ac.nz", "school.nz", "gen.nz",
    "geek.nz", "kiwi.nz", "maori.nz",
    "co.in", "net.in", "org.in", "gen.in", "firm.in", "ac.in", "edu.in", "res.in",
    "gov.in", "mil.in", "nic.in",
    "co.il", "org.il", "net.il", "ac.il", "gov.il", "muni.il", "idf.il",
    "com.br", "net.br", "org.br", "gov.br", "edu.br", "mil.br",
    "com.mx", "org.mx", "net.mx", "gob.mx", "edu.mx",
    "com.ar", "net.ar", "org.ar", "gob.ar", "edu.ar", "mil.ar",
    "co.cl", "gob.cl", "gov.cl", "mil.cl",
    "com.tr", "net.tr", "org.tr", "gov.tr", "edu.tr", "mil.tr", "web.tr",
    "com.cn", "net.cn", "org.cn", "gov.cn", "edu.cn", "ac.cn", "mil.cn",
    "com.hk", "net.hk", "org.hk", "gov.hk", "edu.hk", "idv.hk",
    "com.tw", "net.tw", "org.tw", "gov.tw", "edu.tw", "idv.tw",
    "co.kr", "or.kr", "ne.kr", "re.kr", "pe.kr", "go.kr", "mil.kr", "ac.kr",
    "co.jp", "or.jp", "ne.jp", "ac.jp", "ad.jp", "ed.jp", "go.jp", "gr.jp", "lg.jp",
    "city.kobe.jp",
    "com.sg", "net.sg", "org.sg", "gov.sg", "edu.sg", "per.sg",
    "com.my", "net.my", "org.my", "gov.my", "edu.my", "mil.my", "name.my",
    "co.id", "net.id", "or.id", "go.id", "ac.id", "sch.id", "web.id", "mil.id",
    "co.th", "net.th", "or.th", "go.th", "ac.th", "in.th",
    "com.ph", "net.ph", "org.ph", "gov.ph", "edu.ph", "mil.ph",
    "com.vn", "net.vn", "org.vn", "gov.vn", "edu.vn", "ac.vn",
    "co.za", "net.za", "org.za", "gov.za", "ac.za",
    "com.ng", "net.ng", "org.ng", "gov.ng", "edu.ng", "name.ng", "sch.ng",
    "co.ke", "or.ke", "ne.ke", "go.ke", "ac.ke",
    "com.eg", "net.eg", "org.eg", "gov.eg", "edu.eg", "eun.eg",
    "com.sa", "net.sa", "org.sa", "gov.sa", "edu.sa", "med.sa", "pub.sa", "sch.sa",
    "com.ua", "net.ua", "org.ua", "gov.ua", "edu.ua", "in.ua", "kiev.ua",
    "com.ru", "net.ru", "org.ru", "msk.ru", "spb.ru",
};

const char* const kWildcardRules[] = {
    "ck", "jm", "kh", "kobe.jp",
};

const char* const kExceptionRules[] = {
    "www.ck",
};

struct PslRules {
    std::set<std::string> exact;
    std::set<std::string> wildcard;
    std::set<std::string> exception;

    PslRules() {
        for (const char* rule : kExactRules) exact.insert(rule);
        for (const char* rule : kWildcardRules) wildcard.insert(rule);
        for (const char* rule : kExceptionRules) exception.insert(rule);
    }
};

const PslRules& psl_rules() {
    static const PslRules rules;
    return rules;
}

std::string normalize_suffix_host(const std::string& host) {
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

bool is_localhost_host(const std::string& host) {
    if (host == "localhost") return true;
    return host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0;
}

bool is_ip_literal_host(const std::string& host) {
    if (host.find(':') != std::string::npos) return true;
    if (host.empty()) return false;
    return host.find_first_not_of("0123456789.") == std::string::npos;
}

std::vector<std::string> split_labels(const std::string& host) {
    std::vector<std::string> labels;
    size_t begin = 0;
    while (true) {
        size_t dot = host.find('.', begin);
        if (dot == std::string::npos) {
            labels.push_back(host.substr(begin));
            break;
        }
        labels.push_back(host.substr(begin, dot - begin));
        begin = dot + 1;
    }
    return labels;
}

std::string join_labels(const std::vector<std::string>& labels, size_t from) {
    if (from >= labels.size()) return "";
    std::string out = labels[from];
    for (size_t i = from + 1; i < labels.size(); ++i) {
        out.push_back('.');
        out += labels[i];
    }
    return out;
}

bool has_empty_label(const std::vector<std::string>& labels) {
    for (const std::string& label : labels) {
        if (label.empty()) return true;
    }
    return false;
}

size_t suffix_label_count(const std::vector<std::string>& labels) {
    const PslRules& rules = psl_rules();
    size_t n = labels.size();
    for (size_t i = 0; i < n; ++i) {
        std::string candidate = join_labels(labels, i);
        if (rules.exception.count(candidate)) return n - i - 1;
    }
    size_t best = 0;
    bool best_exact = false;
    for (size_t i = 0; i < n; ++i) {
        std::string candidate = join_labels(labels, i);
        bool exact = rules.exact.count(candidate) != 0;
        bool wild = !candidate.empty() && rules.wildcard.count(join_labels(labels, i + 1)) != 0;
        if (!exact && !wild) continue;
        size_t rule_labels = n - i;
        if (best == 0 || rule_labels > best || (rule_labels == best && exact && !best_exact)) {
            best = rule_labels;
            best_exact = exact;
        }
    }
    return best == 0 ? 1 : best;
}

}

bool is_public_suffix(const std::string& raw) {
    std::string host = normalize_suffix_host(raw);
    if (host.empty() || is_ip_literal_host(host) || is_localhost_host(host)) return false;
    std::vector<std::string> labels = split_labels(host);
    if (labels.empty() || has_empty_label(labels)) return false;
    return suffix_label_count(labels) == labels.size();
}

std::string registrable_domain(const std::string& raw) {
    std::string host = normalize_suffix_host(raw);
    if (host.empty()) return "";
    if (is_ip_literal_host(host) || is_localhost_host(host)) return host;
    std::vector<std::string> labels = split_labels(host);
    if (labels.empty() || has_empty_label(labels)) return "";
    size_t suffix = suffix_label_count(labels);
    if (suffix >= labels.size()) return "";
    return join_labels(labels, labels.size() - suffix - 1);
}

}
