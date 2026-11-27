#pragma once
#include <algorithm>
#include <string>
#include <vector>

enum class Source : unsigned { Automatic, Stable14, Modern15 };

struct Property {
    std::wstring canonical;
    std::wstring title;
    unsigned order;
    bool operator==(const Property& rhs) const {
        return canonical == rhs.canonical && title == rhs.title && order == rhs.order;
    }
};

inline constexpr unsigned firstPropertyOrder = 1000;
// Session-local IDs for Samandarin headers. Only canonical names go into the profile.
inline std::vector<Property> knownProperties;
inline Source preferredSource = Source::Automatic;
inline std::vector<Property> visibleProperties;

inline Property RegisterProperty(const std::wstring& canonical, const std::wstring& title) {
    for (auto& p : knownProperties) {
        if (p.canonical == canonical) {
            p.title = title;
            return p;
        }
    }
    unsigned order = firstPropertyOrder + 2 * static_cast<unsigned>(knownProperties.size());
    return knownProperties.emplace_back(Property{canonical, title, order});
}
inline const Property* PropertyForOrder(unsigned order) {
    if (order < firstPropertyOrder) return nullptr;
    const auto index = (order - firstPropertyOrder) / 2;
    return index < knownProperties.size() ? &knownProperties[index] : nullptr;
}
