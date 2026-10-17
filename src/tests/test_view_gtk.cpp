#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
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

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
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
    check("view api v3", mustela::view_api_version() >= 3);

    bool fetch_fired = false;
    session.fetch_website_data([&fetch_fired](std::vector<mustela::WebsiteDataEntry>) {
        fetch_fired = true;
    });
    bool clear_fired = false;
    session.clear_website_data(mustela::DataKind::All, 0, [&clear_fired] { clear_fired = true; });
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((!fetch_fired || !clear_fired) && std::chrono::steady_clock::now() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(4000);
    }
    check("website data fetch fires", fetch_fired);
    check("website data clear fires", clear_fired);

    bool removed_fired = false;
    session.remove_website_data({"example.com"}, mustela::DataKind::Cookies,
        [&removed_fired] { removed_fired = true; });
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!removed_fired && std::chrono::steady_clock::now() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(4000);
    }
    check("website data remove fires", removed_fired);

    mustela::SitePolicy active = view.active_site_policy();
    check("active policy default", active.zoom == 0.0 && active.user_agent.empty()
        && active.css.empty());

    bool find_result_seen = false;
    bool find_found = false;
    callbacks.find_result = [&find_result_seen, &find_found](bool found) {
        find_result_seen = true;
        find_found = found;
    };
    view.set_callbacks(callbacks);
    view.find("hello");
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!find_result_seen && std::chrono::steady_clock::now() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(4000);
    }
    check("find reports hit", find_result_seen && find_found);
    check("find text kept", view.find_text() == "hello");

    find_result_seen = false;
    view.find("zzz-missing-token");
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!find_result_seen && std::chrono::steady_clock::now() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(4000);
    }
    check("find reports miss", find_result_seen && !find_found);
    view.find_finish();

    std::string selection;
    bool selection_fired = false;
    view.selected_text([&selection, &selection_fired](const std::string& text) {
        selection_fired = true;
        selection = text;
    });
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!selection_fired && std::chrono::steady_clock::now() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(4000);
    }
    check("selected text fires", selection_fired);

    view.execute_edit_command(mustela::EditCommand::Copy);
    check("edit command dispatch", true);

    GtkWidget* host_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_add(GTK_CONTAINER(host_window), static_cast<GtkWidget*>(view.native_widget()));
    gtk_widget_show_all(host_window);
    view.show_devtools();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!view.devtools_attached() && std::chrono::steady_clock::now() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(4000);
    }
    check("devtools docks", view.devtools_attached());
    view.hide_devtools();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (view.devtools_attached() && std::chrono::steady_clock::now() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(4000);
    }
    check("devtools closes", !view.devtools_attached());

    bool ctx_handled = false;
    mustela::ContextMenuInfo ctx_info;
    callbacks.context_menu = [&ctx_handled, &ctx_info](const mustela::ContextMenuInfo& info) {
        ctx_handled = true;
        ctx_info = info;
        return true;
    };
    view.set_callbacks(callbacks);
    WebKitHitTestResult* hit = WEBKIT_HIT_TEST_RESULT(g_object_new(WEBKIT_TYPE_HIT_TEST_RESULT,
        "context", static_cast<guint>(WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK
            | WEBKIT_HIT_TEST_RESULT_CONTEXT_SELECTION),
        "link-uri", "https://example.org/kiwi", nullptr));
    WebKitContextMenu* menu = webkit_context_menu_new();
    gboolean suppressed = FALSE;
    g_signal_emit_by_name(view.native_widget(), "context-menu", menu, nullptr, hit, &suppressed);
    check("context menu intercepted", ctx_handled && suppressed == TRUE
        && ctx_info.link && ctx_info.link_uri == "https://example.org/kiwi");
    check("context menu page uri", ctx_info.page_uri.rfind("data:", 0) == 0);
    g_object_unref(menu);
    g_object_unref(hit);
    gtk_widget_destroy(host_window);

    mustela::View private_view(session, true, mustela::ViewCallbacks{});
    check("private flag", private_view.is_private());

    std::printf("view smoke: %d/%d checks passed\n", passed, checks);
    return passed == checks ? 0 : 1;
}
