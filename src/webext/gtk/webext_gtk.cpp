#include <webkit2/webkit-web-extension.h>

static gboolean on_send_request(WebKitWebPage*, WebKitURIRequest* request, WebKitURIResponse*,
    gpointer user_data) {
    if (!user_data) return FALSE;
    SoupMessageHeaders* headers = webkit_uri_request_get_http_headers(request);
    if (headers) {
        soup_message_headers_append(headers, "Sec-GPC", "1");
    }
    return FALSE;
}

static void on_page_created(WebKitWebExtension*, WebKitWebPage* page, gpointer user_data) {
    g_signal_connect(page, "send-request", G_CALLBACK(on_send_request), user_data);
}

extern "C" G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(
    WebKitWebExtension* extension, const GVariant* user_data) {
    GVariant* data = const_cast<GVariant*>(user_data);
    gboolean gpc = data && g_variant_is_of_type(data, G_VARIANT_TYPE_BOOLEAN)
        ? g_variant_get_boolean(data)
        : FALSE;
    g_signal_connect(extension, "page-created", G_CALLBACK(on_page_created),
        gpc ? reinterpret_cast<gpointer>(extension) : nullptr);
}
