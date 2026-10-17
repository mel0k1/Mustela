#include "autofill.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mustela {

namespace {

std::string escape_field(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string unescape_field(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            switch (text[i]) {
                case 't': out.push_back('\t'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case '\\': out.push_back('\\'); break;
                default: out.push_back(text[i]); break;
            }
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::string trim_copy(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool domain_matches(const std::string& entry_domain, const std::string& domain) {
    if (entry_domain.empty() || domain.empty()) return false;
    if (entry_domain == domain) return true;
    if (domain.size() <= entry_domain.size()) return false;
    return domain.compare(domain.size() - entry_domain.size() - 1, entry_domain.size() + 1,
               "." + entry_domain) == 0;
}

bool newer_first(const AutofillEntry& a, const AutofillEntry& b) {
    if (a.uses != b.uses) return a.uses > b.uses;
    if (a.last_used != b.last_used) return a.last_used > b.last_used;
    return a.value < b.value;
}

std::string js_string(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    out.push_back('"');
    static const char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0xF]);
                    out.push_back(hex[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

std::string entries_json(const std::vector<AutofillEntry>& entries) {
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const AutofillEntry& entry : entries) {
        if (!first) out << ",";
        first = false;
        out << "{\"name\":" << js_string(entry.field)
            << ",\"value\":" << js_string(entry.value) << "}";
    }
    out << "]";
    return out.str();
}

}

AutofillStore::AutofillStore(std::string path) : path_(std::move(path)) {}

void AutofillStore::load() {
    entries_.clear();
    std::ifstream in(path_);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        std::string text = trim_copy(line);
        if (text.empty() || text[0] == '#') continue;

        size_t first = text.find('\t');
        if (first == std::string::npos) continue;
        size_t second = text.find('\t', first + 1);
        if (second == std::string::npos) continue;
        size_t third = text.find('\t', second + 1);
        if (third == std::string::npos) continue;
        size_t fourth = text.find('\t', third + 1);
        if (fourth == std::string::npos) continue;

        AutofillEntry entry;
        entry.domain = unescape_field(text.substr(0, first));
        entry.field = unescape_field(text.substr(first + 1, second - first - 1));
        entry.value = unescape_field(text.substr(second + 1, third - second - 1));
        entry.uses = std::atoi(text.substr(third + 1, fourth - third - 1).c_str());
        entry.last_used = std::atoll(text.substr(fourth + 1).c_str());
        if (entry.domain.empty() || entry.field.empty() || entry.uses < 0) continue;
        entries_.push_back(std::move(entry));
    }
}

void AutofillStore::save() const {
    std::ofstream out(path_);
    if (!out) return;
    for (const auto& entry : entries_) {
        out << escape_field(entry.domain) << '\t'
            << escape_field(entry.field) << '\t'
            << escape_field(entry.value) << '\t'
            << entry.uses << '\t'
            << entry.last_used << '\n';
    }
}

std::vector<AutofillEntry> AutofillStore::find(const std::string& domain, const std::string& field,
    size_t limit) const {
    std::vector<AutofillEntry> matches;
    for (const auto& entry : entries_) {
        if (entry.field != field) continue;
        if (!domain_matches(entry.domain, domain)) continue;
        matches.push_back(entry);
    }
    std::sort(matches.begin(), matches.end(), newer_first);
    if (matches.size() > limit) matches.resize(limit);
    return matches;
}

std::vector<AutofillEntry> AutofillStore::by_domain(const std::string& domain, size_t limit) const {
    std::vector<AutofillEntry> matches;
    for (const auto& entry : entries_) {
        if (!domain_matches(entry.domain, domain)) continue;
        matches.push_back(entry);
    }
    std::sort(matches.begin(), matches.end(), newer_first);
    if (matches.size() > limit) matches.resize(limit);
    return matches;
}

void AutofillStore::upsert(const std::string& domain, const std::string& field,
    const std::string& value, std::int64_t now) {
    if (domain.empty() || field.empty()) return;
    for (auto& entry : entries_) {
        if (entry.domain == domain && entry.field == field && entry.value == value) {
            ++entry.uses;
            entry.last_used = now;
            return;
        }
    }
    AutofillEntry entry;
    entry.domain = domain;
    entry.field = field;
    entry.value = value;
    entry.uses = 1;
    entry.last_used = now;
    entries_.push_back(std::move(entry));
}

bool AutofillStore::remove(const std::string& domain, const std::string& field,
    const std::string& value) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->domain == domain && it->field == field && it->value == value) {
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

void AutofillStore::clear() {
    entries_.clear();
}

std::string autofill_fill_script(const std::vector<AutofillEntry>& entries) {
    std::ostringstream out;
    out << "(function(){var d=" << entries_json(entries) << ";var n=0;"
        "var els=document.querySelectorAll('input,textarea');"
        "for(var i=0;i<els.length;i++){"
        "var el=els[i];if(!el||el.disabled||el.readOnly)continue;"
        "var t=(el.type||'').toLowerCase();"
        "if(t==='password'||t==='hidden'||t==='checkbox'||t==='radio'||t==='file'||"
        "t==='submit'||t==='button'||t==='reset'||t==='image')continue;"
        "var key=el.name||el.id;if(!key)continue;"
        "if(el.value)continue;"
        "for(var j=0;j<d.length;j++){"
        "if(d[j].name!==key)continue;"
        "el.value=d[j].value;"
        "el.dispatchEvent(new Event('input',{bubbles:true}));"
        "el.dispatchEvent(new Event('change',{bubbles:true}));"
        "n++;break;"
        "}"
        "}"
        "return n;})();";
    return out.str();
}

const char* autofill_capture_script() {
    return "(function(){"
        "if(window.__mustelaFormCapture)return;window.__mustelaFormCapture=true;"
        "document.addEventListener('submit',function(ev){"
        "try{"
        "var f=ev.target;if(!f||!f.elements)return;"
        "var fs=[];"
        "for(var i=0;i<f.elements.length;i++){"
        "var el=f.elements[i];if(!el||!el.name||el.disabled)continue;"
        "var t=(el.type||'').toLowerCase();"
        "if(t==='password'||t==='hidden'||t==='file'||t==='checkbox'||t==='radio'||"
        "t==='submit'||t==='button'||t==='reset'||t==='image')continue;"
        "var v=el.value;if(!v||v.length>200)continue;"
        "fs.push({name:el.name,value:v});"
        "}"
        "if(fs.length&&window.webkit&&window.webkit.messageHandlers&&"
        "window.webkit.messageHandlers.mustelaForm){"
        "window.webkit.messageHandlers.mustelaForm.postMessage("
        "{host:location.hostname,fields:fs});"
        "}"
        "}catch(e){}"
        "},true);"
        "})();";
}

}
