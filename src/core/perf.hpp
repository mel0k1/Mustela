#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace mustela {

class Stopwatch {
public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}

    void reset() { start_ = std::chrono::steady_clock::now(); }

    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_).count();
    }

    static double now_ms() {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

struct LoadTimer {
    bool active = false;
    double started_ms = 0;

    void start(double now) {
        active = true;
        started_ms = now;
    }

    double stop(double now) {
        if (!active) return -1.0;
        active = false;
        return now - started_ms;
    }

    void cancel() { active = false; }
};

struct SampleStats {
    int count = 0;
    double min_ms = 0;
    double median_ms = 0;
    double p95_ms = 0;
    double max_ms = 0;
};

SampleStats stats_from_durations(std::vector<double> values);

enum class ProcessModel {
    PerView,
    Shared
};

ProcessModel process_model_from(const std::string& name);
const char* process_model_name(ProcessModel model);

struct PerfRecord {
    std::string kind;
    std::int64_t timestamp = 0;
    double value_ms = 0;
    std::int64_t value_bytes = 0;
    std::string detail;
};

std::string encode_perf_record(const PerfRecord& record);
bool decode_perf_record(const std::string& line, PerfRecord& out);

class PerfLog {
public:
    explicit PerfLog(std::string path, std::size_t capacity = 5000);

    void append(const PerfRecord& record);
    std::vector<PerfRecord> read_all() const;

private:
    std::string path_;
    std::size_t capacity_;
};

std::int64_t parse_status_rss_bytes(const std::string& content);

bool within_budget(int budget_ms, double elapsed_ms);
bool ceiling_exceeded(int ceiling_mb, std::int64_t total_bytes);

}

