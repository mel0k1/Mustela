#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <glib/gstdio.h>

#include <dirent.h>
#include <unistd.h>

#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "browsing_data.hpp"
#include "config.hpp"
#include "fingerprint_scripts.hpp"
#include "history.hpp"
#include "perf.hpp"
#include "privacy.hpp"
#include "site_rules.hpp"
#include "tab.hpp"
#include "ui_text.hpp"
#include "url.hpp"

int mustela_main(int argc, char** argv);

namespace {

using mustela::CookiePolicy;
using mustela::DataKind;
using mustela::FingerprintPolicy;
using mustela::Hardening;
using mustela::PrivacyPolicy;
using mustela::Settings;
using mustela::Tab;
using mustela::TabPrivacy;

struct Browser {
    Settings settings;
    PrivacyPolicy privacy;
    GtkWidget* window = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* url_entry = nullptr;
    GtkWidget* back_btn = nullptr;
    GtkWidget* forward_btn = nullptr;
    GtkWidget* reload_btn = nullptr;
    GtkWidget* private_toggle = nullptr;
    GtkWidget* status_label = nullptr;
    GtkWidget* history_window = nullptr;
    GtkWidget* find_bar = nullptr;
    GtkWidget* find_entry = nullptr;
    GtkWidget* find_label = nullptr;
    std::string find_query;
    std::string hover_link;
    WebKitWebContext* normal_context = nullptr;
    WebKitWebContext* private_context = nullptr;
    std::unique_ptr<mustela::HistoryStore> history;
    std::unique_ptr<mustela::SiteRules> sites;
    GtkWidget* rules_window = nullptr;
    GtkWidget* rules_tree = nullptr;
    GtkListStore* rules_store = nullptr;
    GFileMonitor* settings_monitor = nullptr;
    GFileMonitor* sites_monitor = nullptr;
    guint settings_reload_pending = 0;
    guint sites_reload_pending = 0;
    WebKitUserContentFilterStore* filter_store = nullptr;
    WebKitUserContentFilter* fonts_filter = nullptr;
    std::unique_ptr<mustela::PerfLog> perf;
    std::int64_t memory_bytes = 0;
    bool memory_over = false;
    bool startup_marked = false;
    double startup_entry_ms = 0;
    std::vector<std::unique_ptr<Tab>> tabs;
};

Browser* browser = nullptr;

Tab* open_tab(Browser& b, const char* url, TabPrivacy request);
void close_tab(Browser& b, Tab* tab);
GtkWidget* build_tab_label(Browser& b, Tab* tab);
void on_close_tab_clicked(GtkButton*, gpointer user_data);
void open_history_window(Browser& b);
void open_site_rules_window(Browser& b);
void clear_browsing_data(Browser& b);
void apply_policies_to_tabs(Browser& b);
void apply_fingerprint(Browser& b, WebKitWebView* view, const FingerprintPolicy& fp);
void refresh_status(Browser& b);
void flash_status(const char* text);
void mark_startup(Browser& b);
Tab* tab_from_view(Browser& b, WebKitWebView* view);
void connect_tab_signals(Tab* tab);
void run_find(Browser& b, int direction);
void show_find_bar(Browser& b, bool show);

void record_perf(Browser& b, const std::string& kind, double value_ms, std::int64_t value_bytes,
    const std::string& detail) {
    if (!b.perf || !b.settings.perf_log) return;
    mustela::PerfRecord record;
    record.kind = kind;
    record.timestamp = static_cast<std::int64_t>(std::time(nullptr));
    record.value_ms = value_ms;
    record.value_bytes = value_bytes;
    record.detail = detail;
    b.perf->append(record);
}

void mark_startup(Browser& b) {
    if (b.startup_marked) return;
    b.startup_marked = true;
    double elapsed = mustela::Stopwatch::now_ms() - b.startup_entry_ms;
    record_perf(b, "startup", elapsed, 0, "to-map");
    if (b.settings.startup_budget_ms > 0) {
        std::string text = "startup " + std::to_string(static_cast<int>(elapsed)) + "ms";
        if (!mustela::within_budget(b.settings.startup_budget_ms, elapsed)) text += " over budget";
        flash_status(text.c_str());
    }
}

gboolean on_window_mapped(GtkWidget*, GdkEvent*, gpointer) {
    if (browser) mark_startup(*browser);
    return FALSE;
}

std::string read_small_file(const char* path) {
    std::ifstream in(path);
    if (!in) return "";
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::int64_t children_rss_bytes() {
    std::int64_t total = 0;
    pid_t self = getpid();
    DIR* proc = opendir("/proc");
    if (!proc) return 0;

    while (dirent* entry = readdir(proc)) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        std::string stat_content = read_small_file((std::string("/proc/") + entry->d_name + "/stat").c_str());
        size_t close = stat_content.rfind(')');
        if (close == std::string::npos) continue;

        std::istringstream fields(stat_content.substr(close + 1));
        std::string state;
        long ppid = 0;
        fields >> state >> ppid;
        if (ppid != self) continue;

        std::string status_content = read_small_file((std::string("/proc/") + entry->d_name + "/status").c_str());
        total += mustela::parse_status_rss_bytes(status_content);
    }
    closedir(proc);
    return total;
}

void sample_memory(Browser& b) {
    std::int64_t self_bytes = mustela::parse_status_rss_bytes(read_small_file("/proc/self/status"));
    b.memory_bytes = self_bytes + children_rss_bytes();
    bool over = mustela::ceiling_exceeded(b.settings.memory_ceiling_mb, b.memory_bytes);
    record_perf(b, "memory", 0.0, b.memory_bytes, "rss");
    if (over != b.memory_over) {
        b.memory_over = over;
        refresh_status(b);
    }
}

gboolean on_memory_timer(gpointer) {
    if (browser) sample_memory(*browser);
    return G_SOURCE_CONTINUE;
}

void apply_process_model(WebKitWebContext* context, const std::string& name) {
    WebKitProcessModel model = mustela::process_model_from(name) == mustela::ProcessModel::Shared
        ? WEBKIT_PROCESS_MODEL_SHARED_SECONDARY_PROCESS
        : WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    webkit_web_context_set_process_model(context, model);
#pragma GCC diagnostic pop
}

Tab* active_tab(Browser& b) {
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(b.notebook));
    if (page < 0 || page >= static_cast<gint>(b.tabs.size())) return nullptr;
    return b.tabs[static_cast<size_t>(page)].get();
}

gint tab_index(const Browser& b, const Tab* tab) {
    for (size_t i = 0; i < b.tabs.size(); ++i) {
        if (b.tabs[i].get() == tab) return static_cast<gint>(i);
    }
    return -1;
}

Tab* tab_from_view(Browser& b, WebKitWebView* view) {
    for (std::unique_ptr<Tab>& tab : b.tabs) {
        if (tab->view() == view) return tab.get();
    }
    return nullptr;
}

std::string tab_label_text(const Tab& tab) {
    std::string text = tab.title();
    if (tab.is_private()) text = "private · " + text;
    return text;
}

void update_tab_label(Browser&, Tab* tab) {
    if (!tab || !tab->label()) return;
    gtk_label_set_text(GTK_LABEL(tab->label()), tab_label_text(*tab).c_str());
    gtk_widget_set_tooltip_text(tab->label(), tab->uri().c_str());
}

void update_window_title(Browser& b) {
    Tab* tab = active_tab(b);
    if (!tab) {
        gtk_window_set_title(GTK_WINDOW(b.window), "Mustela");
        return;
    }
    gtk_window_set_title(GTK_WINDOW(b.window), (tab->title() + " — Mustela").c_str());
}

void refresh_status(Browser& b) {
    if (!b.status_label) return;
    if (!b.hover_link.empty()) {
        gtk_label_set_text(GTK_LABEL(b.status_label), ("→ " + b.hover_link).c_str());
        return;
    }
    Tab* tab = active_tab(b);
    std::string text;
    if (tab && tab->is_private()) text = "private · ";
    text += std::string("cookies ") + mustela::cookie_policy_name(b.privacy.cookies);
    if (b.privacy.hardening.tracking_prevention) text += " · itp";
    if (b.privacy.fingerprint.active()) text += " · fp";
    if (b.memory_over) {
        text += " · mem " + std::to_string(static_cast<int>(b.memory_bytes / (1024 * 1024))) + " MB";
    }
    gtk_label_set_text(GTK_LABEL(b.status_label), text.c_str());
}

void refresh_chrome(Browser& b) {
    Tab* tab = active_tab(b);
    if (tab) {
        if (!gtk_widget_has_focus(b.url_entry)) {
            gtk_entry_set_text(GTK_ENTRY(b.url_entry), tab->uri().c_str());
        }
        gtk_widget_set_sensitive(b.back_btn, webkit_web_view_can_go_back(tab->view()) != FALSE);
        gtk_widget_set_sensitive(b.forward_btn, webkit_web_view_can_go_forward(tab->view()) != FALSE);
        gtk_widget_set_sensitive(b.reload_btn, !tab->uri().empty());
        update_tab_label(b, tab);
    }
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(b.notebook), gtk_notebook_get_n_pages(GTK_NOTEBOOK(b.notebook)) > 1);
    update_window_title(b);
    refresh_status(b);
}

gboolean restore_status(gpointer) {
    refresh_chrome(*browser);
    return G_SOURCE_REMOVE;
}

void flash_status(const char* text) {
    if (!browser->status_label) return;
    gtk_label_set_text(GTK_LABEL(browser->status_label), text);
    g_timeout_add_seconds(3, restore_status, nullptr);
}

void apply_cookie_policy(WebKitWebContext* context, CookiePolicy policy) {
    WebKitCookieManager* manager = webkit_web_context_get_cookie_manager(context);
    WebKitCookieAcceptPolicy accept = WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
    if (policy == CookiePolicy::Always) accept = WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
    if (policy == CookiePolicy::Never) accept = WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
    webkit_cookie_manager_set_accept_policy(manager, accept);
}

void harden_view(WebKitWebView* view, const Hardening& hard) {
    WebKitSettings* settings = webkit_web_view_get_settings(view);
    g_object_set(settings,
        "enable-javascript", hard.javascript ? TRUE : FALSE,
        "enable-hyperlink-auditing", hard.hyperlink_auditing ? TRUE : FALSE,
        "enable-dns-prefetching", hard.dns_prefetching ? TRUE : FALSE,
        "media-playback-requires-user-gesture", hard.autoplay_blocked ? TRUE : FALSE,
        nullptr);
}

guint webkit_data_types(DataKind kinds) {
    guint types = 0;
    if (mustela::has(kinds, DataKind::Cookies)) types |= WEBKIT_WEBSITE_DATA_COOKIES;
    if (mustela::has(kinds, DataKind::Cache)) types |= WEBKIT_WEBSITE_DATA_MEMORY_CACHE | WEBKIT_WEBSITE_DATA_DISK_CACHE;
    if (mustela::has(kinds, DataKind::LocalStorage)) types |= WEBKIT_WEBSITE_DATA_LOCAL_STORAGE | WEBKIT_WEBSITE_DATA_SESSION_STORAGE;
    if (mustela::has(kinds, DataKind::IndexedDB)) types |= WEBKIT_WEBSITE_DATA_INDEXEDDB_DATABASES;
    return types;
}

void on_data_cleared(GObject* object, GAsyncResult* result, gpointer) {
    GError* error = nullptr;
    webkit_website_data_manager_clear_finish(WEBKIT_WEBSITE_DATA_MANAGER(object), result, &error);
    if (error) {
        g_error_free(error);
        return;
    }
    flash_status("browsing data cleared");
}

void clear_browsing_data(Browser& b) {
    mustela::ClearRequest request;
    WebKitWebsiteDataTypes types = static_cast<WebKitWebsiteDataTypes>(webkit_data_types(request.kinds));
    GTimeSpan max_age = static_cast<GTimeSpan>(request.max_age_seconds);
    WebKitWebsiteDataManager* managers[] = {
        webkit_web_context_get_website_data_manager(b.normal_context),
        webkit_web_context_get_website_data_manager(b.private_context)
    };
    for (WebKitWebsiteDataManager* manager : managers) {
        webkit_website_data_manager_clear(manager, types, max_age, nullptr, on_data_cleared, nullptr);
    }
}

void apply_view_policy(Browser& b, WebKitWebView* view, const std::string& uri) {
    if (!b.sites) return;
    mustela::SiteDecision decision = mustela::resolve_site(b.privacy, *b.sites, uri);
    WebKitSettings* settings = webkit_web_view_get_settings(view);
    g_object_set(settings, "enable-javascript", decision.javascript ? TRUE : FALSE, nullptr);
    apply_fingerprint(b, view, decision.fingerprint);
}

void apply_policies_to_tabs(Browser& b) {
    for (std::unique_ptr<Tab>& tab : b.tabs) {
        harden_view(tab->view(), b.privacy.hardening);
        apply_view_policy(b, tab->view(), tab->uri());
    }
}

void add_user_script(WebKitUserContentManager* manager, const std::string& source) {
    WebKitUserScript* script = webkit_user_script_new(source.c_str(),
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(manager, script);
    webkit_user_script_unref(script);
}

void apply_fingerprint(Browser& b, WebKitWebView* view, const FingerprintPolicy& fp) {
    WebKitUserContentManager* manager = webkit_web_view_get_user_content_manager(view);
    webkit_user_content_manager_remove_all_scripts(manager);
    webkit_user_content_manager_remove_all_filters(manager);
    if (fp.block_webgl) add_user_script(manager, mustela::fp_scripts::WEBGL_BLOCK);
    if (fp.block_canvas_readback) add_user_script(manager, mustela::fp_scripts::CANVAS_BLANK);
    if (fp.referrer != mustela::ReferrerTrim::Off) {
        add_user_script(manager, mustela::fingerprint_referrer_script(fp.referrer));
    }
    if (fp.block_remote_fonts && b.fonts_filter) {
        webkit_user_content_manager_add_filter(manager, b.fonts_filter);
    }
}

void on_fonts_filter_saved(GObject* object, GAsyncResult* result, gpointer) {
    GError* error = nullptr;
    WebKitUserContentFilter* filter = webkit_user_content_filter_store_save_finish(
        WEBKIT_USER_CONTENT_FILTER_STORE(object), result, &error);
    if (error) {
        g_error_free(error);
        if (browser) flash_status("font filter failed");
        return;
    }
    if (!browser) {
        webkit_user_content_filter_unref(filter);
        return;
    }
    if (browser->fonts_filter) webkit_user_content_filter_unref(browser->fonts_filter);
    browser->fonts_filter = filter;
    apply_policies_to_tabs(*browser);
}

void on_origin_cookies_removed(GObject* object, GAsyncResult* result, gpointer) {
    GError* error = nullptr;
    webkit_website_data_manager_remove_finish(WEBKIT_WEBSITE_DATA_MANAGER(object), result, &error);
    if (error) g_error_free(error);
}

void deny_origin_cookies(Browser& b, const std::string& uri) {
    std::string host = mustela::host_from_uri(uri);
    if (host.empty()) return;
    size_t scheme_end = uri.find("://");
    std::string scheme = scheme_end == std::string::npos ? "https" : uri.substr(0, scheme_end);
    std::string origin = scheme + "://" + host;
    GList* origins = g_list_append(nullptr, const_cast<gchar*>(origin.c_str()));
    WebKitWebsiteDataManager* manager = webkit_web_context_get_website_data_manager(b.normal_context);
    webkit_website_data_manager_remove(manager, WEBKIT_WEBSITE_DATA_COOKIES, origins, nullptr, on_origin_cookies_removed, nullptr);
    g_list_free(origins);
}

void on_destroy(GtkWidget*, gpointer) {
    if (browser) {
        gint width = 0;
        gint height = 0;
        gtk_window_get_size(GTK_WINDOW(browser->window), &width, &height);
        browser->settings.window_width = width;
        browser->settings.window_height = height;
        gchar* path = g_build_filename(g_get_user_config_dir(), "mustela", "settings.toml", nullptr);
        mustela::save_settings(path, browser->settings);
        g_free(path);
        g_clear_object(&browser->settings_monitor);
        g_clear_object(&browser->sites_monitor);
        g_clear_object(&browser->filter_store);
        if (browser->fonts_filter) webkit_user_content_filter_unref(browser->fonts_filter);
    }
    gtk_main_quit();
}

gboolean on_key_press(GtkWidget*, GdkEventKey* event, gpointer) {
    Browser& b = *browser;
    GdkModifierType mods = static_cast<GdkModifierType>(event->state & gtk_accelerator_get_default_mod_mask());
    Tab* tab = active_tab(b);

    if (mods == GDK_CONTROL_MASK && event->keyval == GDK_KEY_t) {
        open_tab(b, b.settings.homepage.c_str(), b.privacy.default_private);
        return TRUE;
    }
    if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && event->keyval == GDK_KEY_P) {
        open_tab(b, b.settings.homepage.c_str(), TabPrivacy::Private);
        return TRUE;
    }
    if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && event->keyval == GDK_KEY_Delete) {
        clear_browsing_data(b);
        return TRUE;
    }
    if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && event->keyval == GDK_KEY_S) {
        open_site_rules_window(b);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && event->keyval == GDK_KEY_w) {
        if (tab) close_tab(b, tab);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && event->keyval == GDK_KEY_l) {
        gtk_widget_grab_focus(b.url_entry);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && event->keyval == GDK_KEY_h) {
        open_history_window(b);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && event->keyval == GDK_KEY_r) {
        if (tab) webkit_web_view_reload(tab->view());
        return TRUE;
    }
    if (mods == 0 && event->keyval == GDK_KEY_F5) {
        if (tab) webkit_web_view_reload(tab->view());
        return TRUE;
    }
    if (mods == GDK_MOD1_MASK && event->keyval == GDK_KEY_Left) {
        if (tab && webkit_web_view_can_go_back(tab->view())) webkit_web_view_go_back(tab->view());
        return TRUE;
    }
    if (mods == GDK_MOD1_MASK && event->keyval == GDK_KEY_Right) {
        if (tab && webkit_web_view_can_go_forward(tab->view())) webkit_web_view_go_forward(tab->view());
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && event->keyval == GDK_KEY_f) {
        show_find_bar(b, TRUE);
        return TRUE;
    }
    if (mods == 0 && event->keyval == GDK_KEY_Escape) {
        if (b.find_bar && gtk_widget_get_visible(b.find_bar)) {
            show_find_bar(b, FALSE);
            return TRUE;
        }
        if (tab) webkit_web_view_stop_loading(tab->view());
        return TRUE;
    }
    return FALSE;
}

void on_view_uri_changed(GObject*, GParamSpec*, gpointer user_data) {
    Tab* tab = static_cast<Tab*>(user_data);
    update_tab_label(*browser, tab);
    if (active_tab(*browser) == tab) refresh_chrome(*browser);
}

void on_view_title_changed(GObject*, GParamSpec*, gpointer user_data) {
    Tab* tab = static_cast<Tab*>(user_data);
    update_tab_label(*browser, tab);
    if (active_tab(*browser) == tab) refresh_chrome(*browser);
}

void on_view_progress_changed(GObject* object, GParamSpec*, gpointer) {
    Tab* tab = active_tab(*browser);
    if (!tab || tab->view() != WEBKIT_WEB_VIEW(object)) return;
    gdouble progress = webkit_web_view_get_estimated_load_progress(tab->view());
    gtk_entry_set_progress_fraction(GTK_ENTRY(browser->url_entry), progress >= 1.0 ? 0.0 : progress);
}

void on_view_load_changed(WebKitWebView*, WebKitLoadEvent event, gpointer user_data) {
    Tab* tab = static_cast<Tab*>(user_data);
    if (event == WEBKIT_LOAD_STARTED) {
        tab->load.start(mustela::Stopwatch::now_ms());
    }
    if (event == WEBKIT_LOAD_COMMITTED) {
        apply_view_policy(*browser, tab->view(), tab->uri());
    }
    if (event == WEBKIT_LOAD_FINISHED) {
        gtk_entry_set_progress_fraction(GTK_ENTRY(browser->url_entry), 0.0);
        double elapsed = tab->load.stop(mustela::Stopwatch::now_ms());
        if (elapsed >= 0.0 && !tab->uri().empty()) {
            record_perf(*browser, "pageload", elapsed, 0, tab->uri());
        }
        if (tab && browser->history) {
            std::string uri = tab->uri();
            if (!uri.empty() && browser->privacy.records_history(tab->privacy())) {
                browser->history->record(uri, tab->title(), static_cast<std::int64_t>(std::time(nullptr)));
            }
        }
        if (tab && !tab->is_private() && browser->sites) {
            mustela::SiteDecision decision = mustela::resolve_site(browser->privacy, *browser->sites, tab->uri());
            if (decision.cookies_denied) deny_origin_cookies(*browser, tab->uri());
        }
    }
    refresh_chrome(*browser);
}

void on_view_load_failed(WebKitWebView*, WebKitLoadEvent, const char*, GError*, gpointer user_data) {
    Tab* tab = static_cast<Tab*>(user_data);
    tab->load.cancel();
}

void on_go_back(GtkButton*, gpointer) {
    Tab* tab = active_tab(*browser);
    if (tab && webkit_web_view_can_go_back(tab->view())) webkit_web_view_go_back(tab->view());
}

void on_go_forward(GtkButton*, gpointer) {
    Tab* tab = active_tab(*browser);
    if (tab && webkit_web_view_can_go_forward(tab->view())) webkit_web_view_go_forward(tab->view());
}

void on_reload(GtkButton*, gpointer) {
    Tab* tab = active_tab(*browser);
    if (tab) webkit_web_view_reload(tab->view());
}

void on_new_tab(GtkButton*, gpointer) {
    open_tab(*browser, browser->settings.homepage.c_str(), browser->privacy.default_private);
}

void on_private_toggled(GtkToggleButton*, gpointer) {
    if (!browser) return;
    bool active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(browser->private_toggle)) != FALSE;
    browser->privacy.default_private = active ? TabPrivacy::Private : TabPrivacy::Normal;
}

void on_url_activate(GtkEntry*, gpointer) {
    Tab* tab = active_tab(*browser);
    if (!tab) return;
    const gchar* text = gtk_entry_get_text(GTK_ENTRY(browser->url_entry));
    std::string url = mustela::normalize_url(text, browser->settings.search_template);
    if (!url.empty()) webkit_web_view_load_uri(tab->view(), url.c_str());
}

void on_switch_page(GtkNotebook*, GtkWidget*, guint, gpointer) {
    gtk_entry_set_progress_fraction(GTK_ENTRY(browser->url_entry), 0.0);
    browser->hover_link.clear();
    refresh_chrome(*browser);
    if (browser->find_bar && gtk_widget_get_visible(browser->find_bar)) {
        run_find(*browser, 0);
    }
}

void on_page_removed(GtkNotebook*, GtkWidget*, gpointer) {
    refresh_chrome(*browser);
}

void run_js_alert(Browser& b, const std::string& message) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(b.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "JavaScript — Mustela");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

bool run_js_confirm(Browser& b, const std::string& message) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(b.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "JavaScript — Mustela");
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

std::string run_js_prompt(Browser& b, const std::string& message, const std::string& default_text) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(b.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "%s", message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "JavaScript — Mustela");
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), default_text.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    gtk_widget_set_margin_bottom(entry, 6);
    GtkWidget* area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 4);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    std::string text = gtk_entry_get_text(GTK_ENTRY(entry));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_OK ? text : std::string();
}

gboolean on_script_dialog(WebKitWebView*, WebKitScriptDialog* dialog, gpointer) {
    Browser& b = *browser;
    const gchar* raw = webkit_script_dialog_get_message(dialog);
    std::string message = mustela::sanitize_dialog_text(raw ? raw : "");
    switch (webkit_script_dialog_get_dialog_type(dialog)) {
        case WEBKIT_SCRIPT_DIALOG_ALERT:
            run_js_alert(b, message);
            return TRUE;
        case WEBKIT_SCRIPT_DIALOG_CONFIRM:
        case WEBKIT_SCRIPT_DIALOG_BEFORE_UNLOAD_CONFIRM:
            webkit_script_dialog_confirm_set_confirmed(dialog, run_js_confirm(b, message) ? TRUE : FALSE);
            return TRUE;
        case WEBKIT_SCRIPT_DIALOG_PROMPT: {
            const gchar* default_text = webkit_script_dialog_prompt_get_default_text(dialog);
            std::string text = run_js_prompt(b, message, default_text ? default_text : "");
            webkit_script_dialog_prompt_set_text(dialog, text.c_str());
            return TRUE;
        }
        default:
            return FALSE;
    }
}

GtkWidget* on_view_create(WebKitWebView* view, WebKitNavigationAction* action, gpointer) {
    Browser& b = *browser;
    Tab* origin = tab_from_view(b, view);
    bool priv = origin ? origin->is_private() : b.privacy.default_private == TabPrivacy::Private;
    WebKitWebView* related = WEBKIT_WEB_VIEW(webkit_web_view_new_with_related_view(view));
    harden_view(related, b.privacy.hardening);
    WebKitURIRequest* request = webkit_navigation_action_get_request(action);
    const gchar* target = request ? webkit_uri_request_get_uri(request) : nullptr;
    apply_view_policy(b, related, target ? target : "");

    Tab* tab = new Tab(related, priv);
    tab->set_label(build_tab_label(b, tab));
    b.tabs.emplace_back(tab);
    gtk_notebook_append_page(GTK_NOTEBOOK(b.notebook), GTK_WIDGET(related), tab->label());
    connect_tab_signals(tab);
    return GTK_WIDGET(related);
}

void on_view_ready_to_show(WebKitWebView* view, gpointer) {
    Browser& b = *browser;
    Tab* tab = tab_from_view(b, view);
    if (!tab) return;
    gint idx = tab_index(b, tab);
    if (idx >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(b.notebook), idx);
    refresh_chrome(b);
}

void on_view_close(WebKitWebView*, gpointer user_data) {
    close_tab(*browser, static_cast<Tab*>(user_data));
}

void on_view_mouse_target_changed(WebKitWebView*, WebKitHitTestResult* hit_test, guint, gpointer user_data) {
    Browser& b = *browser;
    Tab* tab = static_cast<Tab*>(user_data);
    if (active_tab(b) != tab) return;
    const gchar* link = webkit_hit_test_result_get_link_uri(hit_test);
    std::string next = link ? link : "";
    if (next == b.hover_link) return;
    b.hover_link = next;
    refresh_status(b);
}

void on_find_found_text(WebKitFindController*, guint match_count, gpointer user_data) {
    Browser& b = *browser;
    Tab* tab = static_cast<Tab*>(user_data);
    if (active_tab(b) != tab || !b.find_label) return;
    std::string text = std::to_string(match_count) + (match_count == 1 ? " match" : " matches");
    gtk_label_set_text(GTK_LABEL(b.find_label), text.c_str());
}

void on_find_failed(WebKitFindController*, gpointer user_data) {
    Browser& b = *browser;
    Tab* tab = static_cast<Tab*>(user_data);
    if (active_tab(b) != tab || !b.find_label) return;
    gtk_label_set_text(GTK_LABEL(b.find_label), "no matches");
}

void run_find(Browser& b, int direction) {
    Tab* tab = active_tab(b);
    if (!tab || !b.find_entry) return;
    const gchar* raw = gtk_entry_get_text(GTK_ENTRY(b.find_entry));
    std::string query = mustela::sanitize_find_query(raw ? raw : "");
    WebKitFindController* finder = webkit_web_view_get_find_controller(tab->view());
    if (query.empty()) {
        webkit_find_controller_search_finish(finder);
        if (b.find_label) gtk_label_set_text(GTK_LABEL(b.find_label), "");
        return;
    }
    if (direction == 0 || query != b.find_query) {
        b.find_query = query;
        WebKitFindOptions options = static_cast<WebKitFindOptions>(
            WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND);
        webkit_find_controller_search(finder, query.c_str(), options, 1000);
    } else if (direction > 0) {
        webkit_find_controller_search_next(finder);
    } else {
        webkit_find_controller_search_previous(finder);
    }
}

void show_find_bar(Browser& b, bool show) {
    if (!b.find_bar) return;
    if (show) {
        gtk_widget_show_all(b.find_bar);
        gtk_widget_grab_focus(b.find_entry);
        return;
    }
    b.find_query.clear();
    if (b.find_label) gtk_label_set_text(GTK_LABEL(b.find_label), "");
    Tab* tab = active_tab(b);
    if (tab) webkit_find_controller_search_finish(webkit_web_view_get_find_controller(tab->view()));
    gtk_widget_hide(b.find_bar);
    if (tab) gtk_widget_grab_focus(GTK_WIDGET(tab->view()));
}

void on_find_search_changed(GtkWidget*, gpointer) {
    run_find(*browser, 0);
}

void on_find_next_clicked(GtkButton*, gpointer) {
    run_find(*browser, 1);
}

void on_find_prev_clicked(GtkButton*, gpointer) {
    run_find(*browser, -1);
}

void on_find_close_clicked(GtkButton*, gpointer) {
    show_find_bar(*browser, FALSE);
}

gboolean on_find_entry_key(GtkWidget*, GdkEventKey* event, gpointer) {
    Browser& b = *browser;
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        run_find(b, (event->state & GDK_SHIFT_MASK) ? -1 : 1);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape) {
        show_find_bar(b, FALSE);
        return TRUE;
    }
    return FALSE;
}

void connect_tab_signals(Tab* tab) {
    g_signal_connect(tab->view(), "notify::uri", G_CALLBACK(on_view_uri_changed), tab);
    g_signal_connect(tab->view(), "notify::title", G_CALLBACK(on_view_title_changed), tab);
    g_signal_connect(tab->view(), "notify::estimated-load-progress", G_CALLBACK(on_view_progress_changed), tab);
    g_signal_connect(tab->view(), "load-changed", G_CALLBACK(on_view_load_changed), tab);
    g_signal_connect(tab->view(), "load-failed", G_CALLBACK(on_view_load_failed), tab);
    g_signal_connect(tab->view(), "create", G_CALLBACK(on_view_create), nullptr);
    g_signal_connect(tab->view(), "ready-to-show", G_CALLBACK(on_view_ready_to_show), nullptr);
    g_signal_connect(tab->view(), "close", G_CALLBACK(on_view_close), tab);
    g_signal_connect(tab->view(), "script-dialog", G_CALLBACK(on_script_dialog), nullptr);
    g_signal_connect(tab->view(), "mouse-target-changed", G_CALLBACK(on_view_mouse_target_changed), tab);
    WebKitFindController* finder = webkit_web_view_get_find_controller(tab->view());
    g_signal_connect(finder, "found-text", G_CALLBACK(on_find_found_text), tab);
    g_signal_connect(finder, "failed-to-find-text", G_CALLBACK(on_find_failed), tab);
}

void on_close_tab_clicked(GtkButton*, gpointer user_data) {
    close_tab(*browser, static_cast<Tab*>(user_data));
}

void close_tab(Browser& b, Tab* tab) {
    gint idx = tab_index(b, tab);
    if (idx < 0) return;
    gtk_notebook_remove_page(GTK_NOTEBOOK(b.notebook), idx);
    b.tabs.erase(b.tabs.begin() + idx);
    if (b.tabs.empty()) {
        open_tab(b, b.settings.homepage.c_str(), b.privacy.default_private);
        return;
    }
    refresh_chrome(b);
}

GtkWidget* build_tab_label(Browser&, Tab* tab) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* label = gtk_label_new(tab_label_text(*tab).c_str());
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(label), 18);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    gtk_widget_set_valign(close_btn, GTK_ALIGN_CENTER);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_tab_clicked), tab);

    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    return box;
}

Tab* open_tab(Browser& b, const char* url, TabPrivacy request) {
    bool priv = b.privacy.tab_privacy(request) == TabPrivacy::Private;
    WebKitWebContext* context = priv ? b.private_context : b.normal_context;
    WebKitWebView* view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));
    harden_view(view, b.privacy.hardening);
    apply_view_policy(b, view, url ? url : "");
    Tab* tab = new Tab(view, priv);
    tab->set_label(build_tab_label(b, tab));
    b.tabs.emplace_back(tab);

    gint idx = gtk_notebook_append_page(GTK_NOTEBOOK(b.notebook), GTK_WIDGET(view), tab->label());
    gtk_notebook_set_current_page(GTK_NOTEBOOK(b.notebook), idx);
    gtk_widget_show(GTK_WIDGET(view));

    connect_tab_signals(tab);

    if (url && *url) webkit_web_view_load_uri(view, url);
    refresh_chrome(b);
    return tab;
}

enum HistoryCols {
    COL_TIME,
    COL_TITLE,
    COL_URL
};

std::string format_time(std::int64_t seconds) {
    GDateTime* dt = g_date_time_new_from_unix_local(seconds);
    if (!dt) return "";
    gchar* text = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    std::string out = text ? text : "";
    g_date_time_unref(dt);
    g_free(text);
    return out;
}

void on_history_destroy(GtkWidget*, gpointer) {
    if (browser) browser->history_window = nullptr;
}

void on_history_row_activated(GtkTreeView* tree, GtkTreePath* path, GtkTreeViewColumn*, gpointer) {
    GtkTreeModel* model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model, &iter, path)) return;
    gchar* url = nullptr;
    gtk_tree_model_get(model, &iter, COL_URL, &url, -1);
    if (url && *url) open_tab(*browser, url, browser->privacy.default_private);
    g_free(url);
}

void on_history_clear(GtkButton*, gpointer user_data) {
    if (browser->history) browser->history->clear();
    GtkTreeView* tree = GTK_TREE_VIEW(user_data);
    GtkListStore* store = GTK_LIST_STORE(gtk_tree_view_get_model(tree));
    gtk_list_store_clear(store);
}

void open_history_window(Browser& b) {
    if (b.history_window) {
        gtk_window_present(GTK_WINDOW(b.history_window));
        return;
    }

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    b.history_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "History — Mustela");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 480);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(b.window));
    g_signal_connect(window, "destroy", G_CALLBACK(on_history_destroy), nullptr);

    GtkListStore* store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    if (b.history) {
        for (const mustela::HistoryEntry& entry : b.history->recent(500)) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter,
                COL_TIME, format_time(entry.visited_at).c_str(),
                COL_TITLE, entry.title.c_str(),
                COL_URL, entry.url.c_str(), -1);
        }
    }

    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "When", renderer, "text", COL_TIME, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Title", renderer, "text", COL_TITLE, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "URL", renderer, "text", COL_URL, nullptr);
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), COL_TITLE), TRUE);
    g_signal_connect(tree, "row-activated", G_CALLBACK(on_history_row_activated), nullptr);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    GtkWidget* clear_btn = gtk_button_new_with_label("Clear history");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_history_clear), tree);

    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_end(GTK_BOX(bottom), clear_btn, FALSE, FALSE, 0);

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(layout), 4);
    gtk_box_pack_start(GTK_BOX(layout), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(layout), bottom, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), layout);

    gtk_widget_show_all(window);
}

enum RuleCols {
    COL_HOST,
    COL_JS,
    COL_COOKIES,
    COL_FP
};

enum RuleAction {
    RULE_JS_ALLOW,
    RULE_JS_DENY,
    RULE_JS_DEFAULT,
    RULE_COOKIES_BLOCK,
    RULE_COOKIES_DEFAULT,
    RULE_FP_EXEMPT,
    RULE_FP_STRICT,
    RULE_FP_DEFAULT,
    RULE_REMOVE
};

void reload_rules_model(GtkListStore* store) {
    gtk_list_store_clear(store);
    if (!browser || !browser->sites) return;
    for (const mustela::SiteRules::Entry& entry : browser->sites->entries()) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
            COL_HOST, entry.host.c_str(),
            COL_JS, mustela::site_rule_name(entry.policy.javascript),
            COL_COOKIES, mustela::site_rule_name(entry.policy.cookies),
            COL_FP, mustela::site_rule_name(entry.policy.fingerprint), -1);
    }
}

std::string current_or_selected_host() {
    if (browser->rules_tree) {
        GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->rules_tree));
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            gchar* host = nullptr;
            gtk_tree_model_get(model, &iter, COL_HOST, &host, -1);
            std::string out = host ? host : "";
            g_free(host);
            if (!out.empty()) return out;
        }
    }
    Tab* tab = active_tab(*browser);
    return tab ? mustela::host_from_uri(tab->uri()) : "";
}

void on_rule_action(GtkButton*, gpointer user_data) {
    if (!browser || !browser->sites) return;
    RuleAction action = static_cast<RuleAction>(reinterpret_cast<std::intptr_t>(user_data));
    Browser& b = *browser;
    std::string host = current_or_selected_host();
    if (host.empty()) return;
    if (action == RULE_REMOVE) {
        b.sites->remove(host);
    } else {
        const mustela::SitePolicy* existing = b.sites->find(host);
        mustela::SitePolicy policy = existing ? *existing : mustela::SitePolicy{};
        switch (action) {
            case RULE_JS_ALLOW: policy.javascript = mustela::SiteRule::Allow; break;
            case RULE_JS_DENY: policy.javascript = mustela::SiteRule::Deny; break;
            case RULE_JS_DEFAULT: policy.javascript = mustela::SiteRule::Default; break;
            case RULE_COOKIES_BLOCK: policy.cookies = mustela::SiteRule::Deny; break;
            case RULE_COOKIES_DEFAULT: policy.cookies = mustela::SiteRule::Default; break;
            case RULE_FP_EXEMPT: policy.fingerprint = mustela::SiteRule::Allow; break;
            case RULE_FP_STRICT: policy.fingerprint = mustela::SiteRule::Deny; break;
            case RULE_FP_DEFAULT: policy.fingerprint = mustela::SiteRule::Default; break;
            default: break;
        }
        b.sites->set(host, policy);
    }
    b.sites->save();
    if (b.rules_store) reload_rules_model(b.rules_store);
    apply_policies_to_tabs(b);
}

void on_rules_window_destroy(GtkWidget*, gpointer) {
    if (browser) {
        browser->rules_window = nullptr;
        browser->rules_tree = nullptr;
        browser->rules_store = nullptr;
    }
}

void open_site_rules_window(Browser& b) {
    if (b.rules_window) {
        gtk_window_present(GTK_WINDOW(b.rules_window));
        return;
    }

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    b.rules_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Site rules — Mustela");
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 420);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(b.window));
    g_signal_connect(window, "destroy", G_CALLBACK(on_rules_window_destroy), nullptr);

    GtkListStore* store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    b.rules_store = store;
    reload_rules_model(store);

    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    b.rules_tree = tree;
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), GTK_SELECTION_SINGLE);

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Host", renderer, "text", COL_HOST, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "JavaScript", renderer, "text", COL_JS, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Cookies", renderer, "text", COL_COOKIES, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Fingerprint", renderer, "text", COL_FP, nullptr);
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), COL_HOST), TRUE);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    struct RuleButton {
        const char* label;
        RuleAction action;
    };
    const RuleButton buttons[] = {
        {"JS allow", RULE_JS_ALLOW},
        {"JS deny", RULE_JS_DENY},
        {"JS default", RULE_JS_DEFAULT},
        {"Cookies block", RULE_COOKIES_BLOCK},
        {"Cookies default", RULE_COOKIES_DEFAULT},
        {"Fingerprint exempt", RULE_FP_EXEMPT},
        {"Fingerprint strict", RULE_FP_STRICT},
        {"Fingerprint default", RULE_FP_DEFAULT},
        {"Remove", RULE_REMOVE}
    };

    GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* rows[2] = {
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4),
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4)
    };
    for (size_t i = 0; i < G_N_ELEMENTS(buttons); ++i) {
        GtkWidget* btn = gtk_button_new_with_label(buttons[i].label);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_rule_action),
            reinterpret_cast<gpointer>(static_cast<std::intptr_t>(buttons[i].action)));
        gtk_box_pack_start(GTK_BOX(rows[i < 5 ? 0 : 1]), btn, FALSE, FALSE, 0);
    }

    GtkWidget* hint = gtk_label_new("no selection = current site");
    gtk_label_set_xalign(GTK_LABEL(hint), 1.0f);
    gtk_box_pack_end(GTK_BOX(rows[1]), hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), rows[0], FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), rows[1], FALSE, FALSE, 0);

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(layout), 4);
    gtk_box_pack_start(GTK_BOX(layout), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(layout), controls, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), layout);

    gtk_widget_show_all(window);
}

gboolean do_settings_reload(gpointer) {
    if (!browser) return G_SOURCE_REMOVE;
    Browser& b = *browser;
    b.settings_reload_pending = 0;

    gchar* path = g_build_filename(g_get_user_config_dir(), "mustela", "settings.toml", nullptr);
    b.settings = mustela::load_settings(path);
    g_free(path);
    b.privacy = mustela::privacy_from(b.settings);

    apply_cookie_policy(b.normal_context, b.privacy.cookies);
    WebKitWebsiteDataManager* manager = webkit_web_context_get_website_data_manager(b.normal_context);
    webkit_website_data_manager_set_itp_enabled(manager, b.privacy.hardening.tracking_prevention ? TRUE : FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b.private_toggle), b.privacy.default_private == TabPrivacy::Private);

    if (b.privacy.history_enabled && !b.history) {
        gchar* profile_dir = g_build_filename(g_get_user_data_dir(), "mustela", "profiles", "default", nullptr);
        gchar* history_path = g_build_filename(profile_dir, "history.dat", nullptr);
        b.history = std::make_unique<mustela::HistoryStore>(history_path);
        g_free(history_path);
        g_free(profile_dir);
    }

    apply_policies_to_tabs(b);
    flash_status("settings reloaded");
    return G_SOURCE_REMOVE;
}

void schedule_settings_reload() {
    if (!browser || browser->settings_reload_pending) return;
    browser->settings_reload_pending = g_timeout_add(300, do_settings_reload, nullptr);
}

void on_settings_file_changed(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer) {
    schedule_settings_reload();
}

gboolean do_sites_reload(gpointer) {
    if (!browser) return G_SOURCE_REMOVE;
    Browser& b = *browser;
    b.sites_reload_pending = 0;

    b.sites->load();
    apply_policies_to_tabs(b);
    if (b.rules_store) reload_rules_model(b.rules_store);
    flash_status("site rules reloaded");
    return G_SOURCE_REMOVE;
}

void schedule_sites_reload() {
    if (!browser || browser->sites_reload_pending) return;
    browser->sites_reload_pending = g_timeout_add(300, do_sites_reload, nullptr);
}

void on_sites_file_changed(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer) {
    schedule_sites_reload();
}

GFileMonitor* monitor_file(const gchar* path, GCallback changed) {
    GFile* file = g_file_new_for_path(path);
    GFileMonitor* monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, nullptr, nullptr);
    g_object_unref(file);
    if (monitor) g_signal_connect(monitor, "changed", changed, nullptr);
    return monitor;
}

}

int mustela_main(int argc, char** argv) {
    Browser instance;
    browser = &instance;
    browser->startup_entry_ms = mustela::Stopwatch::now_ms();

    gtk_init(&argc, &argv);

    gchar* config_dir = g_build_filename(g_get_user_config_dir(), "mustela", nullptr);
    g_mkdir_with_parents(config_dir, 0700);
    gchar* config_path = g_build_filename(config_dir, "settings.toml", nullptr);
    browser->settings = mustela::load_settings(config_path);
    if (!g_file_test(config_path, G_FILE_TEST_EXISTS)) {
        mustela::save_settings(config_path, browser->settings);
    }
    browser->privacy = mustela::privacy_from(browser->settings);

    gchar* sites_path = g_build_filename(config_dir, "sites.toml", nullptr);
    if (!g_file_test(sites_path, G_FILE_TEST_EXISTS)) {
        mustela::SiteRules initial(sites_path);
        initial.save();
    }
    browser->sites = std::make_unique<mustela::SiteRules>(sites_path);
    browser->sites->load();
    browser->settings_monitor = monitor_file(config_path, G_CALLBACK(on_settings_file_changed));
    browser->sites_monitor = monitor_file(sites_path, G_CALLBACK(on_sites_file_changed));
    g_free(sites_path);

    gchar* profile_dir = g_build_filename(g_get_user_data_dir(), "mustela", "profiles", "default", nullptr);
    g_mkdir_with_parents(profile_dir, 0700);
    browser->perf = std::make_unique<mustela::PerfLog>(std::string(profile_dir) + "/perf.jsonl");

    if (browser->privacy.history_enabled) {
        gchar* history_path = g_build_filename(profile_dir, "history.dat", nullptr);
        browser->history = std::make_unique<mustela::HistoryStore>(history_path);
        g_free(history_path);
    }

    gchar* filters_dir = g_build_filename(profile_dir, "filters", nullptr);
    g_mkdir_with_parents(filters_dir, 0700);
    browser->filter_store = webkit_user_content_filter_store_new(filters_dir);
    g_free(filters_dir);
    static const char fonts_filter_json[] =
        "[{\"trigger\":{\"url-filter\":\".*\",\"resource-type\":[\"font\"]},\"action\":{\"type\":\"block\"}}]";
    GBytes* fonts_source = g_bytes_new_static(fonts_filter_json, sizeof(fonts_filter_json) - 1);
    webkit_user_content_filter_store_save(browser->filter_store, "block-remote-fonts", fonts_source,
        nullptr, on_fonts_filter_saved, nullptr);
    g_bytes_unref(fonts_source);

    WebKitWebsiteDataManager* data_manager =
        webkit_website_data_manager_new("base-data-directory", profile_dir, nullptr);
    webkit_website_data_manager_set_itp_enabled(data_manager,
        browser->privacy.hardening.tracking_prevention ? TRUE : FALSE);
    browser->normal_context = webkit_web_context_new_with_website_data_manager(data_manager);
    g_object_unref(data_manager);
    browser->private_context = webkit_web_context_new_ephemeral();
    apply_process_model(browser->normal_context, browser->settings.process_model);
    apply_process_model(browser->private_context, browser->settings.process_model);
    apply_cookie_policy(browser->normal_context, browser->privacy.cookies);
    apply_cookie_policy(browser->private_context, browser->privacy.cookies);

    const char* url = nullptr;
    bool start_private = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--private") == 0) {
            start_private = true;
        } else {
            url = argv[i];
        }
    }

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    browser->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Mustela");
    gtk_window_set_default_size(GTK_WINDOW(window), browser->settings.window_width, browser->settings.window_height);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), nullptr);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), nullptr);
    g_signal_connect(window, "map-event", G_CALLBACK(on_window_mapped), nullptr);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 2);

    browser->back_btn = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    browser->forward_btn = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON);
    browser->reload_btn = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    browser->private_toggle = gtk_toggle_button_new_with_label("Private");
    gtk_widget_set_tooltip_text(browser->private_toggle, "New tabs open in private mode");
    browser->url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(browser->url_entry), "Search or enter address");
    browser->status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(browser->status_label), 1.0f);
    gtk_widget_set_margin_start(browser->status_label, 6);
    gtk_widget_set_margin_end(browser->status_label, 6);
    gtk_widget_set_valign(browser->status_label, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(browser->status_label,
        "Cookie policy, tracking prevention and fingerprint budget. Ctrl+Shift+Del clears browsing data");
    GtkWidget* new_tab_btn = gtk_button_new_from_icon_name("tab-new-symbolic", GTK_ICON_SIZE_BUTTON);

    GtkWidget* toolbar_buttons[] = {browser->back_btn, browser->forward_btn, browser->reload_btn, new_tab_btn};
    for (GtkWidget* btn : toolbar_buttons) {
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        gtk_widget_set_focus_on_click(btn, FALSE);
    }
    gtk_button_set_relief(GTK_BUTTON(browser->private_toggle), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(browser->private_toggle, FALSE);
    gtk_widget_set_hexpand(browser->url_entry, TRUE);

    gtk_box_pack_start(GTK_BOX(toolbar), browser->back_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), browser->forward_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), browser->reload_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), browser->private_toggle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), browser->url_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), new_tab_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), browser->status_label, FALSE, FALSE, 0);

    browser->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(browser->notebook), TRUE);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(browser->notebook), FALSE);

    browser->find_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(browser->find_bar), 2);
    browser->find_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(browser->find_entry), "Find in page");
    gtk_widget_set_hexpand(browser->find_entry, TRUE);
    browser->find_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(browser->find_label), 1.0f);
    GtkWidget* find_prev_btn = gtk_button_new_from_icon_name("go-up-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget* find_next_btn = gtk_button_new_from_icon_name("go-down-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget* find_close_btn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget* find_buttons[] = {find_prev_btn, find_next_btn, find_close_btn};
    for (GtkWidget* btn : find_buttons) {
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        gtk_widget_set_focus_on_click(btn, FALSE);
    }
    gtk_widget_set_tooltip_text(find_prev_btn, "Previous match (Shift+Enter)");
    gtk_widget_set_tooltip_text(find_next_btn, "Next match (Enter)");
    gtk_box_pack_start(GTK_BOX(browser->find_bar), browser->find_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(browser->find_bar), browser->find_label, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(browser->find_bar), find_prev_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(browser->find_bar), find_next_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(browser->find_bar), find_close_btn, FALSE, FALSE, 0);
    g_signal_connect(browser->find_entry, "search-changed", G_CALLBACK(on_find_search_changed), nullptr);
    g_signal_connect(browser->find_entry, "key-press-event", G_CALLBACK(on_find_entry_key), nullptr);
    g_signal_connect(find_prev_btn, "clicked", G_CALLBACK(on_find_prev_clicked), nullptr);
    g_signal_connect(find_next_btn, "clicked", G_CALLBACK(on_find_next_clicked), nullptr);
    g_signal_connect(find_close_btn, "clicked", G_CALLBACK(on_find_close_clicked), nullptr);

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(layout), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(layout), browser->find_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(layout), browser->notebook, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), layout);

    g_signal_connect(browser->back_btn, "clicked", G_CALLBACK(on_go_back), nullptr);
    g_signal_connect(browser->forward_btn, "clicked", G_CALLBACK(on_go_forward), nullptr);
    g_signal_connect(browser->reload_btn, "clicked", G_CALLBACK(on_reload), nullptr);
    g_signal_connect(browser->private_toggle, "toggled", G_CALLBACK(on_private_toggled), nullptr);
    g_signal_connect(new_tab_btn, "clicked", G_CALLBACK(on_new_tab), nullptr);
    g_signal_connect(browser->url_entry, "activate", G_CALLBACK(on_url_activate), nullptr);
    g_signal_connect(browser->notebook, "switch-page", G_CALLBACK(on_switch_page), nullptr);
    g_signal_connect(browser->notebook, "page-removed", G_CALLBACK(on_page_removed), nullptr);

    TabPrivacy request = start_private ? TabPrivacy::Private : browser->privacy.default_private;
    if (request == TabPrivacy::Private) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(browser->private_toggle), TRUE);
    }
    open_tab(*browser, url && *url ? url : browser->settings.homepage.c_str(), request);

    gtk_widget_show_all(window);
    gtk_widget_hide(browser->find_bar);
    sample_memory(*browser);
    g_timeout_add_seconds(60, on_memory_timer, nullptr);
    gtk_main();

    g_free(config_dir);
    g_free(config_path);
    g_free(profile_dir);
    browser = nullptr;
    return 0;
}
