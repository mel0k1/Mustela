#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <WebKit/WebKit2_C.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKContextDownloadClient.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKView.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageStateClient.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKPageFindClient.h>
#include <WebKit/WKPageContextMenuClient.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKContextMenuItem.h>
#include <WebKit/WKContextMenuItemTypes.h>
#include <WebKit/WKHitTestResultRef.h>
#include <WebKit/WKWebsiteDataStoreRef.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKHTTPCookieStoreRef.h>
#include <WebKit/WKUserContentControllerRef.h>
#include <WebKit/WKUserScriptRef.h>
#include <WebKit/WKUserScriptInjectionTime.h>
#include <WebKit/WKUserContentExtensionStoreRef.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKPreferencesRefPrivate.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKURLResponse.h>
#include <WebKit/WKGeolocationPermissionRequest.h>
#include <WebKit/WKUserMediaPermissionRequest.h>
#include <WebKit/WKAuthenticationChallenge.h>
#include <WebKit/WKAuthenticationDecisionListener.h>
#include <WebKit/WKCredential.h>
#include <WebKit/WKProtectionSpace.h>
#include <WebKit/WKNumber.h>
#include <WebKit/WKArray.h>
#include <WebKit/WKMutableArray.h>

#include "mustela/view.hpp"

#include "blocklist.hpp"
#include "error_page.hpp"
#include "fingerprint_scripts.hpp"
#include "perf.hpp"
#include "permissions.hpp"
#include "privacy.hpp"
#include "proxy_server.hpp"
#include "ui_text.hpp"
#include "url.hpp"

namespace mustela {

namespace {

std::wstring to_wide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string to_utf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

struct WkHold {
    WKTypeRef ref = nullptr;
    explicit WkHold(WKTypeRef value) : ref(value) {}
    ~WkHold() {
        if (ref) WKRelease(ref);
    }
    WkHold(const WkHold&) = delete;
    WkHold& operator=(const WkHold&) = delete;
};

WKStringRef make_wkstring(const std::string& text) {
    return WKStringCreateWithUTF8CString(text.c_str());
}

std::string wkstring_to_std(WKStringRef value) {
    if (!value) return std::string();
    size_t size = WKStringGetMaximumUTF8CStringSize(value);
    std::string out(size, '\0');
    size_t copied = WKStringGetUTF8CString(value, out.data(), size);
    if (copied > 0) out.resize(copied - 1);
    else out.clear();
    return out;
}

std::string wkurl_to_std(WKURLRef value) {
    if (!value) return std::string();
    WkHold hold(WKURLCopyString(value));
    return wkstring_to_std(static_cast<WKStringRef>(hold.ref));
}

WKURLRef make_wkurl(const std::string& text) {
    return WKURLCreateWithUTF8CString(text.c_str());
}

void create_dir_chain(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring built;
    size_t start = 0;
    if (path.size() > 2 && path[1] == L':') start = 3;
    while (start <= path.size()) {
        size_t next = path.find(L'\\', start);
        if (next == std::wstring::npos) next = path.size();
        built = path.substr(0, next);
        if (built.size() >= 3 && built.find(L'\\') != std::wstring::npos) {
            CreateDirectoryW(built.c_str(), nullptr);
        }
        start = next + 1;
    }
}

std::wstring wide_dir(const std::string& path) {
    std::wstring wide = to_wide(path);
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    return wide;
}

HWND hidden_host_window() {
    static HWND host = nullptr;
    if (host && IsWindow(host)) return host;
    host = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"mustela-view-host", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return host;
}

}

struct ViewImpl;

struct ViewState {
    struct Download {
        std::uint64_t id = 0;
        std::string path;
        std::uint64_t received = 0;
        std::int64_t total = 0;
    };

    ViewConfig config;
    SessionCallbacks callbacks;
    WKContextRef context = nullptr;
    WKWebsiteDataStoreRef persistent_store = nullptr;
    WKWebsiteDataStoreRef private_store = nullptr;
    WKUserContentExtensionStoreRef filter_store = nullptr;
    WKUserContentFilterRef fonts_filter = nullptr;
    WKUserContentFilterRef blocklist_filter = nullptr;
    std::map<WKDownloadRef, Download> downloads;
    std::uint64_t download_seq = 0;
    std::vector<ViewImpl*> views;

    ViewState(const ViewConfig& c, const SessionCallbacks& cb) : config(c), callbacks(cb) {}
    ~ViewState();

    WKWebsiteDataStoreRef store_for(bool private_mode) const {
        return private_mode ? private_store : persistent_store;
    }

    void build();
    void apply_context_policy();
    void compile_filters();
    void refresh_view_policies();

    std::string downloads_directory() const;
    void handle_download_started(WKDownloadRef download);
    std::string handle_download_destination(WKDownloadRef download, WKStringRef filename,
        bool* allow_overwrite);
    void handle_download_response(WKDownloadRef download, WKURLResponseRef response);
    void handle_download_data(WKDownloadRef download, std::uint64_t length);
    void handle_download_ended(WKDownloadRef download, bool ok, bool cancelled);
};

void noop_store_callback(void*) {}

WKHTTPCookieAcceptPolicy accept_policy_for(CookiePolicy policy) {
    if (policy == CookiePolicy::Always) return kWKHTTPCookieAcceptPolicyAlways;
    if (policy == CookiePolicy::Never) return kWKHTTPCookieAcceptPolicyNever;
    return kWKHTTPCookieAcceptPolicyOnlyFromMainDocumentDomain;
}

void apply_cookie_policy(WKWebsiteDataStoreRef store, CookiePolicy policy) {
    if (!store) return;
    WKHTTPCookieStoreRef cookies = WKWebsiteDataStoreGetHTTPCookieStore(store);
    WKHTTPCookieStoreSetHTTPCookieAcceptPolicy(cookies, accept_policy_for(policy), nullptr, noop_store_callback);
}

void apply_itp(WKWebsiteDataStoreRef store, bool enabled) {
    if (!store) return;
    WKWebsiteDataStoreSetResourceLoadStatisticsEnabled(store, enabled);
}

void apply_process_model(WKContextRef context, const std::string& name) {
    WKProcessModel model = process_model_from(name) == ProcessModel::Shared
        ? kWKProcessModelSharedSecondaryProcess
        : kWKProcessModelMultipleSecondaryProcesses;
    WKContextSetProcessModel(context, model);
}

void add_user_script(WKUserContentControllerRef content, const std::string& source) {
    WkHold text(make_wkstring(source));
    WKUserScriptRef script = WKUserScriptCreateWithSource(static_cast<WKStringRef>(text.ref),
        kWKInjectAtDocumentStart, false);
    if (!script) return;
    WKUserContentControllerAddUserScript(content, script);
    WKRelease(script);
}

std::string downloads_directory(const ViewConfig& config) {
    if (!config.downloads_dir.empty()) return config.downloads_dir;
    if (config.settings && !config.settings->downloads_dir.empty()) return config.settings->downloads_dir;
    std::string dir;
    PWSTR downloads = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads) == S_OK && downloads) {
        dir = to_utf8(downloads);
        CoTaskMemFree(downloads);
    }
    if (dir.empty()) dir = ".";
    create_dir_chain(wide_dir(dir));
    return dir;
}

std::string download_path_for(const std::string& dir, const std::string& name) {
    std::string path = dir + "\\" + name;
    if (GetFileAttributesW(to_wide(path).c_str()) == INVALID_FILE_ATTRIBUTES) return path;
    std::string base = name;
    std::string ext;
    size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot != 0) {
        base = name.substr(0, dot);
        ext = name.substr(dot);
    }
    for (int n = 2; n < 1000; ++n) {
        std::string candidate = dir + "\\" + base + " (" + std::to_string(n) + ")" + ext;
        if (GetFileAttributesW(to_wide(candidate).c_str()) == INVALID_FILE_ATTRIBUTES) return candidate;
    }
    return path;
}

void view_download_started(WKContextRef, WKDownloadRef download, const void* client_info) {
    static_cast<ViewState*>(const_cast<void*>(client_info))->handle_download_started(download);
}

WKStringRef view_download_destination(WKContextRef, WKDownloadRef download, WKStringRef filename,
    bool* allow_overwrite, const void* client_info) {
    ViewState* state = static_cast<ViewState*>(const_cast<void*>(client_info));
    std::string path = state->handle_download_destination(download, filename, allow_overwrite);
    if (path.empty()) return nullptr;
    return make_wkstring(path);
}

void view_download_response(WKContextRef, WKDownloadRef download, WKURLResponseRef response,
    const void* client_info) {
    static_cast<ViewState*>(const_cast<void*>(client_info))->handle_download_response(download, response);
}

void view_download_data(WKContextRef, WKDownloadRef download, std::uint64_t length,
    const void* client_info) {
    static_cast<ViewState*>(const_cast<void*>(client_info))->handle_download_data(download, length);
}

void view_download_finished(WKContextRef, WKDownloadRef download, const void* client_info) {
    static_cast<ViewState*>(const_cast<void*>(client_info))->handle_download_ended(download, true, false);
}

void view_download_failed(WKContextRef, WKDownloadRef download, WKErrorRef, const void* client_info) {
    static_cast<ViewState*>(const_cast<void*>(client_info))->handle_download_ended(download, false, false);
}

void view_download_cancelled(WKContextRef, WKDownloadRef download, const void* client_info) {
    static_cast<ViewState*>(const_cast<void*>(client_info))->handle_download_ended(download, false, true);
}

void view_fonts_filter_compiled(WKUserContentFilterRef filter, WKUserContentExtensionStoreResult result,
    void* user_data) {
    ViewState* state = static_cast<ViewState*>(user_data);
    if (!state || result != kWKUserContentExtensionStoreSuccess || !filter) return;
    WKRetain(filter);
    if (state->fonts_filter) WKRelease(state->fonts_filter);
    state->fonts_filter = filter;
    state->refresh_view_policies();
}

void view_blocklist_filter_compiled(WKUserContentFilterRef filter, WKUserContentExtensionStoreResult result,
    void* user_data) {
    ViewState* state = static_cast<ViewState*>(user_data);
    if (!state || result != kWKUserContentExtensionStoreSuccess || !filter) return;
    WKRetain(filter);
    if (state->blocklist_filter) WKRelease(state->blocklist_filter);
    state->blocklist_filter = filter;
    state->refresh_view_policies();
}

void ViewState::compile_filters() {
    if (!filter_store) return;
    static const char fonts_json[] =
        "[{\"trigger\":{\"url-filter\":\".*\",\"resource-type\":[\"font\"]},\"action\":{\"type\":\"block\"}}]";
    WkHold json(make_wkstring(fonts_json));
    WkHold identifier(make_wkstring("block-remote-fonts"));
    WKUserContentExtensionStoreCompile(filter_store, static_cast<WKStringRef>(identifier.ref),
        static_cast<WKStringRef>(json.ref), this, view_fonts_filter_compiled);
    if (config.privacy && config.privacy->blocklist && config.settings) {
        Blocklist list = default_blocklist(config.settings->blocklist_file);
        std::string rules = blocklist_rules_json(list.domains());
        if (!rules.empty()) {
            WkHold source(make_wkstring(rules));
            WkHold name(make_wkstring("mustela-blocklist"));
            WKUserContentExtensionStoreCompile(filter_store, static_cast<WKStringRef>(name.ref),
                static_cast<WKStringRef>(source.ref), this, view_blocklist_filter_compiled);
        }
    }
}

void ViewState::apply_context_policy() {
    if (context && config.settings) apply_process_model(context, config.settings->process_model);
    CookiePolicy cookies = config.privacy ? config.privacy->cookies : CookiePolicy::NoThirdParty;
    apply_cookie_policy(persistent_store, cookies);
    apply_cookie_policy(private_store, cookies);
    apply_itp(persistent_store, config.privacy && config.privacy->hardening.tracking_prevention);
}

std::string ViewState::downloads_directory() const {
    return mustela::downloads_directory(config);
}

void ViewState::handle_download_started(WKDownloadRef download) {
    if (!download) return;
    Download entry;
    entry.id = ++download_seq;
    downloads[download] = entry;
}

std::string ViewState::handle_download_destination(WKDownloadRef download, WKStringRef filename,
    bool* allow_overwrite) {
    if (allow_overwrite) *allow_overwrite = false;
    auto it = downloads.find(download);
    if (it == downloads.end()) return std::string();
    std::uint64_t id = it->second.id;
    std::string suggested = filename ? wkstring_to_std(filename) : std::string();
    std::string name = sanitize_filename(suggested.empty() ? "download" : suggested);
    std::string path;
    if (callbacks.download_destination) {
        path = callbacks.download_destination(name);
        if (path.empty()) {
            downloads.erase(it);
            if (callbacks.download_failed) callbacks.download_failed(id);
            return std::string();
        }
    } else {
        path = download_path_for(downloads_directory(), name);
    }
    it->second.path = path;
    if (callbacks.download_started) callbacks.download_started(id, path);
    return path;
}

void ViewState::handle_download_response(WKDownloadRef download, WKURLResponseRef response) {
    auto it = downloads.find(download);
    if (it == downloads.end()) return;
    it->second.total = response ? static_cast<std::int64_t>(
        WKURLResponseGetExpectedContentLength(response)) : 0;
}

void ViewState::handle_download_data(WKDownloadRef download, std::uint64_t length) {
    auto it = downloads.find(download);
    if (it == downloads.end()) return;
    it->second.received += length;
    if (callbacks.download_progress) {
        double fraction = it->second.total > 0
            ? static_cast<double>(it->second.received) / static_cast<double>(it->second.total)
            : 0.0;
        callbacks.download_progress(it->second.id, fraction);
    }
}

void ViewState::handle_download_ended(WKDownloadRef download, bool ok, bool cancelled) {
    auto it = downloads.find(download);
    if (it == downloads.end()) return;
    std::uint64_t id = it->second.id;
    downloads.erase(it);
    if (cancelled) return;
    if (ok) {
        if (callbacks.download_finished) callbacks.download_finished(id);
    } else {
        if (callbacks.download_failed) callbacks.download_failed(id);
    }
}

void ViewState::build() {
    WkHold context_configuration(WKContextConfigurationCreate());
    context = WKContextCreateWithConfiguration(
        static_cast<WKContextConfigurationRef>(context_configuration.ref));

    WKContextDownloadClientV0 download_client = {};
    download_client.base.version = 0;
    download_client.base.clientInfo = this;
    download_client.didStart = view_download_started;
    download_client.decideDestinationWithSuggestedFilename = view_download_destination;
    download_client.didReceiveResponse = view_download_response;
    download_client.didReceiveData = view_download_data;
    download_client.didFinish = view_download_finished;
    download_client.didFail = view_download_failed;
    download_client.didCancel = view_download_cancelled;
#pragma warning(push)
#pragma warning(disable : 4996)
    WKContextSetDownloadClient(context, &download_client.base);
#pragma warning(pop)

    if (config.profile) {
        std::wstring root = wide_dir(config.profile->root());
        create_dir_chain(root);
        WkHold store_configuration(WKWebsiteDataStoreConfigurationCreate());
        WKWebsiteDataStoreConfigurationRef store_config =
            static_cast<WKWebsiteDataStoreConfigurationRef>(store_configuration.ref);
        std::vector<std::unique_ptr<WkHold>> store_strings;
        auto store_dir = [&](const wchar_t* name) {
            store_strings.push_back(std::make_unique<WkHold>(
                make_wkstring(to_utf8(root + L"\\" + name))));
            return static_cast<WKStringRef>(store_strings.back()->ref);
        };
        WKWebsiteDataStoreConfigurationSetNetworkCacheDirectory(store_config, store_dir(L"cache"));
        WKWebsiteDataStoreConfigurationSetCacheStorageDirectory(store_config, store_dir(L"cachestorage"));
        WKWebsiteDataStoreConfigurationSetLocalStorageDirectory(store_config, store_dir(L"storage"));
        WKWebsiteDataStoreConfigurationSetGeneralStorageDirectory(store_config, store_dir(L"storage"));
        WKWebsiteDataStoreConfigurationSetWebSQLDatabaseDirectory(store_config, store_dir(L"websql"));
        WKWebsiteDataStoreConfigurationSetIndexedDBDatabaseDirectory(store_config, store_dir(L"idb"));
        WKWebsiteDataStoreConfigurationSetMediaKeysStorageDirectory(store_config, store_dir(L"mediakeys"));
        WKWebsiteDataStoreConfigurationSetResourceLoadStatisticsDirectory(store_config, store_dir(L"stats"));
        WKWebsiteDataStoreConfigurationSetServiceWorkerRegistrationDirectory(store_config, store_dir(L"sw"));
        WKWebsiteDataStoreConfigurationSetIsolatedSitesDirectory(store_config, store_dir(L"isolated"));
        WKWebsiteDataStoreConfigurationSetCookieStorageFile(store_config, store_dir(L"cookies.txt"));
        persistent_store = WKWebsiteDataStoreCreateWithConfiguration(store_config);
    } else {
        persistent_store = WKWebsiteDataStoreGetDefaultDataStore();
    }
    private_store = WKWebsiteDataStoreCreateNonPersistentDataStore();

    if (config.profile) {
        std::string filters = config.profile->paths().filters;
        create_dir_chain(wide_dir(filters));
        WkHold filter_path(make_wkstring(filters));
        filter_store = WKUserContentExtensionStoreCreate(static_cast<WKStringRef>(filter_path.ref));
    }
    compile_filters();
    apply_context_policy();
}

ViewState::~ViewState() {
    for (auto& entry : downloads) {
        if (entry.first) WKRelease(entry.first);
    }
    if (fonts_filter) WKRelease(fonts_filter);
    if (blocklist_filter) WKRelease(blocklist_filter);
    if (filter_store) WKRelease(filter_store);
    if (private_store) WKRelease(private_store);
    if (persistent_store) {
        WKWebsiteDataStoreRef default_store = WKWebsiteDataStoreGetDefaultDataStore();
        if (persistent_store != default_store) WKRelease(persistent_store);
    }
    if (context) WKRelease(context);
}

struct ViewImpl {
    ViewState* state = nullptr;
    ViewCallbacks callbacks;
    WKPreferencesRef preferences = nullptr;
    WKUserContentControllerRef content = nullptr;
    WKViewRef view = nullptr;
    WKPageRef page = nullptr;
    View* owner = nullptr;
    ViewImpl* parent = nullptr;
    bool private_mode = false;
    bool wired = false;
    std::string default_ua;
    std::string custom_ua;
    bool error_page = false;
    std::string error_page_uri;
    int crashes = 0;
    std::string find_query;
    double scale = 1.0;

    ViewImpl(ViewState* s, bool priv, const ViewCallbacks& cb)
        : state(s), callbacks(cb), private_mode(priv) {}
    ~ViewImpl();

    void create_view();
    void attach();
    void apply_hardening();
    void apply_policies(const std::string& uri);
    std::string current_uri() const;
    std::string current_title() const;
    PermissionAnswer decide_permission(const std::string& setting, const std::string& combined,
        const std::string& what);
    void find_run(const std::string& text, const FindOptions& options, std::uint32_t max_matches,
        int direction);

    static void st_did_change_title(const void* client_info);
    static void st_did_change_active_url(const void* client_info);
    static void st_did_change_is_loading(const void* client_info);
    static void st_did_change_estimated_progress(const void* client_info);
    static void st_did_change_can_go_back(const void* client_info);
    static void st_did_change_can_go_forward(const void* client_info);
    static void nav_did_start_provisional(WKPageRef, WKNavigationRef, WKTypeRef, const void* client_info);
    static void nav_did_commit(WKPageRef, WKNavigationRef, WKTypeRef, const void* client_info);
    static void nav_did_finish(WKPageRef, WKNavigationRef, WKTypeRef, const void* client_info);
    static void nav_did_fail_provisional(WKPageRef, WKNavigationRef, WKErrorRef, WKTypeRef,
        const void* client_info);
    static void nav_web_process_did_terminate(WKPageRef, WKProcessTerminationReason,
        const void* client_info);
    static void nav_authentication_challenge(WKPageRef, WKAuthenticationChallengeRef,
        const void* client_info);
    static WKPageRef uic_create_new_page(WKPageRef, WKPageConfigurationRef, WKNavigationActionRef,
        WKWindowFeaturesRef, const void* client_info);
    static void uic_show_page(WKPageRef, const void* client_info);
    static void uic_close(WKPageRef, const void* client_info);
    static void uic_run_js_alert(WKPageRef, WKStringRef, WKFrameRef, WKSecurityOriginRef,
        WKPageRunJavaScriptAlertResultListenerRef, const void*);
    static void uic_run_js_confirm(WKPageRef, WKStringRef, WKFrameRef, WKSecurityOriginRef,
        WKPageRunJavaScriptConfirmResultListenerRef, const void*);
    static void uic_run_js_prompt(WKPageRef, WKStringRef, WKStringRef, WKFrameRef, WKSecurityOriginRef,
        WKPageRunJavaScriptPromptResultListenerRef, const void*);
    static bool uic_run_before_unload(WKPageRef, WKStringRef, WKFrameRef, const void*);
    static void uic_mouse_did_move_over_element(WKPageRef, WKHitTestResultRef, WKEventModifiers,
        WKTypeRef, const void* client_info);
    static void uic_decide_geolocation(WKPageRef, WKFrameRef, WKSecurityOriginRef,
        WKGeolocationPermissionRequestRef, const void* client_info);
    static void uic_decide_user_media(WKPageRef, WKFrameRef, WKSecurityOriginRef, WKSecurityOriginRef,
        WKUserMediaPermissionRequestRef, const void* client_info);
    static void find_did_find_string(WKPageRef, WKStringRef, unsigned match_count, const void* client_info);
    static void find_did_fail_to_find_string(WKPageRef, WKStringRef, const void* client_info);
    static void cm_get_menu(WKPageRef, WKArrayRef proposed_menu, WKArrayRef* new_menu,
        WKHitTestResultRef hit_test, WKTypeRef, const void* client_info);
};

std::string ViewImpl::current_uri() const {
    if (error_page) return error_page_uri;
    if (!page) return std::string();
    WkHold hold(WKPageCopyActiveURL(page));
    return hold.ref ? wkurl_to_std(static_cast<WKURLRef>(hold.ref)) : std::string();
}

std::string ViewImpl::current_title() const {
    if (!page) return std::string();
    WkHold hold(WKPageCopyTitle(page));
    return hold.ref ? wkstring_to_std(static_cast<WKStringRef>(hold.ref)) : std::string();
}

void ViewImpl::apply_hardening() {
    if (!preferences) return;
    const Hardening* hard = state && state->config.privacy ? &state->config.privacy->hardening : nullptr;
    WKPreferencesSetJavaScriptEnabled(preferences, (!hard || hard->javascript) ? TRUE : FALSE);
    WKPreferencesSetHyperlinkAuditingEnabled(preferences,
        (hard && hard->hyperlink_auditing) ? TRUE : FALSE);
    WKPreferencesSetDNSPrefetchingEnabled(preferences,
        (hard && hard->dns_prefetching) ? TRUE : FALSE);
    WKPreferencesSetMediaPlaybackRequiresUserGesture(preferences,
        (hard && hard->autoplay_blocked) ? TRUE : FALSE);
}

void ViewImpl::apply_policies(const std::string& uri) {
    if (!preferences || !content) return;
    apply_hardening();
    WKUserContentControllerRemoveAllUserScripts(content);
    WKUserContentControllerRemoveAllUserContentFilters(content);
    if (state && state->config.privacy && state->config.privacy->gpc) {
        add_user_script(content, gpc_user_script());
    }
    SiteDecision decision;
    if (state && state->config.privacy && state->config.sites) {
        decision = resolve_site(*state->config.privacy, *state->config.sites, uri);
    }
    WKPreferencesSetJavaScriptEnabled(preferences, decision.javascript ? TRUE : FALSE);
    if (decision.fingerprint.block_webgl) add_user_script(content, fp_scripts::WEBGL_BLOCK);
    if (decision.fingerprint.block_canvas_readback) add_user_script(content, fp_scripts::CANVAS_BLANK);
    if (decision.fingerprint.referrer != ReferrerTrim::Off) {
        add_user_script(content, fingerprint_referrer_script(decision.fingerprint.referrer));
    }
    if (decision.fingerprint.block_remote_fonts && state->fonts_filter) {
        WKUserContentControllerAddUserContentFilter(content, state->fonts_filter);
    }
    if (state && state->config.privacy && state->config.privacy->blocklist && state->blocklist_filter) {
        WKUserContentControllerAddUserContentFilter(content, state->blocklist_filter);
    }
    if (!decision.user_agent.empty()) {
        custom_ua = decision.user_agent;
    } else if (custom_ua.empty()) {
        custom_ua = default_ua;
    }
    if (page) {
        WkHold ua(make_wkstring(custom_ua));
        WKPageSetCustomUserAgent(page, static_cast<WKStringRef>(ua.ref));
    }
    if (decision.zoom > 0.0) {
        scale = decision.zoom;
        if (page) WKPageSetScaleFactor(page, scale, WKPointMake(0, 0));
    }
}

PermissionAnswer ViewImpl::decide_permission(const std::string& setting, const std::string& combined,
    const std::string& what) {
    if (!state) return PermissionAnswer::Deny;
    const Settings* set = state->config.settings;
    const std::string* chosen = combined.empty() ? (set ? &setting : nullptr) : &combined;
    if (!chosen) return PermissionAnswer::Deny;
    std::string host = host_from_uri(current_uri());
    if (host.empty()) host = "this site";
    PermissionChoice global = permission_choice_from(*chosen);
    SiteRule rule = state->config.sites
        ? state->config.sites->policy_for(host).permissions : SiteRule::Default;
    PermissionDecision decision = resolve_permission(global, rule);
    if (decision == PermissionDecision::Denied) return PermissionAnswer::Deny;
    if (decision == PermissionDecision::Granted) return PermissionAnswer::Allow;
    PermissionAnswer answer = callbacks.permission_prompt
        ? callbacks.permission_prompt(host, what) : PermissionAnswer::Deny;
    if ((answer == PermissionAnswer::Always || answer == PermissionAnswer::Never)
        && state->config.sites && !private_mode) {
        const SitePolicy* existing = state->config.sites->find(host);
        SitePolicy policy = existing ? *existing : SitePolicy{};
        policy.permissions = answer == PermissionAnswer::Always ? SiteRule::Allow : SiteRule::Deny;
        state->config.sites->set(host, policy);
        state->config.sites->save();
    }
    return answer == PermissionAnswer::Never ? PermissionAnswer::Deny : answer;
}

void ViewImpl::find_run(const std::string& text, const FindOptions& options, std::uint32_t max_matches,
    int direction) {
    if (!page || text.empty()) return;
    WKFindOptions flags = kWKFindOptionsShowOverlay;
    if (options.case_insensitive) {
        flags = static_cast<WKFindOptions>(flags | kWKFindOptionsCaseInsensitive);
    }
    if (options.wrap_around) {
        flags = static_cast<WKFindOptions>(flags | kWKFindOptionsWrapAround);
    }
    if (direction < 0) {
        flags = static_cast<WKFindOptions>(flags | kWKFindOptionsBackwards);
    }
    find_query = text;
    WkHold query(make_wkstring(text));
    WKPageFindString(page, static_cast<WKStringRef>(query.ref), flags,
        max_matches ? max_matches : 999u);
}

void ViewImpl::st_did_change_title(const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (self && self->callbacks.title_changed) self->callbacks.title_changed(self->current_title());
}

void ViewImpl::st_did_change_active_url(const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (self && self->callbacks.uri_changed) self->callbacks.uri_changed(self->current_uri());
}

void ViewImpl::st_did_change_is_loading(const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !self->page || !self->callbacks.loading_changed) return;
    self->callbacks.loading_changed(WKPageGetEstimatedProgress(self->page) < 1.0);
}

void ViewImpl::st_did_change_estimated_progress(const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (self && self->page && self->callbacks.load_progress) {
        self->callbacks.load_progress(WKPageGetEstimatedProgress(self->page));
    }
}

void ViewImpl::st_did_change_can_go_back(const void*) {}

void ViewImpl::st_did_change_can_go_forward(const void*) {}

void ViewImpl::nav_did_start_provisional(WKPageRef, WKNavigationRef, WKTypeRef, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self) return;
    if (self->error_page) {
        std::string url = self->current_uri();
        if (url != self->error_page_uri) {
            self->error_page = false;
            self->error_page_uri.clear();
        }
    }
    if (self->callbacks.loading_changed) self->callbacks.loading_changed(true);
}

void ViewImpl::nav_did_commit(WKPageRef, WKNavigationRef, WKTypeRef, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self) return;
    self->apply_policies(self->current_uri());
}

void ViewImpl::nav_did_finish(WKPageRef, WKNavigationRef, WKTypeRef, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self) return;
    self->crashes = 0;
    if (self->callbacks.loading_changed) self->callbacks.loading_changed(false);
    if (self->error_page) return;
    std::string uri = self->current_uri();
    if (self->callbacks.committed) {
        self->callbacks.committed(uri, self->current_title(), self->private_mode);
    }
}

void ViewImpl::nav_did_fail_provisional(WKPageRef, WKNavigationRef, WKErrorRef error, WKTypeRef,
    const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self) return;
    if (!error) return;
    int code = WKErrorGetErrorCode(error);
    if (code == kWKErrorCodeCancelled || code == kWKErrorCodeFrameLoadInterruptedByPolicyChange) return;
    WkHold failing(WKErrorCopyFailingURL(error));
    std::string url = failing.ref ? wkurl_to_std(static_cast<WKURLRef>(failing.ref)) : std::string();
    if (url.empty()) return;
    const char* heading = "Could not connect";
    switch (code) {
        case kWKErrorCodeCannotShowMIMEType: heading = "Can't show this content"; break;
        case kWKErrorCodeCannotShowURL: heading = "Unsupported address"; break;
        case kWKErrorCodeCannotUseRestrictedPort: heading = "Port blocked"; break;
        case kWKErrorCodeFileDoesNotExist: heading = "File not found"; break;
        case kWKErrorCodeFrameLoadBlockedByContentBlocker:
        case kWKErrorCodeFrameLoadBlockedByContentFilter:
        case kWKErrorCodeFrameLoadBlockedByRestrictions:
        case kWKErrorCodeCannotFindPlugIn:
        case kWKErrorCodeCannotLoadPlugIn:
        case kWKErrorCodeJavaUnavailable: heading = "Navigation blocked"; break;
        default: break;
    }
    WkHold description(WKErrorCopyLocalizedDescription(error));
    std::string html = branded_error_page(heading,
        description.ref ? wkstring_to_std(static_cast<WKStringRef>(description.ref)) : std::string(), url);
    if (!self->page) return;
    WkHold html_text(make_wkstring(html));
    WkHold base(make_wkurl(url));
    WKPageLoadHTMLString(self->page, static_cast<WKStringRef>(html_text.ref),
        static_cast<WKURLRef>(base.ref));
    self->error_page = true;
    self->error_page_uri = url;
    if (self->callbacks.load_failed) {
        self->callbacks.load_failed(url, heading,
            description.ref ? wkstring_to_std(static_cast<WKStringRef>(description.ref)) : std::string());
    }
}

void ViewImpl::nav_web_process_did_terminate(WKPageRef, WKProcessTerminationReason reason,
    const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self) return;
    ++self->crashes;
    const char* why = "crashed";
    if (reason == kWKProcessTerminationReasonExceededMemoryLimit) why = "ran out of memory";
    else if (reason == kWKProcessTerminationReasonRequestedByClient) why = "was stopped";
    std::string text = std::string("web process ") + why;
    if (self->crashes <= 2 && self->page) {
        WKPageReload(self->page);
        text += " — reloading";
    }
    if (self->callbacks.web_process_gone) self->callbacks.web_process_gone(text);
}

void ViewImpl::nav_authentication_challenge(WKPageRef, WKAuthenticationChallengeRef challenge,
    const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !challenge) return;
    WkHold space(WKAuthenticationChallengeGetProtectionSpace(challenge));
    bool server_trust = false;
    bool proxy = false;
    std::string host;
    int port = 0;
    std::string realm;
    if (space.ref) {
        WKProtectionSpaceRef protection = static_cast<WKProtectionSpaceRef>(space.ref);
        server_trust = WKProtectionSpaceGetAuthenticationScheme(protection)
            == kWKProtectionSpaceAuthenticationSchemeServerTrustEvaluationRequested;
        proxy = WKProtectionSpaceGetIsProxy(protection) != 0;
        WkHold host_hold(WKProtectionSpaceCopyHost(protection));
        host = host_hold.ref ? wkstring_to_std(static_cast<WKStringRef>(host_hold.ref)) : std::string();
        port = WKProtectionSpaceGetPort(protection);
        WkHold realm_hold(WKProtectionSpaceCopyRealm(protection));
        realm = realm_hold.ref ? wkstring_to_std(static_cast<WKStringRef>(realm_hold.ref)) : std::string();
    }
    WKAuthenticationDecisionListenerRef listener = WKAuthenticationChallengeGetDecisionListener(challenge);
    if (server_trust) {
        std::string who = host.empty() ? "this site" : host;
        if (self->callbacks.certificate_prompt) self->callbacks.certificate_prompt(who);
        if (listener) WKAuthenticationDecisionListenerRejectProtectionSpaceAndContinue(listener);
        return;
    }
    std::string label = (proxy ? "proxy " : "") + (host.empty() ? "this site" : host) + ":"
        + std::to_string(port);
    if (!realm.empty()) label += " (" + sanitize_dialog_text(realm, 256) + ")";
    std::string default_user;
    WkHold proposed(WKAuthenticationChallengeGetProposedCredential(challenge));
    if (proposed.ref) {
        WkHold user_hold(WKCredentialCopyUser(static_cast<WKCredentialRef>(proposed.ref)));
        if (user_hold.ref) default_user = wkstring_to_std(static_cast<WKStringRef>(user_hold.ref));
    }
    std::string user;
    std::string password;
    bool ok = self->callbacks.auth_prompt
        && self->callbacks.auth_prompt(label, user, password);
    if (!ok) {
        if (listener) WKAuthenticationDecisionListenerCancel(listener);
        return;
    }
    WkHold user_hold(make_wkstring(user));
    WkHold pass_hold(make_wkstring(password));
    WKCredentialRef credential = WKCredentialCreate(static_cast<WKStringRef>(user_hold.ref),
        static_cast<WKStringRef>(pass_hold.ref), kWKCredentialPersistenceForSession);
    if (listener && credential) WKAuthenticationDecisionListenerUseCredential(listener, credential);
    if (credential) WKRelease(credential);
}

WKPageRef ViewImpl::uic_create_new_page(WKPageRef, WKPageConfigurationRef, WKNavigationActionRef,
    WKWindowFeaturesRef, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !self->state || !self->callbacks.popup_created) return nullptr;
    ViewImpl* popup = new ViewImpl(self->state, self->private_mode, ViewCallbacks{});
    popup->parent = self;
    popup->create_view();
    popup->attach();
    View* wrapper = new View(popup);
    popup->owner = wrapper;
    self->callbacks.popup_created(wrapper);
    if (!popup->page) {
        delete wrapper;
        delete popup;
        return nullptr;
    }
    WKRetain(popup->page);
    return popup->page;
}

void ViewImpl::uic_show_page(WKPageRef, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !self->parent || !self->owner) return;
    if (self->parent->callbacks.popup_ready) self->parent->callbacks.popup_ready(self->owner);
}

void ViewImpl::uic_close(WKPageRef, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (self && self->callbacks.close_requested) self->callbacks.close_requested();
}

void ViewImpl::uic_run_js_alert(WKPageRef, WKStringRef alert_text, WKFrameRef, WKSecurityOriginRef,
    WKPageRunJavaScriptAlertResultListenerRef listener, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (self && self->callbacks.script_alert) {
        self->callbacks.script_alert(sanitize_dialog_text(wkstring_to_std(alert_text)));
    }
    if (listener) WKPageRunJavaScriptAlertResultListenerCall(listener);
}

void ViewImpl::uic_run_js_confirm(WKPageRef, WKStringRef message, WKFrameRef, WKSecurityOriginRef,
    WKPageRunJavaScriptConfirmResultListenerRef listener, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    bool confirmed = self && self->callbacks.script_confirm
        ? self->callbacks.script_confirm(sanitize_dialog_text(wkstring_to_std(message)))
        : false;
    if (listener) {
        WKPageRunJavaScriptConfirmResultListenerCall(listener, confirmed ? TRUE : FALSE);
    }
}

void ViewImpl::uic_run_js_prompt(WKPageRef, WKStringRef message, WKStringRef default_value,
    WKFrameRef, WKSecurityOriginRef,
    WKPageRunJavaScriptPromptResultListenerRef listener, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !self->callbacks.script_prompt) return;
    bool ok = true;
    std::string text = self->callbacks.script_prompt(
        sanitize_dialog_text(wkstring_to_std(message)),
        default_value ? wkstring_to_std(default_value) : std::string(), ok);
    if (!ok) return;
    WkHold result(make_wkstring(text));
    if (listener) {
        WKPageRunJavaScriptPromptResultListenerCall(listener, static_cast<WKStringRef>(result.ref));
    }
}

bool ViewImpl::uic_run_before_unload(WKPageRef, WKStringRef message, WKFrameRef, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !self->callbacks.script_confirm) return true;
    std::string body = sanitize_dialog_text(wkstring_to_std(message));
    if (body.empty()) body = "Leave this site?";
    return self->callbacks.script_confirm(body);
}

void ViewImpl::uic_mouse_did_move_over_element(WKPageRef, WKHitTestResultRef hit_test,
    WKEventModifiers, WKTypeRef, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !self->callbacks.link_hover) return;
    WkHold url(hit_test ? WKHitTestResultCopyAbsoluteLinkURL(hit_test) : nullptr);
    self->callbacks.link_hover(url.ref ? wkurl_to_std(static_cast<WKURLRef>(url.ref)) : std::string());
}

void ViewImpl::uic_decide_geolocation(WKPageRef, WKFrameRef, WKSecurityOriginRef,
    WKGeolocationPermissionRequestRef request, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !request) return;
    const Settings* set = self->state ? self->state->config.settings : nullptr;
    PermissionAnswer answer = self->decide_permission(
        set ? set->permissions_geolocation : std::string(), std::string(), "your location");
    if (answer == PermissionAnswer::Allow) {
        WKGeolocationPermissionRequestAllow(request);
    } else {
        WKGeolocationPermissionRequestDeny(request);
    }
}

void ViewImpl::uic_decide_user_media(WKPageRef, WKFrameRef, WKSecurityOriginRef, WKSecurityOriginRef,
    WKUserMediaPermissionRequestRef request, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !request) return;
    const Settings* set = self->state ? self->state->config.settings : nullptr;
    std::string combined;
    if (set) {
        if (set->permissions_camera == "deny" || set->permissions_microphone == "deny") {
            combined = "deny";
        } else if (set->permissions_camera == "ask" || set->permissions_microphone == "ask") {
            combined = "ask";
        } else {
            combined = "allow";
        }
    }
    PermissionAnswer answer = self->decide_permission(std::string(), combined,
        "camera and microphone");
    if (answer == PermissionAnswer::Allow) {
        WkHold none(make_wkstring(""));
        WKUserMediaPermissionRequestAllow(request, static_cast<WKStringRef>(none.ref),
            static_cast<WKStringRef>(none.ref));
    } else {
        WKUserMediaPermissionRequestDeny(request, kWKPermissionDenied);
    }
}

void ViewImpl::find_did_find_string(WKPageRef, WKStringRef, unsigned, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (self && self->callbacks.find_result) self->callbacks.find_result(true);
}

void ViewImpl::find_did_fail_to_find_string(WKPageRef, WKStringRef, const void* client_info) {
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (self && self->callbacks.find_result) self->callbacks.find_result(false);
}

void ViewImpl::cm_get_menu(WKPageRef, WKArrayRef proposed_menu, WKArrayRef* new_menu,
    WKHitTestResultRef hit_test, WKTypeRef, const void* client_info) {
    if (!new_menu) return;
    ViewImpl* self = static_cast<ViewImpl*>(const_cast<void*>(client_info));
    if (!self || !self->callbacks.context_menu) {
        *new_menu = proposed_menu;
        return;
    }
    ContextMenuInfo info;
    if (hit_test) {
        WkHold link(WKHitTestResultCopyAbsoluteLinkURL(hit_test));
        WkHold image(WKHitTestResultCopyAbsoluteImageURL(hit_test));
        WkHold media(WKHitTestResultCopyAbsoluteMediaURL(hit_test));
        info.link_uri = link.ref ? wkurl_to_std(static_cast<WKURLRef>(link.ref)) : std::string();
        info.image_uri = image.ref ? wkurl_to_std(static_cast<WKURLRef>(image.ref)) : std::string();
        info.media_uri = media.ref ? wkurl_to_std(static_cast<WKURLRef>(media.ref)) : std::string();
        info.link = !info.link_uri.empty();
        info.image = !info.image_uri.empty();
        info.media = !info.media_uri.empty();
        info.editable = WKHitTestResultIsContentEditable(hit_test) != 0;
    }
    info.page_uri = self->current_uri();
    if (self->callbacks.context_menu(info)) {
        *new_menu = WKMutableArrayCreate();
        return;
    }
    *new_menu = proposed_menu;
}

void ViewImpl::create_view() {
    if (!state) return;
    preferences = WKPreferencesCreate();
    content = WKUserContentControllerCreate();
    WkHold configuration(WKPageConfigurationCreate());
    WKPageConfigurationSetContext(static_cast<WKPageConfigurationRef>(configuration.ref), state->context);
    WKPageConfigurationSetPreferences(static_cast<WKPageConfigurationRef>(configuration.ref), preferences);
    WKPageConfigurationSetUserContentController(static_cast<WKPageConfigurationRef>(configuration.ref),
        content);
    WKPageConfigurationSetWebsiteDataStore(static_cast<WKPageConfigurationRef>(configuration.ref),
        state->store_for(private_mode));
    HWND parent = state->config.parent_window
        ? static_cast<HWND>(state->config.parent_window)
        : hidden_host_window();
    RECT rect = {};
    view = WKViewCreate(rect, static_cast<WKPageConfigurationRef>(configuration.ref), parent);
    if (!view) return;
    WKViewSetIsInWindow(view, true);
    page = WKViewGetPage(view);
    if (page) {
        WkHold ua(WKPageCopyUserAgent(page));
        default_ua = ua.ref ? wkstring_to_std(static_cast<WKStringRef>(ua.ref)) : std::string();
    }
    apply_policies(std::string());
}

void ViewImpl::attach() {
    if (wired || !page) return;
    WKPageStateClientV0 state_client = {};
    state_client.base.version = 0;
    state_client.base.clientInfo = this;
    state_client.didChangeTitle = st_did_change_title;
    state_client.didChangeActiveURL = st_did_change_active_url;
    state_client.didChangeIsLoading = st_did_change_is_loading;
    state_client.didChangeEstimatedProgress = st_did_change_estimated_progress;
    state_client.didChangeCanGoBack = st_did_change_can_go_back;
    state_client.didChangeCanGoForward = st_did_change_can_go_forward;
    WKPageSetPageStateClient(page, &state_client.base);

    WKPageNavigationClientV3 navigation = {};
    navigation.base.version = 3;
    navigation.base.clientInfo = this;
    navigation.didStartProvisionalNavigation = nav_did_start_provisional;
    navigation.didCommitNavigation = nav_did_commit;
    navigation.didFinishNavigation = nav_did_finish;
    navigation.didFailProvisionalNavigation = nav_did_fail_provisional;
    navigation.webProcessDidTerminate = nav_web_process_did_terminate;
    navigation.didReceiveAuthenticationChallenge = nav_authentication_challenge;
    WKPageSetPageNavigationClient(page, &navigation.base);

    WKPageUIClientV6 ui = {};
    ui.base.version = 6;
    ui.base.clientInfo = this;
    ui.createNewPage = uic_create_new_page;
    ui.showPage = uic_show_page;
    ui.close = uic_close;
    ui.runJavaScriptAlert = uic_run_js_alert;
    ui.runJavaScriptConfirm = uic_run_js_confirm;
    ui.runJavaScriptPrompt = uic_run_js_prompt;
    ui.runBeforeUnloadConfirmPanel = uic_run_before_unload;
    ui.mouseDidMoveOverElement = uic_mouse_did_move_over_element;
    ui.decidePolicyForGeolocationPermissionRequest = uic_decide_geolocation;
    ui.decidePolicyForUserMediaPermissionRequest = uic_decide_user_media;
    WKPageSetPageUIClient(page, &ui.base);

    WKPageFindClientV0 find = {};
    find.base.version = 0;
    find.base.clientInfo = this;
    find.didFindString = find_did_find_string;
    find.didFailToFindString = find_did_fail_to_find_string;
    WKPageSetPageFindClient(page, &find.base);

    WKPageContextMenuClientV2 context_menu = {};
    context_menu.base.version = 2;
    context_menu.base.clientInfo = this;
    context_menu.getContextMenuFromProposedMenu = cm_get_menu;
    WKPageSetPageContextMenuClient(page, &context_menu.base);

    if (state) state->views.push_back(this);
    wired = true;
}

ViewImpl::~ViewImpl() {
    if (state) {
        for (size_t i = 0; i < state->views.size(); ++i) {
            if (state->views[i] == this) {
                state->views.erase(state->views.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
    if (content) WKRelease(content);
    if (preferences) WKRelease(preferences);
    if (view) WKRelease(view);
}

void ViewState::refresh_view_policies() {
    std::vector<ViewImpl*> current = views;
    for (ViewImpl* view : current) {
        if (view) view->apply_policies(view->current_uri());
    }
}

struct ClearJoin {
    std::atomic<int> pending{0};
    SimpleCallback done;

    void finish_one() {
        if (--pending <= 0 && done) {
            SimpleCallback call = std::move(done);
            done = nullptr;
            call();
        }
    }
};

void clear_store_part(WKWebsiteDataStoreRef store, bool cookies, bool storage,
    const std::shared_ptr<ClearJoin>& join) {
    if (!store) return;
    if (cookies) {
        join->pending++;
        WKHTTPCookieStoreRef cookie_store = WKWebsiteDataStoreGetHTTPCookieStore(store);
        WKHTTPCookieStoreDeleteAllCookies(cookie_store, join.get(), [](void* context) {
            static_cast<ClearJoin*>(context)->finish_one();
        });
    }
    if (storage) {
        join->pending++;
        WKWebsiteDataStoreClearStorage(store, join.get(), [](void* context) {
            static_cast<ClearJoin*>(context)->finish_one();
        });
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

void ViewSession::fetch_website_data(const WebsiteDataCallback& done) {
    if (done) done({});
}

void ViewSession::remove_website_data(const std::vector<std::string>& origins, DataKind kinds,
                                      SimpleCallback done) {
    (void)origins;
    (void)kinds;
    if (done) done();
}

void ViewSession::clear_website_data(DataKind kinds, std::int64_t max_age_seconds,
                                     SimpleCallback done) {
    (void)max_age_seconds;
    bool cookies = has(kinds, DataKind::Cookies);
    bool storage = has(kinds, DataKind::Cache) || has(kinds, DataKind::LocalStorage)
        || has(kinds, DataKind::IndexedDB);
    if (!done) return;
    if ((!cookies && !storage) || !impl->persistent_store) {
        done();
        return;
    }
    auto join = std::make_shared<ClearJoin>();
    join->done = std::move(done);
    clear_store_part(impl->persistent_store, cookies, storage, join);
    clear_store_part(impl->private_store, cookies, storage, join);
    if (join->pending == 0) {
        SimpleCallback call = std::move(join->done);
        join->done = nullptr;
        if (call) call();
    }
}

View::View(ViewSession& session, bool private_mode, const ViewCallbacks& callbacks)
    : impl(new ViewImpl(session.impl.get(), private_mode, callbacks)) {
    impl->owner = this;
    impl->create_view();
    impl->attach();
}

View::View(ViewImpl* adopted) : impl(adopted) {
    impl->owner = this;
}

View::~View() = default;

void* View::native_widget() const {
    return impl->view ? static_cast<void*>(WKViewGetWindow(impl->view)) : nullptr;
}

void View::load_url(const std::string& url) {
    if (!impl->page || url.empty()) return;
    WkHold target(make_wkurl(url));
    WKPageLoadURL(impl->page, static_cast<WKURLRef>(target.ref));
}

void View::go_back() {
    if (impl->page && can_go_back()) WKPageGoBack(impl->page);
}

void View::go_forward() {
    if (impl->page && can_go_forward()) WKPageGoForward(impl->page);
}

bool View::can_go_back() const {
    return impl->page && WKPageCanGoBack(impl->page) != 0;
}

bool View::can_go_forward() const {
    return impl->page && WKPageCanGoForward(impl->page) != 0;
}

void View::reload() {
    if (impl->page) WKPageReload(impl->page);
}

void View::stop() {
    if (impl->page) WKPageStopLoading(impl->page);
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
    return impl->page ? WKPageGetScaleFactor(impl->page) : impl->scale;
}

void View::set_zoom(double level) {
    if (!impl->page) return;
    if (level < 0.3) level = 0.3;
    if (level > 3.0) level = 3.0;
    impl->scale = level;
    WKPageSetScaleFactor(impl->page, level, WKPointMake(0, 0));
}

void View::set_user_agent(const std::string& user_agent) {
    impl->custom_ua = user_agent.empty() ? impl->default_ua : user_agent;
    refresh_policy();
}

void View::set_callbacks(const ViewCallbacks& callbacks) {
    impl->callbacks = callbacks;
}

void View::refresh_policy() {
    impl->apply_policies(impl->current_uri());
}

SitePolicy View::active_site_policy() const {
    const SiteRules* sites = impl->state ? impl->state->config.sites : nullptr;
    if (!sites) return SitePolicy{};
    return sites->policy_for(host_from_uri(impl->current_uri()));
}

void View::find(const std::string& text, const FindOptions& options, std::uint32_t max_matches) {
    if (text.empty()) {
        find_finish();
        return;
    }
    impl->find_run(text, options, max_matches, 0);
}

void View::find_next() {
    if (!impl->find_query.empty()) {
        impl->find_run(impl->find_query, FindOptions{}, 999, 1);
    }
}

void View::find_previous() {
    if (!impl->find_query.empty()) {
        impl->find_run(impl->find_query, FindOptions{}, 999, -1);
    }
}

void View::find_finish() {
    impl->find_query.clear();
    if (impl->page) WKPageHideFindUI(impl->page);
}

std::string View::find_text() const {
    return impl->find_query;
}

void View::selected_text(const TextCallback& done) {
    if (done) done(std::string());
}

void View::execute_edit_command(EditCommand) {}

void View::show_devtools() {}

void View::hide_devtools() {}

bool View::devtools_attached() const {
    return false;
}

void View::save_mhtml(const std::string& path, SaveCallback done) {
    (void)path;
    if (done) done(false);
}

std::string suggested_save_name(const std::string& title, const std::string& uri) {
    std::string base = sanitize_filename(title, 96);
    if (base.empty() || base == "download") base = sanitize_filename(host_from_uri(uri), 96);
    if (base.empty() || base == "download") base = "page";
    bool has_ext = (base.size() >= 6 && base.compare(base.size() - 6, 6, ".mhtml") == 0)
        || (base.size() >= 4 && base.compare(base.size() - 4, 4, ".mht") == 0);
    return has_ext ? base : base + ".mhtml";
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
