#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "mustela/mustela.hpp"
#include "mustela/view.hpp"

namespace {

std::string tmp_root() {
    const char* base = std::getenv("MUSTELA_TEST_TMP");
    std::string root = base && *base ? std::string(base) : std::string("/tmp/mustela-view-smoke");
    return root + "/view-smoke";
}

} 

int main() {
    if (!gtk_init_check(nullptr, nullptr)) {
        std::puts("view smoke: no display available, skipping");
        return 0;
    }
    int checks = 0;
    int passed = 0;
    auto check = [&checks, &passed](const char* name, bool ok) {
        ++checks;
        if (ok) {
            ++passed;
            std::printf("ok %d - %s\n", checks, name);
        } else {
            std::printf("not ok %d - %s\n", checks, name);
        }
    };

    std::string root = tmp_root();
    g_mkdir_with_parents(root.c_str(), 0700);

    mustela::Settings settings;
    mustela::PrivacyPolicy privacy = mustela::privacy_from(settings);
    mustela::Profile profile(root + "/profile", privacy);
    profile.ensure_dirs();
    std::string sites_path = root + "/sites.toml";
    mustela::SiteRules sites(sites_path);
    sites.load();

    mustela::ViewConfig config;
    config.settings = &settings;
    config.privacy = &privacy;
    config.sites = &sites;
    config.profile = &profile;
    config.downloads_dir = root + "/downloads";

    mustela::SessionCallbacks session_callbacks;
    mustela::ViewSession session(config, session_callbacks);

    std::string title_got;
    std::string uri_got;
    std::string committed_uri;
    bool committed_fired = false;
    bool loading_seen = false;

    mustela::ViewCallbacks callbacks;
    callbacks.title_changed = [&title_got](const std::string& title) { title_got = title; };
    callbacks.uri_changed = [&uri_got](const std::string& uri) { uri_got = uri; };
    callbacks.committed = [&committed_uri, &committed_fired](const std::string& uri, const std::string&,
        bool) {
        committed_uri = uri;
        committed_fired = true;
    };
    callbacks.loading_changed = [&loading_seen](bool loading) {
        if (loading) loading_seen = true;
    };

    mustela::View view(session, false, callbacks);
    view.load_url("data:text/html,<html><head><title>kiwi-smoke</title></head><body>hello</body></html>");

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        if (!title_got.empty() && committed_fired) break;
        g_usleep(4000);
    }
    while (gtk_events_pending()) gtk_main_iteration();

    check("title callback", title_got == "kiwi-smoke");
    check("committed callback", committed_fired && committed_uri.rfind("data:", 0) == 0);
    check("uri reported", uri_got.rfind("data:", 0) == 0);
    check("loading state seen", loading_seen);
    check("native widget", view.native_widget() != nullptr);
    check("not private", !view.is_private());
    check("default zoom", view.zoom() > 0.99 && view.zoom() < 1.01);
    view.set_zoom(9.0);
    check("zoom clamps high", view.zoom() > 2.99);
    view.set_zoom(0.1);
    check("zoom clamps low", view.zoom() < 0.31);
    view.set_zoom(1.5);
    check("zoom applies", view.zoom() > 1.49 && view.zoom() < 1.51);
    check("api version", mustela::view_api_version() == mustela::MUSTELA_VIEW_API_VERSION);

    mustela::View private_view(session, true, mustela::ViewCallbacks{});
    check("private flag", private_view.is_private());

    std::printf("view smoke: %d/%d checks passed\n", passed, checks);
    return passed == checks ? 0 : 1;
}
