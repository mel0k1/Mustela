#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <glib/gstdio.h>

#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "browsing_data.hpp"
#include "config.hpp"
#include "history.hpp"
#include "privacy.hpp"
#include "tab.hpp"

int mustela_main(int argc, char** argv);

namespace {

using mustela::CookiePolicy;
using mustela::DataKind;
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
    WebKitWebContext* normal_context = nullptr;
    WebKitWebContext* private_context = nullptr;
    std::unique_ptr<mustela::HistoryStore> history;
    std::vector<std::unique_ptr<Tab>> tabs;
};

Browser* browser = nullptr;

Tab* open_tab(Browser& b, const char* url, TabPrivacy request);
void close_tab(Browser& b, Tab* tab);
GtkWidget* build_tab_label(Browser& b, Tab* tab);
void on_close_tab_clicked(GtkButton*, gpointer user_data);
void open_history_window(Browser& b);
void clear_browsing_data(Browser& b);

std::string trim_copy(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
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

std::string tab_label_text(const Tab& tab) {
    std::string text = tab.title();
    if (tab.is_private()) text = "private · " + text;
    return text;
}

void update_tab_label(Browser& b, Tab* tab) {
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
    Tab* tab = active_tab(b);
    std::string text;
    if (tab && tab->is_private()) text = "private · ";
    text += std::string("cookies ") + mustela::cookie_policy_name(b.privacy.cookies);
    if (b.privacy.hardening.tracking_prevention) text += " · itp";
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
    if (mustela::has(kinds, DataKind::IndexedDB)) types |= WEBKIT_WEBSITE_DATA_INDEXEDDB;
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

std::string normalize_url(const std::string& input, const std::string& search_template) {
    std::string text = trim_copy(input);
    if (text.empty()) return "";
    if (text.find("://") != std::string::npos) return text;

    bool host_like = text.rfind("localhost", 0) == 0 ||
                     (text.find('.') != std::string::npos && text.find(' ') == std::string::npos);
    if (host_like) return "https://" + text;

    gchar* escaped = g_uri_escape_string(text.c_str(), nullptr, TRUE);
    std::string query = escaped ? escaped : "";
    g_free(escaped);

    size_t slot = search_template.find("%s");
    if (slot == std::string::npos) return search_template;
    return search_template.substr(0, slot) + query + search_template.substr(slot + 2);
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
    if (mods == 0 && event->keyval == GDK_KEY_Escape) {
        if (tab) webkit_web_view_stop_loading(tab->view());
        return TRUE;
    }
    return FALSE;
}

void on_view_uri_changed(GObject* object, GParamSpec*, gpointer user_data) {
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
    if (event == WEBKIT_LOAD_FINISHED) {
        gtk_entry_set_progress_fraction(GTK_ENTRY(browser->url_entry), 0.0);
        if (tab && browser->history) {
            std::string uri = tab->uri();
            if (!uri.empty() && browser->privacy.records_history(tab->privacy())) {
                browser->history->record(uri, tab->title(), static_cast<std::int64_t>(std::time(nullptr)));
            }
        }
    }
    refresh_chrome(*browser);
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
    std::string url = normalize_url(text, browser->settings.search_template);
    if (!url.empty()) webkit_web_view_load_uri(tab->view(), url.c_str());
}

void on_switch_page(GtkNotebook*, GtkWidget*, guint, gpointer) {
    gtk_entry_set_progress_fraction(GTK_ENTRY(browser->url_entry), 0.0);
    refresh_chrome(*browser);
}

void on_page_removed(GtkNotebook*, GtkWidget*, gpointer) {
    refresh_chrome(*browser);
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

GtkWidget* build_tab_label(Browser& b, Tab* tab) {
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
    Tab* tab = new Tab(view, priv);
    tab->set_label(build_tab_label(b, tab));
    b.tabs.emplace_back(tab);

    gint idx = gtk_notebook_append_page(GTK_NOTEBOOK(b.notebook), GTK_WIDGET(view), tab->label());
    gtk_notebook_set_current_page(GTK_NOTEBOOK(b.notebook), idx);
    gtk_widget_show(GTK_WIDGET(view));

    g_signal_connect(view, "notify::uri", G_CALLBACK(on_view_uri_changed), tab);
    g_signal_connect(view, "notify::title", G_CALLBACK(on_view_title_changed), tab);
    g_signal_connect(view, "notify::estimated-load-progress", G_CALLBACK(on_view_progress_changed), tab);
    g_signal_connect(view, "load-changed", G_CALLBACK(on_view_load_changed), tab);

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

}

int mustela_main(int argc, char** argv) {
    gtk_init(&argc, &argv);

    Browser instance;
    browser = &instance;

    gchar* config_dir = g_build_filename(g_get_user_config_dir(), "mustela", nullptr);
    g_mkdir_with_parents(config_dir, 0700);
    gchar* config_path = g_build_filename(config_dir, "settings.toml", nullptr);
    browser->settings = mustela::load_settings(config_path);
    if (!g_file_test(config_path, G_FILE_TEST_EXISTS)) {
        mustela::save_settings(config_path, browser->settings);
    }
    browser->privacy = mustela::privacy_from(browser->settings);

    gchar* profile_dir = g_build_filename(g_get_user_data_dir(), "mustela", "profiles", "default", nullptr);
    g_mkdir_with_parents(profile_dir, 0700);

    if (browser->privacy.history_enabled) {
        gchar* history_path = g_build_filename(profile_dir, "history.dat", nullptr);
        browser->history = std::make_unique<mustela::HistoryStore>(history_path);
        g_free(history_path);
    }

    WebKitWebsiteDataManager* data_manager =
        webkit_website_data_manager_new("base-data-directory", profile_dir, nullptr);
    webkit_website_data_manager_set_itp_enabled(data_manager,
        browser->privacy.hardening.tracking_prevention ? TRUE : FALSE);
    browser->normal_context = webkit_web_context_new_with_website_data_manager(data_manager);
    g_object_unref(data_manager);
    browser->private_context = webkit_web_context_new_ephemeral();
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
        "Cookie policy and tracking prevention. Ctrl+Shift+Del clears browsing data");
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

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(layout), toolbar, FALSE, FALSE, 0);
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
    gtk_main();

    g_free(config_dir);
    g_free(config_path);
    g_free(profile_dir);
    browser = nullptr;
    return 0;
}
