#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <psapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "browsing_data.hpp"
#include "blocklist.hpp"
#include "config.hpp"
#include "fingerprint.hpp"
#include "fingerprint_scripts.hpp"
#include "history.hpp"
#include "perf.hpp"
#include "permissions.hpp"
#include "privacy.hpp"
#include "proxy_server.hpp"
#include "session.hpp"
#include "site_rules.hpp"
#include "ui_text.hpp"
#include "url.hpp"

#include <WebKit/WebKit2_C.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKContextDownloadClient.h>
#include <WebKit/WKView.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKPageFindClient.h>
#include <WebKit/WKFindOptions.h>
#include <WebKit/WKHitTestResultRef.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKWebsiteDataStoreRef.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKHTTPCookieStoreRef.h>
#include <WebKit/WKCookieManager.h>
#include <WebKit/WKPageStateClient.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPageContextMenuClient.h>
#include <WebKit/WKContextMenuItem.h>
#include <WebKit/WKContextMenuItemTypes.h>
#include <WebKit/WKBackForwardListRef.h>
#include <WebKit/WKBackForwardListItemRef.h>
#include <WebKit/WKPreferencesRefPrivate.h>
#include <WebKit/WKUserContentExtensionStoreRef.h>
#include <WebKit/WKGeolocationPermissionRequest.h>
#include <WebKit/WKUserMediaPermissionRequest.h>
#include <WebKit/WKAuthenticationChallenge.h>
#include <WebKit/WKAuthenticationDecisionListener.h>
#include <WebKit/WKCredential.h>
#include <WebKit/WKProtectionSpace.h>

int mustela_main(int argc, char** argv);

namespace {

using mustela::CookiePolicy;
using mustela::DataKind;
using mustela::FingerprintPolicy;
using mustela::Hardening;
using mustela::PermissionDecision;
using mustela::PrivacyPolicy;
using mustela::ReferrerTrim;
using mustela::Settings;
using mustela::SiteDecision;
using mustela::SiteRule;
using mustela::SiteRules;
using mustela::TabPrivacy;

enum : WORD {
    ID_BACK = 101,
    ID_FORWARD = 102,
    ID_RELOAD = 103,
    ID_NEW_TAB = 104,
    ID_CLOSE_TAB = 105,
    ID_PRIVATE = 106,
    ID_URL = 107,
    ID_TABS = 108,
    ID_STATUS = 109,
    ID_CLEAR_DATA = 110,
    ID_HISTORY = 111,
    ID_HISTORY_LIST = 112,
    ID_HISTORY_CLEAR = 113,
    ID_ACC_URL = 114,
    ID_ACC_NEW_TAB = 115,
    ID_ACC_CLOSE_TAB = 116,
    ID_ACC_RELOAD = 117,
    ID_ACC_CLEAR_DATA = 118,
    ID_ACC_HISTORY = 119,
    ID_ACC_BACK = 120,
    ID_ACC_FORWARD = 121,
    ID_FIND_EDIT = 122,
    ID_FIND_NEXT = 123,
    ID_FIND_PREV = 124,
    ID_FIND_CLOSE = 125,
    ID_ACC_FIND = 126,
    ID_FIND_COUNT = 127,
    ID_ACC_ZOOM_IN = 128,
    ID_ACC_ZOOM_OUT = 129,
    ID_ACC_ZOOM_RESET = 130,
    ID_ACC_FULLSCREEN = 131,
    ID_PERM_ALLOW = 300,
    ID_PERM_DENY = 301,
    ID_PERM_ALWAYS = 302,
    ID_PERM_NEVER = 303,
    ID_CRED_USER = 310,
    ID_CRED_PASS = 311,
    ID_PROMPT_TEXT = 200,
    ID_PROMPT_EDIT = 201
};

const UINT WM_APP_RELOAD_CHANGED = WM_APP + 1;
const UINT WM_APP_CLOSE_TAB = WM_APP + 2;
const UINT WM_APP_PROMPT_DONE = WM_APP + 3;
const UINT TIMER_FLASH = 1;
const UINT TIMER_RELOAD = 2;
const UINT TIMER_WATCH = 3;
const UINT TIMER_MEMORY = 4;
const UINT_PTR URL_SUBCLASS_ID = 1;
const WORD ID_URL_ENTER = 0x0900;

LRESULT CALLBACK url_edit_subclass(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR) {
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        SendMessageW(GetParent(wnd), WM_COMMAND, MAKEWPARAM(ID_URL, ID_URL_ENTER), 0);
        return 0;
    }
    return DefSubclassProc(wnd, message, wparam, lparam);
}

LRESULT CALLBACK find_edit_subclass(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR) {
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        SendMessageW(GetParent(wnd), WM_COMMAND, MAKEWPARAM(shift ? ID_FIND_PREV : ID_FIND_NEXT, 0), 0);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
        SendMessageW(GetParent(wnd), WM_COMMAND, MAKEWPARAM(ID_FIND_CLOSE, 0), 0);
        return 0;
    }
    return DefSubclassProc(wnd, message, wparam, lparam);
}

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

std::wstring get_window_text(HWND wnd) {
    int length = GetWindowTextLengthW(wnd);
    if (length <= 0) return std::wstring();
    std::wstring text(static_cast<size_t>(length), L'\0');
    int copied = GetWindowTextW(wnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(copied > 0 ? copied : 0));
    return text;
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

std::wstring app_base_dir() {
    PWSTR roaming = nullptr;
    std::wstring base;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming) == S_OK && roaming) {
        base = std::wstring(roaming) + L"\\mustela";
        CoTaskMemFree(roaming);
    } else {
        base = L"mustela";
    }
    create_dir_chain(base);
    return base;
}

std::string config_path(const char* name) {
    return to_utf8(app_base_dir()) + "\\" + name;
}

std::wstring profile_dir_wide() {
    return app_base_dir() + L"\\profiles\\default";
}

std::string profile_path(const char* name) {
    return to_utf8(profile_dir_wide()) + "\\" + name;
}

struct WinTab;

struct WinDownload {
    std::string name;
    std::uint64_t received = 0;
    std::int64_t total = 0;
};

struct Browser {
    Settings settings;
    PrivacyPolicy privacy;
    std::unique_ptr<SiteRules> sites;
    std::unique_ptr<mustela::HistoryStore> history;

    HWND window = nullptr;
    HWND back_btn = nullptr;
    HWND forward_btn = nullptr;
    HWND reload_btn = nullptr;
    HWND new_tab_btn = nullptr;
    HWND close_tab_btn = nullptr;
    HWND private_toggle = nullptr;
    HWND url_entry = nullptr;
    HWND tab_ctrl = nullptr;
    HWND status_bar = nullptr;
    HWND history_window = nullptr;
    HWND history_list = nullptr;
    HWND find_edit = nullptr;
    HWND find_count = nullptr;
    HWND find_prev_btn = nullptr;
    HWND find_next_btn = nullptr;
    HWND find_close_btn = nullptr;

    WKContextRef context = nullptr;
    WKWebsiteDataStoreRef persistent_store = nullptr;
    WKUserContentExtensionStoreRef filter_store = nullptr;
    WKUserContentFilterRef fonts_filter = nullptr;
    WKUserContentFilterRef blocklist_filter = nullptr;
    std::unique_ptr<mustela::ProxyServer> proxy;
    std::string session_file;
    bool fullscreen_active = false;
    WINDOWPLACEMENT saved_placement = {};
    std::unique_ptr<mustela::PerfLog> perf;
    std::int64_t memory_bytes = 0;
    bool memory_over = false;
    bool startup_marked = false;
    double startup_entry_ms = 0;
    std::map<WKDownloadRef, WinDownload> downloads;

    std::vector<std::unique_ptr<WinTab>> tabs;
    int active = -1;
    std::vector<std::string> history_urls;
    bool find_visible = false;
    std::string find_query;
    std::string hover_link;

    bool status_flash = false;
    std::atomic<bool> watcher_stop{false};
    HANDLE watcher_thread = nullptr;
};

struct WinTab {
    WKPreferencesRef preferences = nullptr;
    WKUserContentControllerRef content = nullptr;
    WKWebsiteDataStoreRef store = nullptr;
    bool owns_store = false;
    WKViewRef view = nullptr;
    WKPageRef page = nullptr;
    TabPrivacy privacy = TabPrivacy::Normal;
    std::string uri;
    std::string title;
    double scale = 1.0;
    int crashes = 0;
    mustela::LoadTimer load;
};

Browser* browser = nullptr;

WinTab* active_tab(Browser& b) {
    if (b.active < 0 || b.active >= static_cast<int>(b.tabs.size())) return nullptr;
    return b.tabs[static_cast<size_t>(b.active)].get();
}

int tab_index(const Browser& b, const WinTab* tab) {
    for (size_t i = 0; i < b.tabs.size(); ++i) {
        if (b.tabs[i].get() == tab) return static_cast<int>(i);
    }
    return -1;
}

WKHTTPCookieAcceptPolicy accept_policy_for(CookiePolicy policy) {
    if (policy == CookiePolicy::Always) return kWKHTTPCookieAcceptPolicyAlways;
    if (policy == CookiePolicy::Never) return kWKHTTPCookieAcceptPolicyNever;
    return kWKHTTPCookieAcceptPolicyOnlyFromMainDocumentDomain;
}

void noop_store_callback(void*) {}

void apply_cookie_policy(WKWebsiteDataStoreRef store, CookiePolicy policy) {
    WKHTTPCookieStoreRef cookies = WKWebsiteDataStoreGetHTTPCookieStore(store);
    WKHTTPCookieStoreSetHTTPCookieAcceptPolicy(cookies, accept_policy_for(policy), nullptr, noop_store_callback);
}

void apply_itp(WKWebsiteDataStoreRef store, bool enabled) {
    WKWebsiteDataStoreSetResourceLoadStatisticsEnabled(store, enabled ? TRUE : FALSE);
}

void refresh_status(Browser& b);
void flash_status(Browser& b, const std::string& text);
void mark_startup(Browser& b);
void close_tab(Browser& b, int index);
void layout_window(Browser& b, int width, int height);
void find_run(Browser& b, int direction);
void show_find_ui(Browser& b, bool show);
std::unique_ptr<WinTab> make_tab(TabPrivacy request);
std::string run_modal_prompt(const std::string& message, const std::string& default_text);

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
    record_perf(b, "startup", elapsed, 0, "to-first-paint");
    if (b.settings.startup_budget_ms > 0) {
        std::string text = "startup " + std::to_string(static_cast<int>(elapsed)) + "ms";
        if (!mustela::within_budget(b.settings.startup_budget_ms, elapsed)) text += " over budget";
        flash_status(b, text);
    }
}

void sample_memory(Browser& b) {
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    std::int64_t self_bytes = GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
        ? static_cast<std::int64_t>(counters.WorkingSetSize)
        : 0;

    std::int64_t children_bytes = 0;
    DWORD self_id = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ParentProcessID != self_id) continue;
                HANDLE child = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
                if (!child) continue;
                PROCESS_MEMORY_COUNTERS child_counters = {};
                child_counters.cb = sizeof(child_counters);
                if (GetProcessMemoryInfo(child, &child_counters, sizeof(child_counters))) {
                    children_bytes += static_cast<std::int64_t>(child_counters.WorkingSetSize);
                }
                CloseHandle(child);
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    b.memory_bytes = self_bytes + children_bytes;
    bool over = mustela::ceiling_exceeded(b.settings.memory_ceiling_mb, b.memory_bytes);
    record_perf(b, "memory", 0.0, b.memory_bytes, "rss");
    if (over != b.memory_over) {
        b.memory_over = over;
        refresh_status(b);
    }
}

void apply_process_model(WKContextRef context, const std::string& name) {
    WKProcessModel model = mustela::process_model_from(name) == mustela::ProcessModel::Shared
        ? kWKProcessModelSharedSecondaryProcess
        : kWKProcessModelMultipleSecondaryProcesses;
    WKContextSetProcessModel(context, model);
}

const double zoom_steps[] = {0.3, 0.5, 0.67, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0};

double zoom_next(double current, int direction) {
    const size_t count = sizeof(zoom_steps) / sizeof(zoom_steps[0]);
    if (direction > 0) {
        for (size_t i = 0; i < count; ++i) {
            if (zoom_steps[i] > current + 0.001) return zoom_steps[i];
        }
        return zoom_steps[count - 1];
    }
    if (direction < 0) {
        for (size_t i = count; i > 0; --i) {
            if (zoom_steps[i - 1] < current - 0.001) return zoom_steps[i - 1];
        }
        return zoom_steps[0];
    }
    return 1.0;
}

void apply_tab_scale(WinTab& tab) {
    if (tab.page) WKPageSetScaleFactor(tab.page, tab.scale, WKPointMake(0, 0));
}

#pragma warning(push)
#pragma warning(disable : 4996)
void harden_preferences(WKPreferencesRef preferences, const Hardening& hardening) {
    WKPreferencesSetJavaScriptEnabled(preferences, hardening.javascript ? TRUE : FALSE);
    WKPreferencesSetHyperlinkAuditingEnabled(preferences, hardening.hyperlink_auditing ? TRUE : FALSE);
    WKPreferencesSetDNSPrefetchingEnabled(preferences, hardening.dns_prefetching ? TRUE : FALSE);
    WKPreferencesSetMediaPlaybackRequiresUserGesture(preferences, hardening.autoplay_blocked ? TRUE : FALSE);
}
#pragma warning(pop)

void add_user_script(WKUserContentControllerRef content, const std::string& source) {
    WkHold text(make_wkstring(source));
    WKUserScriptRef script = WKUserScriptCreateWithSource(static_cast<WKStringRef>(text.ref),
        kWKInjectAtDocumentStart, false);
    if (!script) return;
    WKUserContentControllerAddUserScript(content, script);
    WKRelease(script);
}

void apply_fingerprint(WinTab& tab, const FingerprintPolicy& fp) {
    WKUserContentControllerRemoveAllUserScripts(tab.content);
    WKUserContentControllerRemoveAllUserContentFilters(tab.content);
    if (browser->privacy.gpc) add_user_script(tab.content, mustela::gpc_user_script());
    if (fp.block_webgl) add_user_script(tab.content, mustela::fp_scripts::WEBGL_BLOCK);
    if (fp.block_canvas_readback) add_user_script(tab.content, mustela::fp_scripts::CANVAS_BLANK);
    if (fp.referrer != ReferrerTrim::Off) {
        add_user_script(tab.content, mustela::fingerprint_referrer_script(fp.referrer));
    }
    if (fp.block_remote_fonts && browser->fonts_filter) {
        WKUserContentControllerAddUserContentFilter(tab.content, browser->fonts_filter);
    }
    if (browser->privacy.blocklist && browser->blocklist_filter) {
        WKUserContentControllerAddUserContentFilter(tab.content, browser->blocklist_filter);
    }
}

void apply_view_policy(WinTab& tab, const std::string& uri) {
    if (!browser || !browser->sites) return;
    SiteDecision decision = mustela::resolve_site(browser->privacy, *browser->sites, uri);
    WKPreferencesSetJavaScriptEnabled(tab.preferences, decision.javascript ? TRUE : FALSE);
    apply_fingerprint(tab, decision.fingerprint);
}

void apply_policies_to_tabs(Browser& b) {
    for (std::unique_ptr<WinTab>& tab : b.tabs) {
        harden_preferences(tab->preferences, b.privacy.hardening);
        apply_cookie_policy(tab->store, b.privacy.cookies);
        apply_itp(tab->store, b.privacy.hardening.tracking_prevention);
        apply_view_policy(*tab, tab->uri);
    }
}

void on_fonts_filter_compiled(WKUserContentFilterRef filter, WKUserContentExtensionStoreResult result, void*) {
    if (result != kWKUserContentExtensionStoreSuccess || !filter || !browser) return;
    WKRetain(filter);
    browser->fonts_filter = filter;
    for (std::unique_ptr<WinTab>& tab : browser->tabs) {
        std::string uri = tab->uri;
        if (!uri.empty()) apply_view_policy(*tab, uri);
    }
}

void compile_fonts_filter(Browser& b) {
    static const char fonts_filter_json[] =
        "[{\"trigger\":{\"url-filter\":\".*\",\"resource-type\":[\"font\"]},\"action\":{\"type\":\"block\"}}]";
    WkHold json(make_wkstring(fonts_filter_json));
    WkHold identifier(make_wkstring("block-remote-fonts"));
    WKUserContentExtensionStoreCompile(b.filter_store, static_cast<WKStringRef>(identifier.ref),
        static_cast<WKStringRef>(json.ref), nullptr, on_fonts_filter_compiled);
}

void on_blocklist_filter_compiled(WKUserContentFilterRef filter, WKUserContentExtensionStoreResult result, void*) {
    if (result != kWKUserContentExtensionStoreSuccess || !filter || !browser) return;
    WKRetain(filter);
    browser->blocklist_filter = filter;
    for (std::unique_ptr<WinTab>& tab : browser->tabs) {
        std::string uri = tab->uri;
        if (!uri.empty()) apply_view_policy(*tab, uri);
    }
}

void compile_blocklist_filter(Browser& b) {
    if (!b.privacy.blocklist) return;
    mustela::Blocklist list = mustela::default_blocklist(b.settings.blocklist_file);
    std::string json = mustela::blocklist_rules_json(list.domains());
    if (json.empty()) return;
    WkHold json_hold(make_wkstring(json));
    WkHold identifier(make_wkstring("mustela-blocklist"));
    WKUserContentExtensionStoreCompile(b.filter_store, static_cast<WKStringRef>(identifier.ref),
        static_cast<WKStringRef>(json_hold.ref), nullptr, on_blocklist_filter_compiled);
}

void clear_browsing_data(Browser& b) {
    std::vector<WKWebsiteDataStoreRef> stores;
    stores.push_back(b.persistent_store);
    for (std::unique_ptr<WinTab>& tab : b.tabs) {
        if (tab->owns_store) stores.push_back(tab->store);
    }
    for (WKWebsiteDataStoreRef store : stores) {
        if (!store) continue;
        WKHTTPCookieStoreRef cookies = WKWebsiteDataStoreGetHTTPCookieStore(store);
        WKHTTPCookieStoreDeleteAllCookies(cookies, nullptr, noop_store_callback);
        WKWebsiteDataStoreClearStorage(store, nullptr, noop_store_callback);
    }
}

void save_session_now(Browser& b) {
    if (!b.settings.session_restore || b.session_file.empty()) return;
    mustela::SessionData data;
    data.active = b.active;
    for (std::unique_ptr<WinTab>& tab : b.tabs) {
        if (tab->privacy == TabPrivacy::Private) continue;
        mustela::SessionTab entry;
        entry.url = tab->uri;
        entry.title = tab->title;
        if (tab->page) {
            WKBackForwardListRef list = WKPageGetBackForwardList(tab->page);
            if (list) {
                unsigned back_count = WKBackForwardListGetBackListCount(list);
                for (unsigned i = back_count; i >= 1; --i) {
                    WKBackForwardListItemRef item = WKBackForwardListGetItemAtIndex(list, -static_cast<int>(i));
                    if (item) {
                        WkHold url(WKBackForwardListItemCopyURL(item));
                        if (url.ref) entry.back.push_back(wkurl_to_std(static_cast<WKURLRef>(url.ref)));
                    }
                }
                unsigned forward_count = WKBackForwardListGetForwardListCount(list);
                for (unsigned i = 1; i <= forward_count; ++i) {
                    WKBackForwardListItemRef item = WKBackForwardListGetItemAtIndex(list, static_cast<int>(i));
                    if (item) {
                        WkHold url(WKBackForwardListItemCopyURL(item));
                        if (url.ref) entry.forward.push_back(wkurl_to_std(static_cast<WKURLRef>(url.ref)));
                    }
                }
            }
        }
        data.tabs.push_back(std::move(entry));
    }
    if (data.active < 0 || data.active >= static_cast<int>(data.tabs.size())) {
        data.active = data.tabs.empty() ? 0 : static_cast<int>(data.tabs.size()) - 1;
    }
    mustela::save_session(b.session_file, data);
}

void enter_fullscreen_window(Browser& b) {
    if (b.fullscreen_active || !b.window) return;
    b.saved_placement.length = sizeof(WINDOWPLACEMENT);
    if (!GetWindowPlacement(b.window, &b.saved_placement)) return;
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(b.window, MONITOR_DEFAULTTOPRIMARY), &monitor)) return;
    LONG_PTR style = GetWindowLongPtrW(b.window, GWL_STYLE);
    SetWindowLongPtrW(b.window, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
    SetWindowPos(b.window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
        monitor.rcMonitor.right - monitor.rcMonitor.left,
        monitor.rcMonitor.bottom - monitor.rcMonitor.top,
        SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    b.fullscreen_active = true;
    flash_status(b, "fullscreen — F11 to exit");
}

void exit_fullscreen_window(Browser& b) {
    if (!b.fullscreen_active || !b.window) return;
    LONG_PTR style = GetWindowLongPtrW(b.window, GWL_STYLE);
    SetWindowLongPtrW(b.window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
    SetWindowPlacement(b.window, &b.saved_placement);
    SetWindowPos(b.window, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    b.fullscreen_active = false;
}

std::string tab_label(const WinTab& tab) {
    std::string label;
    if (tab.privacy == TabPrivacy::Private) label = "private · ";
    label += tab.title.empty() ? tab.uri.empty() ? "New tab" : tab.uri : tab.title;
    return label;
}

void update_tab_item(Browser& b, int index) {
    WinTab& tab = *b.tabs[static_cast<size_t>(index)];
    TCITEMW item = {};
    item.mask = TCIF_TEXT;
    std::wstring label = to_wide(tab_label(tab));
    item.pszText = label.data();
    SendMessageW(b.tab_ctrl, TCM_SETITEMW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&item));
}

void update_window_title(Browser& b) {
    WinTab* tab = active_tab(b);
    if (!tab || (tab->title.empty() && tab->uri.empty())) {
        SetWindowTextW(b.window, L"Mustela");
        return;
    }
    std::string title = tab->title.empty() ? tab->uri : tab->title;
    SetWindowTextW(b.window, to_wide(title + " — Mustela").c_str());
}

std::string compose_status(Browser& b) {
    WinTab* tab = active_tab(b);
    std::string text;
    if (tab && tab->privacy == TabPrivacy::Private) text = "private · ";
    text += std::string("cookies ") + mustela::cookie_policy_name(b.privacy.cookies);
    if (b.privacy.hardening.tracking_prevention) text += " · itp";
    if (b.privacy.fingerprint.active()) text += " · fp";
    if (b.memory_over) {
        text += " · mem " + std::to_string(static_cast<int>(b.memory_bytes / (1024 * 1024))) + " MB";
    }
    if (!b.downloads.empty()) {
        std::string line;
        if (b.downloads.size() == 1) {
            const WinDownload& state = b.downloads.begin()->second;
            if (state.total > 0 && state.received <= static_cast<std::uint64_t>(state.total)) {
                double pct = static_cast<double>(state.received) / static_cast<double>(state.total);
                line = "↓ " + state.name + " " + std::to_string(static_cast<int>(pct * 100.0)) + "%";
            } else {
                line = "↓ " + state.name;
            }
        } else {
            line = "↓ " + std::to_string(b.downloads.size()) + " downloads";
        }
        text = line + " · " + text;
    }
    return text;
}

void refresh_status(Browser& b) {
    if (!b.status_bar) return;
    std::string text;
    if (!b.hover_link.empty()) {
        text = "→ " + b.hover_link;
    } else {
        text = compose_status(b);
        WinTab* tab = active_tab(b);
        if (tab && tab->page && WKPageGetEstimatedProgress(tab->page) < 1.0) {
            double progress = WKPageGetEstimatedProgress(tab->page);
            text = "loading " + std::to_string(static_cast<int>(progress * 100.0)) + "% · " + text;
        }
    }
    SendMessageW(b.status_bar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(to_wide(text).c_str()));
}

void flash_status(Browser& b, const std::string& text) {
    if (!b.status_bar) return;
    b.status_flash = true;
    SendMessageW(b.status_bar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(to_wide(text).c_str()));
    SetTimer(b.window, TIMER_FLASH, 3000, nullptr);
}

void refresh_chrome(Browser& b) {
    WinTab* tab = active_tab(b);
    if (tab) {
        if (GetFocus() != b.url_entry) {
            SetWindowTextW(b.url_entry, to_wide(tab->uri).c_str());
        }
        EnableWindow(b.back_btn, tab->page && WKPageCanGoBack(tab->page) ? TRUE : FALSE);
        EnableWindow(b.forward_btn, tab->page && WKPageCanGoForward(tab->page) ? TRUE : FALSE);
        EnableWindow(b.reload_btn, tab->page ? TRUE : FALSE);
        EnableWindow(b.close_tab_btn, TRUE);
        int index = tab_index(b, tab);
        if (index >= 0) update_tab_item(b, index);
    } else {
        SetWindowTextW(b.url_entry, L"");
        EnableWindow(b.back_btn, FALSE);
        EnableWindow(b.forward_btn, FALSE);
        EnableWindow(b.reload_btn, FALSE);
        EnableWindow(b.close_tab_btn, FALSE);
    }
    update_window_title(b);
    refresh_status(b);
}

void position_view(Browser& b, WinTab& tab) {
    RECT client = {};
    GetClientRect(b.window, &client);
    RECT tab_area = client;
    SendMessageW(b.tab_ctrl, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&tab_area));
    SetWindowPos(WKViewGetWindow(tab.view), HWND_TOP,
        tab_area.left, tab_area.top,
        tab_area.right - tab_area.left, tab_area.bottom - tab_area.top, SWP_NOACTIVATE);
}

void switch_tab(Browser& b, int index) {
    if (index < 0 || index >= static_cast<int>(b.tabs.size())) return;
    if (b.active >= 0 && b.active < static_cast<int>(b.tabs.size()) && b.active != index) {
        ShowWindow(WKViewGetWindow(b.tabs[static_cast<size_t>(b.active)]->view), SW_HIDE);
    }
    b.active = index;
    b.hover_link.clear();
    SendMessageW(b.tab_ctrl, TCM_SETCURSEL, static_cast<WPARAM>(index), 0);
    WinTab& tab = *b.tabs[static_cast<size_t>(index)];
    position_view(b, tab);
    ShowWindow(WKViewGetWindow(tab.view), SW_SHOW);
    SetFocus(WKViewGetWindow(tab.view));
    refresh_chrome(b);
    save_session_now(b);
    if (b.find_visible) find_run(b, 0);
}

void on_tab_state_changed(WinTab& tab) {
    if (!browser) return;
    int index = tab_index(*browser, &tab);
    if (index >= 0) update_tab_item(*browser, index);
    if (browser->active == index) refresh_chrome(*browser);
}

void did_change_title(const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!tab.page || !browser) return;
    WkHold hold(WKPageCopyTitle(tab.page));
    tab.title = wkstring_to_std(static_cast<WKStringRef>(hold.ref));
    on_tab_state_changed(tab);
}

void did_change_active_url(const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!tab.page || !browser) return;
    WkHold hold(WKPageCopyActiveURL(tab.page));
    std::string uri = wkurl_to_std(static_cast<WKURLRef>(hold.ref));
    if (uri == tab.uri) return;
    tab.uri = uri;
    if (!uri.empty()) apply_view_policy(tab, uri);
    on_tab_state_changed(tab);
}

void did_change_is_loading(const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!tab.page || !browser) return;
    if (WKPageGetEstimatedProgress(tab.page) >= 1.0) refresh_status(*browser);
}

void did_change_estimated_progress(const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!tab.page || !browser) return;
    int index = tab_index(*browser, &tab);
    if (browser->active == index) refresh_status(*browser);
}

void did_change_can_go_back(const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    on_tab_state_changed(tab);
}

void did_change_can_go_forward(const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    on_tab_state_changed(tab);
}

void did_start_provisional_navigation(WKPageRef, WKNavigationRef, WKTypeRef, const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    tab.load.start(mustela::Stopwatch::now_ms());
}

void did_commit_navigation(WKPageRef, WKNavigationRef, WKTypeRef, const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!browser) return;
    apply_view_policy(tab, tab.uri);
    apply_tab_scale(tab);
}

void did_finish_navigation(WKPageRef, WKNavigationRef, WKTypeRef, const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!browser || tab.uri.empty()) return;
    tab.crashes = 0;
    if (browser->privacy.records_history(tab.privacy)) {
        browser->history->record(tab.uri, tab.title, static_cast<std::int64_t>(std::time(nullptr)));
    }
    double elapsed = tab.load.stop(mustela::Stopwatch::now_ms());
    if (elapsed >= 0.0) {
        record_perf(*browser, "pageload", elapsed, 0, tab.uri);
    }
    save_session_now(*browser);
    on_tab_state_changed(tab);
}

void did_fail_provisional_navigation(WKPageRef, WKNavigationRef, WKErrorRef, WKTypeRef, const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!browser) return;
    tab.load.cancel();
    on_tab_state_changed(tab);
    flash_status(*browser, "load failed");
}

int append_tab(Browser& b, std::unique_ptr<WinTab> tab) {
    WinTab* raw = tab.get();
    b.tabs.push_back(std::move(tab));
    int index = static_cast<int>(b.tabs.size()) - 1;

    TCITEMW item = {};
    item.mask = TCIF_TEXT;
    std::wstring label = to_wide(tab_label(*raw));
    item.pszText = label.data();
    SendMessageW(b.tab_ctrl, TCM_INSERTITEMW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&item));
    return index;
}

std::string dialog_text(WKStringRef text) {
    return mustela::sanitize_dialog_text(wkstring_to_std(text));
}

WKPageRef uic_create_new_page(WKPageRef, WKPageConfigurationRef, WKNavigationActionRef, WKWindowFeaturesRef,
    const void* client_info) {
    WinTab& origin = *static_cast<WinTab*>(const_cast<void*>(client_info));
    int index = append_tab(*browser, make_tab(origin.privacy));
    WinTab* popup = browser->tabs[static_cast<size_t>(index)].get();
    WKRetain(popup->page);
    return popup->page;
}

void uic_show_page(WKPageRef, const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    int index = tab_index(*browser, &tab);
    if (index >= 0) switch_tab(*browser, index);
}

void uic_close(WKPageRef, const void* client_info) {
    PostMessageW(browser->window, WM_APP_CLOSE_TAB,
        reinterpret_cast<WPARAM>(const_cast<void*>(client_info)), 0);
}

void uic_run_js_alert(WKPageRef, WKStringRef alert_text, WKFrameRef, WKSecurityOriginRef,
    WKPageRunJavaScriptAlertResultListenerRef listener, const void*) {
    std::string body = dialog_text(alert_text);
    MessageBoxW(browser->window, to_wide(body).c_str(), L"Mustela",
        MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
    if (listener) WKPageRunJavaScriptAlertResultListenerCall(listener);
}

void uic_run_js_confirm(WKPageRef, WKStringRef message, WKFrameRef, WKSecurityOriginRef,
    WKPageRunJavaScriptConfirmResultListenerRef listener, const void*) {
    std::string body = dialog_text(message);
    int choice = MessageBoxW(browser->window, to_wide(body).c_str(), L"Mustela",
        MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
    if (listener) WKPageRunJavaScriptConfirmResultListenerCall(listener, choice == IDYES ? TRUE : FALSE);
}

void uic_run_js_prompt(WKPageRef, WKStringRef message, WKStringRef default_value, WKFrameRef, WKSecurityOriginRef,
    WKPageRunJavaScriptPromptResultListenerRef listener, const void*) {
    std::string value = run_modal_prompt(wkstring_to_std(message),
        default_value ? wkstring_to_std(default_value) : std::string());
    WkHold result(make_wkstring(value));
    if (listener) WKPageRunJavaScriptPromptResultListenerCall(listener, static_cast<WKStringRef>(result.ref));
}

bool uic_run_before_unload(WKPageRef, WKStringRef message, WKFrameRef, const void*) {
    std::string body = dialog_text(message);
    if (body.empty()) body = "Leave this site?";
    int choice = MessageBoxW(browser->window, to_wide(body).c_str(), L"Leave site? — Mustela",
        MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
    return choice == IDYES;
}

void download_did_start(WKContextRef, WKDownloadRef download, const void*) {
    if (!browser || !download) return;
    browser->downloads[download] = WinDownload{};
}

std::string download_directory(const Browser& b) {
    if (!b.settings.downloads_dir.empty()) return b.settings.downloads_dir;
    std::string dir;
    PWSTR downloads = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads) == S_OK && downloads) {
        dir = to_utf8(downloads);
        CoTaskMemFree(downloads);
    }
    if (dir.empty()) dir = to_utf8(app_base_dir()) + "\\downloads";
    create_dir_chain(to_wide(dir));
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

WKStringRef download_decide_destination(WKContextRef, WKDownloadRef download, WKStringRef filename,
    bool* allow_overwrite, const void*) {
    Browser& b = *browser;
    if (allow_overwrite) *allow_overwrite = false;
    if (!download) return nullptr;
    std::string suggested = filename ? wkstring_to_std(filename) : std::string();
    std::string name = mustela::sanitize_filename(suggested.empty() ? "download" : suggested);
    std::string dir = download_directory(b);
    std::string path;
    if (b.settings.downloads_mode == "ask") {
        wchar_t buffer[MAX_PATH * 2] = {};
        std::wstring wide_name = to_wide(name);
        lstrcpynW(buffer, wide_name.c_str(), MAX_PATH * 2);
        std::wstring wide_dir = to_wide(dir);
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = b.window;
        ofn.lpstrFilter = L"All files (*.*)\0*.*\0";
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = MAX_PATH * 2;
        ofn.lpstrInitialDir = wide_dir.c_str();
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        if (!GetSaveFileNameW(&ofn)) return nullptr;
        path = to_utf8(buffer);
    } else {
        path = download_path_for(dir, name);
    }
    std::string shown = path;
    size_t slash = shown.find_last_of("\\/");
    if (slash != std::string::npos) shown = shown.substr(slash + 1);
    auto it = b.downloads.find(download);
    if (it != b.downloads.end()) it->second.name = shown;
    flash_status(b, "downloading " + shown);
    return make_wkstring(path);
}

void download_did_receive_response(WKContextRef, WKDownloadRef download, WKURLResponseRef response, const void*) {
    if (!browser || !download) return;
    auto it = browser->downloads.find(download);
    if (it == browser->downloads.end()) return;
    it->second.total = response ? WKURLResponseGetExpectedContentLength(response) : 0;
}

void download_did_receive_data(WKContextRef, WKDownloadRef download, std::uint64_t length, const void*) {
    if (!browser || !download) return;
    auto it = browser->downloads.find(download);
    if (it == browser->downloads.end()) return;
    it->second.received += length;
    if (browser->hover_link.empty()) refresh_status(*browser);
}

void download_did_finish(WKContextRef, WKDownloadRef download, const void*) {
    Browser& b = *browser;
    auto it = b.downloads.find(download);
    std::string name = it != b.downloads.end() ? it->second.name : std::string();
    if (it != b.downloads.end()) b.downloads.erase(it);
    flash_status(b, "download finished: " + name);
}

void download_did_fail(WKContextRef, WKDownloadRef download, WKErrorRef, const void*) {
    Browser& b = *browser;
    auto it = b.downloads.find(download);
    std::string name = it != b.downloads.end() ? it->second.name : std::string();
    if (it != b.downloads.end()) b.downloads.erase(it);
    flash_status(b, "download failed: " + name);
}

void download_did_cancel(WKContextRef, WKDownloadRef download, const void*) {
    Browser& b = *browser;
    auto it = b.downloads.find(download);
    if (it != b.downloads.end()) b.downloads.erase(it);
    flash_status(b, "download cancelled");
}

struct PermState {
    int answer = 0;
};

LRESULT CALLBACK perm_proc(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam) {
    PermState* state = reinterpret_cast<PermState*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return 0;
        }
        case WM_COMMAND: {
            if (!state) return 0;
            WORD id = LOWORD(wparam);
            if (id == ID_PERM_ALLOW || id == ID_PERM_DENY || id == ID_PERM_ALWAYS || id == ID_PERM_NEVER) {
                state->answer = id;
                DestroyWindow(wnd);
            } else if (id == IDCANCEL) {
                state->answer = 0;
                DestroyWindow(wnd);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(wnd);
            return 0;
        case WM_DESTROY:
            PostThreadMessageW(GetCurrentThreadId(), WM_APP_PROMPT_DONE, 0, 0);
            return 0;
        default:
            return DefWindowProcW(wnd, message, wparam, lparam);
    }
}

int run_modal_permission(const std::string& host, const std::string& what) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW perm_class = {};
        perm_class.lpfnWndProc = perm_proc;
        perm_class.hInstance = GetModuleHandleW(nullptr);
        perm_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        perm_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        perm_class.lpszClassName = L"MUSTELA_PERM_WND";
        RegisterClassW(&perm_class);
        registered = true;
    }
    PermState state;
    HWND owner = browser->window;
    RECT owner_rect = {};
    GetWindowRect(owner, &owner_rect);
    int width = 460;
    int height = 200;
    int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 3;
    std::string text = host + " wants to use " + what;
    HWND wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MUSTELA_PERM_WND", L"Permission — Mustela",
        WS_CAPTION | WS_SYSMENU, x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!wnd) return 0;
    CreateWindowExW(0, L"STATIC", to_wide(mustela::sanitize_dialog_text(text, 512)).c_str(),
        WS_CHILD | WS_VISIBLE, 14, 14, width - 28, 60, wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"Allow", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        14, 90, 208, 34, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PERM_ALLOW)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"Deny", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        width - 222, 90, 208, 34, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PERM_DENY)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"Always allow", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        14, 134, 208, 34, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PERM_ALWAYS)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"Never allow", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        width - 222, 134, 208, 34, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PERM_NEVER)), GetModuleHandleW(nullptr), nullptr);
    EnableWindow(owner, FALSE);
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);
    SetForegroundWindow(wnd);
    for (;;) {
        MSG msg = {};
        int got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (msg.message == WM_APP_PROMPT_DONE) break;
        if (!IsDialogMessageW(wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(wnd)) break;
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (IsWindow(wnd)) DestroyWindow(wnd);
    return state.answer;
}

struct CredState {
    std::wstring user;
    std::wstring pass;
    bool confirmed = false;
};

LRESULT CALLBACK cred_proc(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam) {
    CredState* state = reinterpret_cast<CredState*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return 0;
        }
        case WM_COMMAND:
            if (!state) return 0;
            if (LOWORD(wparam) == IDOK) {
                state->confirmed = true;
                state->user = get_window_text(GetDlgItem(wnd, ID_CRED_USER));
                state->pass = get_window_text(GetDlgItem(wnd, ID_CRED_PASS));
                DestroyWindow(wnd);
                return 0;
            }
            if (LOWORD(wparam) == IDCANCEL) {
                DestroyWindow(wnd);
                return 0;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(wnd);
            return 0;
        case WM_DESTROY:
            PostThreadMessageW(GetCurrentThreadId(), WM_APP_PROMPT_DONE, 0, 0);
            return 0;
        default:
            return DefWindowProcW(wnd, message, wparam, lparam);
    }
}

bool run_modal_credentials(const std::string& message, const std::string& default_user,
    std::string& user, std::string& pass) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW cred_class = {};
        cred_class.lpfnWndProc = cred_proc;
        cred_class.hInstance = GetModuleHandleW(nullptr);
        cred_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        cred_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        cred_class.lpszClassName = L"MUSTELA_CRED_WND";
        RegisterClassW(&cred_class);
        registered = true;
    }
    CredState state;
    HWND owner = browser->window;
    RECT owner_rect = {};
    GetWindowRect(owner, &owner_rect);
    int width = 440;
    int height = 210;
    int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 3;
    HWND wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MUSTELA_CRED_WND", L"Authentication — Mustela",
        WS_CAPTION | WS_SYSMENU, x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!wnd) return false;
    CreateWindowExW(0, L"STATIC", to_wide(mustela::sanitize_dialog_text(message, 512)).c_str(),
        WS_CHILD | WS_VISIBLE, 14, 14, width - 28, 40, wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", to_wide(default_user).c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 14, 62, width - 28, 30, wnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CRED_USER)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, 14, 100, width - 28, 30, wnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CRED_PASS)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"Sign in", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        width - 222, 142, 100, 34, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        width - 114, 142, 100, 34, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), GetModuleHandleW(nullptr), nullptr);
    EnableWindow(owner, FALSE);
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);
    SetForegroundWindow(wnd);
    SetFocus(GetDlgItem(wnd, default_user.empty() ? ID_CRED_USER : ID_CRED_PASS));
    SendMessageW(GetDlgItem(wnd, ID_CRED_USER), EM_SETSEL, 0, -1);
    for (;;) {
        MSG msg = {};
        int got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (msg.message == WM_APP_PROMPT_DONE) break;
        if (!IsDialogMessageW(wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(wnd)) break;
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (IsWindow(wnd)) DestroyWindow(wnd);
    if (!state.confirmed) return false;
    user = to_utf8(state.user);
    pass = to_utf8(state.pass);
    return true;
}

int resolve_permission_answer(WinTab& tab, const std::string& what, const std::string& setting) {
    std::string host = tab.uri.empty() ? std::string() : mustela::host_from_uri(tab.uri);
    if (host.empty()) host = "this site";
    mustela::PermissionChoice global = mustela::permission_choice_from(setting);
    SiteRule rule = browser->sites ? browser->sites->policy_for(host).permissions : SiteRule::Default;
    PermissionDecision decision = mustela::resolve_permission(global, rule);
    if (decision == PermissionDecision::Denied) return ID_PERM_DENY;
    if (decision == PermissionDecision::Granted) return ID_PERM_ALLOW;
    int answer = run_modal_permission(host, what);
    if ((answer == ID_PERM_ALWAYS || answer == ID_PERM_NEVER) && browser->sites
        && tab.privacy != TabPrivacy::Private) {
        const mustela::SitePolicy* existing = browser->sites->find(host);
        mustela::SitePolicy policy = existing ? *existing : mustela::SitePolicy{};
        policy.permissions = answer == ID_PERM_ALWAYS ? SiteRule::Allow : SiteRule::Deny;
        browser->sites->set(host, policy);
        browser->sites->save();
        flash_status(*browser, "permission saved for " + host);
    }
    return answer;
}

void uic_decide_geolocation(WKPageRef, WKFrameRef, WKSecurityOriginRef,
    WKGeolocationPermissionRequestRef request, const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!browser || !request) return;
    int answer = resolve_permission_answer(tab, "your location", browser->settings.permissions_geolocation);
    if (answer == ID_PERM_DENY || answer == ID_PERM_NEVER) {
        WKGeolocationPermissionRequestDeny(request);
    } else {
        WKGeolocationPermissionRequestAllow(request);
    }
}

void uic_decide_user_media(WKPageRef, WKFrameRef, WKSecurityOriginRef, WKSecurityOriginRef,
    WKUserMediaPermissionRequestRef request, const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!browser || !request) return;
    std::string setting;
    if (browser->settings.permissions_camera == "deny" || browser->settings.permissions_microphone == "deny") {
        setting = "deny";
    } else if (browser->settings.permissions_camera == "ask" || browser->settings.permissions_microphone == "ask") {
        setting = "ask";
    } else {
        setting = "allow";
    }
    int answer = resolve_permission_answer(tab, "camera or microphone", setting);
    if (answer == ID_PERM_DENY || answer == ID_PERM_NEVER) {
        WKUserMediaPermissionRequestDeny(request, kWKPermissionDenied);
    } else {
        WkHold none(make_wkstring(""));
        WKUserMediaPermissionRequestAllow(request, static_cast<WKStringRef>(none.ref),
            static_cast<WKStringRef>(none.ref));
    }
}

void nav_web_process_did_terminate(WKPageRef, WKProcessTerminationReason reason, const void* client_info) {
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (!browser) return;
    ++tab.crashes;
    const char* why = "crashed";
    if (reason == kWKProcessTerminationReasonExceededMemoryLimit) why = "ran out of memory";
    else if (reason == kWKProcessTerminationReasonRequestedByClient) why = "was stopped";
    std::string text = std::string("web process ") + why;
    if (tab.crashes <= 2) {
        WKPageReload(tab.page);
        flash_status(*browser, text + " — reloading");
    } else {
        flash_status(*browser, text + " — Ctrl+R to reload");
    }
}

void nav_did_receive_authentication_challenge(WKPageRef, WKAuthenticationChallengeRef challenge,
    const void*) {
    if (!browser || !challenge) return;
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
        flash_status(*browser, "TLS certificate rejected on " + (host.empty() ? "this site" : host));
        if (listener) WKAuthenticationDecisionListenerRejectProtectionSpaceAndContinue(listener);
        return;
    }
    std::string label = (proxy ? "proxy " : "") + (host.empty() ? "this site" : host) + ":"
        + std::to_string(port);
    if (!realm.empty()) label += " (" + mustela::sanitize_dialog_text(realm, 256) + ")";
    std::string default_user;
    WkHold proposed(WKAuthenticationChallengeGetProposedCredential(challenge));
    if (proposed.ref) {
        WkHold user_hold(WKCredentialCopyUser(static_cast<WKCredentialRef>(proposed.ref)));
        if (user_hold.ref) default_user = wkstring_to_std(static_cast<WKStringRef>(user_hold.ref));
    }
    std::string user;
    std::string pass;
    if (!run_modal_credentials("Sign in to " + label, default_user, user, pass)) {
        if (listener) WKAuthenticationDecisionListenerCancel(listener);
        return;
    }
    WkHold user_hold(make_wkstring(user));
    WkHold pass_hold(make_wkstring(pass));
    WKCredentialRef credential = WKCredentialCreate(static_cast<WKStringRef>(user_hold.ref),
        static_cast<WKStringRef>(pass_hold.ref), kWKCredentialPersistenceForSession);
    if (listener && credential) WKAuthenticationDecisionListenerUseCredential(listener, credential);
    if (credential) WKRelease(credential);
}

void uic_set_status_text(WKPageRef, WKStringRef text, const void* client_info) {
    Browser& b = *browser;
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (active_tab(b) != &tab) return;
    b.hover_link = text ? wkstring_to_std(text) : std::string();
    refresh_status(b);
}

void uic_mouse_did_move_over_element(WKPageRef, WKHitTestResultRef hit_test, WKEventModifiers, WKTypeRef,
    const void* client_info) {
    Browser& b = *browser;
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    if (active_tab(b) != &tab) return;
    WkHold url(WKHitTestResultCopyAbsoluteLinkURL(hit_test));
    b.hover_link = url.ref ? wkurl_to_std(static_cast<WKURLRef>(url.ref)) : std::string();
    refresh_status(b);
}

void cm_get_menu(WKPageRef, WKArrayRef proposed_menu, WKArrayRef* new_menu, WKHitTestResultRef hit_test,
    WKTypeRef, const void* client_info) {
    if (!new_menu) return;
    WinTab& tab = *static_cast<WinTab*>(const_cast<void*>(client_info));
    WKMutableArrayRef menu = WKMutableArrayCreate();
    if (!menu) {
        *new_menu = proposed_menu;
        return;
    }
    auto add_item = [&menu](WKContextMenuItemTag tag, const char* title, bool enabled) {
        if (!enabled) return;
        WkHold text(make_wkstring(title));
        WKContextMenuItemRef item = WKContextMenuItemCreateAsAction(tag,
            static_cast<WKStringRef>(text.ref), true);
        if (item) {
            WKArrayAppendItem(menu, item);
            WKRelease(item);
        }
    };
    auto add_separator = [&menu]() {
        WKContextMenuItemRef item = WKContextMenuItemSeparatorItem();
        if (item) {
            WKArrayAppendItem(menu, item);
            WKRelease(item);
        }
    };
    add_item(kWKContextMenuItemTagGoBack, "Back", tab.page && WKPageCanGoBack(tab.page));
    add_item(kWKContextMenuItemTagGoForward, "Forward", tab.page && WKPageCanGoForward(tab.page));
    add_item(kWKContextMenuItemTagReload, "Reload", tab.page != nullptr);
    add_item(kWKContextMenuItemTagStop, "Stop", tab.page != nullptr);
    add_separator();
    WkHold link(hit_test ? WKHitTestResultCopyAbsoluteLinkURL(hit_test) : nullptr);
    if (link.ref) {
        add_item(kWKContextMenuItemTagOpenLinkInNewWindow, "Open link in new tab", true);
        add_item(kWKContextMenuItemTagDownloadLinkToDisk, "Save link as", true);
        add_item(kWKContextMenuItemTagCopyLinkToClipboard, "Copy link address", true);
        add_separator();
    }
    WkHold image(hit_test ? WKHitTestResultCopyAbsoluteImageURL(hit_test) : nullptr);
    if (image.ref) {
        add_item(kWKContextMenuItemTagOpenImageInNewWindow, "Open image in new tab", true);
        add_item(kWKContextMenuItemTagDownloadImageToDisk, "Save image as", true);
        add_item(kWKContextMenuItemTagCopyImageToClipboard, "Copy image", true);
        add_item(kWKContextMenuItemTagCopyImageURLToClipboard, "Copy image address", true);
        add_separator();
    }
    WkHold lookup(hit_test ? WKHitTestResultCopyLookupText(hit_test) : nullptr);
    bool editable = hit_test && WKHitTestResultIsContentEditable(hit_test);
    if (editable) {
        add_item(kWKContextMenuItemTagCut, "Cut", true);
        add_item(kWKContextMenuItemTagCopy, "Copy", true);
        add_item(kWKContextMenuItemTagPaste, "Paste", true);
        add_item(kWKContextMenuItemTagSelectAll, "Select all", true);
    } else if (lookup.ref) {
        add_item(kWKContextMenuItemTagCopy, "Copy", true);
    }
    if (WKArrayGetSize(menu) == 0 && proposed_menu) {
        size_t count = WKArrayGetSize(proposed_menu);
        for (size_t i = 0; i < count; ++i) {
            WKArrayAppendItem(menu, WKArrayGetItemAtIndex(proposed_menu, i));
        }
    }
    *new_menu = menu;
}

void find_did_find(WKPageRef, WKStringRef, unsigned match_count, const void*) {
    Browser& b = *browser;
    if (!b.find_visible || !b.find_count) return;
    std::string label = std::to_string(match_count) + (match_count == 1 ? " match" : " matches");
    SetWindowTextW(b.find_count, to_wide(label).c_str());
}

void find_did_fail(WKPageRef, WKStringRef, const void*) {
    Browser& b = *browser;
    if (!b.find_visible || !b.find_count) return;
    SetWindowTextW(b.find_count, L"no matches");
}

void find_run(Browser& b, int direction) {
    WinTab* tab = active_tab(b);
    if (!tab || !tab->page || !b.find_edit) return;
    std::string query = mustela::sanitize_find_query(to_utf8(get_window_text(b.find_edit)));
    if (query.empty()) {
        b.find_query.clear();
        WKPageHideFindUI(tab->page);
        SetWindowTextW(b.find_count, L"");
        return;
    }
    WKFindOptions options = static_cast<WKFindOptions>(
        kWKFindOptionsCaseInsensitive | kWKFindOptionsWrapAround | kWKFindOptionsShowOverlay);
    if (direction < 0) options = static_cast<WKFindOptions>(options | kWKFindOptionsBackwards);
    WkHold text(make_wkstring(query));
    b.find_query = query;
    WKPageFindString(tab->page, static_cast<WKStringRef>(text.ref), options, 1000);
}

void show_find_ui(Browser& b, bool show) {
    if (!b.find_edit) return;
    b.find_visible = show;
    UINT flags = show ? SW_SHOW : SW_HIDE;
    ShowWindow(b.find_edit, flags);
    ShowWindow(b.find_count, flags);
    ShowWindow(b.find_prev_btn, flags);
    ShowWindow(b.find_next_btn, flags);
    ShowWindow(b.find_close_btn, flags);
    if (show) {
        SetFocus(b.find_edit);
    } else {
        b.find_query.clear();
        SetWindowTextW(b.find_count, L"");
        WinTab* tab = active_tab(b);
        if (tab && tab->page) WKPageHideFindUI(tab->page);
        if (tab) SetFocus(WKViewGetWindow(tab->view));
    }
    RECT client = {};
    if (b.window && GetClientRect(b.window, &client)) {
        layout_window(b, client.right, client.bottom);
    }
}

struct PromptState {
    std::wstring text;
    bool confirmed = false;
};

LRESULT CALLBACK prompt_proc(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam) {
    PromptState* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return 0;
        }
        case WM_COMMAND:
            if (!state) return 0;
            if (LOWORD(wparam) == IDOK) {
                state->confirmed = true;
                state->text = get_window_text(GetDlgItem(wnd, ID_PROMPT_EDIT));
                DestroyWindow(wnd);
                return 0;
            }
            if (LOWORD(wparam) == IDCANCEL) {
                DestroyWindow(wnd);
                return 0;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(wnd);
            return 0;
        case WM_DESTROY:
            PostThreadMessageW(GetCurrentThreadId(), WM_APP_PROMPT_DONE, 0, 0);
            return 0;
        default:
            return DefWindowProcW(wnd, message, wparam, lparam);
    }
}

std::string run_modal_prompt(const std::string& message, const std::string& default_text) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW prompt_class = {};
        prompt_class.lpfnWndProc = prompt_proc;
        prompt_class.hInstance = GetModuleHandleW(nullptr);
        prompt_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        prompt_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        prompt_class.lpszClassName = L"MUSTELA_PROMPT_WND";
        RegisterClassW(&prompt_class);
        registered = true;
    }

    PromptState state;
    HWND owner = browser->window;
    RECT owner_rect = {};
    GetWindowRect(owner, &owner_rect);
    int width = 440;
    int height = 200;
    int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 3;

    HWND wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MUSTELA_PROMPT_WND", L"Mustela",
        WS_CAPTION | WS_SYSMENU, x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!wnd) return std::string();
    CreateWindowExW(0, L"STATIC", to_wide(mustela::sanitize_dialog_text(message, 2048)).c_str(),
        WS_CHILD | WS_VISIBLE, 14, 14, width - 28, 70, wnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROMPT_TEXT)), GetModuleHandleW(nullptr), nullptr);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", to_wide(default_text).c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 14, 90, width - 28, 30, wnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROMPT_EDIT)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        width - 204, 130, 92, 34, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        width - 106, 130, 92, 34, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(edit, EM_SETLIMITTEXT, 4096, 0);

    EnableWindow(owner, FALSE);
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);
    SetForegroundWindow(wnd);
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);

    for (;;) {
        MSG msg = {};
        int got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (msg.message == WM_APP_PROMPT_DONE) break;
        if (!IsDialogMessageW(wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(wnd)) break;
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (IsWindow(wnd)) DestroyWindow(wnd);
    return state.confirmed ? to_utf8(state.text) : std::string();
}

void set_page_clients(WinTab& tab) {
    WKPageStateClientV0 state = {};
    state.base.version = 0;
    state.base.clientInfo = &tab;
    state.didChangeTitle = did_change_title;
    state.didChangeActiveURL = did_change_active_url;
    state.didChangeIsLoading = did_change_is_loading;
    state.didChangeEstimatedProgress = did_change_estimated_progress;
    state.didChangeCanGoBack = did_change_can_go_back;
    state.didChangeCanGoForward = did_change_can_go_forward;
    WKPageSetPageStateClient(tab.page, &state.base);

    WKPageNavigationClientV3 navigation = {};
    navigation.base.version = 3;
    navigation.base.clientInfo = &tab;
    navigation.didStartProvisionalNavigation = did_start_provisional_navigation;
    navigation.didCommitNavigation = did_commit_navigation;
    navigation.didFinishNavigation = did_finish_navigation;
    navigation.didFailProvisionalNavigation = did_fail_provisional_navigation;
    navigation.webProcessDidTerminate = nav_web_process_did_terminate;
    navigation.didReceiveAuthenticationChallenge = nav_did_receive_authentication_challenge;
    WKPageSetPageNavigationClient(tab.page, &navigation.base);

    WKPageUIClientV6 ui = {};
    ui.base.version = 6;
    ui.base.clientInfo = &tab;
    ui.createNewPage = uic_create_new_page;
    ui.showPage = uic_show_page;
    ui.close = uic_close;
    ui.runJavaScriptAlert = uic_run_js_alert;
    ui.runJavaScriptConfirm = uic_run_js_confirm;
    ui.runJavaScriptPrompt = uic_run_js_prompt;
    ui.runBeforeUnloadConfirmPanel = uic_run_before_unload;
    ui.setStatusText = uic_set_status_text;
    ui.mouseDidMoveOverElement = uic_mouse_did_move_over_element;
    ui.decidePolicyForGeolocationPermissionRequest = uic_decide_geolocation;
    ui.decidePolicyForUserMediaPermissionRequest = uic_decide_user_media;
    WKPageSetPageUIClient(tab.page, &ui.base);

    WKPageFindClientV0 find = {};
    find.base.version = 0;
    find.base.clientInfo = &tab;
    find.didFindString = find_did_find;
    find.didFailToFindString = find_did_fail;
    WKPageSetPageFindClient(tab.page, &find.base);

    WKPageContextMenuClientV2 context_menu = {};
    context_menu.base.version = 2;
    context_menu.base.clientInfo = &tab;
    context_menu.getContextMenuFromProposedMenu = cm_get_menu;
    WKPageSetPageContextMenuClient(tab.page, &context_menu.base);
}

std::unique_ptr<WinTab> make_tab(TabPrivacy request) {
    auto tab = std::make_unique<WinTab>();
    tab->privacy = browser->privacy.tab_privacy(request);

    tab->preferences = WKPreferencesCreate();
    harden_preferences(tab->preferences, browser->privacy.hardening);

    tab->content = WKUserContentControllerCreate();

    if (tab->privacy == TabPrivacy::Private) {
        tab->store = WKWebsiteDataStoreCreateNonPersistentDataStore();
        tab->owns_store = true;
    } else {
        tab->store = browser->persistent_store;
        tab->owns_store = false;
    }

    WkHold configuration(WKPageConfigurationCreate());
    WKPageConfigurationSetContext(static_cast<WKPageConfigurationRef>(configuration.ref), browser->context);
    WKPageConfigurationSetPreferences(static_cast<WKPageConfigurationRef>(configuration.ref), tab->preferences);
    WKPageConfigurationSetUserContentController(static_cast<WKPageConfigurationRef>(configuration.ref), tab->content);
    WKPageConfigurationSetWebsiteDataStore(static_cast<WKPageConfigurationRef>(configuration.ref), tab->store);

    RECT rect = {};
    tab->view = WKViewCreate(rect, static_cast<WKPageConfigurationRef>(configuration.ref), browser->window);
    WKViewSetIsInWindow(tab->view, true);
    tab->page = WKViewGetPage(tab->view);

    set_page_clients(*tab);
    apply_cookie_policy(tab->store, browser->privacy.cookies);
    apply_itp(tab->store, browser->privacy.hardening.tracking_prevention);
    apply_view_policy(*tab, tab->uri);

    double dpi = static_cast<double>(GetDpiForWindow(browser->window));
    if (dpi > 0) WKPageSetCustomBackingScaleFactor(tab->page, dpi / 96.0);
    tab->scale = browser->settings.zoom_default;
    apply_tab_scale(*tab);

    ShowWindow(WKViewGetWindow(tab->view), SW_HIDE);
    return tab;
}

void navigate_tab(WinTab& tab, const std::string& url) {
    if (url.empty()) return;
    WkHold target(make_wkurl(url));
    WKPageLoadURL(tab.page, static_cast<WKURLRef>(target.ref));
}

void load_url_text(Browser& b, const std::string& text) {
    WinTab* tab = active_tab(b);
    if (!tab) return;
    std::string url = mustela::normalize_url(text, b.settings.search_template);
    navigate_tab(*tab, url);
}

int open_tab(Browser& b, const std::string& url, TabPrivacy request) {
    std::unique_ptr<WinTab> tab = make_tab(request);
    WinTab* raw = tab.get();
    int index = append_tab(b, std::move(tab));

    if (!url.empty()) navigate_tab(*raw, url);
    switch_tab(b, index);
    return index;
}

void close_tab(Browser& b, int index) {
    if (index < 0 || index >= static_cast<int>(b.tabs.size())) return;
    WinTab& tab = *b.tabs[static_cast<size_t>(index)];

    if (tab.content) WKRelease(tab.content);
    if (tab.preferences) WKRelease(tab.preferences);
    if (tab.owns_store && tab.store) WKRelease(tab.store);
    if (tab.view) WKRelease(tab.view);

    b.tabs.erase(b.tabs.begin() + index);
    SendMessageW(b.tab_ctrl, TCM_DELETEITEM, static_cast<WPARAM>(index), 0);

    if (b.tabs.empty()) {
        DestroyWindow(b.window);
        return;
    }
    if (b.active == index) {
        int next = index < static_cast<int>(b.tabs.size()) ? index : static_cast<int>(b.tabs.size()) - 1;
        b.active = -1;
        switch_tab(b, next);
    } else if (b.active > index) {
        b.active--;
    }
    refresh_chrome(b);
    save_session_now(b);
}

void fill_history_list(Browser& b) {
    if (!b.history_list) return;
    SendMessageW(b.history_list, LB_RESETCONTENT, 0, 0);
    b.history_urls.clear();
    for (const mustela::HistoryEntry& entry : b.history->recent(200)) {
        b.history_urls.push_back(entry.url);
        std::wstring line = to_wide(entry.title.empty() ? entry.url : entry.title + "  —  " + entry.url);
        SendMessageW(b.history_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
}

void open_history_window(Browser& b) {
    if (b.history_window) {
        ShowWindow(b.history_window, SW_RESTORE);
        SetForegroundWindow(b.history_window);
        return;
    }
    b.history_window = CreateWindowExW(0, L"MUSTELA_HISTORY_WND", L"History — Mustela",
        WS_OVERLAPPEDWINDOW | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
        b.window, nullptr, GetModuleHandleW(nullptr), nullptr);
    fill_history_list(b);
    ShowWindow(b.history_window, SW_SHOW);
    UpdateWindow(b.history_window);
}

LRESULT CALLBACK history_proc(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!browser) return DefWindowProcW(wnd, message, wparam, lparam);
    Browser& b = *browser;
    switch (message) {
        case WM_CREATE: {
            RECT client = {};
            GetClientRect(wnd, &client);
            b.history_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, nullptr,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                12, 12, client.right - 24, client.bottom - 72, wnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HISTORY_LIST)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"Clear history", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                12, client.bottom - 48, 140, 32, wnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HISTORY_CLEAR)), GetModuleHandleW(nullptr), nullptr);
            return 0;
        }
        case WM_SIZE: {
            if (b.history_list) {
                MoveWindow(b.history_list, 12, 12, LOWORD(lparam) - 24, HIWORD(lparam) - 72, TRUE);
                MoveWindow(GetDlgItem(wnd, ID_HISTORY_CLEAR), 12, HIWORD(lparam) - 48, 140, 32, TRUE);
            }
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wparam) == ID_HISTORY_CLEAR) {
                b.history->clear();
                fill_history_list(b);
                flash_status(b, "history cleared");
            }
            if (LOWORD(wparam) == ID_HISTORY_LIST && HIWORD(wparam) == LBN_DBLCLK) {
                int selected = static_cast<int>(SendMessageW(b.history_list, LB_GETCURSEL, 0, 0));
                if (selected >= 0 && selected < static_cast<int>(b.history_urls.size())) {
                    std::string url = b.history_urls[static_cast<size_t>(selected)];
                    open_tab(b, url, TabPrivacy::Normal);
                }
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(wnd);
            return 0;
        case WM_DESTROY:
            b.history_window = nullptr;
            b.history_list = nullptr;
            return 0;
        default:
            return DefWindowProcW(wnd, message, wparam, lparam);
    }
}

DWORD WINAPI watcher_main(LPVOID) {
    Browser& b = *browser;
    HANDLE dir = CreateFileW(app_base_dir().c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (dir == INVALID_HANDLE_VALUE) return 0;

    alignas(FILE_NOTIFY_INFORMATION) char buffer[4096];
    DWORD bytes = 0;
    while (!b.watcher_stop) {
        if (!ReadDirectoryChangesW(dir, buffer, sizeof(buffer), FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME, &bytes, nullptr, nullptr)) {
            break;
        }
        if (b.watcher_stop) break;
        FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
        while (info) {
            std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
            if (name == L"settings.toml" || name == L"sites.toml") {
                PostMessageW(b.window, WM_APP_RELOAD_CHANGED, 0, 0);
                break;
            }
            if (info->NextEntryOffset == 0) break;
            info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<char*>(info) + info->NextEntryOffset);
        }
    }
    CancelIoEx(dir, nullptr);
    CloseHandle(dir);
    return 0;
}

void report_settings_issues(const mustela::SettingsReport& report) {
    if (report.unknown_keys.empty() && report.invalid_values.empty() && report.warnings.empty()) return;
    std::string text = "mustela: settings " + report.summary() + "\n";
    for (const std::string& key : report.unknown_keys) {
        text += "mustela: unknown settings key " + key + "\n";
    }
    for (const std::string& item : report.invalid_values) {
        text += "mustela: invalid settings value for " + item + "\n";
    }
    for (const std::string& item : report.warnings) {
        text += "mustela: " + item + "\n";
    }
    OutputDebugStringA(text.c_str());
}

void reload_settings_and_policies(Browser& b) {
    mustela::SettingsReport report;
    b.settings = mustela::load_settings(config_path("settings.toml"), report);
    report_settings_issues(report);
    b.privacy = mustela::privacy_from(b.settings);
    if (b.sites) b.sites->load();

    if (b.persistent_store) {
        apply_cookie_policy(b.persistent_store, b.privacy.cookies);
        apply_itp(b.persistent_store, b.privacy.hardening.tracking_prevention);
    }
    apply_policies_to_tabs(b);
    SendMessageW(b.private_toggle, BM_SETCHECK,
        b.privacy.default_private == TabPrivacy::Private ? BST_CHECKED : BST_UNCHECKED, 0);
    refresh_chrome(b);
    flash_status(b, "settings reloaded");
}

void layout_window(Browser& b, int width, int height) {
    if (!b.back_btn || !b.forward_btn || !b.reload_btn || !b.close_tab_btn || !b.url_entry ||
        !b.new_tab_btn || !b.private_toggle || !b.tab_ctrl || !b.status_bar) {
        return;
    }
    int toolbar_y = 8;
    int toolbar_h = 30;
    MoveWindow(b.back_btn, 8, toolbar_y, 40, toolbar_h, TRUE);
    MoveWindow(b.forward_btn, 52, toolbar_y, 40, toolbar_h, TRUE);
    MoveWindow(b.reload_btn, 96, toolbar_y, 40, toolbar_h, TRUE);
    MoveWindow(b.close_tab_btn, 140, toolbar_y, 28, toolbar_h, TRUE);
    MoveWindow(b.url_entry, 176, toolbar_y, width - 292, toolbar_h, TRUE);
    MoveWindow(b.new_tab_btn, width - 112, toolbar_y, 28, toolbar_h, TRUE);
    MoveWindow(b.private_toggle, width - 80, toolbar_y, 72, toolbar_h, TRUE);

    int tabs_y = toolbar_y + toolbar_h + 6;
    int tabs_h = 28;
    MoveWindow(b.tab_ctrl, 4, tabs_y, width - 8, tabs_h, TRUE);
    SendMessageW(b.status_bar, WM_SIZE, 0, 0);

    RECT status_rect = {};
    GetWindowRect(b.status_bar, &status_rect);
    int status_h = status_rect.bottom - status_rect.top;

    int find_h = b.find_visible ? 34 : 0;
    if (b.find_visible && b.find_edit) {
        int find_y = tabs_y + tabs_h + 3;
        MoveWindow(b.find_edit, 8, find_y, width - 244, 28, TRUE);
        MoveWindow(b.find_count, width - 228, find_y + 5, 104, 22, TRUE);
        MoveWindow(b.find_prev_btn, width - 116, find_y, 28, 28, TRUE);
        MoveWindow(b.find_next_btn, width - 84, find_y, 28, 28, TRUE);
        MoveWindow(b.find_close_btn, width - 52, find_y, 28, 28, TRUE);
    }

    RECT view_area = {4, tabs_y + tabs_h + find_h, width - 4, height - status_h};
    SendMessageW(b.tab_ctrl, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&view_area));
    WinTab* tab = active_tab(b);
    if (tab) {
        SetWindowPos(WKViewGetWindow(tab->view), HWND_TOP,
            view_area.left, view_area.top,
            view_area.right - view_area.left, view_area.bottom - view_area.top, SWP_NOACTIVATE);
    }
}

LRESULT CALLBACK main_proc(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!browser) return DefWindowProcW(wnd, message, wparam, lparam);
    Browser& b = *browser;
    switch (message) {
        case WM_SIZE:
            layout_window(b, LOWORD(lparam), HIWORD(lparam));
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = 720;
            info->ptMinTrackSize.y = 480;
            return 0;
        }
        case WM_SETFOCUS: {
            WinTab* tab = active_tab(b);
            if (tab) SetFocus(WKViewGetWindow(tab->view));
            else SetFocus(b.url_entry);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint = {};
            HDC dc = BeginPaint(wnd, &paint);
            EndPaint(wnd, &paint);
            (void)dc;
            mark_startup(b);
            return 0;
        }
        case WM_TIMER:
            if (wparam == TIMER_FLASH) {
                KillTimer(wnd, TIMER_FLASH);
                b.status_flash = false;
                refresh_status(b);
            }
            if (wparam == TIMER_RELOAD) {
                KillTimer(wnd, TIMER_RELOAD);
                reload_settings_and_policies(b);
            }
            if (wparam == TIMER_MEMORY) {
                sample_memory(b);
            }
            return 0;
        case WM_APP_RELOAD_CHANGED:
            SetTimer(wnd, TIMER_RELOAD, 300, nullptr);
            return 0;
        case WM_APP_CLOSE_TAB: {
            WinTab* target = reinterpret_cast<WinTab*>(wparam);
            int index = tab_index(b, target);
            if (index >= 0) close_tab(b, index);
            return 0;
        }
        case WM_NOTIFY: {
            NMHDR* header = reinterpret_cast<NMHDR*>(lparam);
            if (header && header->idFrom == ID_TABS && header->code == TCN_SELCHANGE) {
                int index = static_cast<int>(SendMessageW(b.tab_ctrl, TCM_GETCURSEL, 0, 0));
                switch_tab(b, index);
            }
            return 0;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wparam);
            WORD code = HIWORD(wparam);
            if (id == ID_URL && code == ID_URL_ENTER) {
                load_url_text(b, to_utf8(get_window_text(b.url_entry)));
                SetFocus(b.url_entry);
                return 0;
            }
            if (id == ID_FIND_EDIT && code == EN_CHANGE) {
                find_run(b, 0);
                return 0;
            }
            if (id == ID_PRIVATE && code == BN_CLICKED) {
                bool checked = SendMessageW(b.private_toggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (checked && !b.privacy.private_tabs_available) {
                    SendMessageW(b.private_toggle, BM_SETCHECK, BST_UNCHECKED, 0);
                    flash_status(b, "private tabs disabled");
                    return 0;
                }
                b.privacy.default_private = checked ? TabPrivacy::Private : TabPrivacy::Normal;
                flash_status(b, checked ? "new tabs open private" : "new tabs open normal");
                return 0;
            }
            switch (id) {
                case ID_BACK: {
                    WinTab* tab = active_tab(b);
                    if (tab && tab->page && WKPageCanGoBack(tab->page)) WKPageGoBack(tab->page);
                    return 0;
                }
                case ID_FORWARD: {
                    WinTab* tab = active_tab(b);
                    if (tab && tab->page && WKPageCanGoForward(tab->page)) WKPageGoForward(tab->page);
                    return 0;
                }
                case ID_RELOAD:
                case ID_ACC_RELOAD: {
                    WinTab* tab = active_tab(b);
                    if (tab && tab->page) WKPageReload(tab->page);
                    return 0;
                }
                case ID_NEW_TAB:
                case ID_ACC_NEW_TAB:
                    open_tab(b, b.settings.homepage, b.privacy.default_private);
                    return 0;
                case ID_CLOSE_TAB:
                case ID_ACC_CLOSE_TAB:
                    close_tab(b, b.active);
                    return 0;
                case ID_CLEAR_DATA:
                case ID_ACC_CLEAR_DATA:
                    clear_browsing_data(b);
                    flash_status(b, "browsing data cleared");
                    return 0;
                case ID_HISTORY:
                case ID_ACC_HISTORY:
                    open_history_window(b);
                    return 0;
                case ID_ACC_FIND:
                    show_find_ui(b, true);
                    return 0;
                case ID_ACC_ZOOM_IN:
                case ID_ACC_ZOOM_OUT:
                case ID_ACC_ZOOM_RESET: {
                    WinTab* tab = active_tab(b);
                    if (tab && tab->page) {
                        if (id == ID_ACC_ZOOM_IN) tab->scale = zoom_next(tab->scale, 1);
                        else if (id == ID_ACC_ZOOM_OUT) tab->scale = zoom_next(tab->scale, -1);
                        else tab->scale = 1.0;
                        apply_tab_scale(*tab);
                        flash_status(b, "zoom " + std::to_string(static_cast<int>(tab->scale * 100.0)) + "%");
                    }
                    return 0;
                }
                case ID_ACC_FULLSCREEN:
                    if (b.fullscreen_active) exit_fullscreen_window(b);
                    else enter_fullscreen_window(b);
                    return 0;
                case ID_FIND_NEXT:
                    find_run(b, 1);
                    return 0;
                case ID_FIND_PREV:
                    find_run(b, -1);
                    return 0;
                case ID_FIND_CLOSE:
                    show_find_ui(b, false);
                    return 0;
                case ID_ACC_URL:
                    SetFocus(b.url_entry);
                    SendMessageW(b.url_entry, EM_SETSEL, 0, -1);
                    return 0;
                case ID_ACC_BACK:
                    SendMessageW(wnd, WM_COMMAND, MAKEWPARAM(ID_BACK, 0), 0);
                    return 0;
                case ID_ACC_FORWARD:
                    SendMessageW(wnd, WM_COMMAND, MAKEWPARAM(ID_FORWARD, 0), 0);
                    return 0;
                default:
                    return 0;
            }
        }
        case WM_DESTROY: {
            save_session_now(b);
            b.watcher_stop = true;
            if (b.watcher_thread) {
                WaitForSingleObject(b.watcher_thread, 2000);
                CloseHandle(b.watcher_thread);
                b.watcher_thread = nullptr;
            }
            RECT client = {};
            if (GetClientRect(wnd, &client) && client.right > 0) {
                b.settings.window_width = client.right;
                b.settings.window_height = client.bottom;
                mustela::save_settings(config_path("settings.toml"), b.settings);
            }
            KillTimer(wnd, TIMER_FLASH);
            KillTimer(wnd, TIMER_RELOAD);
            KillTimer(wnd, TIMER_MEMORY);
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(wnd, message, wparam, lparam);
    }
}

bool doh_transport_winhttp(const std::string& url, const std::vector<unsigned char>& query,
    std::vector<unsigned char>& answer) {
    std::wstring wide_url = to_wide(url);
    URL_COMPONENTSW parts = {};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    wchar_t path[1024] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 255;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 1023;
    if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &parts)) return false;
    HINTERNET session = WinHttpOpen(L"Mustela-DoH/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = nullptr;
    bool ok = false;
    if (connection) {
        request = WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    }
    if (request) {
        const wchar_t* headers = L"Content-Type: application/dns-message\r\n";
        if (WinHttpSendRequest(request, headers, static_cast<DWORD>(wcslen(headers)),
                static_cast<LPVOID>(const_cast<unsigned char*>(query.data())),
                static_cast<DWORD>(query.size()), static_cast<DWORD>(query.size()), 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0;
            DWORD status_size = sizeof(status);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX) &&
                status == 200) {
                std::vector<unsigned char> data;
                for (;;) {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                    if (data.size() + available > 65536) break;
                    size_t offset = data.size();
                    data.resize(offset + available);
                    DWORD read = 0;
                    if (!WinHttpReadData(request, data.data() + offset, available, &read) || read == 0) {
                        data.resize(offset);
                        break;
                    }
                    data.resize(offset + read);
                }
                if (!data.empty()) {
                    answer = std::move(data);
                    ok = true;
                }
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

void configure_proxy(Browser& b) {
    _putenv_s("http_proxy", "");
    _putenv_s("https_proxy", "");
    _putenv_s("no_proxy", "localhost,127.0.0.1,::1");
    if (b.settings.proxy_mode == "local") {
        mustela::Blocklist list = mustela::default_blocklist(b.settings.blocklist_file);
        if (!b.privacy.blocklist) list.clear();
        mustela::ProxyConfig config;
        config.resolve_via_doh = !b.settings.dns_over_https.empty();
        config.doh_url = b.settings.dns_over_https;
        mustela::ProxyEndpoint endpoint;
        if (!b.settings.proxy_upstream.empty() && mustela::parse_proxy_url(b.settings.proxy_upstream, endpoint)) {
            config.has_upstream = true;
            config.upstream = endpoint;
        }
        b.proxy = std::make_unique<mustela::ProxyServer>();
        if (b.proxy->start(config, list, doh_transport_winhttp)) {
            std::string url = "http://127.0.0.1:" + std::to_string(b.proxy->port());
            _putenv_s("http_proxy", url.c_str());
            _putenv_s("https_proxy", url.c_str());
        } else {
            b.proxy.reset();
        }
    } else if (b.settings.proxy_mode == "upstream") {
        mustela::ProxyEndpoint endpoint;
        if (mustela::parse_proxy_url(b.settings.proxy_upstream, endpoint)) {
            std::string url = mustela::proxy_url_from(endpoint);
            _putenv_s("http_proxy", url.c_str());
            _putenv_s("https_proxy", url.c_str());
        }
    }
}

}

int mustela_main(int argc, char** argv) {
    double startup_entry_ms = mustela::Stopwatch::now_ms();

    INITCOMMONCONTROLSEX controls = {sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    OleInitialize(nullptr);

    mustela::SettingsReport settings_report;
    Settings settings = mustela::load_settings(config_path("settings.toml"), settings_report);
    report_settings_issues(settings_report);
    PrivacyPolicy privacy = mustela::privacy_from(settings);

    bool start_private = false;
    std::string start_url;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--private") == 0) {
            start_private = true;
        } else {
            start_url = argv[i];
        }
    }

    Browser& b = *(browser = new Browser());
    b.settings = settings;
    b.privacy = privacy;
    b.startup_entry_ms = startup_entry_ms;
    if (start_private && b.privacy.private_tabs_available) {
        b.privacy.default_private = TabPrivacy::Private;
    }
    b.sites = std::make_unique<SiteRules>(config_path("sites.toml"));
    b.sites->load();
    b.history = std::make_unique<mustela::HistoryStore>(profile_path("history.tsv"));
    b.perf = std::make_unique<mustela::PerfLog>(profile_path("perf.jsonl"));
    configure_proxy(b);

    WkHold context_configuration(WKContextConfigurationCreate());
    b.context = WKContextCreateWithConfiguration(static_cast<WKContextConfigurationRef>(context_configuration.ref));
    apply_process_model(b.context, b.settings.process_model);

    WKContextDownloadClientV0 download_client = {};
    download_client.base.version = 0;
    download_client.didStart = download_did_start;
    download_client.decideDestinationWithSuggestedFilename = download_decide_destination;
    download_client.didReceiveResponse = download_did_receive_response;
    download_client.didReceiveData = download_did_receive_data;
    download_client.didFinish = download_did_finish;
    download_client.didFail = download_did_fail;
    download_client.didCancel = download_did_cancel;
#pragma warning(push)
#pragma warning(disable : 4996)
    WKContextSetDownloadClient(b.context, &download_client.base);
#pragma warning(pop)

    create_dir_chain(profile_dir_wide());
    std::wstring profile = profile_dir_wide();
    create_dir_chain(profile + L"\\filters");
    WkHold store_configuration(WKWebsiteDataStoreConfigurationCreate());
    WKWebsiteDataStoreConfigurationRef store_config = static_cast<WKWebsiteDataStoreConfigurationRef>(store_configuration.ref);
    std::vector<std::unique_ptr<WkHold>> store_strings;
    auto store_dir = [&](const wchar_t* name) {
        store_strings.push_back(std::make_unique<WkHold>(make_wkstring(to_utf8(profile + L"\\" + name))));
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
    b.persistent_store = WKWebsiteDataStoreCreateWithConfiguration(store_config);
    apply_cookie_policy(b.persistent_store, b.privacy.cookies);
    apply_itp(b.persistent_store, b.privacy.hardening.tracking_prevention);

    WkHold filter_path(make_wkstring(profile_path("filters")));
    b.filter_store = WKUserContentExtensionStoreCreate(static_cast<WKStringRef>(filter_path.ref));
    compile_fonts_filter(b);
    compile_blocklist_filter(b);
    b.session_file = config_path("session.json");

    WNDCLASSW main_class = {};
    main_class.lpfnWndProc = main_proc;
    main_class.hInstance = GetModuleHandleW(nullptr);
    main_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    main_class.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    main_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    main_class.lpszClassName = L"MUSTELA_MAIN_WND";
    RegisterClassW(&main_class);

    WNDCLASSW history_class = main_class;
    history_class.lpfnWndProc = history_proc;
    history_class.lpszClassName = L"MUSTELA_HISTORY_WND";
    RegisterClassW(&history_class);

    int width = b.settings.window_width;
    int height = b.settings.window_height;
    HWND window = CreateWindowExW(0, L"MUSTELA_MAIN_WND", L"Mustela", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    b.window = window;

    b.back_btn = CreateWindowExW(0, WC_BUTTONW, L"←", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        8, 8, 40, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BACK)), GetModuleHandleW(nullptr), nullptr);
    b.forward_btn = CreateWindowExW(0, WC_BUTTONW, L"→", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        52, 8, 40, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FORWARD)), GetModuleHandleW(nullptr), nullptr);
    b.reload_btn = CreateWindowExW(0, WC_BUTTONW, L"⟳", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        96, 8, 40, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RELOAD)), GetModuleHandleW(nullptr), nullptr);
    b.close_tab_btn = CreateWindowExW(0, WC_BUTTONW, L"×", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        140, 8, 28, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CLOSE_TAB)), GetModuleHandleW(nullptr), nullptr);
    b.url_entry = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        176, 8, width - 292, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_URL)), GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(b.url_entry, url_edit_subclass, URL_SUBCLASS_ID, 0);
    b.new_tab_btn = CreateWindowExW(0, WC_BUTTONW, L"+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        width - 112, 8, 28, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_NEW_TAB)), GetModuleHandleW(nullptr), nullptr);
    b.private_toggle = CreateWindowExW(0, WC_BUTTONW, L"Private", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        width - 80, 8, 72, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PRIVATE)), GetModuleHandleW(nullptr), nullptr);
    b.tab_ctrl = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        4, 44, width - 8, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TABS)), GetModuleHandleW(nullptr), nullptr);
    b.status_bar = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), GetModuleHandleW(nullptr), nullptr);

    b.find_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP, 8, 76, width - 240, 28, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_EDIT)), GetModuleHandleW(nullptr), nullptr);
    b.find_count = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, width - 224, 82, 104, 22, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_COUNT)), GetModuleHandleW(nullptr), nullptr);
    b.find_prev_btn = CreateWindowExW(0, WC_BUTTONW, L"↑", WS_CHILD | BS_PUSHBUTTON,
        width - 112, 76, 28, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_PREV)), GetModuleHandleW(nullptr), nullptr);
    b.find_next_btn = CreateWindowExW(0, WC_BUTTONW, L"↓", WS_CHILD | BS_PUSHBUTTON,
        width - 80, 76, 28, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_NEXT)), GetModuleHandleW(nullptr), nullptr);
    b.find_close_btn = CreateWindowExW(0, WC_BUTTONW, L"×", WS_CHILD | BS_PUSHBUTTON,
        width - 48, 76, 28, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_CLOSE)), GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(b.find_edit, find_edit_subclass, URL_SUBCLASS_ID + 1, 0);
    SendMessageW(b.find_edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Find in page"));

    SendMessageW(b.url_entry, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search or enter address"));
    if (b.privacy.default_private == TabPrivacy::Private) {
        SendMessageW(b.private_toggle, BM_SETCHECK, BST_CHECKED, 0);
    }

    ACCEL accelerators[] = {
        {FVIRTKEY | FCONTROL, 'L', ID_ACC_URL},
        {FVIRTKEY | FCONTROL, 'T', ID_ACC_NEW_TAB},
        {FVIRTKEY | FCONTROL, 'W', ID_ACC_CLOSE_TAB},
        {FVIRTKEY | FCONTROL, 'R', ID_ACC_RELOAD},
        {FVIRTKEY, VK_F5, ID_ACC_RELOAD},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_DELETE, ID_ACC_CLEAR_DATA},
        {FVIRTKEY | FCONTROL, 'H', ID_ACC_HISTORY},
        {FVIRTKEY | FCONTROL, 'F', ID_ACC_FIND},
        {FVIRTKEY | FALT, VK_LEFT, ID_ACC_BACK},
        {FVIRTKEY | FALT, VK_RIGHT, ID_ACC_FORWARD},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, ID_ACC_ZOOM_IN},
        {FVIRTKEY | FCONTROL, VK_ADD, ID_ACC_ZOOM_IN},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, ID_ACC_ZOOM_OUT},
        {FVIRTKEY | FCONTROL, VK_SUBTRACT, ID_ACC_ZOOM_OUT},
        {FVIRTKEY | FCONTROL, '0', ID_ACC_ZOOM_RESET},
        {FVIRTKEY, VK_F11, ID_ACC_FULLSCREEN}
    };
    HACCEL accel = CreateAcceleratorTableW(accelerators, 16);

    b.watcher_thread = CreateThread(nullptr, 0, watcher_main, nullptr, 0, nullptr);

    TabPrivacy first_request = start_private ? TabPrivacy::Private : b.privacy.default_private;
    bool restored = false;
    if (b.settings.session_restore && start_url.empty() && first_request != TabPrivacy::Private) {
        mustela::SessionData data;
        if (mustela::load_session(b.session_file, data) && !data.tabs.empty()) {
            for (const mustela::SessionTab& entry : data.tabs) {
                open_tab(b, entry.url, first_request);
            }
            if (data.active > 0 && data.active < static_cast<int>(b.tabs.size())) {
                switch_tab(b, data.active);
            }
            restored = true;
        }
    }
    if (!restored) {
        open_tab(b, start_url.empty() ? b.settings.homepage : start_url, first_request);
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    sample_memory(b);
    SetTimer(window, TIMER_MEMORY, 60000, nullptr);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window, accel, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    DestroyAcceleratorTable(accel);
    for (std::unique_ptr<WinTab>& tab : b.tabs) {
        if (tab->content) WKRelease(tab->content);
        if (tab->preferences) WKRelease(tab->preferences);
        if (tab->owns_store && tab->store) WKRelease(tab->store);
        if (tab->view) WKRelease(tab->view);
    }
    b.tabs.clear();

    if (b.filter_store) WKRelease(b.filter_store);
    if (b.fonts_filter) WKRelease(b.fonts_filter);
    if (b.blocklist_filter) WKRelease(b.blocklist_filter);
    if (b.persistent_store) WKRelease(b.persistent_store);
    if (b.context) WKRelease(b.context);

    delete &b;
    browser = nullptr;

    OleUninitialize();
    return 0;
}
