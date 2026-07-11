#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace voxmesh::core {

// Semantic version (major.minor.patch with optional -prerelease). Single source of
// version identity for the application, installers, and upgrade checks.
//
// Precedence follows semver: numeric fields first; a release (empty prerelease)
// outranks any prerelease of the same numeric version; prereleases of the same
// numeric version compare lexicographically by their full prerelease string
// (a simplification of semver's dot-identifier rules, sufficient until release
// channels need more).
struct SemanticVersion {
    std::uint32_t major{0};
    std::uint32_t minor{0};
    std::uint32_t patch{0};
    std::string prerelease{};

    // Accepts "1.2.3" and "1.2.3-rc.1". Rejects empty/non-numeric components,
    // leading zeros (semver rule), and empty or malformed prerelease identifiers.
    [[nodiscard]] static std::optional<SemanticVersion> parse(std::string_view text);

    [[nodiscard]] std::string toString() const;

    [[nodiscard]] std::strong_ordering operator<=>(const SemanticVersion& other) const;
    [[nodiscard]] bool operator==(const SemanticVersion& other) const = default;
};

// Version compiled into this build (from the CMake project version).
[[nodiscard]] SemanticVersion projectVersion();

}  // namespace voxmesh::core
