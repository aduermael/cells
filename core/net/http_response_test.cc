// HTTP response tests

#include <gtest/gtest.h>

#include "core/net/include/HttpResponse.h"

namespace cells::net {

TEST(HttpResponseTest, StatusCode) {
    HttpResponse response;
    EXPECT_EQ(response.getStatusCode(), 0);

    response.setStatusCode(200);
    EXPECT_EQ(response.getStatusCode(), 200);
    EXPECT_TRUE(response.isSuccess());

    response.setStatusCode(404);
    EXPECT_EQ(response.getStatusCode(), 404);
    EXPECT_FALSE(response.isSuccess());

    response.setStatusCode(201);
    EXPECT_TRUE(response.isSuccess());

    response.setStatusCode(299);
    EXPECT_TRUE(response.isSuccess());

    response.setStatusCode(300);
    EXPECT_FALSE(response.isSuccess());
}

TEST(HttpResponseTest, Headers) {
    HttpResponse response;

    response.setHeader("Content-Type", "application/json");
    response.setHeader("X-Custom-Header", "value");

    EXPECT_TRUE(response.hasHeader("content-type"));
    EXPECT_TRUE(response.hasHeader("Content-Type"));
    EXPECT_TRUE(response.hasHeader("CONTENT-TYPE"));

    EXPECT_EQ(response.getHeader("Content-Type"), "application/json");
    EXPECT_EQ(response.getHeader("content-type"), "application/json");

    EXPECT_FALSE(response.hasHeader("Missing"));
    EXPECT_EQ(response.getHeader("Missing"), "");
}

TEST(HttpResponseTest, Body) {
    HttpResponse response;

    const std::vector<uint8_t> data1 = {'H', 'e', 'l', 'l', 'o'};
    response.appendBytes(data1);
    EXPECT_EQ(response.getBytes().size(), 5);
    EXPECT_EQ(response.getBodyAsString(), "Hello");

    const std::vector<uint8_t> data2 = {' ', 'W', 'o', 'r', 'l', 'd'};
    response.appendBytes(data2);
    EXPECT_EQ(response.getBytes().size(), 11);
    EXPECT_EQ(response.getBodyAsString(), "Hello World");
}

TEST(HttpResponseTest, AppendBytesPointer) {
    HttpResponse response;

    const uint8_t data[] = {'t', 'e', 's', 't'};
    response.appendBytes(data, sizeof(data));
    EXPECT_EQ(response.getBodyAsString(), "test");
}

TEST(HttpResponseTest, ContentLength) {
    HttpResponse response;

    // Without Content-Length header, returns body size
    const std::vector<uint8_t> data = {'1', '2', '3'};
    response.appendBytes(data);
    EXPECT_EQ(response.getContentLength(), 3);

    // With Content-Length header, returns header value
    response.setHeader("Content-Length", "100");
    EXPECT_EQ(response.getContentLength(), 100);

    // Invalid Content-Length falls back to body size
    response.setHeader("Content-Length", "invalid");
    EXPECT_EQ(response.getContentLength(), 3);
}

TEST(HttpResponseTest, Clear) {
    HttpResponse response;
    response.setStatusCode(200);
    response.setHeader("X-Test", "value");
    response.appendBytes({'a', 'b', 'c'});

    response.clear();

    EXPECT_EQ(response.getStatusCode(), 0);
    EXPECT_FALSE(response.hasHeader("X-Test"));
    EXPECT_TRUE(response.getBytes().empty());
}

}  // namespace cells::net
