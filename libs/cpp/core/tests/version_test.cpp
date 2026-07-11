#include "voxmesh/core/version.hpp"

#include <gtest/gtest.h>

namespace voxmesh::core {
namespace {

TEST(VersionTest, ParsesPlainVersion)
{
    const auto v = SemanticVersion::parse("1.2.3");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->major, 1u);
    EXPECT_EQ(v->minor, 2u);
    EXPECT_EQ(v->patch, 3u);
    EXPECT_TRUE(v->prerelease.empty());
}

TEST(VersionTest, ParsesPrereleaseVersion)
{
    const auto v = SemanticVersion::parse("0.1.0-rc.1");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ((SemanticVersion{0, 1, 0, "rc.1"}), *v);
}

TEST(VersionTest, RejectsMalformedInput)
{
    EXPECT_FALSE(SemanticVersion::parse("").has_value());
    EXPECT_FALSE(SemanticVersion::parse("1.2").has_value());
    EXPECT_FALSE(SemanticVersion::parse("1.2.3.4").has_value());
    EXPECT_FALSE(SemanticVersion::parse("1.a.3").has_value());
    EXPECT_FALSE(SemanticVersion::parse("01.2.3").has_value());
    EXPECT_FALSE(SemanticVersion::parse("1.2.3-").has_value());
    EXPECT_FALSE(SemanticVersion::parse("1.2.3-rc..1").has_value());
    EXPECT_FALSE(SemanticVersion::parse("1.2.3-rc_1").has_value());
    EXPECT_FALSE(SemanticVersion::parse("-1.2.3").has_value());
}

TEST(VersionTest, OrdersNumericComponents)
{
    const auto low = SemanticVersion::parse("1.2.3").value();
    const auto high = SemanticVersion::parse("1.10.0").value();
    EXPECT_LT(low, high);
    EXPECT_LT(SemanticVersion::parse("0.9.9").value(),
              SemanticVersion::parse("1.0.0").value());
}

TEST(VersionTest, ReleaseOutranksPrerelease)
{
    const auto release = SemanticVersion::parse("1.0.0").value();
    const auto prerelease = SemanticVersion::parse("1.0.0-rc.2").value();
    EXPECT_LT(prerelease, release);
    EXPECT_GT(SemanticVersion::parse("1.0.1-alpha").value(), release);
}

TEST(VersionTest, RoundTripsThroughToString)
{
    for (const auto* text : {"1.2.3", "0.1.0-rc.1", "10.20.30-alpha-2.beta"}) {
        const auto v = SemanticVersion::parse(text);
        ASSERT_TRUE(v.has_value()) << text;
        EXPECT_EQ(v->toString(), text);
    }
}

TEST(VersionTest, ProjectVersionIsValid)
{
    const auto v = projectVersion();
    EXPECT_NE(v.prerelease, "invalid");
    EXPECT_EQ(v, SemanticVersion::parse(v.toString()).value());
}

}  // namespace
}  // namespace voxmesh::core
