#include "error_page.hpp"

#include "ui_text.hpp"

namespace mustela {

namespace {

std::string html_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string trim_spaces(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return std::string();
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

}

std::string branded_error_page(const std::string& heading, const std::string& detail, const std::string& url) {
    std::string title = html_escape(sanitize_dialog_text(heading, 128));
    std::string body = html_escape(sanitize_dialog_text(detail, 512));
    std::string link = html_escape(sanitize_dialog_text(trim_spaces(url), 512));

    std::string page;
    page += "<!doctype html><html><head><meta charset=\"utf-8\">";
    page += "<meta name=\"referrer\" content=\"no-referrer\">";
    page += "<title>" + title + " — Mustela</title>";
    page += "<style>";
    page += "body{background:#f5f4f2;margin:0;font-family:system-ui,sans-serif;color:#24211d}";
    page += ".card{max-width:34rem;margin:12vh auto;padding:2.2rem;background:#fff;";
    page += "border:1px solid #e2e0dc;border-radius:12px}";
    page += ".brand{text-transform:uppercase;letter-spacing:.18em;font-size:.72rem;";
    page += "color:#b8541f;font-weight:700}";
    page += "h1{font-size:1.4rem;margin:.4rem 0 .6rem;font-weight:600}";
    page += ".detail{color:#5c564e;margin:.2rem 0}";
    page += ".url{font-family:monospace;font-size:.85rem;color:#5c564e;word-break:break-all;";
    page += "background:#f5f4f2;border-radius:6px;padding:.5rem .7rem}";
    page += "button{margin-top:.8rem;padding:.5rem 1.3rem;border:0;border-radius:8px;";
    page += "background:#b8541f;color:#fff;font-size:.95rem;cursor:pointer}";
    page += "button:hover{background:#9c4519}";
    page += "</style></head><body><main><div class=\"card\">";
    page += "<div class=\"brand\">Mustela</div>";
    page += "<h1>" + title + "</h1>";
    if (!body.empty()) page += "<p class=\"detail\">" + body + "</p>";
    if (!link.empty()) {
        page += "<p class=\"url\" id=\"mst-url\">" + link + "</p>";
        page += "<button onclick=\"location.replace(document.getElementById('mst-url').textContent)\">";
        page += "Try again</button>";
    }
    page += "</div></main></body></html>";
    return page;
}

}
