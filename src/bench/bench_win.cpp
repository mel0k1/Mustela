#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "perf.hpp"

#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKType.h>
#include <WebKit/WKView.h>
#include <WebKit/WKWebsiteDataStoreRef.h>

namespace {

enum : UINT_PTR {
    TIMER_START = 1,
    TIMER_WATCHDOG = 2
};

struct Bench {
    std::string url;
    int runs = 5;
    double max_ms = 0;
    std::string out_path;
    std::vector<double> durations;
    double started_ms = 0;
    bool load_active = false;
    HWND window = nullptr;
    WKViewRef view = nullptr;
    WKPageRef page = nullptr;
};

Bench* bench = nullptr;

struct WkHold {
    WKTypeRef ref = nullptr;
    explicit WkHold(WKTypeRef value) : ref(value) {}
    ~WkHold() {
        if (ref) WKRelease(ref);
    }
    WkHold(const WkHold&) = delete;
    WkHold& operator=(const WkHold&) = delete;
};

void start_load() {
    Bench& b = *bench;
    b.started_ms = mustela::Stopwatch::now_ms();
    b.load_active = true;
    SetTimer(b.window, TIMER_WATCHDOG, 30000, nullptr);
    WkHold target(WKURLCreateWithUTF8CString(b.url.c_str()));
    WKPageLoadURL(b.page, static_cast<WKURLRef>(target.ref));
}

void finish_run(double elapsed) {
    Bench& b = *bench;
    KillTimer(b.window, TIMER_WATCHDOG);
    b.load_active = false;
    b.durations.push_back(elapsed);
    if (static_cast<int>(b.durations.size()) >= b.runs) {
        PostQuitMessage(0);
        return;
    }
    SetTimer(b.window, TIMER_START, 150, nullptr);
}

void on_start_provisional(WKPageRef, WKNavigationRef, WKTypeRef, const void*) {
    Bench& b = *bench;
    b.started_ms = mustela::Stopwatch::now_ms();
    b.load_active = true;
}

void on_finish_navigation(WKPageRef, WKNavigationRef, WKTypeRef, const void*) {
    if (bench->load_active) {
        finish_run(mustela::Stopwatch::now_ms() - bench->started_ms);
    }
}

void on_fail_provisional(WKPageRef, WKNavigationRef, WKErrorRef, WKTypeRef, const void*) {
    std::fprintf(stderr, "bench: load failed\n");
    std::exit(3);
}

void emit_result(const Bench& b, const mustela::SampleStats& stats) {
    char line[640];
    std::snprintf(line, sizeof(line),
        "{\"url\":\"%s\",\"runs\":%d,\"n\":%d,\"min_ms\":%.1f,\"median_ms\":%.1f,\"p95_ms\":%.1f,\"max_ms\":%.1f}",
        b.url.c_str(), b.runs, stats.count, stats.min_ms, stats.median_ms, stats.p95_ms, stats.max_ms);
    std::printf("%s\n", line);
    if (!b.out_path.empty()) {
        FILE* out = std::fopen(b.out_path.c_str(), "wb");
        if (out) {
            std::fprintf(out, "%s\n", line);
            std::fclose(out);
        }
    }
}

LRESULT CALLBACK bench_proc(HWND wnd, UINT message, WPARAM wparam, LPARAM) {
    switch (message) {
        case WM_TIMER:
            if (wparam == TIMER_START) {
                KillTimer(wnd, TIMER_START);
                start_load();
            } else if (wparam == TIMER_WATCHDOG) {
                std::fprintf(stderr, "bench: load timed out\n");
                std::exit(4);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(wnd, message, wparam, 0);
    }
}

}

int main(int argc, char** argv) {
    Bench instance;
    bench = &instance;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            instance.url = argv[++i];
        } else if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            instance.runs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--max-ms") == 0 && i + 1 < argc) {
            instance.max_ms = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            instance.out_path = argv[++i];
        }
    }

    if (instance.url.empty() || instance.runs < 1) {
        std::fprintf(stderr, "usage: mustela-bench-win --url URI [--runs N] [--max-ms M] [--out FILE]\n");
        return 1;
    }

    OleInitialize(nullptr);

    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = bench_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"MUSTELA_BENCH_WND";
    RegisterClassW(&window_class);

    RECT frame = {0, 0, 800, 600};
    AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
    instance.window = CreateWindowExW(0, L"MUSTELA_BENCH_WND", L"mustela-bench", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    WkHold context_configuration(WKContextConfigurationCreate());
    WKContextRef context = WKContextCreateWithConfiguration(static_cast<WKContextConfigurationRef>(context_configuration.ref));

    WkHold store(WKWebsiteDataStoreCreateNonPersistentDataStore());

    WkHold configuration(WKPageConfigurationCreate());
    WKPageConfigurationRef config = static_cast<WKPageConfigurationRef>(configuration.ref);
    WKPageConfigurationSetContext(config, context);
    WKPageConfigurationSetWebsiteDataStore(config, static_cast<WKWebsiteDataStoreRef>(store.ref));

    RECT view_rect = {0, 0, 800, 600};
    instance.view = WKViewCreate(view_rect, config, instance.window);
    WKViewSetIsInWindow(instance.view, TRUE);
    instance.page = WKViewGetPage(instance.view);

    WKPageNavigationClientV3 navigation = {};
    navigation.base.version = 3;
    navigation.base.clientInfo = bench;
    navigation.didStartProvisionalNavigation = on_start_provisional;
    navigation.didFinishNavigation = on_finish_navigation;
    navigation.didFailProvisionalNavigation = on_fail_provisional;
    WKPageSetPageNavigationClient(instance.page, &navigation.base);

    SetTimer(instance.window, TIMER_START, 200, nullptr);
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    mustela::SampleStats stats = mustela::stats_from_durations(instance.durations);
    emit_result(instance, stats);

    WKRelease(instance.view);
    WKRelease(context);

    OleUninitialize();

    if (instance.max_ms > 0 && stats.count > 0 && stats.median_ms > instance.max_ms) {
        std::fprintf(stderr, "bench: median %.1fms exceeds budget %.1fms\n",
            stats.median_ms, instance.max_ms);
        return 2;
    }
    return stats.count > 0 ? 0 : 2;
}
