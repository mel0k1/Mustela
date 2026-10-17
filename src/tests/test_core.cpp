#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "browsing_data.hpp"
#include "blocklist.hpp"
#include "bookmarks.hpp"
#include "config.hpp"
#include "discard.hpp"
#include "doh.hpp"
#include "error_page.hpp"
#include "fingerprint.hpp"
#include "fingerprint_scripts.hpp"
#include "history.hpp"
#include "import.hpp"
#include "perf.hpp"
#include "permissions.hpp"
#include "privacy.hpp"
#include "profile.hpp"
#include "psl.hpp"
#include "proxy_server.hpp"
#include "search.hpp"
#include "autofill.hpp"
#include "session.hpp"
#include "site_rules.hpp"
#include "ui_text.hpp"
#include "url.hpp"
#include "mustela/mustela.hpp"

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
    original.session_restore = false;
    original.devtools = false;
    original.spellcheck = false;
    original.spellcheck_languages = "en_US,ru_RU";
    original.autofill = true;
    original.autofill_save = true;
    original.gpc = false;
    original.blocklist = false;
    original.blocklist_file = "/tmp/trackers.list";
    original.dns_over_https = "https://dns.example/dns-query";

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
    CHECK(!loaded.session_restore);
    CHECK(!loaded.devtools);
    CHECK(!loaded.spellcheck);
    CHECK_STR(loaded.spellcheck_languages, "en_US,ru_RU");
    CHECK(loaded.autofill);
    CHECK(loaded.autofill_save);
    CHECK(!loaded.gpc);
    CHECK(!loaded.blocklist);
    CHECK_STR(loaded.blocklist_file, "/tmp/trackers.list");
    CHECK_STR(loaded.dns_over_https, "https://dns.example/dns-query");

    mustela::Settings fallback = mustela::load_settings(path + "_missing");
    CHECK_STR(fallback.homepage, "https://example.org");
    CHECK_STR(fallback.cookie_policy, "no-third-party");
    CHECK(fallback.tracking_prevention);
    CHECK(!fallback.fingerprint_webgl);
    CHECK_STR(fallback.fingerprint_referrer, "same-origin");
    CHECK_STR(fallback.process_model, "per-view");
    CHECK(fallback.spellcheck);
    CHECK(fallback.spellcheck_languages.empty());
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
        "spellcheck = yes\n"
        "spellcheck_languages = \"en_US, ru_RU\"\n"
        "window_width = 800  # trailing comment\n"
        "window_height = not-a-number\n"
        "[privacy]\n"
        "cookie_policy = always\n"
        "javascript = off\n");
    mustela::Settings loaded = mustela::load_settings(path);
    CHECK_STR(loaded.homepage, "https://partial.example");
    CHECK(loaded.spellcheck);
    CHECK_STR(loaded.spellcheck_languages, "en_US, ru_RU");
    CHECK(loaded.window_width == 800);
    CHECK(loaded.window_height == 800);
    CHECK(loaded.cookie_policy == "always");
    CHECK(!loaded.javascript_enabled);
    CHECK(loaded.tracking_prevention);
}

void test_settings_load_report() {
    std::string path = write_temp("report.toml",
        "version = 2\n"
        "[shell]\n"
        "homepage = \"https://report.example\"\n"
        "window_height = not-a-number\n"
        "zoom = 5.0\n"
        "homeslice = \"oops\"\n"
        "[proxy]\n"
        "mode = \"bogus\"\n"
        "[permissions]\n"
        "camera = \"maybe\"\n");
    mustela::SettingsReport report;
    mustela::Settings loaded = mustela::load_settings(path, report);
    CHECK(report.file_found);
    CHECK(report.version == 2);
    CHECK(report.warnings.size() == 1);
    CHECK(report.warnings[0].find("newer than supported") != std::string::npos);
    CHECK(report.unknown_keys.size() == 1);
    CHECK_STR(report.unknown_keys[0], "shell.homeslice");
    CHECK(report.invalid_values.size() == 3);
    CHECK(report.applied == 2);
    CHECK(report.summary().find("unknown=1") != std::string::npos);
    CHECK(report.summary().find("invalid=3") != std::string::npos);
    CHECK_STR(loaded.homepage, "https://report.example");
    CHECK(loaded.window_height == 800);
    CHECK(loaded.zoom_default == 3.0);
    CHECK_STR(loaded.proxy_mode, "off");
    CHECK_STR(loaded.permissions_camera, "deny");

    std::string clean = write_temp("clean_settings.toml",
        "version = 1\n[shell]\nhomepage = \"https://clean.example\"\n");
    mustela::SettingsReport ok;
    mustela::Settings clean_settings = mustela::load_settings(clean, ok);
    CHECK(ok.version == 1);
    CHECK(ok.applied == 1);
    CHECK(ok.warnings.empty());
    CHECK(ok.unknown_keys.empty());
    CHECK(ok.invalid_values.empty());
    CHECK_STR(clean_settings.homepage, "https://clean.example");

    mustela::SettingsReport missing;
    mustela::Settings defaults = mustela::load_settings(temp_path("missing_settings.toml"), missing);
    CHECK(!missing.file_found);
    CHECK(missing.version == 0);
    CHECK(missing.applied == 0);
    CHECK(missing.unknown_keys.empty());
    CHECK(missing.invalid_values.empty());
    CHECK(missing.warnings.empty());
    CHECK_STR(defaults.homepage, "https://example.org");

    std::string saved_path = temp_path("saved_report.toml");
    mustela::Settings original;
    mustela::save_settings(saved_path, original);
    mustela::SettingsReport roundtrip;
    mustela::load_settings(saved_path, roundtrip);
    CHECK(roundtrip.version == mustela::kSettingsVersion);
    CHECK(roundtrip.applied > 30);
    CHECK(roundtrip.unknown_keys.empty());
    CHECK(roundtrip.invalid_values.empty());
    CHECK(roundtrip.warnings.empty());
}

void test_psl() {
    CHECK(mustela::is_public_suffix("com"));
    CHECK(mustela::is_public_suffix("COM"));
    CHECK(mustela::is_public_suffix("uk"));
    CHECK(mustela::is_public_suffix("co.uk"));
    CHECK(mustela::is_public_suffix("com.au"));
    CHECK(mustela::is_public_suffix("example"));
    CHECK(!mustela::is_public_suffix("example.com"));
    CHECK(!mustela::is_public_suffix("foo.co.uk"));
    CHECK(!mustela::is_public_suffix("deep.www.example.com"));
    CHECK(!mustela::is_public_suffix(""));
    CHECK(!mustela::is_public_suffix("localhost"));
    CHECK(!mustela::is_public_suffix("www.localhost"));
    CHECK(!mustela::is_public_suffix("192.168.1.1"));
    CHECK(!mustela::is_public_suffix("2001:db8::1"));
    CHECK(mustela::is_public_suffix("foo.ck"));
    CHECK(!mustela::is_public_suffix("www.ck"));
    CHECK(!mustela::is_public_suffix("a.www.ck"));
    CHECK(mustela::is_public_suffix("foo.kobe.jp"));
    CHECK(!mustela::is_public_suffix("a.foo.kobe.jp"));
    CHECK(!mustela::is_public_suffix("city.kobe.jp"));
    CHECK(!mustela::is_public_suffix("kobe.jp"));
    CHECK(!mustela::is_public_suffix("www.city.kobe.jp"));
    CHECK(!mustela::is_public_suffix("city.kawasaki.jp"));
    CHECK(mustela::is_public_suffix("b.kawasaki.jp"));
    CHECK(!mustela::is_public_suffix("a.b.kawasaki.jp"));
    CHECK(mustela::is_public_suffix("blogspot.com"));
    CHECK(mustela::is_public_suffix("cloudfront.net"));
    CHECK(!mustela::is_public_suffix("compute.amazonaws.com"));
    CHECK(mustela::is_public_suffix("a.compute.amazonaws.com"));
    CHECK(mustela::is_public_suffix("xn--p1ai"));
    CHECK(mustela::is_public_suffix("in-addr.arpa"));
    CHECK(!mustela::psl_version().empty());
    CHECK_STR(mustela::registrable_domain("deep.www.example.com"), "example.com");
    CHECK_STR(mustela::registrable_domain("foo.co.uk"), "foo.co.uk");
    CHECK_STR(mustela::registrable_domain("co.uk"), "");
    CHECK_STR(mustela::registrable_domain("uk"), "");
    CHECK_STR(mustela::registrable_domain("example.com."), "example.com");
    CHECK_STR(mustela::registrable_domain("WWW.Example.COM"), "example.com");
    CHECK_STR(mustela::registrable_domain("a.example"), "a.example");
    CHECK_STR(mustela::registrable_domain("localhost"), "localhost");
    CHECK_STR(mustela::registrable_domain("192.168.1.1"), "192.168.1.1");
    CHECK_STR(mustela::registrable_domain("www.ck"), "www.ck");
    CHECK_STR(mustela::registrable_domain("a.www.ck"), "www.ck");
    CHECK_STR(mustela::registrable_domain("foo.ck"), "");
    CHECK_STR(mustela::registrable_domain("ward.city.kobe.jp"), "city.kobe.jp");
    CHECK_STR(mustela::registrable_domain("city.kobe.jp"), "city.kobe.jp");
    CHECK_STR(mustela::registrable_domain("kobe.jp"), "kobe.jp");
    CHECK_STR(mustela::registrable_domain("a.b.kawasaki.jp"), "a.b.kawasaki.jp");
    CHECK_STR(mustela::registrable_domain("a.b.blogspot.com"), "b.blogspot.com");
    CHECK_STR(mustela::registrable_domain("a.b.cloudfront.net"), "b.cloudfront.net");
    CHECK_STR(mustela::registrable_domain("x.a.compute.amazonaws.com"), "x.a.compute.amazonaws.com");
    CHECK_STR(mustela::registrable_domain("www.xn--e1afmkfd.xn--p1ai"), "xn--e1afmkfd.xn--p1ai");
    CHECK_STR(mustela::registrable_domain("4.3.2.1.in-addr.arpa"), "1.in-addr.arpa");
    CHECK_STR(mustela::registrable_domain("a.foo.kobe.jp"), "a.foo.kobe.jp");
    CHECK_STR(mustela::registrable_domain("foo.kobe.jp"), "");
    CHECK_STR(mustela::registrable_domain(""), "");
}

void test_site_rules_psl_guard() {
    std::string path = write_temp("sites_psl.toml",
        "[co.uk]\n"
        "javascript = \"deny\"\n"
        "[com]\n"
        "javascript = \"deny\"\n"
        "[foo.co.uk]\n"
        "cookies = \"allow\"\n");
    mustela::SiteRules rules(path);
    rules.load();
    CHECK(rules.policy_for("shop.example.co.uk").javascript == mustela::SiteRule::Default);
    CHECK(rules.policy_for("shop.example.co.uk").cookies == mustela::SiteRule::Default);
    CHECK(rules.policy_for("foo.co.uk").cookies == mustela::SiteRule::Allow);
    CHECK(rules.policy_for("anything.com").javascript == mustela::SiteRule::Default);

    mustela::SitePolicy denied;
    denied.javascript = mustela::SiteRule::Deny;
    rules.set("co.uk", denied);
    rules.set("org.uk", denied);
    CHECK(rules.policy_for("bar.co.uk").javascript == mustela::SiteRule::Default);
    CHECK(rules.entries().size() == 1);
    rules.set("example.org", denied);
    CHECK(rules.entries().size() == 2);
    CHECK(rules.policy_for("www.example.org").javascript == mustela::SiteRule::Deny);
    rules.save();

    mustela::SiteRules reloaded(path);
    reloaded.load();
    CHECK(reloaded.entries().size() == 2);
    CHECK(reloaded.policy_for("sub.foo.co.uk").cookies == mustela::SiteRule::Allow);
    CHECK(reloaded.policy_for("sub.foo.co.uk").javascript == mustela::SiteRule::Default);
    CHECK(reloaded.policy_for("deep.example.org").javascript == mustela::SiteRule::Deny);
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

    mustela::SitePolicy dressed;
    dressed.zoom = 1.5;
    dressed.user_agent = "MustelaUA/1.0";
    dressed.css = "body{display:none}";
    rules.set("dress.example", dressed);
    mustela::SiteDecision styled = mustela::resolve_site(policy, rules, "https://dress.example/what");
    CHECK(styled.zoom == 1.5);
    CHECK_STR(styled.user_agent, "MustelaUA/1.0");
    CHECK_STR(styled.css, "body{display:none}");
    CHECK(plain.zoom == 0.0);
    CHECK_STR(plain.user_agent, "");
    CHECK_STR(plain.css, "");
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

void test_blocklist() {
    mustela::Blocklist defaults;
    CHECK(defaults.size() > 50);
    CHECK(defaults.hit("doubleclick.net"));
    CHECK(defaults.hit("ad.doubleclick.net"));
    CHECK(defaults.hit("AD.DoubleClick.NET."));
    CHECK(!defaults.hit("notdoubleclick.net"));
    CHECK(!defaults.hit("doubleclick.net.evil.com"));
    CHECK(!defaults.hit("example.org"));
    CHECK_STR(mustela::normalize_host(" Example.COM. "), "example.com");
    CHECK(mustela::host_matches_domain("a.b.example.com", "example.com"));
    CHECK(!mustela::host_matches_domain("example.com", "a.b.example.com"));
    CHECK(!mustela::host_matches_domain("", "example.com"));

    mustela::Blocklist custom;
    size_t base = custom.size();
    custom.add("Example.Net");
    custom.add("example.net");
    CHECK(custom.size() == base + 1);
    CHECK(custom.hit("www.example.net"));
    CHECK(!custom.hit("example.org"));
    custom.add("bad..host");
    custom.add("-bad");
    custom.add("not a domain!");
    CHECK(custom.size() == base + 1);

    std::string extra = write_temp("blocklist_extra.list",
        "# comment\n  ads.example.net  \n\nBAD.HOST\nnot a domain!\n");
    mustela::Blocklist loaded = mustela::default_blocklist(extra);
    CHECK(loaded.hit("ads.example.net"));
    CHECK(loaded.hit("bad.host"));
    CHECK(!loaded.hit("example.net"));

    std::string json = mustela::blocklist_rules_json({"Example.com", "bad host", "bad..host", ""});
    CHECK_STR(json,
        std::string("[{\"trigger\":{\"url-filter\":\".*\",\"if-domain\":[\"example.com\",\"*.example.com\"]},"
            "\"action\":{\"type\":\"block\"}}]"));
    CHECK_STR(mustela::blocklist_rules_json({}), "");
}

void test_doh_codec() {
    std::vector<unsigned char> query = mustela::doh::build_query("example.com", 1);
    static const unsigned char expected[] = {
        0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0x03, 'c', 'o', 'm', 0x00,
        0x00, 0x01, 0x00, 0x01
    };
    CHECK(query.size() == sizeof(expected));
    bool same = query.size() == sizeof(expected);
    for (size_t i = 0; same && i < query.size(); ++i) same = query[i] == expected[i];
    CHECK(same);
    CHECK(mustela::doh::build_query("", 1).empty());
    CHECK(mustela::doh::build_query("a..b", 1).empty());
    CHECK(mustela::doh::build_query(std::string(64, 'a'), 1).empty());

    CHECK_STR(mustela::doh::encode_base64url({0x00, 0x01, 0x02}), "AAEC");
    CHECK_STR(mustela::doh::encode_base64url({0xFB, 0xFF}), "-_8");
    CHECK_STR(mustela::doh::encode_base64url({0x00}), "AA");
    CHECK_STR(mustela::doh::encode_base64url({}), "");

    std::string uri = mustela::doh::query_uri("https://dns.example/dns-query", "example.com");
    CHECK(uri.rfind("https://dns.example/dns-query?dns=", 0) == 0);
    std::string chained = mustela::doh::query_uri("https://dns.example/dns-query?cache=1", "example.com");
    CHECK(chained.rfind("https://dns.example/dns-query?cache=1&dns=", 0) == 0);
    CHECK_STR(mustela::doh::query_uri("", "example.com"), "");

    std::vector<unsigned char> packet;
    auto u16 = [&packet](unsigned short value) {
        packet.push_back(static_cast<unsigned char>(value >> 8));
        packet.push_back(static_cast<unsigned char>(value & 0xFF));
    };
    u16(0x1234);
    u16(0x8180);
    u16(1);
    u16(1);
    u16(0);
    u16(0);
    packet.push_back(7);
    for (char c : std::string("example")) packet.push_back(static_cast<unsigned char>(c));
    packet.push_back(3);
    packet.push_back('c');
    packet.push_back('o');
    packet.push_back('m');
    packet.push_back(0);
    u16(1);
    u16(1);
    packet.push_back(0xC0);
    packet.push_back(0x0C);
    u16(1);
    u16(1);
    u16(0);
    u16(300);
    u16(4);
    packet.push_back(93);
    packet.push_back(184);
    packet.push_back(216);
    packet.push_back(34);

    mustela::doh::Response response;
    CHECK(mustela::doh::parse_response(packet.data(), packet.size(), response));
    CHECK(response.rcode == mustela::doh::Rcode::NoError);
    CHECK(response.answers.size() == 1);
    CHECK_STR(response.answers[0].name, "example.com");
    CHECK(response.answers[0].type == 1);
    CHECK(response.answers[0].ttl == 300);
    CHECK_STR(response.answers[0].data, "93.184.216.34");

    std::vector<unsigned char> name_error = packet;
    name_error[3] = 0x83;
    CHECK(mustela::doh::parse_response(name_error.data(), name_error.size(), response));
    CHECK(response.rcode == mustela::doh::Rcode::NameError);

    std::vector<unsigned char> cname = packet;
    size_t answer_type_offset = 12 + 13 + 4 + 2;
    size_t answer_rdata_offset = answer_type_offset + 8 + 2;
    cname[answer_type_offset] = 0;
    cname[answer_type_offset + 1] = 5;
    cname[answer_rdata_offset] = 0xC0;
    cname[answer_rdata_offset + 1] = 0x0C;
    CHECK(mustela::doh::parse_response(cname.data(), cname.size(), response));
    CHECK_STR(response.answers[0].data, "example.com");

    std::vector<unsigned char> ipv6 = packet;
    ipv6[answer_type_offset] = 0;
    ipv6[answer_type_offset + 1] = 28;
    ipv6[answer_type_offset + 8] = 0;
    ipv6[answer_type_offset + 9] = 16;
    ipv6[answer_rdata_offset] = 0x20;
    ipv6[answer_rdata_offset + 1] = 0x01;
    ipv6[answer_rdata_offset + 2] = 0x0d;
    ipv6[answer_rdata_offset + 3] = 0xb8;
    for (int i = 0; i < 10; ++i) ipv6.push_back(0);
    ipv6.push_back(0);
    ipv6.push_back(1);
    CHECK(mustela::doh::parse_response(ipv6.data(), ipv6.size(), response));
    CHECK_STR(response.answers[0].data, "2001:db8::1");

    CHECK(!mustela::doh::parse_response(packet.data(), 11, response));
    CHECK(!mustela::doh::parse_response(packet.data(), 20, response));
    CHECK(!mustela::doh::parse_response(nullptr, 0, response));

    std::vector<unsigned char> loop;
    auto u16v = [](std::vector<unsigned char>& out, unsigned short value) {
        out.push_back(static_cast<unsigned char>(value >> 8));
        out.push_back(static_cast<unsigned char>(value & 0xFF));
    };
    u16v(loop, 0);
    u16v(loop, 0x8180);
    u16v(loop, 0);
    u16v(loop, 1);
    u16v(loop, 0);
    u16v(loop, 0);
    loop.push_back(0xC0);
    loop.push_back(0x0C);
    u16v(loop, 1);
    u16v(loop, 1);
    u16v(loop, 0);
    u16v(loop, 60);
    u16v(loop, 0);
    CHECK(!mustela::doh::parse_response(loop.data(), loop.size(), response));
}

void test_session_store() {
    std::string path = temp_path("session.json");
    mustela::SessionData data;
    data.active = 1;
    mustela::SessionTab first;
    first.url = "https://example.org/page?a=1&b=2";
    first.title = "Ex\"ample \\ \n\tтест";
    first.back = {"https://example.org/", "https://example.org/about"};
    first.forward = {"https://example.org/next"};
    first.state = "AAEC+/==";
    mustela::SessionTab second;
    second.url = "https://mustela.example";
    second.title = "Second";
    data.tabs = {first, second};
    CHECK(mustela::save_session(path, data));

    mustela::SessionData loaded;
    CHECK(mustela::load_session(path, loaded));
    CHECK(loaded.active == 1);
    CHECK(loaded.tabs.size() == 2);
    CHECK_STR(loaded.tabs[0].url, first.url);
    CHECK_STR(loaded.tabs[0].title, first.title);
    CHECK_STR(loaded.tabs[0].state, first.state);
    CHECK(loaded.tabs[0].back.size() == 2);
    CHECK_STR(loaded.tabs[0].back[1], "https://example.org/about");
    CHECK(loaded.tabs[0].forward.size() == 1);
    CHECK_STR(loaded.tabs[1].title, "Second");

    std::string hand = "{\"version\":1,\"active\":7,\"future\":true,\"tabs\":["
        "{\"url\":\"https://a.example\",\"rank\":3,\"back\":[],\"title\":\"A\"},"
        "{\"url\":\"https://b.example\"}]}";
    std::string hand_path = write_temp("session_hand.json", hand);
    mustela::SessionData parsed;
    CHECK(mustela::load_session(hand_path, parsed));
    CHECK(parsed.tabs.size() == 2);
    CHECK(parsed.active == 1);
    CHECK_STR(parsed.tabs[0].title, "A");
    CHECK_STR(parsed.tabs[1].url, "https://b.example");

    CHECK(!mustela::load_session(path + "_missing", parsed));
    std::string broken = write_temp("session_broken.json", "{\"tabs\":[{\"url\":}");
    CHECK(!mustela::load_session(broken, parsed));
    std::string empty = write_temp("session_empty.json", "");
    CHECK(!mustela::load_session(empty, parsed));

    std::string empty_path = temp_path("session_empty_save.json");
    mustela::SessionData empty_data;
    CHECK(mustela::save_session(empty_path, empty_data));
    mustela::SessionData empty_loaded;
    CHECK(mustela::load_session(empty_path, empty_loaded));
    CHECK(empty_loaded.tabs.empty());
    CHECK(empty_loaded.active == 0);
}

}

using namespace mustela;

void test_proxy_settings_keys() {
    std::string path = write_temp("proxy_settings.toml",
        "[proxy]\n"
        "mode = \"local\"\n"
        "upstream = \"http://gw.example:8080\"\n"
        "user = \"u\"\n"
        "password = \"p\"\n");
    Settings settings = load_settings(path);
    CHECK_STR(settings.proxy_mode, "local");
    CHECK_STR(settings.proxy_upstream, "http://gw.example:8080");
    CHECK_STR(settings.proxy_user, "u");
    CHECK_STR(settings.proxy_password, "p");
    std::string bad = write_temp("proxy_settings_bad.toml",
        "[proxy]\n"
        "mode = \"bogus\"\n");
    Settings bad_settings = load_settings(bad);
    CHECK_STR(bad_settings.proxy_mode, "off");
    std::string saved = write_temp("proxy_settings_saved.toml", "");
    save_settings(saved, settings);
    Settings roundtrip = load_settings(saved);
    CHECK_STR(roundtrip.proxy_mode, "local");
    CHECK_STR(roundtrip.proxy_upstream, "http://gw.example:8080");
    CHECK_STR(roundtrip.proxy_user, "u");
    CHECK_STR(roundtrip.proxy_password, "p");
}

void test_proxy_urls() {
    ProxyEndpoint ep;
    CHECK(parse_proxy_url("http://127.0.0.1:3128", ep));
    CHECK_STR(ep.host, "127.0.0.1");
    CHECK(ep.port == 3128);
    CHECK(ep.user.empty() && ep.password.empty());
    CHECK(parse_proxy_url("http://user:secret@gw.example:8080", ep));
    CHECK_STR(ep.user, "user");
    CHECK_STR(ep.password, "secret");
    CHECK(ep.port == 8080);
    CHECK(parse_proxy_url("http://gw.example", ep));
    CHECK(ep.port == 80);
    CHECK(!parse_proxy_url("https://gw.example:443", ep));
    CHECK(!parse_proxy_url("gw.example:8080", ep));
    CHECK(!parse_proxy_url("http://", ep));
    ProxyEndpoint made;
    made.host = "127.0.0.1";
    made.port = 9052;
    CHECK_STR(proxy_url_from(made), "http://127.0.0.1:9052");
    made.user = "u";
    made.password = "p";
    CHECK_STR(proxy_url_from(made), "http://u:p@127.0.0.1:9052");
    CHECK(host_is_ip_literal("127.0.0.1"));
    CHECK(host_is_ip_literal("[::1]"));
    CHECK(!host_is_ip_literal("ok.example"));
    CHECK(!host_is_ip_literal(""));
}

#ifdef _WIN32
using TestSocket = SOCKET;
constexpr TestSocket kTestInvalidSocket = INVALID_SOCKET;

void test_close_socket(TestSocket handle) {
    if (handle != kTestInvalidSocket) closesocket(handle);
}
#else
using TestSocket = int;
constexpr TestSocket kTestInvalidSocket = -1;

void test_close_socket(TestSocket handle) {
    if (handle != kTestInvalidSocket) {
        shutdown(handle, SHUT_RDWR);
        close(handle);
    }
}
#endif

void test_sockets_init() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        done = true;
    }
#endif
}

TestSocket test_listen(int& port) {
    TestSocket fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kTestInvalidSocket) return kTestInvalidSocket;
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(fd, 8) != 0) {
        test_close_socket(fd);
        return kTestInvalidSocket;
    }
    socklen_t length = sizeof(address);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        test_close_socket(fd);
        return kTestInvalidSocket;
    }
    port = ntohs(address.sin_port);
    return fd;
}

TestSocket test_connect(int port) {
    TestSocket fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kTestInvalidSocket) return kTestInvalidSocket;
    timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        test_close_socket(fd);
        return kTestInvalidSocket;
    }
    return fd;
}

bool test_send(TestSocket handle, const std::string& text) {
    size_t sent = 0;
    while (sent < text.size()) {
        int written = static_cast<int>(send(handle, text.data() + sent,
            static_cast<int>(text.size() - sent), 0));
        if (written <= 0) return false;
        sent += static_cast<size_t>(written);
    }
    return true;
}

std::string test_recv_until(TestSocket handle, const std::string& marker) {
    std::string out;
    char buffer[2048];
    while (out.find(marker) == std::string::npos) {
        int got = static_cast<int>(recv(handle, buffer, static_cast<int>(sizeof(buffer)), 0));
        if (got <= 0) break;
        out.append(buffer, buffer + got);
    }
    return out;
}

std::string test_recv_all(TestSocket handle) {
    std::string out;
    char buffer[4096];
    for (;;) {
        int got = static_cast<int>(recv(handle, buffer, static_cast<int>(sizeof(buffer)), 0));
        if (got <= 0) break;
        out.append(buffer, buffer + got);
    }
    return out;
}

void test_relay(TestSocket a, TestSocket b) {
    char buffer[4096];
    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(a, &readfds);
        FD_SET(b, &readfds);
        timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
#ifdef _WIN32
        int ready = select(0, &readfds, nullptr, nullptr, &timeout);
#else
        int ready = select(std::max(a, b) + 1, &readfds, nullptr, nullptr, &timeout);
#endif
        if (ready <= 0) break;
        bool alive = true;
        if (FD_ISSET(a, &readfds)) {
            int got = static_cast<int>(recv(a, buffer, static_cast<int>(sizeof(buffer)), 0));
            if (got <= 0 || !test_send(b, std::string(buffer, buffer + got))) alive = false;
        }
        if (alive && FD_ISSET(b, &readfds)) {
            int got = static_cast<int>(recv(b, buffer, static_cast<int>(sizeof(buffer)), 0));
            if (got <= 0 || !test_send(a, std::string(buffer, buffer + got))) alive = false;
        }
        if (!alive) break;
    }
    test_close_socket(a);
    test_close_socket(b);
}

struct StubOrigin {
    TestSocket listener = kTestInvalidSocket;
    int port = 0;
    std::thread worker;
    std::string reply;
    bool raw = false;

    bool start(const std::string& reply_text, bool raw_mode) {
        test_sockets_init();
        reply = reply_text;
        raw = raw_mode;
        listener = test_listen(port);
        if (listener == kTestInvalidSocket) return false;
        TestSocket handle = listener;
        worker = std::thread([this, handle]() { run(handle); });
        return true;
    }

    void run(TestSocket handle) {
        for (;;) {
            TestSocket client = accept(handle, nullptr, nullptr);
            if (client == kTestInvalidSocket) break;
            if (raw) {
                char buffer[64];
                recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
                test_send(client, reply);
            } else {
                test_recv_until(client, "\r\n\r\n");
                test_send(client, reply);
            }
            test_close_socket(client);
        }
    }

    void stop() {
        if (listener != kTestInvalidSocket) {
            test_close_socket(listener);
            listener = kTestInvalidSocket;
        }
        if (worker.joinable()) worker.join();
    }
};

struct StubUpstream {
    TestSocket listener = kTestInvalidSocket;
    int port = 0;
    std::thread worker;
    int tunnel_origin_port = 0;

    bool start(int tunnel_port) {
        test_sockets_init();
        tunnel_origin_port = tunnel_port;
        listener = test_listen(port);
        if (listener == kTestInvalidSocket) return false;
        TestSocket handle = listener;
        worker = std::thread([this, handle]() { run(handle); });
        return true;
    }

    void run(TestSocket handle) {
        for (;;) {
            TestSocket client = accept(handle, nullptr, nullptr);
            if (client == kTestInvalidSocket) break;
            std::string request = test_recv_until(client, "\r\n\r\n");
            if (request.rfind("CONNECT", 0) == 0) {
                test_send(client, "HTTP/1.1 200 Connection established\r\n\r\n");
                TestSocket origin = test_connect(tunnel_origin_port);
                if (origin != kTestInvalidSocket) {
                    test_relay(client, origin);
                    continue;
                }
            } else {
                test_send(client, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nup");
            }
            test_close_socket(client);
        }
    }

    void stop() {
        if (listener != kTestInvalidSocket) {
            test_close_socket(listener);
            listener = kTestInvalidSocket;
        }
        if (worker.joinable()) worker.join();
    }
};

std::vector<unsigned char> make_a_response(const std::vector<unsigned char>& query, const unsigned char ip[4],
    unsigned ttl, unsigned char rcode) {
    std::vector<unsigned char> out = query;
    out[2] |= 0x80;
    out[3] = static_cast<unsigned char>((out[3] & 0x70) | 0x80 | (rcode & 0x0F));
    out[6] = 0;
    out[7] = rcode == 0 ? 1 : 0;
    out.push_back(0xC0);
    out.push_back(0x0C);
    out.push_back(0);
    out.push_back(1);
    out.push_back(0);
    out.push_back(1);
    out.push_back(static_cast<unsigned char>((ttl >> 24) & 0xFF));
    out.push_back(static_cast<unsigned char>((ttl >> 16) & 0xFF));
    out.push_back(static_cast<unsigned char>((ttl >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>(ttl & 0xFF));
    out.push_back(0);
    out.push_back(4);
    for (int i = 0; i < 4; ++i) out.push_back(ip[i]);
    return out;
}

void test_doh_resolver() {
    int calls = 0;
    bool fail_transport = false;
    const unsigned char ip[4] = {93, 184, 216, 34};
    DohTransport transport = [&](const std::string&, const std::vector<unsigned char>& query,
        std::vector<unsigned char>& answer) {
        ++calls;
        if (fail_transport) return false;
        answer = make_a_response(query, ip, 120, 0);
        return true;
    };
    DohResolver resolver(transport, "https://doh.example/dns-query");
    CHECK_STR(resolver.resolve("ok.example"), "93.184.216.34");
    CHECK_STR(resolver.resolve("ok.example"), "93.184.216.34");
    CHECK(calls == 1);
    fail_transport = true;
    CHECK_STR(resolver.resolve("fresh.example"), "");
    fail_transport = false;
    CHECK_STR(resolver.resolve("127.0.0.1"), "");
    CHECK(calls == 2);
    DohTransport nxdomain = [&](const std::string&, const std::vector<unsigned char>& query,
        std::vector<unsigned char>& answer) {
        answer = make_a_response(query, ip, 120, 3);
        return true;
    };
    DohResolver strict(nxdomain, "https://doh.example/dns-query");
    CHECK_STR(strict.resolve("missing.example"), "");
}

void test_proxy_server_loopback() {
    test_sockets_init();
    StubOrigin http_origin;
    CHECK(http_origin.start("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello", false));
    StubOrigin raw_origin;
    CHECK(raw_origin.start("pong", true));
    Blocklist list;
    list.add("tracker.example");
    const unsigned char ip[4] = {127, 0, 0, 1};
    DohTransport transport = [&](const std::string&, const std::vector<unsigned char>& query,
        std::vector<unsigned char>& answer) {
        answer = make_a_response(query, ip, 60, 0);
        return true;
    };

    ProxyConfig config;
    config.resolve_via_doh = true;
    config.doh_url = "https://doh.example/dns-query";
    ProxyServer proxy;
    CHECK(proxy.start(config, list, transport));
    int port = proxy.port();
    CHECK(port > 0);

    TestSocket client = test_connect(port);
    CHECK(client != kTestInvalidSocket);
    CHECK(test_send(client, "CONNECT tracker.example:443 HTTP/1.1\r\nHost: tracker.example:443\r\n\r\n"));
    std::string reply = test_recv_until(client, "\r\n\r\n");
    CHECK(reply.rfind("HTTP/1.1 403", 0) == 0);
    test_close_socket(client);

    client = test_connect(port);
    CHECK(test_send(client, "CONNECT sub.tracker.example:443 HTTP/1.1\r\nHost: sub.tracker.example:443\r\n\r\n"));
    reply = test_recv_until(client, "\r\n\r\n");
    CHECK(reply.rfind("HTTP/1.1 403", 0) == 0);
    test_close_socket(client);

    client = test_connect(port);
    CHECK(test_send(client, "CONNECT raw.example:" + std::to_string(raw_origin.port) +
        " HTTP/1.1\r\nHost: raw.example\r\n\r\n"));
    reply = test_recv_until(client, "\r\n\r\n");
    CHECK(reply.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(test_send(client, "ping"));
    std::string tunneled = test_recv_all(client);
    CHECK_STR(tunneled, "pong");
    test_close_socket(client);

    client = test_connect(port);
    CHECK(test_send(client, "GET http://tracker.example/x HTTP/1.1\r\nHost: tracker.example\r\n\r\n"));
    reply = test_recv_until(client, "\r\n\r\n");
    CHECK(reply.rfind("HTTP/1.1 403", 0) == 0);
    test_close_socket(client);

    client = test_connect(port);
    CHECK(test_send(client, "GET http://raw.example:" + std::to_string(http_origin.port) +
        "/path HTTP/1.1\r\nHost: raw.example\r\n\r\n"));
    reply = test_recv_all(client);
    CHECK(reply.find("hello") != std::string::npos);
    test_close_socket(client);

    DohTransport failing = [](const std::string&, const std::vector<unsigned char>&,
        std::vector<unsigned char>&) {
        return false;
    };
    ProxyServer strict_proxy;
    CHECK(strict_proxy.start(config, list, failing));
    client = test_connect(strict_proxy.port());
    CHECK(test_send(client, "CONNECT raw.example:443 HTTP/1.1\r\nHost: raw.example\r\n\r\n"));
    reply = test_recv_until(client, "\r\n\r\n");
    CHECK(reply.rfind("HTTP/1.1 502", 0) == 0);
    test_close_socket(client);
    strict_proxy.stop();

    proxy.stop();
    CHECK(proxy.port() == 0);

    StubUpstream upstream;
    CHECK(upstream.start(raw_origin.port));
    ProxyConfig chained;
    chained.has_upstream = true;
    chained.upstream.host = "127.0.0.1";
    chained.upstream.port = upstream.port;
    ProxyServer chained_proxy;
    CHECK(chained_proxy.start(chained, list, transport));
    client = test_connect(chained_proxy.port());
    CHECK(test_send(client, "CONNECT raw.example:" + std::to_string(raw_origin.port) +
        " HTTP/1.1\r\nHost: raw.example\r\n\r\n"));
    reply = test_recv_until(client, "\r\n\r\n");
    CHECK(reply.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(test_send(client, "ping"));
    tunneled = test_recv_all(client);
    CHECK_STR(tunneled, "pong");
    test_close_socket(client);

    client = test_connect(chained_proxy.port());
    CHECK(test_send(client, "GET http://raw.example:" + std::to_string(http_origin.port) +
        "/path HTTP/1.1\r\nHost: raw.example\r\n\r\n"));
    reply = test_recv_all(client);
    CHECK(reply.find("up") != std::string::npos);
    test_close_socket(client);
    chained_proxy.stop();
    upstream.stop();
    http_origin.stop();
    raw_origin.stop();
}

void test_bookmarks_store() {
    std::string path = temp_path("bookmarks.tsv");
    {
        mustela::BookmarkStore store(path);
        CHECK(store.entries().empty());
        CHECK(!store.contains("https://mustela.example/"));
        CHECK(!store.remove("https://mustela.example/"));
        CHECK(!store.add("", "no url", 300));
        CHECK(store.add("https://mustela.example/", "Mustela", 100));
        CHECK(store.add("https://webkit.example/x", "WebKit", 200));
        CHECK(store.contains("https://mustela.example/"));
        CHECK(store.contains("https://webkit.example/x"));
        CHECK(!store.contains("https://other.example/"));
        CHECK(store.add("https://mustela.example/", "Mustela 2", 400));
        CHECK(store.entries().size() == 2);
        CHECK_STR(store.entries()[0].url, "https://mustela.example/");
        CHECK_STR(store.entries()[0].title, "Mustela 2");
        CHECK(store.entries()[0].added_at == 400);
        CHECK_STR(store.entries()[1].url, "https://webkit.example/x");
        CHECK(store.remove("https://webkit.example/x"));
        CHECK(!store.remove("https://webkit.example/x"));
        CHECK(store.entries().size() == 1);
    }
    mustela::BookmarkStore reloaded(path);
    CHECK(reloaded.entries().size() == 1);
    CHECK_STR(reloaded.entries()[0].url, "https://mustela.example/");
    CHECK_STR(reloaded.entries()[0].title, "Mustela 2");
    CHECK(reloaded.entries()[0].added_at == 400);

    std::string escaped_path = temp_path("bookmarks_esc.tsv");
    {
        mustela::BookmarkStore store(escaped_path);
        CHECK(store.add("https://tab.example/a\tb", "title\twith\ttabs\nand newline", 500));
    }
    mustela::BookmarkStore roundtrip(escaped_path);
    CHECK(roundtrip.entries().size() == 1);
    CHECK_STR(roundtrip.entries()[0].url, "https://tab.example/a\tb");
    CHECK_STR(roundtrip.entries()[0].title, "title\twith\ttabs\nand newline");
    CHECK(roundtrip.entries()[0].added_at == 500);
}

void test_discard_policy() {
    CHECK(mustela::memory_action_from("warn") == mustela::MemoryAction::Warn);
    CHECK(mustela::memory_action_from("discard") == mustela::MemoryAction::Discard);
    CHECK(mustela::memory_action_from("aggressive") == mustela::MemoryAction::Warn);
    CHECK_STR(mustela::memory_action_name(mustela::MemoryAction::Warn), "warn");
    CHECK_STR(mustela::memory_action_name(mustela::MemoryAction::Discard), "discard");

    using mustela::DiscardPolicy;
    using mustela::DiscardableTab;

    DiscardableTab tab_a;
    tab_a.id = 0;
    tab_a.active = true;
    tab_a.idle_ms = 999999;
    DiscardableTab tab_b;
    tab_b.id = 1;
    tab_b.idle_ms = 10000;
    DiscardableTab tab_c;
    tab_c.id = 2;
    tab_c.idle_ms = 900000;
    DiscardableTab tab_d;
    tab_d.id = 3;
    tab_d.idle_ms = 500000;
    tab_d.loading = true;
    DiscardableTab tab_e;
    tab_e.id = 4;
    tab_e.idle_ms = 800000;
    tab_e.discarded = true;
    std::vector<DiscardableTab> tabs = {tab_a, tab_b, tab_c, tab_d, tab_e};

    DiscardPolicy policy;
    policy.action = mustela::MemoryAction::Warn;
    policy.ceiling_mb = 1;
    policy.total_bytes = 1 << 30;
    CHECK(mustela::pick_discard_candidates(tabs, policy).empty());

    policy.action = mustela::MemoryAction::Discard;
    std::vector<std::size_t> picked = mustela::pick_discard_candidates(tabs, policy);
    CHECK(picked.size() == 1);
    CHECK(picked[0] == 2);

    policy.ceiling_mb = 4096;
    CHECK(!mustela::ceiling_exceeded(policy.ceiling_mb, policy.total_bytes));
    CHECK(mustela::pick_discard_candidates(tabs, policy).empty());

    policy.ceiling_mb = 256;
    CHECK(mustela::ceiling_exceeded(policy.ceiling_mb, policy.total_bytes));
    picked = mustela::pick_discard_candidates(tabs, policy);
    CHECK(picked.size() == 1);
    CHECK(picked[0] == 2);

    DiscardableTab tab_f;
    tab_f.id = 5;
    tab_f.idle_ms = 700000;
    tabs.push_back(tab_f);
    CHECK(mustela::alive_tab_count(tabs) == 5);

    policy.max_per_pass = 3;
    picked = mustela::pick_discard_candidates(tabs, policy);
    CHECK(picked.size() == 2);
    CHECK(picked[0] == 2);
    CHECK(picked[1] == 5);
    bool loading_excluded = true;
    for (std::size_t id : picked) {
        if (id == 3) loading_excluded = false;
    }
    CHECK(loading_excluded);

    policy.max_per_pass = 10;
    picked = mustela::pick_discard_candidates(tabs, policy);
    CHECK(picked.size() == 2);

    policy.min_alive = 6;
    CHECK(mustela::pick_discard_candidates(tabs, policy).empty());
    policy.min_alive = 1;

    policy.min_idle_ms = 950000.0;
    CHECK(mustela::pick_discard_candidates(tabs, policy).empty());
    policy.min_idle_ms = 600000.0;

    std::vector<DiscardableTab> only_active = {tab_a};
    CHECK(mustela::pick_discard_candidates(only_active, policy).empty());

    policy.max_per_pass = 1;
    std::vector<DiscardableTab> two_alive = {tab_a, tab_c};
    picked = mustela::pick_discard_candidates(two_alive, policy);
    CHECK(picked.size() == 1);
    CHECK(picked[0] == 2);

    policy.min_alive = 0;
    picked = mustela::pick_discard_candidates(two_alive, policy);
    CHECK(picked.size() == 1);
    CHECK(picked[0] == 2);

    std::vector<DiscardableTab> single = {tab_c};
    picked = mustela::pick_discard_candidates(single, policy);
    CHECK(picked.size() == 1);
    CHECK(picked[0] == 2);
}

void test_error_page() {
    std::string page = mustela::branded_error_page("Could not connect", "host not found",
        "https://ok.example/path?a=1&b=<2>");
    CHECK(page.rfind("<!doctype html>", 0) == 0);
    CHECK(page.find("Mustela") != std::string::npos);
    CHECK(page.find("Could not connect") != std::string::npos);
    CHECK(page.find("host not found") != std::string::npos);
    CHECK(page.find("Try again") != std::string::npos);
    CHECK(page.find("a=1&amp;b=&lt;2&gt;") != std::string::npos);
    CHECK(page.find("<script") == std::string::npos);
    CHECK(page.find("no-referrer") != std::string::npos);

    std::string blank = mustela::branded_error_page("", "", "");
    CHECK(blank.rfind("<!doctype html>", 0) == 0);
    CHECK(blank.find("mst-url") == std::string::npos);
    CHECK(blank.find("Try again") == std::string::npos);

    std::string hostile = mustela::branded_error_page("<img src=x onerror=alert(1)>", "\x01ctrl\x02",
        "https://h.example/\"onclick=\"x");
    CHECK(hostile.find("<img src=x") == std::string::npos);
    CHECK(hostile.find("&lt;img") != std::string::npos);
    CHECK(hostile.find("&quot;onclick=") != std::string::npos);
    CHECK(hostile.find("\x01") == std::string::npos);

    std::string long_input(10000, 'x');
    std::string capped = mustela::branded_error_page(long_input, long_input, "https://" + long_input + ".example/");
    CHECK(capped.size() < 8192);
    CHECK(capped.find("Try again") != std::string::npos);
}

void test_site_rules_extras() {
    std::string path = write_temp("sites_extra.toml",
        "[www.example.org]\n"
        "zoom = 1.5\n"
        "user_agent = \"Mozilla/5.0 MustelaTest\"\n"
        "css = \"body { margin: 0 }\"\n"
        "[wide.example]\n"
        "zoom = 9.0\n"
        "[zoomed.example]\n"
        "zoom = 0.05\n"
        "[bad.example]\n"
        "zoom = not-a-number\n");
    mustela::SiteRules rules(path);
    rules.load();
    const mustela::SitePolicy* exact = rules.find("www.example.org");
    CHECK(exact);
    CHECK(exact->zoom == 1.5);
    CHECK_STR(exact->user_agent, "Mozilla/5.0 MustelaTest");
    CHECK_STR(exact->css, "body { margin: 0 }");
    CHECK(rules.policy_for("sub.www.example.org").zoom == 1.5);
    CHECK_STR(rules.policy_for("sub.www.example.org").css, "body { margin: 0 }");
    CHECK(rules.policy_for("wide.example").zoom == 3.0);
    CHECK(rules.policy_for("zoomed.example").zoom == 0.3);
    CHECK(rules.policy_for("bad.example").zoom == 0.0);
    CHECK(rules.entries().size() == 3);

    mustela::SitePolicy only_zoom;
    only_zoom.zoom = 1.25;
    rules.set("zoomonly.example", only_zoom);
    CHECK(rules.entries().size() == 4);
    CHECK(rules.policy_for("zoomonly.example").zoom == 1.25);
    CHECK(rules.policy_for("zoomonly.example").javascript == mustela::SiteRule::Default);

    mustela::SitePolicy blank;
    rules.set("zoomonly.example", blank);
    CHECK(rules.entries().size() == 3);

    mustela::SitePolicy clamped;
    clamped.zoom = 42.0;
    rules.set("clamp.example", clamped);
    CHECK(rules.policy_for("clamp.example").zoom == 3.0);
    clamped.zoom = 0.1;
    rules.set("clamp.example", clamped);
    CHECK(rules.policy_for("clamp.example").zoom == 0.3);
    CHECK(rules.entries().size() == 4);

    rules.save();
    mustela::SiteRules reloaded(path);
    reloaded.load();
    CHECK(reloaded.policy_for("www.example.org").zoom == 1.5);
    CHECK_STR(reloaded.policy_for("www.example.org").user_agent, "Mozilla/5.0 MustelaTest");
    CHECK_STR(reloaded.policy_for("www.example.org").css, "body { margin: 0 }");
    CHECK(reloaded.policy_for("zoomonly.example").zoom == 0.0);
    CHECK(reloaded.policy_for("clamp.example").zoom == 0.3);
}

void test_profile_handle() {
    std::string root = temp_path("profile-root");
    std::error_code wipe;
    std::filesystem::remove_all(root, wipe);
    mustela::PrivacyPolicy policy;
    policy.gpc = false;
    mustela::Profile profile(root, policy);
    CHECK_STR(profile.root(), root);
    CHECK_STR(profile.paths().history, root + "/history.dat");
    CHECK_STR(profile.paths().bookmarks, root + "/bookmarks.tsv");
    CHECK_STR(profile.paths().session, root + "/session.json");
    CHECK_STR(profile.paths().perf_log, root + "/perf.jsonl");
    CHECK_STR(profile.paths().filters, root + "/filters");
    CHECK_STR(profile.paths().favicons, root + "/favicons");
    CHECK(!profile.has(mustela::ProfileStore::History));
    CHECK(!profile.has(mustela::ProfileStore::Filters));
    profile.ensure_dirs();
    CHECK(profile.has(mustela::ProfileStore::Filters));
    CHECK(profile.has(mustela::ProfileStore::Favicons));
    {
        std::ofstream out(profile.paths().history);
        out << "stub";
    }
    {
        std::ofstream out(profile.paths().session);
        out << "{}";
    }
    CHECK(profile.has(mustela::ProfileStore::History));
    CHECK(profile.has(mustela::ProfileStore::Session));
    CHECK(!profile.has(mustela::ProfileStore::Bookmarks));
    profile.clear(mustela::ProfileStore::History);
    profile.clear(mustela::ProfileStore::Session);
    CHECK(!profile.has(mustela::ProfileStore::History));
    CHECK(!profile.has(mustela::ProfileStore::Session));
    CHECK(profile.has(mustela::ProfileStore::Favicons));
    profile.clear(mustela::ProfileStore::Favicons);
    CHECK(!profile.has(mustela::ProfileStore::Favicons));
    CHECK(!profile.policy().gpc);
    profile.set_policy(mustela::PrivacyPolicy{});
    CHECK(profile.policy().gpc);

    std::string def = mustela::default_profile_root();
    CHECK(!def.empty());
    CHECK(def.find("mustela") != std::string::npos);
    CHECK(def.find("profiles/default") != std::string::npos);

    mustela::Profile empty;
    CHECK(empty.root().empty());
    CHECK(!empty.has(mustela::ProfileStore::History));
}

void test_search_engines() {
    mustela::Settings plain;
    CHECK(plain.search_engines.empty());
    CHECK(plain.search_default.empty());
    CHECK_STR(plain.shell_profile, "default");

    mustela::SearchEngine fallback = mustela::default_search_engine(plain);
    CHECK(fallback.keyword.empty());
    CHECK_STR(fallback.url_template, plain.search_template);
    CHECK_STR(mustela::resolve_entry("kittens", plain),
        "https://duckduckgo.com/?q=kittens");
    CHECK_STR(mustela::resolve_entry("  kittens  ", plain),
        "https://duckduckgo.com/?q=kittens");
    CHECK_STR(mustela::resolve_entry("two words", plain),
        "https://duckduckgo.com/?q=two%20words");
    CHECK_STR(mustela::resolve_entry("https://example.org/x", plain),
        "https://example.org/x");
    CHECK_STR(mustela::resolve_entry("example.org", plain), "https://example.org");
    CHECK_STR(mustela::resolve_entry("localhost:8080/x", plain),
        "https://localhost:8080/x");
    CHECK_STR(mustela::resolve_entry("", plain), "");

    mustela::Settings multi;
    mustela::SearchEngine ddg;
    ddg.keyword = "ddg";
    ddg.name = "DuckDuckGo";
    ddg.url_template = "https://duckduckgo.com/?q=%s";
    mustela::SearchEngine gh;
    gh.keyword = "GH";
    gh.name = "GitHub";
    gh.url_template = "https://github.com/search?q=%s&type=repositories";
    multi.search_engines.push_back(ddg);
    multi.search_engines.push_back(gh);
    multi.search_default = "gh";

    mustela::SearchEngine def = mustela::default_search_engine(multi);
    CHECK_STR(def.keyword, "GH");
    CHECK_STR(mustela::resolve_entry("cats", multi),
        "https://github.com/search?q=cats&type=repositories");
    CHECK_STR(mustela::resolve_entry("!ddg cats & dogs", multi),
        "https://duckduckgo.com/?q=cats%20%26%20dogs");
    CHECK_STR(mustela::resolve_entry("!gh webkit", multi),
        "https://github.com/search?q=webkit&type=repositories");
    CHECK_STR(mustela::resolve_entry("!DDG mixed case", multi),
        "https://duckduckgo.com/?q=mixed%20case");
    CHECK_STR(mustela::resolve_entry("!nosuch tags", multi),
        "https://github.com/search?q=tags&type=repositories");
    CHECK_STR(mustela::resolve_entry("!", multi),
        "https://github.com/search?q=&type=repositories");
    CHECK(mustela::find_search_engine(multi, "ddg").keyword == "ddg");
    CHECK(mustela::find_search_engine(multi, "missing").url_template.empty());

    multi.search_default = "gone";
    def = mustela::default_search_engine(multi);
    CHECK_STR(def.keyword, "ddg");

    std::string path = temp_path("settings_engines");
    std::error_code wipe;
    std::filesystem::remove(path, wipe);
    mustela::Settings original;
    original.search_default = "gh";
    original.search_engines.push_back(ddg);
    original.search_engines.push_back(gh);
    original.shell_profile = "work";
    mustela::save_settings(path, original);
    mustela::SettingsReport report;
    mustela::Settings loaded = mustela::load_settings(path, report);
    CHECK(report.unknown_keys.empty());
    CHECK(report.invalid_values.empty());
    CHECK(loaded.search_engines.size() == 2);
    CHECK_STR(loaded.search_engines[0].keyword, "GH");
    CHECK_STR(loaded.search_engines[0].url_template,
        "https://github.com/search?q=%s&type=repositories");
    CHECK_STR(loaded.search_engines[1].keyword, "ddg");
    CHECK_STR(loaded.search_engines[1].name, "DuckDuckGo");
    CHECK_STR(loaded.search_engines[1].url_template, "https://duckduckgo.com/?q=%s");
    CHECK_STR(loaded.search_default, "gh");
    CHECK_STR(loaded.shell_profile, "work");
    CHECK_STR(mustela::resolve_entry("!gh round trip", loaded),
        "https://github.com/search?q=round%20trip&type=repositories");

    std::string bad = temp_path("settings_engines_bad");
    {
        std::ofstream out(bad);
        out << "[search.engines]\n"
            << "solo.template = \"https://solo.example/?q=%s\"\n"
            << "solo.name = \"Solo\"\n"
            << "broken = \"x\"\n";
    }
    loaded = mustela::load_settings(bad, report);
    CHECK(loaded.search_engines.size() == 1);
    CHECK_STR(loaded.search_engines[0].keyword, "solo");
    CHECK_STR(loaded.search_engines[0].url_template, "https://solo.example/?q=%s");
}

void test_profile_registry() {
    std::string path = temp_path("profiles_registry");
    std::error_code wipe;
    std::filesystem::remove(path, wipe);

    mustela::ProfileRegistry missing = mustela::load_profile_registry(path);
    CHECK(missing.entries.size() == 1);
    CHECK_STR(missing.entries[0].id, "default");
    CHECK_STR(missing.entries[0].root, "profiles/default");
    CHECK_STR(missing.default_id, "default");
    CHECK(missing.find("default") != nullptr);
    CHECK(missing.find("other") == nullptr);

    mustela::ProfileRegistry registry;
    mustela::ProfileEntry main;
    main.id = "main";
    main.name = "Main";
    main.root = "profiles/main";
    registry.entries.push_back(main);
    mustela::ProfileEntry work;
    work.id = "work";
    work.name = "Work";
    work.root = "/abs/work";
    registry.entries.push_back(work);
    registry.default_id = "work";
    mustela::save_profile_registry(path, registry);

    mustela::ProfileRegistry loaded = mustela::load_profile_registry(path);
    CHECK(loaded.entries.size() == 2);
    CHECK_STR(loaded.default_id, "work");
    const mustela::ProfileEntry* entry = loaded.find("main");
    CHECK(entry != nullptr);
    CHECK_STR(entry->name, "Main");
    CHECK_STR(entry->root, "profiles/main");

    loaded.upsert({"main", "Renamed", "profiles/renamed"});
    CHECK(loaded.entries.size() == 2);
    CHECK_STR(loaded.find("main")->name, "Renamed");
    loaded.upsert({"extra", "Extra", "profiles/extra"});
    CHECK(loaded.entries.size() == 3);
    CHECK(loaded.find("extra") != nullptr);
    loaded.upsert({"", "Empty", ""});
    CHECK(loaded.entries.size() == 3);

    CHECK(loaded.remove("extra"));
    CHECK(!loaded.remove("extra"));
    CHECK(loaded.entries.size() == 2);
    CHECK(loaded.remove("work"));
    CHECK_STR(loaded.default_id, "main");

    mustela::ProfileEntry relative{"r", "R", "profiles/r"};
    CHECK_STR(mustela::resolve_profile_root(relative, "/base"), "/base/profiles/r");
    mustela::ProfileEntry absolute{"a", "A", "/abs/root"};
    CHECK_STR(mustela::resolve_profile_root(absolute, "/base"), "/abs/root");
    mustela::ProfileEntry empty_root{"e", "E", ""};
    CHECK_STR(mustela::resolve_profile_root(empty_root, "/base"), "/base/e");
}

void test_autofill_store() {
    std::string path = temp_path("autofill_store");
    std::error_code wipe;
    std::filesystem::remove(path, wipe);

    mustela::AutofillStore store(path);
    store.load();
    CHECK(store.size() == 0);

    store.upsert("example.org", "email", "a@example.org", 100);
    store.upsert("example.org", "email", "b@example.org", 101);
    store.upsert("example.org", "email", "a@example.org", 102);
    store.upsert("www.example.org", "email", "c@example.org", 103);
    store.upsert("other.net", "email", "d@example.net", 104);
    store.upsert("example.org", "name", "Alice", 105);
    CHECK(store.size() == 5);

    std::vector<mustela::AutofillEntry> hits = store.find("example.org", "email");
    CHECK(hits.size() == 2);
    CHECK_STR(hits[0].value, "a@example.org");
    CHECK(hits[0].uses == 2);
    CHECK(hits[0].last_used == 102);
    CHECK_STR(hits[1].value, "b@example.org");

    std::vector<mustela::AutofillEntry> subdomain = store.find("www.example.org", "email");
    CHECK(subdomain.size() == 3);
    CHECK_STR(subdomain[0].value, "a@example.org");
    CHECK_STR(subdomain[1].value, "c@example.org");
    CHECK_STR(subdomain[2].value, "b@example.org");
    std::vector<mustela::AutofillEntry> sibling = store.find("mail.example.org", "email");
    CHECK(sibling.size() == 2);
    CHECK(store.find("example.org", "missing").empty());
    CHECK(store.find("example.net", "email").empty());
    CHECK(store.find("", "email").empty());

    std::vector<mustela::AutofillEntry> limited = store.find("example.org", "email", 1);
    CHECK(limited.size() == 1);
    CHECK_STR(limited[0].value, "a@example.org");

    CHECK(store.remove("example.org", "email", "b@example.org"));
    CHECK(!store.remove("example.org", "email", "b@example.org"));
    CHECK(store.size() == 4);

    store.save();
    mustela::AutofillStore second(path);
    second.load();
    CHECK(second.size() == 4);
    std::vector<mustela::AutofillEntry> reloaded = second.find("example.org", "email");
    CHECK(reloaded.size() == 1);
    CHECK_STR(reloaded[0].value, "a@example.org");
    CHECK(reloaded[0].uses == 2);

    mustela::AutofillStore weird(temp_path("autofill_weird"));
    weird.upsert("example.org", "field", "with\ttab and\nnewline", 200);
    weird.save();
    mustela::AutofillStore back(temp_path("autofill_weird"));
    back.load();
    CHECK(back.size() == 1);
    CHECK_STR(back.find("example.org", "field")[0].value, "with\ttab and\nnewline");

    back.clear();
    CHECK(back.size() == 0);

    mustela::Profile profile(temp_path("autofill-profile-root"));
    profile.ensure_dirs();
    CHECK(!profile.has(mustela::ProfileStore::Autofill));
    CHECK_STR(profile.paths().autofill, profile.root() + "/autofill.tsv");
    {
        std::ofstream out(profile.paths().autofill);
        out << "example.org\temail\tx@example.org\t1\t5\n";
    }
    CHECK(profile.has(mustela::ProfileStore::Autofill));
    mustela::AutofillStore from_profile(profile.paths().autofill);
    from_profile.load();
    CHECK(from_profile.size() == 1);
    profile.clear(mustela::ProfileStore::Autofill);
    CHECK(!profile.has(mustela::ProfileStore::Autofill));
}

void test_autofill_scripts() {
    mustela::AutofillStore store(temp_path("autofill_scripts"));
    store.upsert("shop.example", "email", "a@shop.example", 10);
    store.upsert("shop.example", "name", "Alice \"A\"\\smith\nline", 11);
    store.upsert("other.example", "phone", "123", 12);

    std::vector<mustela::AutofillEntry> entries = store.by_domain("shop.example");
    CHECK(entries.size() == 2);
    CHECK_STR(entries[0].field, "name");
    CHECK_STR(entries[1].field, "email");
    CHECK(store.by_domain("www.shop.example").size() == 2);
    CHECK(store.by_domain("shop2.example").empty());
    CHECK(store.by_domain("shop.example", 1).size() == 1);

    std::string script = mustela::autofill_fill_script(entries);
    CHECK(script.find("a@shop.example") != std::string::npos);
    CHECK(script.find("Alice \\\"A\\\"\\\\smith\\nline") != std::string::npos);
    CHECK(script.find("querySelectorAll('input,textarea')") != std::string::npos);
    CHECK(script.find("'password'") != std::string::npos);

    std::string escaped = mustela::autofill_fill_script(
        {{mustela::AutofillEntry{"d", "f", std::string("ctrl") + char(0x01) + "end", 1, 0}}});
    CHECK(escaped.find("\\u0001") != std::string::npos);

    std::string capture = mustela::autofill_capture_script();
    CHECK(std::string(capture).find("mustelaForm") != std::string::npos);
    CHECK(std::string(capture).find("'password'") != std::string::npos);
    CHECK(std::string(capture).find("postMessage") != std::string::npos);
}

void test_chrome_time() {
    CHECK(mustela::chrome_time_to_unix(13350000000000000) == 1705526400);
    CHECK(mustela::chrome_time_to_unix(0) == 0);
    CHECK(mustela::chrome_time_to_unix(1000) == 0);
    CHECK(mustela::firefox_time_to_unix(1710000000000000) == 1710000000);
    CHECK(mustela::firefox_time_to_unix(1710000000) == 1710000000);
    CHECK(mustela::firefox_time_to_unix(0) == 0);
}

std::string lz4_all_literals(const std::string& data) {
    std::string out;
    if (data.empty()) return out;
    out += static_cast<char>(0xF0);
    size_t rest = data.size() - 15;
    while (rest >= 255) {
        out += static_cast<char>(255);
        rest -= 255;
    }
    out += static_cast<char>(rest);
    out += data;
    return out;
}

std::string build_mozlz4(const std::string& json) {
    std::string out = "mozLz40\0";
    out.resize(8);
    std::uint32_t size = static_cast<std::uint32_t>(json.size());
    for (int i = 0; i < 4; ++i) {
        out += static_cast<char>((size >> (8 * i)) & 0xFF);
    }
    out += lz4_all_literals(json);
    return out;
}

void test_import_chromium() {
    std::string dir = temp_path("chrome_profile");
    std::filesystem::create_directories(dir);
    const char* json =
        "{\"checksum\":\"x\",\"version\":1,\"roots\":{"
        "\"bookmark_bar\":{\"type\":\"folder\",\"name\":\"bar\",\"children\":["
        "{\"type\":\"url\",\"name\":\"Example\",\"url\":\"https://example.com/\"," 
        "\"date_added\":\"13350000000000000\"},"
        "{\"type\":\"folder\",\"name\":\"F\",\"children\":["
        "{\"type\":\"url\",\"name\":\"Second\",\"url\":\"https://second.example/\"}]},"
        "{\"type\":\"url\",\"name\":\"Dup\",\"url\":\"https://example.com/\"}]},"
        "\"other\":{\"type\":\"folder\",\"name\":\"other\",\"children\":[]},"
        "\"synced\":{\"type\":\"folder\",\"name\":\"synced\",\"children\":[]}}}";
    write_temp("chrome_profile/Bookmarks", json);

    mustela::BookmarkStore store(temp_path("import_chromium.tsv"));
    mustela::ImportReport report = mustela::import_bookmarks_chromium(dir, store);
    CHECK(report.error.empty());
    CHECK(report.bookmarks_added == 2);
    CHECK(report.bookmarks_skipped == 1);
    CHECK(store.contains("https://example.com/"));
    CHECK(store.contains("https://second.example/"));
    const std::vector<mustela::BookmarkEntry>& entries = store.entries();
    CHECK(entries.size() == 2);
    bool found_time = false;
    for (const mustela::BookmarkEntry& entry : entries) {
        if (entry.url == "https://example.com/") {
            found_time = entry.title == "Example" && entry.added_at == 1705526400;
        }
    }
    CHECK(found_time);

    mustela::ImportReport again = mustela::import_bookmarks_chromium(dir, store);
    CHECK(again.error.empty());
    CHECK(again.bookmarks_added == 0);
    CHECK(again.bookmarks_skipped == 3);

    mustela::ImportReport missing = mustela::import_bookmarks_chromium(
        temp_path("no_such_profile"), store);
    CHECK(!missing.error.empty());
}

void test_import_firefox() {
    std::string dir = temp_path("firefox_profile");
    std::filesystem::create_directories(dir + "/bookmarkbackups");
    const char* json =
        "{\"version\":1,\"roots\":{"
        "\"menu\":[{\"typeCode\":1,\"uri\":\"https://moz.example/\",\"title\":\"Moz\"," 
        "\"dateAdded\":1710000000000000}],"
        "\"toolbar\":[{\"typeCode\":2,\"title\":\"TB\",\"children\":["
        "{\"typeCode\":1,\"uri\":\"https://tb.example/\",\"title\":\"TB page\"," 
        "\"dateAdded\":1710000000000001}]}],"
        "\"unfiled\":[{\"typeCode\":1,\"uri\":\"https://moz.example/\",\"title\":\"Moz\"," 
        "\"dateAdded\":1710000000000000}],"
        "\"mobile\":[]}}";
    std::string blob = build_mozlz4(json);
    write_temp("firefox_profile/bookmarkbackups/bookmarks-2025.jsonlz4", blob);

    mustela::BookmarkStore store(temp_path("import_firefox.tsv"));
    mustela::ImportReport report = mustela::import_bookmarks_firefox(dir, store);
    CHECK(report.error.empty());
    CHECK(report.bookmarks_added == 2);
    CHECK(report.bookmarks_skipped == 1);
    CHECK(store.contains("https://moz.example/"));
    CHECK(store.contains("https://tb.example/"));

    std::filesystem::create_directories(temp_path("firefox_empty_profile"));
    mustela::BookmarkStore lonely(temp_path("import_firefox_empty.tsv"));
    mustela::ImportReport none = mustela::import_bookmarks_firefox(
        temp_path("firefox_empty_profile"), lonely);
    CHECK(!none.error.empty());

    std::filesystem::create_directories(temp_path("firefox_bad_profile/bookmarkbackups"));
    write_temp("firefox_bad_profile/bookmarkbackups/bad.jsonlz4", "garbage-data!");
    mustela::BookmarkStore bad(temp_path("import_firefox_bad.tsv"));
    mustela::ImportReport corrupt = mustela::import_bookmarks_firefox(
        temp_path("firefox_bad_profile"), bad);
    CHECK(!corrupt.error.empty());
}

void test_import_html() {
    const char* html =
        "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
        "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
        "<DL><p>\n"
        "  <DT><A HREF=\"https://html.example/\" ADD_DATE=\"1700000000\">HTML &amp; page</A>\n"
        "  <DT><A HREF='https://html2.example/' ADD_DATE='1700000001'>Second</A>\n"
        "  <DT><A HREF=\"https://html.example/\">Dup</A>\n"
        "</DL><p>\n";
    write_temp("bookmarks.html", html);
    mustela::BookmarkStore store(temp_path("import_html.tsv"));
    mustela::ImportReport report = mustela::import_bookmarks_html(temp_path("bookmarks.html"), store);
    CHECK(report.error.empty());
    CHECK(report.bookmarks_added == 2);
    CHECK(report.bookmarks_skipped == 1);
    bool entity_ok = false;
    bool quoted_ok = false;
    for (const mustela::BookmarkEntry& entry : store.entries()) {
        if (entry.url == "https://html.example/") {
            entity_ok = entry.title == "HTML & page" && entry.added_at == 1700000000;
        }
        if (entry.url == "https://html2.example/") quoted_ok = entry.title == "Second";
    }
    CHECK(entity_ok);
    CHECK(quoted_ok);

    mustela::BookmarkStore empty(temp_path("import_html_missing.tsv"));
    mustela::ImportReport error = mustela::import_bookmarks_html(
        temp_path("no_bookmarks.html"), empty);
    CHECK(!error.error.empty());
}

void test_import_detect() {
    std::string home = temp_path("detect_home");
    std::filesystem::create_directories(home + "/.config/google-chrome/Default");
    write_temp("detect_home/.config/google-chrome/Default/Bookmarks", "{\"roots\":{}}");
    std::filesystem::create_directories(home + "/.config/google-chrome/Skip Me");
    std::filesystem::create_directories(home + "/.mozilla/firefox/abc.default");
    write_temp("detect_home/.mozilla/firefox/abc.default/bookmarks.html", "<DL></DL>");
    std::vector<mustela::DetectedProfile> found = mustela::detect_browsers_in(home);
    CHECK(found.size() == 2);
    bool chrome_found = false;
    bool firefox_found = false;
    for (const mustela::DetectedProfile& item : found) {
        if (item.browser == "Chrome" && item.profile == "Default") chrome_found = true;
        if (item.browser == "Firefox" && item.profile == "abc.default") firefox_found = true;
    }
    CHECK(chrome_found);
    CHECK(firefox_found);
}

void test_public_surface() {
    CHECK(mustela::core_api_version() >= 1);
    CHECK(MUSTELA_CORE_API_VERSION >= 1);
    mustela::SitePolicy surface_fields;
    surface_fields.zoom = 1.0;
    surface_fields.user_agent = "ua";
    surface_fields.css = "c";
    mustela::ProfilePaths surface_paths;
    CHECK(surface_paths.history.empty());
}

int main() {
    test_settings_roundtrip();
    test_settings_partial_file();
    test_settings_load_report();
    test_privacy_mapping();
    test_host_from_uri();
    test_psl();
    test_site_rules_matching();
    test_site_rules_psl_guard();
    test_resolve_site();
    test_history_store();
    test_fingerprint_policy();
    test_browsing_data_kinds();
    test_normalize_url();
    test_perf_module();
    test_ui_text();
    test_permissions();
    test_site_rules_permissions();
    test_blocklist();
    test_doh_codec();
    test_session_store();
    test_proxy_settings_keys();
    test_proxy_urls();
    test_doh_resolver();
    test_proxy_server_loopback();
    test_bookmarks_store();
    test_discard_policy();
    test_error_page();
    test_site_rules_extras();
    test_profile_handle();
    test_search_engines();
    test_profile_registry();
    test_autofill_store();
    test_autofill_scripts();
    test_chrome_time();
    test_import_chromium();
    test_import_firefox();
    test_import_html();
    test_import_detect();
    test_public_surface();

    if (failures > 0) {
        std::fprintf(stderr, "%d/%d checks failed\n", failures, checks);
        return 1;
    }
    std::printf("%d core checks passed\n", checks);
    return 0;
}
