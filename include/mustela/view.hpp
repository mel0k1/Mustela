#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <memory>
#include <string>

#include "browsing_data.hpp"
#include "privacy.hpp"
#include "profile.hpp"
#include "settings.hpp"
#include "site_rules.hpp"

namespace mustela {

constexpr int MUSTELA_VIEW_API_VERSION = 3;

inline int view_api_version() { return MUSTELA_VIEW_API_VERSION; }

struct ViewConfig {
    const Settings* settings = nullptr;
    const PrivacyPolicy* privacy = nullptr;
    SiteRules* sites = nullptr;
    Profile* profile = nullptr;
    std::string webext_dir;
    std::string downloads_dir;
};

enum class PermissionAnswer { Allow, Deny, Always, Never };

struct WebsiteDataEntry {
    std::string origin;
    DataKind kinds = DataKind::None;
};

using WebsiteDataCallback = std::function<void(std::vector<WebsiteDataEntry>)>;
using SimpleCallback = std::function<void()>;
using TextCallback = std::function<void(const std::string&)>;

struct FindOptions {
    bool case_insensitive = true;
    bool wrap_around = true;
};

enum class EditCommand { Copy, Cut, Paste, SelectAll, Undo, Redo };

struct ContextMenuInfo {
    bool link = false;
    bool image = false;
    bool media = false;
    bool selection = false;
    bool editable = false;
    std::string link_uri;
    std::string image_uri;
    std::string media_uri;
    std::string page_uri;
};

struct SessionCallbacks {
    std::function<std::string(const std::string& suggested_name)> download_destination;
    std::function<void(std::uint64_t id, const std::string& path)> download_started;
    std::function<void(std::uint64_t id, double fraction)> download_progress;
    std::function<void(std::uint64_t id)> download_finished;
    std::function<void(std::uint64_t id)> download_failed;
};

class View;

struct ViewCallbacks {
    std::function<void(const std::string& uri)> uri_changed;
    std::function<void(const std::string& title)> title_changed;
    std::function<void(double fraction)> load_progress;
    std::function<void(bool loading)> loading_changed;
    std::function<void(const std::string& uri, const std::string& title, bool private_mode)> committed;
    std::function<void(const std::string& uri, const std::string& heading, const std::string& detail)> load_failed;
    std::function<void(const std::string& uri)> favicon_changed;
    std::function<void(const std::string& uri)> link_hover;
    std::function<void(const std::string& reason)> web_process_gone;

    std::function<void(const std::string& message)> script_alert;
    std::function<bool(const std::string& message)> script_confirm;
    std::function<std::string(const std::string& message, const std::string& default_text, bool& ok)> script_prompt;

    std::function<PermissionAnswer(const std::string& host, const std::string& what)> permission_prompt;
    std::function<bool(const std::string& host)> certificate_prompt;
    std::function<bool(const std::string& label, std::string& user, std::string& password)> auth_prompt;

    std::function<void(View* popup)> popup_created;
    std::function<void(View* popup)> popup_ready;
    std::function<void()> close_requested;

    std::function<bool(const ContextMenuInfo& info)> context_menu;
    std::function<void(bool found)> find_result;
    std::function<void(bool docked)> devtools_docked;
};

struct ViewState;

class ViewSession {
public:
    ViewSession(const ViewConfig& config, const SessionCallbacks& callbacks = {});
    ~ViewSession();

    ViewSession(const ViewSession&) = delete;
    ViewSession& operator=(const ViewSession&) = delete;

    void refresh();

    void fetch_website_data(const WebsiteDataCallback& done);
    void remove_website_data(const std::vector<std::string>& origins, DataKind kinds,
                             SimpleCallback done = {});
    void clear_website_data(DataKind kinds, std::int64_t max_age_seconds,
                            SimpleCallback done = {});

private:
    std::unique_ptr<ViewState> impl;
    friend struct ViewState;
    friend class View;
};

struct ViewImpl;

class View {
public:
    View(ViewSession& session, bool private_mode, const ViewCallbacks& callbacks = {});
    ~View();

    View(const View&) = delete;
    View& operator=(const View&) = delete;

    void* native_widget() const;

    void load_url(const std::string& url);
    void go_back();
    void go_forward();
    bool can_go_back() const;
    bool can_go_forward() const;
    void reload();
    void stop();

    std::string uri() const;
    std::string title() const;
    bool is_private() const;

    double zoom() const;
    void set_zoom(double level);
    void set_user_agent(const std::string& user_agent);

    void refresh_policy();
    SitePolicy active_site_policy() const;
    void set_callbacks(const ViewCallbacks& callbacks);

    void find(const std::string& text, const FindOptions& options = {},
              std::uint32_t max_matches = 999);
    void find_next();
    void find_previous();
    void find_finish();
    std::string find_text() const;
    void selected_text(const TextCallback& done);
    void execute_edit_command(EditCommand command);

    void show_devtools();
    void hide_devtools();
    bool devtools_attached() const;

private:
    std::unique_ptr<ViewImpl> impl;
    explicit View(ViewImpl* adopted);
    friend struct ViewImpl;
};

double zoom_step(double current, int direction);

}
