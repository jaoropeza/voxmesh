#include "voxmesh/core/version.hpp"

#include <charconv>

namespace voxmesh::core {

namespace {

// Parses one numeric component, enforcing semver's no-leading-zeros rule.
std::optional<std::uint32_t> parseNumeric(std::string_view text)
{
    if (text.empty() || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    std::uint32_t value{};
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

bool isValidPrerelease(std::string_view text)
{
    if (text.empty() || text.front() == '.' || text.back() == '.') {
        return false;
    }
    bool previousWasDot = false;
    for (const char c : text) {
        if (c == '.') {
            if (previousWasDot) {
                return false;
            }
            previousWasDot = true;
            continue;
        }
        previousWasDot = false;
        const bool valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') || c == '-';
        if (!valid) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<SemanticVersion> SemanticVersion::parse(std::string_view text)
{
    std::string_view numericPart = text;
    std::string_view prereleasePart{};
    if (const auto dash = text.find('-'); dash != std::string_view::npos) {
        numericPart = text.substr(0, dash);
        prereleasePart = text.substr(dash + 1);
        if (!isValidPrerelease(prereleasePart)) {
            return std::nullopt;
        }
    }

    const auto firstDot = numericPart.find('.');
    const auto lastDot = numericPart.rfind('.');
    if (firstDot == std::string_view::npos || firstDot == lastDot) {
        return std::nullopt;
    }

    const auto major = parseNumeric(numericPart.substr(0, firstDot));
    const auto minor = parseNumeric(numericPart.substr(firstDot + 1, lastDot - firstDot - 1));
    const auto patch = parseNumeric(numericPart.substr(lastDot + 1));
    if (!major || !minor || !patch) {
        return std::nullopt;
    }

    return SemanticVersion{*major, *minor, *patch, std::string{prereleasePart}};
}

std::string SemanticVersion::toString() const
{
    std::string result = std::to_string(major) + '.' + std::to_string(minor) + '.' +
                         std::to_string(patch);
    if (!prerelease.empty()) {
        result += '-';
        result += prerelease;
    }
    return result;
}

std::strong_ordering SemanticVersion::operator<=>(const SemanticVersion& other) const
{
    if (const auto cmp = major <=> other.major; cmp != std::strong_ordering::equal) {
        return cmp;
    }
    if (const auto cmp = minor <=> other.minor; cmp != std::strong_ordering::equal) {
        return cmp;
    }
    if (const auto cmp = patch <=> other.patch; cmp != std::strong_ordering::equal) {
        return cmp;
    }
    // Release outranks prerelease of the same numeric version.
    if (prerelease.empty() != other.prerelease.empty()) {
        return prerelease.empty() ? std::strong_ordering::greater
                                  : std::strong_ordering::less;
    }
    return prerelease.compare(other.prerelease) <=> 0;
}

SemanticVersion projectVersion()
{
    constexpr std::string_view compiledVersion{VOXMESH_VERSION};
    const auto parsed = SemanticVersion::parse(compiledVersion);
    // The CMake project version is validated by tests; an unparsable value is a
    // build configuration defect, not a runtime condition.
    return parsed.value_or(SemanticVersion{0, 0, 0, "invalid"});
}

}  // namespace voxmesh::core
