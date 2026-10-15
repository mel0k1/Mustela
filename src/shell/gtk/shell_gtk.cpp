#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <glib/gstdio.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "privacy.hpp"
#include "tab.hpp"

int mustela_main(int argc, char** argv);

namespace {

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
    WebKitWebContext* normal_context = nullptr;
    WebKitWebContext* private_context = nullptr;
    std::vector<std::unique_ptr<Tab>> tabs;
};

Browser* browser = nullptr;

Tab* open_tab(Browser& b, const char* url, TabPrivacy request);
void close_tab(Browser& b, Tab* tab);
GtkWidget* build_tab_label(Browser& b, Tab* tab);
void on_close_tab_clicked(GtkButton*, gpointer user_data);

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
    if (mods == GDK_CONTROL_MASK && event->keyval == GDK_KEY_w) {
        if (tab) close_tab(b, tab);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && event->keyval == GDK_KEY_l) {
        gtk_widget_grab_focus(b.url_entry);
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

void on_view_load_changed(WebKitWebView*, WebKitLoadEvent event, gpointer) {
    if (event == WEBKIT_LOAD_FINISHED) {
        gtk_entry_set_progress_fraction(GTK_ENTRY(browser->url_entry), 0.0);
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

    WebKitWebsiteDataManager* data_manager =
        webkit_website_data_manager_new("base-data-directory", profile_dir, nullptr);
    browser->normal_context = webkit_web_context_new_with_website_data_manager(data_manager);
    g_object_unref(data_manager);
    browser->private_context = webkit_web_context_new_ephemeral();

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
    browser->url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(browser->url_entry), "Search or enter address");
    GtkWidget* new_tab_btn = gtk_button_new_from_icon_name("tab-new-symbolic", GTK_ICON_SIZE_BUTTON);

    GtkWidget* toolbar_buttons[] = {browser->back_btn, browser->forward_btn, browser->reload_btn, new_tab_btn};
    for (GtkWidget* btn : toolbar_buttons) {
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        gtk_widget_set_focus_on_click(btn, FALSE);
    }
    gtk_widget_set_hexpand(browser->url_entry, TRUE);

    gtk_box_pack_start(GTK_BOX(toolbar), browser->back_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), browser->forward_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), browser->reload_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), browser->url_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), new_tab_btn, FALSE, FALSE, 0);

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
    g_signal_connect(new_tab_btn, "clicked", G_CALLBACK(on_new_tab), nullptr);
    g_signal_connect(browser->url_entry, "activate", G_CALLBACK(on_url_activate), nullptr);
    g_signal_connect(browser->notebook, "switch-page", G_CALLBACK(on_switch_page), nullptr);
    g_signal_connect(browser->notebook, "page-removed", G_CALLBACK(on_page_removed), nullptr);

    TabPrivacy request = start_private ? TabPrivacy::Private : browser->privacy.default_private;
    open_tab(*browser, url && *url ? url : browser->settings.homepage.c_str(), request);

    gtk_widget_show_all(window);
    gtk_main();

    g_free(config_dir);
    g_free(config_path);
    g_free(profile_dir);
    browser = nullptr;
    return 0;
}
