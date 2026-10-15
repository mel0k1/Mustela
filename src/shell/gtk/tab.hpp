#pragma once

#include <gtk/gtk.h>
#include <string>

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
    bool is_private() const { return private_; }
    TabPrivacy privacy() const { return private_ ? TabPrivacy::Private : TabPrivacy::Normal; }

    std::string uri() const;
    std::string title() const;

private:
    WebKitWebView* view_ = nullptr;
    GtkWidget* label_ = nullptr;
    bool private_ = false;
};

}
