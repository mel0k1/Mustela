#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "browsing_data.hpp"
#include "config.hpp"
#include "fingerprint.hpp"
#include "fingerprint_scripts.hpp"
#include "history.hpp"
#include "perf.hpp"
#include "permissions.hpp"
#include "privacy.hpp"
#include "site_rules.hpp"
#include "ui_text.hpp"
#include "url.hpp"

namespace {

int failures = 0;
int checks = 0;

void fail(const char* file, int line, const std::string& what) {
    std::fprintf(stderr, "FAIL %s:%d %s\n", file, line, what.c_str());
    ++failures;
}

void expect(bool ok, const char* file, int line, const std::string& what) {
    ++checks;
    if (!ok) fail(file, line, what);
}

void expect_eq(const std::string& got, const std::string& want, const char* file, int line, const std::string& what) {
    ++checks;
    if (got != want) fail(file, line, what + " (got \"" + got + "\", want \"" + want + "\")");
}

#define CHECK(cond) expect((cond), __FILE__, __LINE__, #cond)
#define CHECK_STR(got, want) expect_eq((got), (want), __FILE__, __LINE__, #got)

std::string temp_path(const std::string& name) {
    const char* base = std::getenv("MUSTELA_TEST_TMP");
    std::string dir = (base && *base) ? base : ".";
    std::filesystem::create_directories(dir);
    return dir + "/mustela_test_" + name;
}

std::string write_temp(const std::string& name, const std::string& content) {
    std::string path = temp_path(name);
    FILE* out = std::fopen(path.c_str(), "wb");
    if (out) {
        std::fwrite(content.data(), 1, content.size(), out);
        std::fclose(out);
    }
    return path;
}

void test_settings_roundtrip() {
    std::string path = temp_path("settings.toml");
    mustela::Settings original;
    original.homepage = "https://mustela.example";
    original.search_template = "https://search.example/?q=%s";
    original.window_width = 1024;
    original.window_height = 768;
    original.private_tabs_enabled = false;
    original.start_in_private = true;
    original.history_enabled = false;
    original.cookie_policy = "never";
    original.tracking_prevention = false;
    original.javascript_enabled = false;
    original.hyperlink_auditing = true;
    original.dns_prefetching = true;
    original.block_autoplay = false;
    original.fingerprint_webgl = true;
    original.fingerprint_canvas = true;
    original.fingerprint_fonts = true;
    original.fingerprint_referrer = "never";
    original.process_model = "shared";
    original.startup_budget_ms = 2500;
    original.memory_ceiling_mb = 2048;
    original.perf_log = false;
    original.downloads_dir = "/tmp/dl";
    original.downloads_mode = "ask";
    original.zoom_default = 1.5;
    original.permissions_geolocation = "ask";
    original.permissions_camera = "allow";
    original.permissions_microphone = "deny";
    original.permissions_notifications = "ask";
    original.tls_strict = false;

    mustela::save_settings(path, original);
    mustela::Settings loaded = mustela::load_settings(path);

    CHECK_STR(loaded.homepage, original.homepage);
    CHECK_STR(loaded.search_template, original.search_template);
    CHECK(loaded.window_width == 1024);
    CHECK(loaded.window_height == 768);
    CHECK(!loaded.private_tabs_enabled);
    CHECK(loaded.start_in_private);
    CHECK(!loaded.history_enabled);
    CHECK_STR(loaded.cookie_policy, "never");
    CHECK(!loaded.tracking_prevention);
    CHECK(!loaded.javascript_enabled);
    CHECK(loaded.hyperlink_auditing);
    CHECK(loaded.dns_prefetching);
    CHECK(!loaded.block_autoplay);
    CHECK(loaded.fingerprint_webgl);
    CHECK(loaded.fingerprint_canvas);
    CHECK(loaded.fingerprint_fonts);
    CHECK_STR(loaded.fingerprint_referrer, "never");
    CHECK_STR(loaded.process_model, "shared");
    CHECK(loaded.startup_budget_ms == 2500);
    CHECK(loaded.memory_ceiling_mb == 2048);
    CHECK(!loaded.perf_log);
    CHECK_STR(loaded.downloads_dir, "/tmp/dl");
    CHECK_STR(loaded.downloads_mode, "ask");
    CHECK(loaded.zoom_default == 1.5);
    CHECK_STR(loaded.permissions_geolocation, "ask");
    CHECK_STR(loaded.permissions_camera, "allow");
    CHECK_STR(loaded.permissions_microphone, "deny");
    CHECK_STR(loaded.permissions_notifications, "ask");
    CHECK(!loaded.tls_strict);

    mustela::Settings fallback = mustela::load_settings(path + "_missing");
    CHECK_STR(fallback.homepage, "https://example.org");
    CHECK_STR(fallback.cookie_policy, "no-third-party");
    CHECK(fallback.tracking_prevention);
    CHECK(!fallback.fingerprint_webgl);
    CHECK_STR(fallback.fingerprint_referrer, "same-origin");
    CHECK_STR(fallback.process_model, "per-view");
    CHECK(fallback.startup_budget_ms == 0);
    CHECK(fallback.memory_ceiling_mb == 0);
    CHECK(fallback.perf_log);
    CHECK(fallback.downloads_dir.empty());
    CHECK_STR(fallback.downloads_mode, "auto");
    CHECK(fallback.zoom_default == 1.0);
    CHECK_STR(fallback.permissions_geolocation, "deny");
    CHECK_STR(fallback.permissions_camera, "deny");
    CHECK_STR(fallback.permissions_notifications, "deny");
    CHECK(fallback.tls_strict);

    std::string invalid = write_temp("invalid.toml",
        "[shell]\n"
        "zoom = 12\n"
        "[downloads]\n"
        "mode = prompt\n"
        "[permissions]\n"
        "camera = maybe\n");
    mustela::Settings clamped = mustela::load_settings(invalid);
    CHECK(clamped.zoom_default == 3.0);
    CHECK_STR(clamped.downloads_mode, "auto");
    CHECK_STR(clamped.permissions_camera, "deny");
}

void test_settings_partial_file() {
    std::string path = write_temp("partial.toml",
        "# comment\n"
        "[shell]\n"
        "homepage = \"https://partial.example\"\n"
        "window_width = 800  # trailing comment\n"
        "window_height = not-a-number\n"
        "[privacy]\n"
        "cookie_policy = always\n"
        "javascript = off\n");
    mustela::Settings loaded = mustela::load_settings(path);
    CHECK_STR(loaded.homepage, "https://partial.example");
    CHECK(loaded.window_width == 800);
    CHECK(loaded.window_height == 800);
    CHECK(loaded.cookie_policy == "always");
    CHECK(!loaded.javascript_enabled);
    CHECK(loaded.tracking_prevention);
}

void test_privacy_mapping() {
    mustela::Settings settings;
    settings.private_tabs_enabled = false;
    settings.start_in_private = true;
    settings.history_enabled = false;
    settings.cookie_policy = "always";
    settings.fingerprint_referrer = "bogus";

    mustela::PrivacyPolicy policy = mustela::privacy_from(settings);
    CHECK(!policy.private_tabs_available);
    CHECK(!policy.history_enabled);
    CHECK(policy.default_private == mustela::TabPrivacy::Normal);
    CHECK(policy.cookies == mustela::CookiePolicy::Always);
    CHECK(policy.fingerprint.referrer == mustela::ReferrerTrim::Off);

    CHECK(!policy.records_history(mustela::TabPrivacy::Normal));
    CHECK(!policy.records_history(mustela::TabPrivacy::Private));
    CHECK(policy.tab_privacy(mustela::TabPrivacy::Private) == mustela::TabPrivacy::Normal);

    mustela::Settings enabled;
    enabled.history_enabled = true;
    mustela::PrivacyPolicy normal_policy = mustela::privacy_from(enabled);
    CHECK(normal_policy.records_history(mustela::TabPrivacy::Normal));
    CHECK(!normal_policy.records_history(mustela::TabPrivacy::Private));
    CHECK(normal_policy.tab_privacy(mustela::TabPrivacy::Private) == mustela::TabPrivacy::Private);

    CHECK(mustela::cookie_policy_from("never") == mustela::CookiePolicy::Never);
    CHECK(mustela::cookie_policy_from("always") == mustela::CookiePolicy::Always);
    CHECK(mustela::cookie_policy_from("weird") == mustela::CookiePolicy::NoThirdParty);
    CHECK_STR(mustela::cookie_policy_name(mustela::CookiePolicy::Never), "never");
    CHECK_STR(mustela::cookie_policy_name(mustela::CookiePolicy::NoThirdParty), "no-third-party");
}

void test_host_from_uri() {
    CHECK_STR(mustela::host_from_uri("https://Example.ORG/path?query"), "example.org");
    CHECK_STR(mustela::host_from_uri("http://user:pass@www.test.example:8080/x"), "www.test.example");
    CHECK_STR(mustela::host_from_uri("http://[::1]:9000/index"), "::1");
    CHECK_STR(mustela::host_from_uri("file:///home/user/page.html"), "");
    CHECK_STR(mustela::host_from_uri("not a uri"), "not a uri");
}

void test_site_rules_matching() {
    std::string path = temp_path("sites.toml");
    mustela::SiteRules rules(path);

    mustela::SitePolicy parent;
    parent.javascript = mustela::SiteRule::Deny;
    rules.set("example.org", parent);

    mustela::SitePolicy child;
    child.cookies = mustela::SiteRule::Allow;
    rules.set("www.example.org", child);

    CHECK(rules.policy_for("example.org").javascript == mustela::SiteRule::Deny);
    CHECK(rules.policy_for("www.example.org").cookies == mustela::SiteRule::Allow);
    CHECK(rules.policy_for("WWW.EXAMPLE.ORG").cookies == mustela::SiteRule::Allow);
    CHECK(rules.policy_for("deep.www.example.org").cookies == mustela::SiteRule::Allow);
    CHECK(rules.policy_for("deep.www.example.org").javascript == mustela::SiteRule::Default);
    CHECK(rules.policy_for("other.org").javascript == mustela::SiteRule::Default);

    mustela::SitePolicy cleared;
    cleared.javascript = mustela::SiteRule::Default;
    rules.set("example.org", cleared);
    CHECK(rules.policy_for("example.org").javascript == mustela::SiteRule::Default);
    CHECK(rules.policy_for("deep.www.example.org").javascript == mustela::SiteRule::Default);
    CHECK(rules.policy_for("www.example.org").cookies == mustela::SiteRule::Allow);

    rules.save();

    mustela::SiteRules reloaded(path);
    reloaded.load();
    CHECK(reloaded.policy_for("www.example.org").cookies == mustela::SiteRule::Allow);
    CHECK(reloaded.entries().size() == 1);

    reloaded.remove("www.example.org");
    CHECK(reloaded.entries().empty());
}

void test_resolve_site() {
    std::string path = temp_path("resolve.toml");
    mustela::SiteRules rules(path);

    mustela::Settings settings;
    mustela::PrivacyPolicy policy = mustela::privacy_from(settings);
    policy.hardening.javascript = true;
    policy.fingerprint.block_webgl = true;

    mustela::SiteDecision plain = mustela::resolve_site(policy, rules, "https://plain.example/page");
    CHECK(plain.javascript);
    CHECK(!plain.cookies_denied);
    CHECK(plain.fingerprint.block_webgl);

    mustela::SitePolicy js_deny;
    js_deny.javascript = mustela::SiteRule::Deny;
    rules.set("deny.example", js_deny);
    mustela::SiteDecision denied = mustela::resolve_site(policy, rules, "https://deny.example/");
    CHECK(!denied.javascript);
    CHECK(!denied.cookies_denied);

    mustela::SitePolicy strict;
    strict.fingerprint = mustela::SiteRule::Deny;
    strict.cookies = mustela::SiteRule::Deny;
    rules.set("strict.example", strict);
    mustela::SiteDecision hardened = mustela::resolve_site(policy, rules, "https://strict.example/");
    CHECK(hardened.fingerprint.block_webgl);
    CHECK(hardened.fingerprint.block_canvas_readback);
    CHECK(hardened.fingerprint.block_remote_fonts);
    CHECK(hardened.fingerprint.referrer == mustela::ReferrerTrim::Never);
    CHECK(hardened.cookies_denied);

    mustela::SitePolicy exempt;
    exempt.fingerprint = mustela::SiteRule::Allow;
    exempt.javascript = mustela::SiteRule::Allow;
    rules.set("exempt.example", exempt);
    policy.hardening.javascript = false;
    mustela::SiteDecision allowed = mustela::resolve_site(policy, rules, "https://exempt.example/");
    CHECK(allowed.javascript);
    CHECK(!allowed.fingerprint.active());
}

void test_history_store() {
    std::string path = temp_path("history.tsv");
    {
        mustela::HistoryStore history(path, 3);
        history.record("https://a.example/", "First", 100);
        history.record("https://b.example/", "Second\tTab", 200);
        history.record("https://b.example/", "Second\\Two", 300);
        history.record("https://b.example/", "Second\\Two", 400);
        history.record("https://c.example/", "Third\nLine", 500);
        history.record("https://d.example/", "Fourth", 600);

        std::vector<mustela::HistoryEntry> entries = history.recent(10);
        CHECK(entries.size() == 3);
        CHECK_STR(entries[0].url, "https://d.example/");
        CHECK_STR(entries[0].title, "Fourth");
        CHECK(entries[0].visited_at == 600);
        CHECK_STR(entries[2].url, "https://b.example/");
        CHECK_STR(entries[2].title, "Second\\Two");
    }

    mustela::HistoryStore reloaded(path, 10);
    std::vector<mustela::HistoryEntry> entries = reloaded.recent(10);
    CHECK(entries.size() == 3);
    CHECK_STR(entries[1].title, "Third\nLine");

    reloaded.record("https://e.example/", "Fifth", 700);
    CHECK(reloaded.recent(10).size() == 4);
    reloaded.clear();
    CHECK(reloaded.recent(10).empty());

    mustela::HistoryStore after_clear(path, 10);
    CHECK(after_clear.recent(10).empty());
}

void test_fingerprint_policy() {
    CHECK_STR(mustela::referrer_trim_name(mustela::ReferrerTrim::Off), "off");
    CHECK_STR(mustela::referrer_trim_name(mustela::ReferrerTrim::SameOrigin), "same-origin");
    CHECK_STR(mustela::referrer_trim_name(mustela::ReferrerTrim::Never), "no-referrer");
    CHECK(mustela::referrer_trim_from("no-referrer") == mustela::ReferrerTrim::Never);
    CHECK(mustela::referrer_trim_from("same-origin") == mustela::ReferrerTrim::SameOrigin);
    CHECK(mustela::referrer_trim_from("junk") == mustela::ReferrerTrim::Off);

    mustela::FingerprintPolicy off = mustela::fingerprint_preset("off");
    CHECK(!off.active());
    mustela::FingerprintPolicy standard = mustela::fingerprint_preset("standard");
    CHECK(!standard.block_webgl);
    CHECK(!standard.block_canvas_readback);
    CHECK(!standard.block_remote_fonts);
    CHECK(standard.referrer == mustela::ReferrerTrim::SameOrigin);
    mustela::FingerprintPolicy strict = mustela::fingerprint_preset("strict");
    CHECK(strict.block_webgl);
    CHECK(strict.block_canvas_readback);
    CHECK(strict.block_remote_fonts);
    CHECK(strict.referrer == mustela::ReferrerTrim::Never);
    CHECK(mustela::fingerprint_strict().referrer == mustela::ReferrerTrim::Never);
    CHECK(!mustela::fingerprint_preset("unknown").active());

    std::string script = mustela::fingerprint_referrer_script(mustela::ReferrerTrim::Never);
    CHECK(script.find("content = 'no-referrer'") != std::string::npos);
    CHECK(script.find("__POLICY__") == std::string::npos);
    CHECK(std::string(mustela::fp_scripts::WEBGL_BLOCK).find("webgl") != std::string::npos);
    CHECK(std::string(mustela::fp_scripts::CANVAS_BLANK).find("toDataURL") != std::string::npos);
}

void test_browsing_data_kinds() {
    using mustela::DataKind;
    CHECK(mustela::has(DataKind::All, DataKind::Cookies));
    CHECK(mustela::has(DataKind::All, DataKind::Cache));
    CHECK(mustela::has(DataKind::All, DataKind::LocalStorage));
    CHECK(mustela::has(DataKind::All, DataKind::IndexedDB));
    CHECK(!mustela::has(DataKind::Cookies, DataKind::Cache));
    DataKind mixed = DataKind::Cookies | DataKind::IndexedDB;
    CHECK(mustela::has(mixed, DataKind::Cookies));
    CHECK(mustela::has(mixed, DataKind::IndexedDB));
    CHECK(!mustela::has(mixed, DataKind::LocalStorage));
    mustela::ClearRequest request;
    CHECK(request.kinds == DataKind::All);
    CHECK(request.max_age_seconds == 0);
}

void test_normalize_url() {
    const std::string search = "https://search.example/?q=%s";
    CHECK_STR(mustela::normalize_url("", search), "");
    CHECK_STR(mustela::normalize_url("   ", search), "");
    CHECK_STR(mustela::normalize_url("https://example.org/a b", search), "https://example.org/a b");
    CHECK_STR(mustela::normalize_url("localhost:8080", search), "https://localhost:8080");
    CHECK_STR(mustela::normalize_url("example.org", search), "https://example.org");
    CHECK_STR(mustela::normalize_url("Example.ORG/page", search), "https://Example.ORG/page");
    CHECK_STR(mustela::normalize_url("two words", search), "https://search.example/?q=two%20words");
    CHECK_STR(mustela::normalize_url("c++ & rust", search), "https://search.example/?q=c%2B%2B%20%26%20rust");
    CHECK_STR(mustela::normalize_url("mustela~_-.09", search), "https://mustela~_-.09");
    CHECK_STR(mustela::normalize_url("hello", "https://no.slot.example/"), "https://no.slot.example/");
}

void test_perf_module() {
    mustela::SampleStats stats = mustela::stats_from_durations({30, 10, 20});
    CHECK(stats.count == 3);
    CHECK(std::fabs(stats.min_ms - 10.0) < 1e-9);
    CHECK(std::fabs(stats.median_ms - 20.0) < 1e-9);
    CHECK(std::fabs(stats.max_ms - 30.0) < 1e-9);
    CHECK(std::fabs(stats.p95_ms - 30.0) < 1e-9);

    stats = mustela::stats_from_durations({10, 20, 30, 40});
    CHECK(stats.count == 4);
    CHECK(std::fabs(stats.median_ms - 25.0) < 1e-9);
    CHECK(std::fabs(stats.p95_ms - 40.0) < 1e-9);
    CHECK(mustela::stats_from_durations({}).count == 0);
    CHECK(std::fabs(mustela::stats_from_durations({7.5}).median_ms - 7.5) < 1e-9);

    CHECK(mustela::within_budget(0, 12345.0));
    CHECK(mustela::within_budget(100, 100.0));
    CHECK(!mustela::within_budget(100, 100.5));
    CHECK(mustela::within_budget(-5, 9999.0));

    CHECK(!mustela::ceiling_exceeded(0, 1LL << 30));
    CHECK(!mustela::ceiling_exceeded(-10, 1LL << 40));
    CHECK(!mustela::ceiling_exceeded(100, 100LL * 1024 * 1024));
    CHECK(mustela::ceiling_exceeded(100, 100LL * 1024 * 1024 + 1));

    CHECK_STR(mustela::process_model_name(mustela::process_model_from("shared")), "shared");
    CHECK_STR(mustela::process_model_name(mustela::process_model_from("per-view")), "per-view");
    CHECK_STR(mustela::process_model_name(mustela::process_model_from("bogus")), "per-view");

    CHECK(mustela::parse_status_rss_bytes("Name:\twebkit\nVmRSS:\t     1536 kB\n") == 1536 * 1024);
    CHECK(mustela::parse_status_rss_bytes("VmHWM:\t 10 kB\n") == 0);
    CHECK(mustela::parse_status_rss_bytes("VmRSS:\t 2 mB\n") == 2LL * 1024 * 1024);
    CHECK(mustela::parse_status_rss_bytes("VmRSS:\t 1 gB\n") == 1LL * 1024 * 1024 * 1024);
    CHECK(mustela::parse_status_rss_bytes("VmRSS:\t\n") == 0);
    CHECK(mustela::parse_status_rss_bytes("") == 0);

    mustela::PerfRecord record;
    record.kind = "pageload";
    record.timestamp = 1729000000;
    record.value_ms = 812.5;
    record.value_bytes = 0;
    record.detail = "https://example.org/?a=\"b\"\\c";
    std::string encoded = mustela::encode_perf_record(record);
    mustela::PerfRecord decoded;
    CHECK(mustela::decode_perf_record(encoded, decoded));
    CHECK_STR(decoded.kind, "pageload");
    CHECK(decoded.timestamp == 1729000000);
    CHECK(std::fabs(decoded.value_ms - 812.5) < 0.01);
    CHECK_STR(decoded.detail, record.detail);
    CHECK(!mustela::decode_perf_record("garbage", decoded));
    CHECK(!mustela::decode_perf_record("{\"kind\":\"\",\"ts\":0,\"ms\":0,\"bytes\":0,\"detail\":\"\"}", decoded));

    mustela::PerfRecord memory_record;
    memory_record.kind = "memory";
    memory_record.timestamp = 5;
    memory_record.value_bytes = 734003200;
    CHECK(mustela::decode_perf_record(mustela::encode_perf_record(memory_record), decoded));
    CHECK(decoded.value_bytes == 734003200);
    CHECK_STR(decoded.kind, "memory");

    std::string path = temp_path("perf.jsonl");
    mustela::PerfLog log(path, 3);
    for (int i = 0; i < 5; ++i) {
        mustela::PerfRecord item;
        item.kind = "startup";
        item.timestamp = i;
        item.value_ms = i + 0.5;
        log.append(item);
    }
    std::vector<mustela::PerfRecord> records = mustela::PerfLog(path, 3).read_all();
    CHECK(records.size() == 3);
    if (records.size() == 3) {
        CHECK(records[0].timestamp == 2);
        CHECK(records[1].timestamp == 3);
        CHECK(records[2].timestamp == 4);
        CHECK(std::fabs(records[2].value_ms - 4.5) < 0.01);
    }

    mustela::Stopwatch watch;
    CHECK(watch.elapsed_ms() < 1000.0);
    mustela::LoadTimer timer;
    CHECK(timer.stop(10.0) < 0.0);
    timer.start(10.0);
    CHECK(std::fabs(timer.stop(42.5) - 32.5) < 1e-9);
    CHECK(!timer.active);
    timer.start(1.0);
    timer.cancel();
    CHECK(timer.stop(5.0) < 0.0);
}

void test_permissions() {
    CHECK(mustela::permission_choice_from("deny") == mustela::PermissionChoice::Deny);
    CHECK(mustela::permission_choice_from("ask") == mustela::PermissionChoice::Ask);
    CHECK(mustela::permission_choice_from("allow") == mustela::PermissionChoice::Allow);
    CHECK(mustela::permission_choice_from("bogus") == mustela::PermissionChoice::Deny);
    CHECK(mustela::permission_choice_from("") == mustela::PermissionChoice::Deny);
    CHECK_STR(mustela::permission_choice_name(mustela::PermissionChoice::Ask), "ask");
    CHECK_STR(mustela::permission_choice_name(mustela::PermissionChoice::Allow), "allow");
    CHECK_STR(mustela::permission_choice_name(mustela::PermissionChoice::Deny), "deny");

    CHECK(mustela::resolve_permission(mustela::PermissionChoice::Deny, mustela::SiteRule::Default)
        == mustela::PermissionDecision::Denied);
    CHECK(mustela::resolve_permission(mustela::PermissionChoice::Ask, mustela::SiteRule::Default)
        == mustela::PermissionDecision::Prompt);
    CHECK(mustela::resolve_permission(mustela::PermissionChoice::Allow, mustela::SiteRule::Default)
        == mustela::PermissionDecision::Granted);
    CHECK(mustela::resolve_permission(mustela::PermissionChoice::Deny, mustela::SiteRule::Allow)
        == mustela::PermissionDecision::Granted);
    CHECK(mustela::resolve_permission(mustela::PermissionChoice::Allow, mustela::SiteRule::Deny)
        == mustela::PermissionDecision::Denied);
    CHECK(mustela::resolve_permission(mustela::PermissionChoice::Ask, mustela::SiteRule::Deny)
        == mustela::PermissionDecision::Denied);
    CHECK(mustela::resolve_permission(mustela::PermissionChoice::Ask, mustela::SiteRule::Allow)
        == mustela::PermissionDecision::Granted);
}

void test_site_rules_permissions() {
    std::string path = temp_path("site-permissions.toml");
    mustela::SiteRules rules(path);
    mustela::SitePolicy policy;
    policy.permissions = mustela::SiteRule::Allow;
    rules.set("trusted.example", policy);
    CHECK(rules.policy_for("trusted.example").permissions == mustela::SiteRule::Allow);
    CHECK(rules.entries().size() == 1);
    rules.save();

    mustela::SiteRules reloaded(path);
    reloaded.load();
    CHECK(reloaded.policy_for("trusted.example").permissions == mustela::SiteRule::Allow);

    mustela::SitePolicy denied = reloaded.policy_for("trusted.example");
    denied.permissions = mustela::SiteRule::Deny;
    reloaded.set("trusted.example", denied);
    CHECK(reloaded.policy_for("trusted.example").permissions == mustela::SiteRule::Deny);

    mustela::SitePolicy cleared = reloaded.policy_for("trusted.example");
    cleared.permissions = mustela::SiteRule::Default;
    reloaded.set("trusted.example", cleared);
    CHECK(reloaded.entries().empty());
}

void test_ui_text() {
    CHECK_STR(mustela::sanitize_dialog_text("hello"), "hello");
    CHECK_STR(mustela::sanitize_dialog_text(""), "");
    CHECK_STR(mustela::sanitize_dialog_text(std::string("a\x01" "b\x02" "c\x07" "d")), "abcd");
    CHECK_STR(mustela::sanitize_dialog_text(std::string("a\x0B" "b\x0C" "c\x0E" "d\x1F" "e\x7F" "f")), "abcdef");
    CHECK_STR(mustela::sanitize_dialog_text("line1\nline2"), "line1\nline2");
    CHECK_STR(mustela::sanitize_dialog_text("tab\there"), "tab\there");
    CHECK_STR(mustela::sanitize_dialog_text("win\r\nline"), "win\nline");
    CHECK_STR(mustela::sanitize_dialog_text("mac\rold"), "mac\nold");

    std::string cyrillic = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    std::string cut_dialog = mustela::sanitize_dialog_text(cyrillic, 7);
    CHECK_STR(cut_dialog, std::string("\xD0\xBF\xD1\x80\xE2\x80\xA6"));
    std::string cut_ascii = mustela::sanitize_dialog_text("abcdefghij", 5);
    CHECK_STR(cut_ascii, "ab\xE2\x80\xA6");
    CHECK_STR(mustela::sanitize_dialog_text("abcdefghij", 3), "\xE2\x80\xA6");

    CHECK_STR(mustela::sanitize_find_query("one two"), "one two");
    CHECK_STR(mustela::sanitize_find_query(std::string("one\ntwo\r\nthree\rfour")), "one two three four");
    CHECK_STR(mustela::sanitize_find_query(std::string("a\x01" "b")), "ab");
    std::string cut_query = mustela::sanitize_find_query(cyrillic, 7);
    CHECK_STR(cut_query, std::string("\xD0\xBF\xD1\x80\xD0\xB8"));
    bool valid_utf8 = true;
    for (size_t i = 0; i < cut_query.size(); ++i) {
        if ((static_cast<unsigned char>(cut_query[i]) & 0xC0) == 0x80) continue;
        unsigned char lead = static_cast<unsigned char>(cut_query[i]);
        size_t need = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        if (i + need > cut_query.size()) valid_utf8 = false;
    }
    CHECK(valid_utf8);
    CHECK(mustela::sanitize_find_query("abcdef", 3).size() == 3);

    CHECK_STR(mustela::sanitize_filename("report.pdf"), "report.pdf");
    CHECK_STR(mustela::sanitize_filename("../../etc/passwd"), "etcpasswd");
    CHECK_STR(mustela::sanitize_filename("a/b\\c.txt"), "abc.txt");
    CHECK_STR(mustela::sanitize_filename(std::string("a\x01" "b\x7F" "c.txt")), "abc.txt");
    CHECK_STR(mustela::sanitize_filename("..hidden.."), "hidden");
    CHECK_STR(mustela::sanitize_filename("..."), "download");
    CHECK_STR(mustela::sanitize_filename(""), "download");
    CHECK_STR(mustela::sanitize_filename("  "), "download");
    std::string cut_name = mustela::sanitize_filename(cyrillic, 6);
    CHECK_STR(cut_name, std::string("\xD0\xBF\xD1\x80\xD0\xB8"));
    CHECK_STR(mustela::sanitize_filename("abcdef", 4), "abcd");
}

}

int main() {
    test_settings_roundtrip();
    test_settings_partial_file();
    test_privacy_mapping();
    test_host_from_uri();
    test_site_rules_matching();
    test_resolve_site();
    test_history_store();
    test_fingerprint_policy();
    test_browsing_data_kinds();
    test_normalize_url();
    test_perf_module();
    test_ui_text();
    test_permissions();
    test_site_rules_permissions();

    if (failures > 0) {
        std::fprintf(stderr, "%d/%d checks failed\n", failures, checks);
        return 1;
    }
    std::printf("%d core checks passed\n", checks);
    return 0;
}
