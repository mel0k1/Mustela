#pragma once

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>

#include "perf.hpp"
#include "privacy.hpp"

namespace mustela {

class Tab {
public:
    Tab(WebKitWebView* view, bool private_tab);
    ~Tab() = default;

    Tab(const Tab&) = delete;
    Tab& operator=(const Tab&) = delete;

    WebKitWebView* view() const { return view_; }
    GtkWidget* label() const { return label_; }
    void set_label(GtkWidget* label) { label_ = label; }
    GtkWidget* icon() const { return icon_; }
    void set_icon(GtkWidget* icon) { icon_ = icon; }
    bool is_private() const { return private_; }
    TabPrivacy privacy() const { return private_ ? TabPrivacy::Private : TabPrivacy::Normal; }
    int crashes = 0;
    LoadTimer load;
    bool error_page = false;
    std::string error_page_uri;
    std::string default_ua;

    std::string uri() const;
    std::string title() const;

private:
    WebKitWebView* view_ = nullptr;
    GtkWidget* label_ = nullptr;
    GtkWidget* icon_ = nullptr;
    bool private_ = false;
};

}
