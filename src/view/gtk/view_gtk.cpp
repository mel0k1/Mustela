#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <jsc/jsc.h>
#include <libsoup/soup.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <cairo/cairo.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "mustela/view.hpp"

#include "blocklist.hpp"
#include "error_page.hpp"
#include "fingerprint_scripts.hpp"
#include "permissions.hpp"
#include "perf.hpp"
#include "privacy.hpp"
#include "proxy_server.hpp"
#include "ui_text.hpp"
#include "url.hpp"

#ifndef MUSTELA_WEBEXT_DIR
#define MUSTELA_WEBEXT_DIR ""
#endif

namespace mustela {

namespace {

void apply_cookie_policy(WebKitWebContext* context, CookiePolicy policy) {
    WebKitCookieManager* manager = webkit_web_context_get_cookie_manager(context);
    WebKitCookieAcceptPolicy accept = WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
    if (policy == CookiePolicy::Always) accept = WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
    if (policy == CookiePolicy::Never) accept = WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
    webkit_cookie_manager_set_accept_policy(manager, accept);
}

bool doh_transport_soup(const std::string& url, const std::vector<unsigned char>& query,
    std::vector<unsigned char>& answer) {
    static std::mutex transport_mutex;
    static SoupSession* session = soup_session_new();
    std::lock_guard<std::mutex> lock(transport_mutex);
    SoupMessage* message = soup_message_new(SOUP_METHOD_POST, url.c_str());
    if (!message) return false;
    GBytes* body = g_bytes_new(query.data(), query.size());
    soup_message_set_request_body_from_bytes(message, "application/dns-message", body);
    GError* error = nullptr;
    GInputStream* stream = soup_session_send(session, message, nullptr, &error);
    if (!stream) {
        if (error) g_error_free(error);
        g_object_unref(message);
        g_bytes_unref(body);
        return false;
    }
    bool ok = false;
    if (SOUP_STATUS_IS_SUCCESSFUL(soup_message_get_status(message))) {
        std::vector<unsigned char> data;
        char buffer[8192];
        gssize got = 0;
        while ((got = g_input_stream_read(stream, buffer, sizeof(buffer), nullptr, nullptr)) > 0) {
            data.insert(data.end(), buffer, buffer + got);
            if (data.size() > 65536) break;
        }
        if (got == 0 && !data.empty()) {
            answer = std::move(data);
            ok = true;
        }
    }
    g_object_unref(stream);
    g_object_unref(message);
    g_bytes_unref(body);
    return ok;
}

std::string download_path_collision(const std::string& dir, const std::string& name) {
    std::string path = dir + "/" + name;
    if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) return path;
    std::string base = name;
    std::string ext;
    size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot != 0) {
        base = name.substr(0, dot);
        ext = name.substr(dot);
    }
    for (int n = 2; n < 1000; ++n) {
        std::string candidate = dir + "/" + base + " (" + std::to_string(n) + ")" + ext;
        if (!g_file_test(candidate.c_str(), G_FILE_TEST_EXISTS)) return candidate;
    }
    return path;
}

void add_user_script(WebKitUserContentManager* manager, const std::string& source) {
    WebKitUserScript* script = webkit_user_script_new(source.c_str(),
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(manager, script);
    webkit_user_script_unref(script);
}

std::string stricter_permission(const std::string& first, const std::string& second) {
    if (first == "deny" || second == "deny") return "deny";
    if (first == "ask" || second == "ask") return "ask";
    return "allow";
}

}

struct ViewImpl;

struct ViewState {
    struct Download {
        WebKitDownload* download = nullptr;
        std::uint64_t id = 0;
        std::string path;
    };

    ViewConfig config;
    SessionCallbacks callbacks;
    WebKitWebContext* normal_context = nullptr;
    WebKitWebContext* private_context = nullptr;
    WebKitUserContentFilterStore* filter_store = nullptr;
    WebKitUserContentFilter* fonts_filter = nullptr;
    WebKitUserContentFilter* blocklist_filter = nullptr;
    std::unique_ptr<ProxyServer> proxy;
    std::map<WebKitDownload*, Download> downloads;
    std::uint64_t download_seq = 0;
    std::vector<ViewImpl*> views;

    ViewState(const ViewConfig& c, const SessionCallbacks& cb) : config(c), callbacks(cb) {}
    ~ViewState();

    WebKitWebContext* context_for(bool private_mode) const {
        return private_mode ? private_context : normal_context;
    }

    void build();
    void apply_context_policy();
    void compile_filters();
    void refresh_view_policies();

    std::string downloads_directory() const;
    void handle_download_started(WebKitDownload* download);
    void handle_download_progress(WebKitDownload* download);
    void handle_download_failed(WebKitDownload* download);
    void handle_download_finished(WebKitDownload* download);
};

void view_download_started(WebKitWebContext*, WebKitDownload* download, gpointer user_data) {
    static_cast<ViewState*>(user_data)->handle_download_started(download);
}

void view_download_progress(GObject* object, GParamSpec*, gpointer user_data) {
    static_cast<ViewState*>(user_data)->handle_download_progress(WEBKIT_DOWNLOAD(object));
}

void view_download_failed(WebKitDownload* download, GError*, gpointer user_data) {
    static_cast<ViewState*>(user_data)->handle_download_failed(download);
}

void view_download_finished(WebKitDownload* download, gpointer user_data) {
    static_cast<ViewState*>(user_data)->handle_download_finished(download);
}

void view_fonts_filter_saved(GObject* object, GAsyncResult* result, gpointer user_data) {
    GError* error = nullptr;
    WebKitUserContentFilter* filter = webkit_user_content_filter_store_save_finish(
        WEBKIT_USER_CONTENT_FILTER_STORE(object), result, &error);
    ViewState* state = static_cast<ViewState*>(user_data);
    if (error) {
        g_error_free(error);
        return;
    }
    if (!state) {
        webkit_user_content_filter_unref(filter);
        return;
    }
    if (state->fonts_filter) webkit_user_content_filter_unref(state->fonts_filter);
    state->fonts_filter = filter;
    state->refresh_view_policies();
}

void view_blocklist_filter_saved(GObject* object, GAsyncResult* result, gpointer user_data) {
    GError* error = nullptr;
    WebKitUserContentFilter* filter = webkit_user_content_filter_store_save_finish(
        WEBKIT_USER_CONTENT_FILTER_STORE(object), result, &error);
    ViewState* state = static_cast<ViewState*>(user_data);
    if (error) {
        g_error_free(error);
        return;
    }
    if (!state) {
        webkit_user_content_filter_unref(filter);
        return;
    }
    if (state->blocklist_filter) webkit_user_content_filter_unref(state->blocklist_filter);
    state->blocklist_filter = filter;
    state->refresh_view_policies();
}

std::string ViewState::downloads_directory() const {
    if (!config.downloads_dir.empty()) return config.downloads_dir;
    if (config.settings && !config.settings->downloads_dir.empty()) return config.settings->downloads_dir;
    const gchar* special = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (special && *special) return special;
    const gchar* home = g_get_home_dir();
    return home ? std::string(home) + "/Downloads" : std::string(".");
}

void ViewState::handle_download_started(WebKitDownload* download) {
    WebKitURIResponse* response = webkit_download_get_response(download);
    const gchar* suggested = response ? webkit_uri_response_get_suggested_filename(response) : nullptr;
    std::string name = sanitize_filename(suggested && *suggested ? suggested : "download");
    std::string dir = downloads_directory();
    std::string path;
    if (callbacks.download_destination) {
        path = callbacks.download_destination(name);
        if (path.empty()) {
            webkit_download_cancel(download);
            return;
        }
    } else {
        path = download_path_collision(dir, name);
    }
    gchar* target = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    if (!target) {
        webkit_download_cancel(download);
        return;
    }
    webkit_download_set_destination(download, target);
    g_free(target);
    Download entry;
    entry.download = download;
    entry.id = ++download_seq;
    entry.path = path;
    g_object_ref(download);
    downloads[download] = entry;
    g_signal_connect(download, "notify::estimated-progress", G_CALLBACK(view_download_progress), this);
    g_signal_connect(download, "failed", G_CALLBACK(view_download_failed), this);
    g_signal_connect(download, "finished", G_CALLBACK(view_download_finished), this);
    if (callbacks.download_started) callbacks.download_started(entry.id, path);
}

void ViewState::handle_download_progress(WebKitDownload* download) {
    auto it = downloads.find(download);
    if (it == downloads.end()) return;
    if (callbacks.download_progress) {
        callbacks.download_progress(it->second.id, webkit_download_get_estimated_progress(download));
    }
}

void ViewState::handle_download_failed(WebKitDownload* download) {
    auto it = downloads.find(download);
    std::uint64_t id = it != downloads.end() ? it->second.id : 0;
    if (it != downloads.end()) downloads.erase(it);
    g_object_unref(download);
    if (id && callbacks.download_failed) callbacks.download_failed(id);
}

void ViewState::handle_download_finished(WebKitDownload* download) {
    auto it = downloads.find(download);
    std::uint64_t id = it != downloads.end() ? it->second.id : 0;
    if (it != downloads.end()) downloads.erase(it);
    g_object_unref(download);
    if (id && callbacks.download_finished) callbacks.download_finished(id);
}

void ViewState::compile_filters() {
    if (!filter_store) return;
    static const char fonts_json[] =
        "[{\"trigger\":{\"url-filter\":\".*\",\"resource-type\":[\"font\"]},\"action\":{\"type\":\"block\"}}]";
    GBytes* fonts_source = g_bytes_new_static(fonts_json, sizeof(fonts_json) - 1);
    webkit_user_content_filter_store_save(filter_store, "block-remote-fonts", fonts_source,
        nullptr, view_fonts_filter_saved, this);
    g_bytes_unref(fonts_source);
    if (config.privacy && config.privacy->blocklist && config.settings) {
        Blocklist list = default_blocklist(config.settings->blocklist_file);
        std::string json = blocklist_rules_json(list.domains());
        if (!json.empty()) {
            GBytes* source = g_bytes_new(json.data(), json.size());
            webkit_user_content_filter_store_save(filter_store, "mustela-blocklist", source,
                nullptr, view_blocklist_filter_saved, this);
            g_bytes_unref(source);
        }
    }
}

void ViewState::apply_context_policy() {
    const Settings* set = config.settings;
    const PrivacyPolicy* pol = config.privacy;
    WebKitProcessModel model = (!set || process_model_from(set->process_model) == ProcessModel::Shared)
        ? WEBKIT_PROCESS_MODEL_SHARED_SECONDARY_PROCESS
        : WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    webkit_web_context_set_process_model(normal_context, model);
    webkit_web_context_set_process_model(private_context, model);
#pragma GCC diagnostic pop
    WebKitWebContext* contexts[] = {normal_context, private_context};
    for (WebKitWebContext* context : contexts) {
        apply_cookie_policy(context, pol ? pol->cookies : CookiePolicy::NoThirdParty);
    }
    WebKitTLSErrorsPolicy tls = (set && set->tls_strict)
        ? WEBKIT_TLS_ERRORS_POLICY_FAIL
        : WEBKIT_TLS_ERRORS_POLICY_IGNORE;
    for (WebKitWebContext* context : contexts) {
        WebKitWebsiteDataManager* manager = webkit_web_context_get_website_data_manager(context);
        webkit_website_data_manager_set_tls_errors_policy(manager, tls);
    }
    const char* ext = config.webext_dir.empty() ? MUSTELA_WEBEXT_DIR : config.webext_dir.c_str();
    if (ext && *ext) {
        for (WebKitWebContext* context : contexts) {
            webkit_web_context_set_web_extensions_directory(context, ext);
            webkit_web_context_set_web_extensions_initialization_user_data(context,
                g_variant_new_boolean((pol && pol->gpc) ? TRUE : FALSE));
        }
    }
    const char* ignore_hosts[] = {"localhost", "127.0.0.1", "::1", nullptr};
    WebKitNetworkProxyMode mode = WEBKIT_NETWORK_PROXY_MODE_DEFAULT;
    WebKitNetworkProxySettings* proxy_settings = nullptr;
    std::string proxy_mode = set ? set->proxy_mode : std::string("off");
    if (proxy_mode == "local") {
        Blocklist list = default_blocklist(set ? set->blocklist_file : std::string());
        if (!pol || !pol->blocklist) list.clear();
        ProxyConfig proxy_config;
        proxy_config.resolve_via_doh = set && !set->dns_over_https.empty();
        proxy_config.doh_url = set ? set->dns_over_https : std::string();
        ProxyEndpoint endpoint;
        if (set && !set->proxy_upstream.empty() && parse_proxy_url(set->proxy_upstream, endpoint)) {
            proxy_config.has_upstream = true;
            proxy_config.upstream = endpoint;
        }
        if (!proxy) proxy = std::make_unique<ProxyServer>();
        if (proxy->start(proxy_config, list, doh_transport_soup)) {
            std::string url = "http://127.0.0.1:" + std::to_string(proxy->port());
            proxy_settings = webkit_network_proxy_settings_new(url.c_str(), ignore_hosts);
            mode = WEBKIT_NETWORK_PROXY_MODE_CUSTOM;
        } else {
            proxy.reset();
        }
    } else if (proxy_mode == "upstream") {
        ProxyEndpoint endpoint;
        if (set && parse_proxy_url(set->proxy_upstream, endpoint)) {
            std::string url = proxy_url_from(endpoint);
            proxy_settings = webkit_network_proxy_settings_new(url.c_str(), ignore_hosts);
            mode = WEBKIT_NETWORK_PROXY_MODE_CUSTOM;
        }
    }
    for (WebKitWebContext* context : contexts) {
        WebKitWebsiteDataManager* manager = webkit_web_context_get_website_data_manager(context);
        webkit_website_data_manager_set_network_proxy_settings(manager, mode, proxy_settings);
    }
    if (proxy_settings) webkit_network_proxy_settings_free(proxy_settings);
}

void ViewState::build() {
    if (config.profile) {
        WebKitWebsiteDataManager* manager = webkit_website_data_manager_new(
            "base-data-directory", config.profile->root().c_str(), nullptr);
        webkit_website_data_manager_set_itp_enabled(manager,
            (config.privacy && config.privacy->hardening.tracking_prevention) ? TRUE : FALSE);
        normal_context = webkit_web_context_new_with_website_data_manager(manager);
        g_object_unref(manager);
    } else {
        normal_context = webkit_web_context_new();
    }
    private_context = webkit_web_context_new_ephemeral();
    if (config.profile) {
        g_mkdir_with_parents(config.profile->paths().favicons.c_str(), 0700);
        webkit_web_context_set_favicon_database_directory(normal_context, config.profile->paths().favicons.c_str());
        filter_store = webkit_user_content_filter_store_new(config.profile->paths().filters.c_str());
    }
    compile_filters();
    apply_context_policy();
    g_signal_connect(normal_context, "download-started", G_CALLBACK(view_download_started), this);
    g_signal_connect(private_context, "download-started", G_CALLBACK(view_download_started), this);
}

ViewState::~ViewState() {
    for (auto& entry : downloads) {
        g_object_unref(entry.first);
    }
    if (filter_store) g_clear_object(&filter_store);
    if (fonts_filter) webkit_user_content_filter_unref(fonts_filter);
    if (blocklist_filter) webkit_user_content_filter_unref(blocklist_filter);
    if (normal_context) g_object_unref(normal_context);
    if (private_context) g_object_unref(private_context);
}

struct ViewImpl {
    ViewState* state = nullptr;
    ViewCallbacks callbacks;
    WebKitWebView* web = nullptr;
    View* owner = nullptr;
    ViewImpl* parent = nullptr;
    bool private_mode = false;
    bool wired = false;
    std::string engine_default_ua;
    std::string default_ua;
    bool error_page = false;
    std::string error_page_uri;
    int crashes = 0;
    bool devtools_wanted = false;
    int devtools_attempts = 0;
    guint devtools_retry_id = 0;

    ViewImpl(ViewState* s, bool priv, const ViewCallbacks& cb)
        : state(s), callbacks(cb), private_mode(priv) {}
    ~ViewImpl();

    void attach();
    void apply_policies(const std::string& uri);
    void deny_origin_cookies(const std::string& uri);
    std::string current_uri() const;
    std::string current_title() const;

    static void on_notify_uri(GObject* object, GParamSpec*, gpointer user_data);
    static void on_notify_title(GObject* object, GParamSpec*, gpointer user_data);
    static void on_notify_progress(GObject* object, GParamSpec*, gpointer user_data);
    static void on_load_changed(WebKitWebView* view, WebKitLoadEvent event, gpointer user_data);
    static gboolean on_load_failed(WebKitWebView* view, WebKitLoadEvent event, const char* failing_uri,
        GError* error, gpointer user_data);
    static void on_notify_favicon(GObject* object, GParamSpec*, gpointer user_data);
    static GtkWidget* on_create(WebKitWebView* view, WebKitNavigationAction* action, gpointer user_data);
    static void on_ready_to_show(WebKitWebView* view, gpointer user_data);
    static void on_close(WebKitWebView* view, gpointer user_data);
    static gboolean on_script_dialog(WebKitWebView* view, WebKitScriptDialog* dialog, gpointer user_data);
    static void on_mouse_target(WebKitWebView* view, WebKitHitTestResult* hit_test, guint modifiers,
        gpointer user_data);
    static gboolean on_permission_request(WebKitWebView* view, WebKitPermissionRequest* request,
        gpointer user_data);
    static gboolean on_accept_certificate(WebKitWebView* view, GTlsCertificate* certificate,
        GTlsCertificateFlags flags, gpointer user_data);
    static gboolean on_authenticate(WebKitWebView* view, WebKitAuthenticationRequest* request,
        gpointer user_data);
    static void on_process_gone(WebKitWebView* view, WebKitWebProcessTerminationReason reason,
        gpointer user_data);
    static gboolean on_context_menu(WebKitWebView* view, WebKitContextMenu* menu, GdkEvent* event,
        WebKitHitTestResult* hit_test, gpointer user_data);
    static void on_find_found(WebKitFindController* finder, guint match_count, gpointer user_data);
    static void on_find_failed(WebKitFindController* finder, gpointer user_data);
    static gboolean on_inspector_dock(WebKitWebInspector* inspector, gpointer user_data);
    static gboolean on_inspector_detach(WebKitWebInspector* inspector, gpointer user_data);
    static void on_inspector_closed(WebKitWebInspector* inspector, gpointer user_data);
    static gboolean on_devtools_attach_retry(gpointer user_data);
    void start_devtools_attach_retry();
    void stop_devtools_attach_retry();
};

std::string ViewImpl::current_uri() const {
    if (error_page) return error_page_uri;
    const gchar* uri = web ? webkit_web_view_get_uri(web) : nullptr;
    return uri ? std::string(uri) : std::string();
}

std::string ViewImpl::current_title() const {
    const gchar* title = web ? webkit_web_view_get_title(web) : nullptr;
    return title ? std::string(title) : std::string();
}

void ViewImpl::attach() {
    if (wired || !web) return;
    WebKitSettings* settings = webkit_web_view_get_settings(web);
    gchar* ua = nullptr;
    g_object_get(settings, "user-agent", &ua, nullptr);
    engine_default_ua = ua ? ua : "";
    default_ua = engine_default_ua;
    g_free(ua);
    g_signal_connect(web, "notify::uri", G_CALLBACK(on_notify_uri), this);
    g_signal_connect(web, "notify::title", G_CALLBACK(on_notify_title), this);
    g_signal_connect(web, "notify::estimated-load-progress", G_CALLBACK(on_notify_progress), this);
    g_signal_connect(web, "load-changed", G_CALLBACK(on_load_changed), this);
    g_signal_connect(web, "load-failed", G_CALLBACK(on_load_failed), this);
    g_signal_connect(web, "notify::favicon", G_CALLBACK(on_notify_favicon), this);
    g_signal_connect(web, "create", G_CALLBACK(on_create), this);
    g_signal_connect(web, "ready-to-show", G_CALLBACK(on_ready_to_show), this);
    g_signal_connect(web, "close", G_CALLBACK(on_close), this);
    g_signal_connect(web, "script-dialog", G_CALLBACK(on_script_dialog), this);
    g_signal_connect(web, "mouse-target-changed", G_CALLBACK(on_mouse_target), this);
    g_signal_connect(web, "permission-request", G_CALLBACK(on_permission_request), this);
    g_signal_connect(web, "accept-certificate", G_CALLBACK(on_accept_certificate), this);
    g_signal_connect(web, "authenticate", G_CALLBACK(on_authenticate), this);
    g_signal_connect(web, "web-process-terminated", G_CALLBACK(on_process_gone), this);
    g_signal_connect(web, "context-menu", G_CALLBACK(on_context_menu), this);
    WebKitFindController* finder = webkit_web_view_get_find_controller(web);
    g_signal_connect(finder, "found-text", G_CALLBACK(on_find_found), this);
    g_signal_connect(finder, "failed-to-find-text", G_CALLBACK(on_find_failed), this);
    WebKitWebInspector* inspector = webkit_web_view_get_inspector(web);
    g_signal_connect(inspector, "attach", G_CALLBACK(on_inspector_dock), this);
    g_signal_connect(inspector, "detach", G_CALLBACK(on_inspector_detach), this);
    g_signal_connect(inspector, "closed", G_CALLBACK(on_inspector_closed), this);
    if (state) state->views.push_back(this);
    wired = true;
}

ViewImpl::~ViewImpl() {
    stop_devtools_attach_retry();
    if (state) {
        for (size_t i = 0; i < state->views.size(); ++i) {
            if (state->views[i] == this) {
                state->views.erase(state->views.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
}

void ViewImpl::deny_origin_cookies(const std::string& uri) {
    std::string host = host_from_uri(uri);
    if (host.empty() || !state || !state->normal_context) return;
    size_t scheme_end = uri.find("://");
    std::string scheme = scheme_end == std::string::npos ? "https" : uri.substr(0, scheme_end);
    std::string origin = scheme + "://" + host;
    GList* origins = g_list_append(nullptr, const_cast<gchar*>(origin.c_str()));
    WebKitWebsiteDataManager* manager =
        webkit_web_context_get_website_data_manager(state->normal_context);
    webkit_website_data_manager_remove(manager, WEBKIT_WEBSITE_DATA_COOKIES, origins, nullptr, nullptr, nullptr);
    g_list_free(origins);
}

void ViewImpl::apply_policies(const std::string& uri) {
    if (!web || !state) return;
    WebKitSettings* settings = webkit_web_view_get_settings(web);
    const PrivacyPolicy* pol = state->config.privacy;
    const Hardening* hard = pol ? &pol->hardening : nullptr;
    bool js = !hard || hard->javascript;
    g_object_set(settings,
        "enable-javascript", js ? TRUE : FALSE,
        "enable-hyperlink-auditing", (hard && hard->hyperlink_auditing) ? TRUE : FALSE,
        "enable-dns-prefetching", (hard && hard->dns_prefetching) ? TRUE : FALSE,
        "media-playback-requires-user-gesture", (hard && hard->autoplay_blocked) ? TRUE : FALSE,
        nullptr);
    WebKitUserContentManager* manager = webkit_web_view_get_user_content_manager(web);
    webkit_user_content_manager_remove_all_scripts(manager);
    webkit_user_content_manager_remove_all_filters(manager);
    if (pol && pol->gpc) add_user_script(manager, gpc_user_script());
    SiteDecision decision;
    if (pol && state->config.sites) decision = resolve_site(*pol, *state->config.sites, uri);
    g_object_set(settings, "enable-javascript", (js && decision.javascript) ? TRUE : FALSE, nullptr);
    if (decision.fingerprint.block_webgl) add_user_script(manager, fp_scripts::WEBGL_BLOCK);
    if (decision.fingerprint.block_canvas_readback) add_user_script(manager, fp_scripts::CANVAS_BLANK);
    if (decision.fingerprint.referrer != ReferrerTrim::Off) {
        add_user_script(manager, fingerprint_referrer_script(decision.fingerprint.referrer));
    }
    if (decision.fingerprint.block_remote_fonts && state->fonts_filter) {
        webkit_user_content_manager_add_filter(manager, state->fonts_filter);
    }
    if (pol && pol->blocklist && state->blocklist_filter) {
        webkit_user_content_manager_add_filter(manager, state->blocklist_filter);
    }
    webkit_user_content_manager_remove_all_style_sheets(manager);
    if (!decision.css.empty()) {
        WebKitUserStyleSheet* sheet = webkit_user_style_sheet_new(decision.css.c_str(),
            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_STYLE_LEVEL_USER, nullptr, nullptr);
        webkit_user_content_manager_add_style_sheet(manager, sheet);
        webkit_user_style_sheet_unref(sheet);
    }
    if (!decision.user_agent.empty()) {
        webkit_settings_set_user_agent(settings, decision.user_agent.c_str());
    } else if (!default_ua.empty()) {
        webkit_settings_set_user_agent(settings, default_ua.c_str());
    }
    if (decision.zoom > 0.0) {
        webkit_web_view_set_zoom_level(web, decision.zoom);
    }
}

void ViewImpl::on_notify_uri(GObject*, GParamSpec*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->callbacks.uri_changed) self->callbacks.uri_changed(self->current_uri());
}

void ViewImpl::on_notify_title(GObject*, GParamSpec*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->callbacks.title_changed) self->callbacks.title_changed(self->current_title());
}

void ViewImpl::on_notify_progress(GObject* object, GParamSpec*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->callbacks.load_progress) {
        self->callbacks.load_progress(webkit_web_view_get_estimated_load_progress(WEBKIT_WEB_VIEW(object)));
    }
}

void ViewImpl::on_load_changed(WebKitWebView*, WebKitLoadEvent event, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self) return;
    if (event == WEBKIT_LOAD_STARTED) {
        if (self->error_page && self->current_uri() != self->error_page_uri) {
            self->error_page = false;
            self->error_page_uri.clear();
        }
        if (self->callbacks.loading_changed) self->callbacks.loading_changed(true);
    }
    if (event == WEBKIT_LOAD_COMMITTED) {
        self->apply_policies(self->current_uri());
    }
    if (event == WEBKIT_LOAD_FINISHED) {
        self->crashes = 0;
        if (self->callbacks.loading_changed) self->callbacks.loading_changed(false);
        if (!self->error_page) {
            if (self->callbacks.committed) {
                self->callbacks.committed(self->current_uri(), self->current_title(), self->private_mode);
            }
            if (!self->private_mode && self->state && self->state->config.privacy
                && self->state->config.sites) {
                SiteDecision decision = resolve_site(*self->state->config.privacy,
                    *self->state->config.sites, self->current_uri());
                if (decision.cookies_denied) self->deny_origin_cookies(self->current_uri());
            }
        }
    }
}

gboolean ViewImpl::on_load_failed(WebKitWebView* view, WebKitLoadEvent event, const char* failing_uri,
    GError* error, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self) return FALSE;
    if (event != WEBKIT_LOAD_STARTED || !error || !failing_uri || !*failing_uri) return FALSE;
    if (error->domain == webkit_network_error_quark()
        && error->code == WEBKIT_NETWORK_ERROR_CANCELLED) return FALSE;
    if (error->domain == webkit_policy_error_quark()
        && error->code == WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE) return FALSE;
    const char* heading = "Load failed";
    if (error->domain == webkit_policy_error_quark()) {
        switch (error->code) {
            case WEBKIT_POLICY_ERROR_CANNOT_SHOW_MIME_TYPE: heading = "Can't show this content"; break;
            case WEBKIT_POLICY_ERROR_CANNOT_SHOW_URI: heading = "Unsupported address"; break;
            case WEBKIT_POLICY_ERROR_CANNOT_USE_RESTRICTED_PORT: heading = "Port blocked"; break;
            default: heading = "Navigation blocked"; break;
        }
    } else if (error->domain == webkit_plugin_error_quark()) {
        heading = "Plug-in failure";
    } else if (error->domain == webkit_network_error_quark()) {
        switch (error->code) {
            case WEBKIT_NETWORK_ERROR_UNKNOWN_PROTOCOL: heading = "Unsupported address"; break;
            case WEBKIT_NETWORK_ERROR_FILE_DOES_NOT_EXIST: heading = "File not found"; break;
            case WEBKIT_NETWORK_ERROR_FAILED:
            case WEBKIT_NETWORK_ERROR_TRANSPORT: heading = "Could not connect"; break;
            default: break;
        }
    }
    std::string html = branded_error_page(heading, error->message ? error->message : "", failing_uri);
    webkit_web_view_load_alternate_html(view, html.c_str(), failing_uri, nullptr);
    self->error_page = true;
    self->error_page_uri = failing_uri;
    if (self->callbacks.load_failed) {
        self->callbacks.load_failed(failing_uri, heading, error->message ? error->message : "");
    }
    return FALSE;
}

void ViewImpl::on_notify_favicon(GObject*, GParamSpec*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self || !self->web) return;
    cairo_surface_t* surface = webkit_web_view_get_favicon(self->web);
    if (surface && !self->private_mode && self->state && self->state->config.profile) {
        std::string host = host_from_uri(self->current_uri());
        std::string name = sanitize_filename(host);
        if (!host.empty() && !name.empty()) {
            cairo_surface_write_to_png(surface,
                (self->state->config.profile->paths().favicons + "/" + name + ".png").c_str());
        }
    }
    if (self->callbacks.favicon_changed) self->callbacks.favicon_changed(self->current_uri());
}

GtkWidget* ViewImpl::on_create(WebKitWebView* view, WebKitNavigationAction* action, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self || !self->state || !self->callbacks.popup_created) return nullptr;
    WebKitWebView* related = WEBKIT_WEB_VIEW(webkit_web_view_new_with_related_view(view));
    WebKitURIRequest* request = webkit_navigation_action_get_request(action);
    const gchar* target = request ? webkit_uri_request_get_uri(request) : nullptr;
    ViewImpl* popup = new ViewImpl(self->state, self->private_mode, ViewCallbacks{});
    popup->parent = self;
    popup->web = related;
    popup->attach();
    popup->apply_policies(target ? target : "");
    View* wrapper = new View(popup);
    self->callbacks.popup_created(wrapper);
    return GTK_WIDGET(related);
}

void ViewImpl::on_ready_to_show(WebKitWebView*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->parent && self->owner && self->parent->callbacks.popup_ready) {
        self->parent->callbacks.popup_ready(self->owner);
    }
}

void ViewImpl::on_close(WebKitWebView*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->callbacks.close_requested) self->callbacks.close_requested();
}

gboolean ViewImpl::on_script_dialog(WebKitWebView*, WebKitScriptDialog* dialog, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self || !dialog) return FALSE;
    const gchar* raw = webkit_script_dialog_get_message(dialog);
    std::string message = sanitize_dialog_text(raw ? raw : "");
    switch (webkit_script_dialog_get_dialog_type(dialog)) {
        case WEBKIT_SCRIPT_DIALOG_ALERT:
            if (self->callbacks.script_alert) self->callbacks.script_alert(message);
            return TRUE;
        case WEBKIT_SCRIPT_DIALOG_CONFIRM:
        case WEBKIT_SCRIPT_DIALOG_BEFORE_UNLOAD_CONFIRM: {
            bool confirmed = self->callbacks.script_confirm ? self->callbacks.script_confirm(message) : false;
            webkit_script_dialog_confirm_set_confirmed(dialog, confirmed ? TRUE : FALSE);
            return TRUE;
        }
        case WEBKIT_SCRIPT_DIALOG_PROMPT: {
            if (!self->callbacks.script_prompt) return FALSE;
            const gchar* default_text = webkit_script_dialog_prompt_get_default_text(dialog);
            bool ok = true;
            std::string text = self->callbacks.script_prompt(message, default_text ? default_text : "", ok);
            if (!ok) return FALSE;
            webkit_script_dialog_prompt_set_text(dialog, text.c_str());
            return TRUE;
        }
        default:
            return FALSE;
    }
}

void ViewImpl::on_mouse_target(WebKitWebView*, WebKitHitTestResult* hit_test, guint, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self || !self->callbacks.link_hover) return;
    const gchar* link = hit_test ? webkit_hit_test_result_get_link_uri(hit_test) : nullptr;
    self->callbacks.link_hover(link ? link : "");
}

gboolean ViewImpl::on_permission_request(WebKitWebView*, WebKitPermissionRequest* request,
    gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self || !self->state) return FALSE;
    ViewState* state = self->state;
    const Settings* set = state->config.settings;
    const std::string* setting = nullptr;
    std::string what;
    std::string combined;
    if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(request)) {
        what = "your location";
        setting = set ? &set->permissions_geolocation : nullptr;
    } else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)) {
        WebKitUserMediaPermissionRequest* media = WEBKIT_USER_MEDIA_PERMISSION_REQUEST(request);
        bool audio = webkit_user_media_permission_is_for_audio_device(media) != FALSE;
        bool video = webkit_user_media_permission_is_for_video_device(media) != FALSE;
        bool display = webkit_user_media_permission_is_for_display_device(media) != FALSE;
        if (display) {
            what = "your screen";
            setting = set ? &set->permissions_camera : nullptr;
        } else if (audio && video) {
            what = "camera and microphone";
            if (set) {
                combined = stricter_permission(set->permissions_camera, set->permissions_microphone);
                setting = &combined;
            }
        } else if (video) {
            what = "camera";
            setting = set ? &set->permissions_camera : nullptr;
        } else {
            what = "microphone";
            setting = set ? &set->permissions_microphone : nullptr;
        }
    } else if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(request)) {
        what = "notifications";
        setting = set ? &set->permissions_notifications : nullptr;
    } else {
        webkit_permission_request_deny(request);
        return TRUE;
    }
    if (!setting) {
        webkit_permission_request_deny(request);
        return TRUE;
    }
    std::string host = host_from_uri(self->current_uri());
    if (host.empty()) host = "this site";
    PermissionChoice global = permission_choice_from(*setting);
    SiteRule rule = state->config.sites ? state->config.sites->policy_for(host).permissions
                                        : SiteRule::Default;
    PermissionDecision decision = resolve_permission(global, rule);
    if (decision == PermissionDecision::Denied) {
        webkit_permission_request_deny(request);
        return TRUE;
    }
    if (decision == PermissionDecision::Granted) {
        webkit_permission_request_allow(request);
        return TRUE;
    }
    PermissionAnswer answer = self->callbacks.permission_prompt
        ? self->callbacks.permission_prompt(host, what)
        : PermissionAnswer::Deny;
    if (answer == PermissionAnswer::Allow) {
        webkit_permission_request_allow(request);
        return TRUE;
    }
    if (answer == PermissionAnswer::Deny) {
        webkit_permission_request_deny(request);
        return TRUE;
    }
    if (answer == PermissionAnswer::Always) webkit_permission_request_allow(request);
    else webkit_permission_request_deny(request);
    if (state->config.sites && !self->private_mode) {
        const SitePolicy* existing = state->config.sites->find(host);
        SitePolicy policy = existing ? *existing : SitePolicy{};
        policy.permissions = answer == PermissionAnswer::Always ? SiteRule::Allow : SiteRule::Deny;
        state->config.sites->set(host, policy);
        state->config.sites->save();
    }
    return TRUE;
}

gboolean ViewImpl::on_accept_certificate(WebKitWebView*, GTlsCertificate*, GTlsCertificateFlags,
    gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self) return FALSE;
    std::string host = host_from_uri(self->current_uri());
    if (host.empty()) host = "this site";
    return self->callbacks.certificate_prompt && self->callbacks.certificate_prompt(host) ? TRUE : FALSE;
}

gboolean ViewImpl::on_authenticate(WebKitWebView*, WebKitAuthenticationRequest* request,
    gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self || !request) return FALSE;
    const gchar* host = webkit_authentication_request_get_host(request);
    const gchar* realm = webkit_authentication_request_get_realm(request);
    guint port = webkit_authentication_request_get_port(request);
    bool proxy_auth = webkit_authentication_request_is_for_proxy(request) != FALSE;
    std::string label = std::string(proxy_auth ? "proxy " : "") + (host ? host : "")
        + ":" + std::to_string(static_cast<int>(port));
    if (realm && *realm) label += std::string(" (") + sanitize_dialog_text(realm, 256) + ")";
    std::string user;
    std::string password;
    bool ok = self->callbacks.auth_prompt && self->callbacks.auth_prompt(label, user, password);
    if (ok) {
        WebKitCredential* credential = webkit_credential_new(user.c_str(), password.c_str(),
            WEBKIT_CREDENTIAL_PERSISTENCE_FOR_SESSION);
        webkit_authentication_request_authenticate(request, credential);
        webkit_credential_free(credential);
    } else {
        webkit_authentication_request_cancel(request);
    }
    return TRUE;
}

void ViewImpl::on_process_gone(WebKitWebView*, WebKitWebProcessTerminationReason reason,
    gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self) return;
    ++self->crashes;
    const char* why = "crashed";
    if (reason == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT) why = "ran out of memory";
    else if (reason == WEBKIT_WEB_PROCESS_TERMINATED_BY_API) why = "was stopped";
    std::string text = std::string("web process ") + why;
    if (self->crashes <= 2 && self->web) {
        webkit_web_view_reload(self->web);
        text += " — reloading";
    }
    if (self->callbacks.web_process_gone) self->callbacks.web_process_gone(text);
}

gboolean ViewImpl::on_context_menu(WebKitWebView*, WebKitContextMenu*, GdkEvent*,
    WebKitHitTestResult* hit_test, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self || !self->callbacks.context_menu) return FALSE;
    ContextMenuInfo info;
    if (hit_test) {
        info.link = webkit_hit_test_result_context_is_link(hit_test) != FALSE;
        info.image = webkit_hit_test_result_context_is_image(hit_test) != FALSE;
        info.media = webkit_hit_test_result_context_is_media(hit_test) != FALSE;
        info.selection = webkit_hit_test_result_context_is_selection(hit_test) != FALSE;
        info.editable = webkit_hit_test_result_context_is_editable(hit_test) != FALSE;
        const gchar* link = webkit_hit_test_result_get_link_uri(hit_test);
        const gchar* image = webkit_hit_test_result_get_image_uri(hit_test);
        const gchar* media = webkit_hit_test_result_get_media_uri(hit_test);
        info.link_uri = link ? link : "";
        info.image_uri = image ? image : "";
        info.media_uri = media ? media : "";
    }
    info.page_uri = self->current_uri();
    return self->callbacks.context_menu(info) ? TRUE : FALSE;
}

void ViewImpl::on_find_found(WebKitFindController*, guint, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->callbacks.find_result) self->callbacks.find_result(true);
}

void ViewImpl::on_find_failed(WebKitFindController*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->callbacks.find_result) self->callbacks.find_result(false);
}

gboolean ViewImpl::on_inspector_dock(WebKitWebInspector*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->callbacks.devtools_docked) self->callbacks.devtools_docked(true);
    return FALSE;
}

gboolean ViewImpl::on_inspector_detach(WebKitWebInspector*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->callbacks.devtools_docked) self->callbacks.devtools_docked(false);
    return FALSE;
}

void ViewImpl::on_inspector_closed(WebKitWebInspector*, gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (self && self->callbacks.devtools_docked) self->callbacks.devtools_docked(false);
}

gboolean ViewImpl::on_devtools_attach_retry(gpointer user_data) {
    ViewImpl* self = static_cast<ViewImpl*>(user_data);
    if (!self || !self->web) {
        if (self) self->devtools_retry_id = 0;
        return FALSE;
    }
    WebKitWebInspector* inspector = webkit_web_view_get_inspector(self->web);
    if (webkit_web_inspector_is_attached(inspector)) {
        self->devtools_retry_id = 0;
        return FALSE;
    }
    webkit_web_inspector_attach(inspector);
    if (webkit_web_inspector_is_attached(inspector) || ++self->devtools_attempts > 200) {
        self->devtools_retry_id = 0;
        return FALSE;
    }
    return TRUE;
}

void ViewImpl::start_devtools_attach_retry() {
    stop_devtools_attach_retry();
    devtools_attempts = 0;
    devtools_retry_id = g_timeout_add(100, on_devtools_attach_retry, this);
}

void ViewImpl::stop_devtools_attach_retry() {
    if (devtools_retry_id) {
        g_source_remove(devtools_retry_id);
        devtools_retry_id = 0;
    }
}

void ViewState::refresh_view_policies() {
    std::vector<ViewImpl*> current = views;
    for (ViewImpl* view : current) {
        if (view) view->apply_policies(view->current_uri());
    }
}

ViewSession::ViewSession(const ViewConfig& config, const SessionCallbacks& callbacks)
    : impl(new ViewState(config, callbacks)) {
    impl->build();
}

ViewSession::~ViewSession() = default;

void ViewSession::refresh() {
    impl->apply_context_policy();
    impl->compile_filters();
    impl->refresh_view_policies();
}

namespace {

WebKitWebsiteDataTypes webkit_types_from_kind(DataKind kinds) {
    std::uint32_t out = 0;
    if (has(kinds, DataKind::Cookies)) out |= WEBKIT_WEBSITE_DATA_COOKIES;
    if (has(kinds, DataKind::Cache))
        out |= WEBKIT_WEBSITE_DATA_MEMORY_CACHE | WEBKIT_WEBSITE_DATA_DISK_CACHE;
    if (has(kinds, DataKind::LocalStorage)) out |= WEBKIT_WEBSITE_DATA_LOCAL_STORAGE;
    if (has(kinds, DataKind::IndexedDB)) out |= WEBKIT_WEBSITE_DATA_INDEXEDDB_DATABASES;
    return static_cast<WebKitWebsiteDataTypes>(out);
}

DataKind data_kind_from_webkit(WebKitWebsiteDataTypes types) {
    std::uint32_t out = 0;
    if (types & WEBKIT_WEBSITE_DATA_COOKIES) out |= static_cast<std::uint32_t>(DataKind::Cookies);
    if ((types & WEBKIT_WEBSITE_DATA_MEMORY_CACHE) || (types & WEBKIT_WEBSITE_DATA_DISK_CACHE))
        out |= static_cast<std::uint32_t>(DataKind::Cache);
    if (types & WEBKIT_WEBSITE_DATA_LOCAL_STORAGE) out |= static_cast<std::uint32_t>(DataKind::LocalStorage);
    if (types & WEBKIT_WEBSITE_DATA_INDEXEDDB_DATABASES) out |= static_cast<std::uint32_t>(DataKind::IndexedDB);
    return static_cast<DataKind>(out);
}

struct FetchCtx {
    WebsiteDataCallback done;
};

void on_fetch_done(GObject* object, GAsyncResult* result, gpointer user_data) {
    std::unique_ptr<FetchCtx> ctx(static_cast<FetchCtx*>(user_data));
    std::vector<WebsiteDataEntry> entries;
    GError* error = nullptr;
    GList* items = webkit_website_data_manager_fetch_finish(WEBKIT_WEBSITE_DATA_MANAGER(object),
        result, &error);
    if (error) {
        g_error_free(error);
    } else if (items) {
        for (GList* it = items; it; it = it->next) {
            WebKitWebsiteData* data = static_cast<WebKitWebsiteData*>(it->data);
            WebsiteDataEntry entry;
            const char* name = data ? webkit_website_data_get_name(data) : nullptr;
            entry.origin = name ? std::string(name) : std::string();
            entry.kinds = data ? data_kind_from_webkit(webkit_website_data_get_types(data))
                               : DataKind::None;
            if (!entry.origin.empty()) entries.push_back(std::move(entry));
        }
        g_list_free_full(items, reinterpret_cast<GDestroyNotify>(webkit_website_data_unref));
    }
    if (ctx->done) ctx->done(std::move(entries));
}

struct RemoveCtx {
    std::vector<std::string> origins;
    SimpleCallback done;
    WebKitWebsiteDataTypes types = static_cast<WebKitWebsiteDataTypes>(0);
    GList* matched = nullptr;
};

void on_remove_fetch(GObject* object, GAsyncResult* result, gpointer user_data) {
    std::unique_ptr<RemoveCtx> ctx(static_cast<RemoveCtx*>(user_data));
    WebKitWebsiteDataManager* manager = WEBKIT_WEBSITE_DATA_MANAGER(object);
    GError* error = nullptr;
    GList* items = webkit_website_data_manager_fetch_finish(manager, result, &error);
    if (error) {
        g_error_free(error);
    } else {
        for (GList* it = items; it; it = it->next) {
            WebKitWebsiteData* data = static_cast<WebKitWebsiteData*>(it->data);
            if (!data) continue;
            const char* name = webkit_website_data_get_name(data);
            bool wanted = name && std::find(ctx->origins.begin(), ctx->origins.end(),
                std::string(name)) != ctx->origins.end();
            if (wanted && (webkit_website_data_get_types(data) & ctx->types)) {
                webkit_website_data_ref(data);
                ctx->matched = g_list_prepend(ctx->matched, data);
            }
        }
        g_list_free_full(items, reinterpret_cast<GDestroyNotify>(webkit_website_data_unref));
    }
    if (!ctx->matched) {
        if (ctx->done) ctx->done();
        return;
    }
    webkit_website_data_manager_remove(manager, ctx->types, ctx->matched, nullptr,
        [](GObject* object, GAsyncResult* result, gpointer user_data) {
            std::unique_ptr<RemoveCtx> ctx(static_cast<RemoveCtx*>(user_data));
            GError* error = nullptr;
            webkit_website_data_manager_remove_finish(WEBKIT_WEBSITE_DATA_MANAGER(object),
                result, &error);
            if (error) g_error_free(error);
            g_list_free_full(ctx->matched,
                reinterpret_cast<GDestroyNotify>(webkit_website_data_unref));
            ctx->matched = nullptr;
            if (ctx->done) ctx->done();
        }, ctx.release());
}

struct ClearCtx {
    SimpleCallback done;
};

void on_clear_done(GObject* object, GAsyncResult* result, gpointer user_data) {
    std::unique_ptr<ClearCtx> ctx(static_cast<ClearCtx*>(user_data));
    GError* error = nullptr;
    webkit_website_data_manager_clear_finish(WEBKIT_WEBSITE_DATA_MANAGER(object), result, &error);
    if (error) g_error_free(error);
    if (ctx->done) ctx->done();
}

}  // namespace

void ViewSession::fetch_website_data(const WebsiteDataCallback& done) {
    if (!done) return;
    WebKitWebsiteDataManager* manager = impl->normal_context
        ? webkit_web_context_get_website_data_manager(impl->normal_context) : nullptr;
    if (!manager) return;
    FetchCtx* ctx = new FetchCtx{done};
    webkit_website_data_manager_fetch(manager, WEBKIT_WEBSITE_DATA_ALL, nullptr,
        on_fetch_done, ctx);
}

void ViewSession::remove_website_data(const std::vector<std::string>& origins, DataKind kinds,
                                      SimpleCallback done) {
    WebKitWebsiteDataManager* manager = impl->normal_context
        ? webkit_web_context_get_website_data_manager(impl->normal_context) : nullptr;
    if (!manager) {
        if (done) done();
        return;
    }
    RemoveCtx* ctx = new RemoveCtx{origins, std::move(done), webkit_types_from_kind(kinds), nullptr};
    webkit_website_data_manager_fetch(manager, ctx->types, nullptr, on_remove_fetch, ctx);
}

void ViewSession::clear_website_data(DataKind kinds, std::int64_t max_age_seconds,
                                     SimpleCallback done) {
    WebKitWebsiteDataManager* manager = impl->normal_context
        ? webkit_web_context_get_website_data_manager(impl->normal_context) : nullptr;
    if (!manager) {
        if (done) done();
        return;
    }
    GTimeSpan span = max_age_seconds > 0
        ? static_cast<GTimeSpan>(max_age_seconds) * G_TIME_SPAN_SECOND : 0;
    ClearCtx* ctx = new ClearCtx{std::move(done)};
    webkit_website_data_manager_clear(manager, webkit_types_from_kind(kinds), span, nullptr,
        on_clear_done, ctx);
}

View::View(ViewSession& session, bool private_mode, const ViewCallbacks& callbacks)
    : impl(new ViewImpl(session.impl.get(), private_mode, callbacks)) {
    impl->owner = this;
    impl->web = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(
        impl->state->context_for(private_mode)));
    impl->attach();
    impl->apply_policies("");
}

View::View(ViewImpl* adopted) : impl(adopted) {
    impl->owner = this;
}

View::~View() = default;

void* View::native_widget() const {
    return static_cast<void*>(impl->web);
}

void View::load_url(const std::string& url) {
    if (impl->web) webkit_web_view_load_uri(impl->web, url.c_str());
}

void View::go_back() {
    if (impl->web && can_go_back()) webkit_web_view_go_back(impl->web);
}

void View::go_forward() {
    if (impl->web && can_go_forward()) webkit_web_view_go_forward(impl->web);
}

bool View::can_go_back() const {
    return impl->web && webkit_web_view_can_go_back(impl->web) != FALSE;
}

bool View::can_go_forward() const {
    return impl->web && webkit_web_view_can_go_forward(impl->web) != FALSE;
}

void View::reload() {
    if (impl->web) webkit_web_view_reload(impl->web);
}

void View::stop() {
    if (impl->web) webkit_web_view_stop_loading(impl->web);
}

std::string View::uri() const {
    return impl->current_uri();
}

std::string View::title() const {
    return impl->current_title();
}

bool View::is_private() const {
    return impl->private_mode;
}

double View::zoom() const {
    return impl->web ? webkit_web_view_get_zoom_level(impl->web) : 1.0;
}

void View::set_zoom(double level) {
    if (!impl->web) return;
    if (level < 0.3) level = 0.3;
    if (level > 3.0) level = 3.0;
    webkit_web_view_set_zoom_level(impl->web, level);
}

void View::set_user_agent(const std::string& user_agent) {
    impl->default_ua = user_agent.empty() ? impl->engine_default_ua : user_agent;
    refresh_policy();
}

void View::set_callbacks(const ViewCallbacks& callbacks) {
    impl->callbacks = callbacks;
}

void View::refresh_policy() {
    impl->apply_policies(impl->current_uri());
}

SitePolicy View::active_site_policy() const {
    const SiteRules* sites = impl->state->config.sites;
    if (!sites) return SitePolicy{};
    return sites->policy_for(host_from_uri(impl->current_uri()));
}

namespace {

struct JsTextCtx {
    TextCallback done;
};

void on_selection_value(GObject* object, GAsyncResult* result, gpointer user_data) {
    std::unique_ptr<JsTextCtx> ctx(static_cast<JsTextCtx*>(user_data));
    std::string text;
    GError* error = nullptr;
    JSCValue* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(object), result, &error);
    if (error) {
        g_error_free(error);
    } else if (value && jsc_value_is_string(value)) {
        gchar* raw = jsc_value_to_string(value);
        if (raw) {
            text = raw;
            g_free(raw);
        }
    }
    if (value) g_object_unref(value);
    if (ctx && ctx->done) ctx->done(text);
}

}  // namespace

void View::find(const std::string& text, const FindOptions& options, std::uint32_t max_matches) {
    if (!impl->web) return;
    WebKitFindController* finder = webkit_web_view_get_find_controller(impl->web);
    if (text.empty()) {
        webkit_find_controller_search_finish(finder);
        return;
    }
    std::uint32_t flags = WEBKIT_FIND_OPTIONS_NONE;
    if (options.case_insensitive) flags |= WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE;
    if (options.wrap_around) flags |= WEBKIT_FIND_OPTIONS_WRAP_AROUND;
    webkit_find_controller_search(finder, text.c_str(), static_cast<WebKitFindOptions>(flags),
        max_matches ? max_matches : 999u);
}

void View::find_next() {
    if (impl->web) webkit_find_controller_search_next(webkit_web_view_get_find_controller(impl->web));
}

void View::find_previous() {
    if (impl->web) webkit_find_controller_search_previous(webkit_web_view_get_find_controller(impl->web));
}

void View::find_finish() {
    if (impl->web) webkit_find_controller_search_finish(webkit_web_view_get_find_controller(impl->web));
}

std::string View::find_text() const {
    if (!impl->web) return std::string();
    const gchar* text = webkit_find_controller_get_search_text(
        webkit_web_view_get_find_controller(impl->web));
    return text ? std::string(text) : std::string();
}

void View::selected_text(const TextCallback& done) {
    if (!impl->web || !done) return;
    JsTextCtx* ctx = new JsTextCtx{done};
    webkit_web_view_evaluate_javascript(impl->web,
        "window.getSelection ? window.getSelection().toString() : ''", -1, nullptr, nullptr, nullptr,
        on_selection_value, ctx);
}

void View::execute_edit_command(EditCommand command) {
    if (!impl->web) return;
    const char* name = WEBKIT_EDITING_COMMAND_COPY;
    switch (command) {
        case EditCommand::Cut: name = WEBKIT_EDITING_COMMAND_CUT; break;
        case EditCommand::Paste: name = WEBKIT_EDITING_COMMAND_PASTE; break;
        case EditCommand::SelectAll: name = WEBKIT_EDITING_COMMAND_SELECT_ALL; break;
        case EditCommand::Undo: name = WEBKIT_EDITING_COMMAND_UNDO; break;
        case EditCommand::Redo: name = WEBKIT_EDITING_COMMAND_REDO; break;
        case EditCommand::Copy: break;
    }
    webkit_web_view_execute_editing_command(impl->web, name);
}

void View::show_devtools() {
    if (!impl->web) return;
    WebKitSettings* settings = webkit_web_view_get_settings(impl->web);
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    impl->devtools_wanted = true;
    WebKitWebInspector* inspector = webkit_web_view_get_inspector(impl->web);
    webkit_web_inspector_show(inspector);
    if (!webkit_web_inspector_is_attached(inspector)) impl->start_devtools_attach_retry();
}

void View::hide_devtools() {
    if (!impl->web) return;
    impl->devtools_wanted = false;
    impl->stop_devtools_attach_retry();
    WebKitWebInspector* inspector = webkit_web_view_get_inspector(impl->web);
    webkit_web_inspector_close(inspector);
    WebKitSettings* settings = webkit_web_view_get_settings(impl->web);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
}

bool View::devtools_attached() const {
    if (!impl->web) return false;
    return webkit_web_inspector_is_attached(webkit_web_view_get_inspector(impl->web)) != FALSE;
}

double zoom_step(double current, int direction) {
    static const double steps[] = {0.3, 0.5, 0.67, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0};
    const size_t count = sizeof(steps) / sizeof(steps[0]);
    if (direction > 0) {
        for (size_t i = 0; i < count; ++i) {
            if (steps[i] > current + 0.001) return steps[i];
        }
        return steps[count - 1];
    }
    if (direction < 0) {
        for (size_t i = count; i > 0; --i) {
            if (steps[i - 1] < current - 0.001) return steps[i - 1];
        }
        return steps[0];
    }
    return 1.0;
}

}
