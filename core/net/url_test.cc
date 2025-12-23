// URL parsing tests

#include "core/net/include/URL.h"

#include <gtest/gtest.h>

namespace cells::net {

TEST(URLTest, ParseSimpleHttps) {
    const auto url = URL::parse("https://example.com/path");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url->getScheme(), "https");
    EXPECT_EQ(url->getHost(), "example.com");
    EXPECT_EQ(url->getPort(), 0);
    EXPECT_EQ(url->getEffectivePort(), 443);
    EXPECT_EQ(url->getPath(), "/path");
    EXPECT_TRUE(url->isSecure());
}

TEST(URLTest, ParseSimpleHttp) {
    const auto url = URL::parse("http://example.com/");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url->getScheme(), "http");
    EXPECT_EQ(url->getHost(), "example.com");
    EXPECT_EQ(url->getEffectivePort(), 80);
    EXPECT_EQ(url->getPath(), "/");
    EXPECT_FALSE(url->isSecure());
}

TEST(URLTest, ParseWithPort) {
    const auto url = URL::parse("https://example.com:8443/api/v1");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url->getHost(), "example.com");
    EXPECT_EQ(url->getPort(), 8443);
    EXPECT_EQ(url->getEffectivePort(), 8443);
    EXPECT_EQ(url->getPath(), "/api/v1");
}

TEST(URLTest, ParseWithQuery) {
    const auto url = URL::parse("https://example.com/search?q=hello&page=1");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url->getPath(), "/search");
    EXPECT_EQ(url->getQuery(), "q=hello&page=1");
    EXPECT_TRUE(url->hasQueryParam("q"));
    EXPECT_EQ(url->getQueryParam("q"), "hello");
    EXPECT_EQ(url->getQueryParam("page"), "1");
}

TEST(URLTest, ParseWithFragment) {
    const auto url = URL::parse("https://example.com/page#section");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url->getPath(), "/page");
    EXPECT_EQ(url->getFragment(), "section");
}

TEST(URLTest, ParseFullUrl) {
    const auto url = URL::parse("https://user:pass@example.com:8080/path?query=1#frag");
    ASSERT_TRUE(url.has_value());
    // Note: user:pass@ is treated as part of the host for now
    // Full userinfo parsing would require additional logic
    EXPECT_EQ(url->getPort(), 8080);
    EXPECT_EQ(url->getPath(), "/path");
    EXPECT_EQ(url->getQuery(), "query=1");
    EXPECT_EQ(url->getFragment(), "frag");
}

TEST(URLTest, ParseWebSocket) {
    const auto url = URL::parse("wss://ws.example.com/socket");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url->getScheme(), "wss");
    EXPECT_EQ(url->getHost(), "ws.example.com");
    EXPECT_EQ(url->getEffectivePort(), 443);
    EXPECT_TRUE(url->isSecure());
}

TEST(URLTest, ParseNoPath) {
    const auto url = URL::parse("https://example.com");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url->getHost(), "example.com");
    EXPECT_EQ(url->getPath(), "/");
}

TEST(URLTest, ParseInvalid) {
    EXPECT_FALSE(URL::parse("").has_value());
    EXPECT_FALSE(URL::parse("not-a-url").has_value());
    EXPECT_FALSE(URL::parse("://missing-scheme.com").has_value());
    EXPECT_FALSE(URL::parse("https://").has_value());
}

TEST(URLTest, ToString) {
    URL url("https", "example.com", 443, "/path");
    EXPECT_EQ(url.toString(), "https://example.com/path");

    url.setPort(8443);
    EXPECT_EQ(url.toString(), "https://example.com:8443/path");

    url.setQuery("foo=bar");
    EXPECT_EQ(url.toString(), "https://example.com:8443/path?foo=bar");

    url.setFragment("section");
    EXPECT_EQ(url.toString(), "https://example.com:8443/path?foo=bar#section");
}

TEST(URLTest, QueryParams) {
    URL url("https", "example.com", 0, "/search");
    url.setQueryParam("q", "hello world");
    url.setQueryParam("page", "1");

    EXPECT_TRUE(url.hasQueryParam("q"));
    EXPECT_TRUE(url.hasQueryParam("page"));
    EXPECT_FALSE(url.hasQueryParam("missing"));

    // Query string should be URL-encoded
    const std::string query = url.getQuery();
    EXPECT_NE(query.find("q=hello%20world"), std::string::npos);
}

TEST(URLTest, UrlEncode) {
    EXPECT_EQ(urlEncode("hello world"), "hello%20world");
    EXPECT_EQ(urlEncode("a+b=c&d"), "a%2Bb%3Dc%26d");
    EXPECT_EQ(urlEncode("abc123"), "abc123");
    EXPECT_EQ(urlEncode(""), "");
}

TEST(URLTest, UrlDecode) {
    EXPECT_EQ(urlDecode("hello%20world"), "hello world");
    EXPECT_EQ(urlDecode("a%2Bb%3Dc%26d"), "a+b=c&d");
    EXPECT_EQ(urlDecode("abc123"), "abc123");
    EXPECT_EQ(urlDecode("hello+world"), "hello world");
    EXPECT_EQ(urlDecode(""), "");
}

TEST(URLTest, GetPathAndQuery) {
    URL url("https", "example.com", 0, "/api/data");
    EXPECT_EQ(url.getPathAndQuery(), "/api/data");

    url.setQuery("format=json");
    EXPECT_EQ(url.getPathAndQuery(), "/api/data?format=json");
}

}  // namespace cells::net
