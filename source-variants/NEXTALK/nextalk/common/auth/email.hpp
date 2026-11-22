#pragma once
#include <algorithm>
#include <string>
#include <string_view>

namespace cc::auth {
// Registration supports ordinary ASCII mailboxes; reject header/control injection.
inline std::string normalizedEmail(std::string_view input) {
    if (input.size() > 254) return {};
    std::string s(input);
    const auto at = s.find('@');
    if (at == std::string::npos || at == 0 || at > 64 || s.find('@', at + 1) != std::string::npos) return {};
    const auto local = s.substr(0, at), domain = s.substr(at + 1);
    auto alnum = [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
    if (local.front() == '.' || local.back() == '.' || local.find("..") != std::string::npos ||
        !std::all_of(local.begin(), local.end(), [&](unsigned char c) { return alnum(c) || std::string_view(".!#$%&'*+-/=?^_`{|}~").find(c) != std::string_view::npos; })) return {};
    if (domain.empty() || domain.find('.') == std::string::npos) return {};
    std::size_t start = 0;
    while (start < domain.size()) {
        auto end = domain.find('.', start); if (end == std::string::npos) end = domain.size();
        if (end == start || end - start > 63 || !alnum(domain[start]) || !alnum(domain[end - 1])) return {};
        for (auto i = start; i < end; ++i) if (!alnum(domain[i]) && domain[i] != '-') return {};
        start = end + 1;
    }
    if (domain.back() == '.') return {};
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
}
