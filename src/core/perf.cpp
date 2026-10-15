#include "perf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mustela {

namespace {

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string json_unescape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            char next = text[++i];
            switch (next) {
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: out += next; break;
            }
        } else {
            out += text[i];
        }
    }
    return out;
}

}

SampleStats stats_from_durations(std::vector<double> values) {
    SampleStats stats;
    if (values.empty()) return stats;

    std::sort(values.begin(), values.end());
    size_t n = values.size();
    size_t mid = n / 2;

    stats.count = static_cast<int>(n);
    stats.min_ms = values.front();
    stats.max_ms = values.back();
    stats.median_ms = (n % 2 == 1)
        ? values[mid]
        : (values[mid - 1] + values[mid]) / 2.0;

    size_t index = static_cast<size_t>(std::ceil(n * 0.95));
    if (index == 0) index = 1;
    if (index > n) index = n;
    stats.p95_ms = values[index - 1];
    return stats;
}

ProcessModel process_model_from(const std::string& name) {
    if (name == "shared") return ProcessModel::Shared;
    return ProcessModel::PerView;
}

const char* process_model_name(ProcessModel model) {
    return model == ProcessModel::Shared ? "shared" : "per-view";
}

std::string encode_perf_record(const PerfRecord& record) {
    char head[160];
    std::snprintf(head, sizeof(head),
        "{\"kind\":\"%s\",\"ts\":%lld,\"ms\":%.2f,\"bytes\":%lld,\"detail\":\"",
        record.kind.c_str(),
        static_cast<long long>(record.timestamp),
        record.value_ms,
        static_cast<long long>(record.value_bytes));
    std::string out = head;
    out += json_escape(record.detail);
    out += "\"}";
    return out;
}

bool decode_perf_record(const std::string& line, PerfRecord& out) {
    size_t kind_at = line.find("\"kind\":\"");
    if (kind_at == std::string::npos) return false;
    size_t kind_begin = kind_at + 8;
    size_t kind_end = line.find('"', kind_begin);
    if (kind_end == std::string::npos) return false;

    size_t ts_at = line.find("\"ts\":", kind_end);
    if (ts_at == std::string::npos) return false;

    size_t ms_at = line.find("\"ms\":", ts_at);
    if (ms_at == std::string::npos) return false;

    size_t bytes_at = line.find("\"bytes\":", ms_at);
    if (bytes_at == std::string::npos) return false;

    size_t detail_at = line.find("\"detail\":\"", bytes_at);
    if (detail_at == std::string::npos) return false;

    std::string tail = line.substr(detail_at + 10);
    if (tail.size() < 2 || tail.compare(tail.size() - 2, 2, "\"}") != 0) return false;
    tail.resize(tail.size() - 2);

    PerfRecord record;
    record.kind = line.substr(kind_begin, kind_end - kind_begin);
    record.timestamp = std::strtoll(line.c_str() + ts_at + 5, nullptr, 10);
    record.value_ms = std::strtod(line.c_str() + ms_at + 5, nullptr);
    record.value_bytes = std::strtoll(line.c_str() + bytes_at + 8, nullptr, 10);
    record.detail = json_unescape(tail);

    if (record.kind.empty()) return false;
    out = std::move(record);
    return true;
}

PerfLog::PerfLog(std::string path, std::size_t capacity)
    : path_(std::move(path)), capacity_(capacity) {
    std::vector<PerfRecord> records = read_all();
    if (records.size() > capacity_) {
        std::ofstream out(path_, std::ios::trunc);
        if (!out) return;
        size_t skip = records.size() - capacity_;
        for (size_t i = 0; i < records.size(); ++i) {
            if (i < skip) continue;
            out << encode_perf_record(records[i]) << '\n';
        }
    }
}

void PerfLog::append(const PerfRecord& record) {
    std::ofstream out(path_, std::ios::app);
    if (!out) return;
    out << encode_perf_record(record) << '\n';
}

std::vector<PerfRecord> PerfLog::read_all() const {
    std::vector<PerfRecord> records;
    std::ifstream in(path_);
    if (!in) return records;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        PerfRecord record;
        if (decode_perf_record(line, record)) records.push_back(std::move(record));
    }
    return records;
}

std::int64_t parse_status_rss_bytes(const std::string& content) {
    size_t at = content.find("VmRSS:");
    if (at == std::string::npos) return 0;

    size_t begin = content.find_first_not_of(" \t", at + 6);
    if (begin == std::string::npos) return 0;
    if (content[begin] == '-') return 0;

    size_t end = begin;
    while (end < content.size() &&
        ((content[end] >= '0' && content[end] <= '9') || content[end] == '.')) {
        ++end;
    }
    if (end == begin) return 0;

    double value = std::strtod(content.substr(begin, end - begin).c_str(), nullptr);

    size_t unit_begin = content.find_first_not_of(" \t", end);
    if (unit_begin == std::string::npos) return 0;
    std::string unit = content.substr(unit_begin, 2);

    double factor = 1.0;
    if (unit == "kB" || unit == "KB") factor = 1024.0;
    else if (unit == "mB" || unit == "MB") factor = 1024.0 * 1024.0;
    else if (unit == "gB" || unit == "GB") factor = 1024.0 * 1024.0 * 1024.0;

    return static_cast<std::int64_t>(value * factor);
}

bool within_budget(int budget_ms, double elapsed_ms) {
    if (budget_ms <= 0) return true;
    return elapsed_ms <= static_cast<double>(budget_ms);
}

bool ceiling_exceeded(int ceiling_mb, std::int64_t total_bytes) {
    if (ceiling_mb <= 0) return false;
    return total_bytes > static_cast<std::int64_t>(ceiling_mb) * 1024 * 1024;
}

}

