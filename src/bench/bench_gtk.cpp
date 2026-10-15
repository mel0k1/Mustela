#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "perf.hpp"

int run_bench(int argc, char** argv);

namespace {

struct Bench {
    std::string url;
    int runs = 5;
    double max_ms = 0;
    std::string out_path;
    WebKitWebView* view = nullptr;
    std::vector<double> durations;
    double started_ms = 0;
    bool load_active = false;
    guint watchdog = 0;
};

Bench* bench = nullptr;

gboolean start_next_load(gpointer data);

gboolean watchdog_fired(gpointer) {
    std::fprintf(stderr, "bench: load timed out\n");
    std::exit(4);
    return G_SOURCE_REMOVE;
}

void on_load_failed(WebKitWebView*, WebKitLoadEvent, const char*, GError*, gpointer) {
    std::fprintf(stderr, "bench: load failed\n");
    std::exit(3);
}

void finish_run(Bench& b, double elapsed) {
    if (b.watchdog) {
        g_source_remove(b.watchdog);
        b.watchdog = 0;
    }
    b.load_active = false;
    b.durations.push_back(elapsed);
    if (static_cast<int>(b.durations.size()) >= b.runs) {
        gtk_main_quit();
        return;
    }
    g_timeout_add(150, start_next_load, &b);
}

gboolean start_next_load(gpointer data) {
    Bench& b = *static_cast<Bench*>(data);
    b.started_ms = mustela::Stopwatch::now_ms();
    b.load_active = true;
    b.watchdog = g_timeout_add_seconds(30, watchdog_fired, nullptr);
    webkit_web_view_load_uri(b.view, b.url.c_str());
    return G_SOURCE_REMOVE;
}

void on_load_changed(WebKitWebView*, WebKitLoadEvent event, gpointer) {
    Bench& b = *bench;
    if (event == WEBKIT_LOAD_STARTED) {
        b.started_ms = mustela::Stopwatch::now_ms();
        b.load_active = true;
    } else if (event == WEBKIT_LOAD_FINISHED && b.load_active) {
        finish_run(b, mustela::Stopwatch::now_ms() - b.started_ms);
    }
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

}

int run_bench(int argc, char** argv) {
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
        std::fprintf(stderr, "usage: mustela-bench --url URI [--runs N] [--max-ms M] [--out FILE]\n");
        return 1;
    }

    gtk_init(&argc, &argv);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "mustela-bench");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    instance.view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(instance.view));
    g_signal_connect(instance.view, "load-changed", G_CALLBACK(on_load_changed), nullptr);
    g_signal_connect(instance.view, "load-failed", G_CALLBACK(on_load_failed), nullptr);

    gtk_widget_show_all(window);
    g_timeout_add(200, start_next_load, &instance);
    gtk_main();

    mustela::SampleStats stats = mustela::stats_from_durations(instance.durations);
    emit_result(instance, stats);

    if (instance.max_ms > 0 && stats.count > 0 && stats.median_ms > instance.max_ms) {
        std::fprintf(stderr, "bench: median %.1fms exceeds budget %.1fms\n",
            stats.median_ms, instance.max_ms);
        return 2;
    }
    return stats.count > 0 ? 0 : 2;
}

int main(int argc, char** argv) {
    return run_bench(argc, argv);
}
