#include "tab.hpp"

namespace mustela {

Tab::Tab(WebKitWebView* view, bool private_tab) : view_(view), private_(private_tab) {}

std::string Tab::uri() const {
    if (discarded) return discard_uri;
    const gchar* uri = webkit_web_view_get_uri(view_);
    return uri ? uri : "";
}

std::string Tab::title() const {
    if (discarded) return discard_title.empty() ? "Discarded tab" : discard_title;
    const gchar* title = webkit_web_view_get_title(view_);
    if (title && *title) return title;
    const gchar* uri = webkit_web_view_get_uri(view_);
    return (uri && *uri) ? uri : "New Tab";
}

}
