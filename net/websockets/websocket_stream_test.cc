// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_stream.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/scoped_vector.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/statistics_recorder.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/timer/mock_timer.h"
#include "base/timer/timer.h"
#include "net/base/net_errors.h"
#include "net/base/test_data_directory.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/socket_test_util.h"
#include "net/test/cert_test_util.h"
#include "net/url_request/url_request_test_util.h"
#include "net/websockets/websocket_basic_handshake_stream.h"
#include "net/websockets/websocket_frame.h"
#include "net/websockets/websocket_handshake_request_info.h"
#include "net/websockets/websocket_handshake_response_info.h"
#include "net/websockets/websocket_handshake_stream_create_helper.h"
#include "net/websockets/websocket_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {
namespace {

typedef std::pair<std::string, std::string> HeaderKeyValuePair;

std::vector<HeaderKeyValuePair> ToVector(const HttpRequestHeaders& headers) {
  HttpRequestHeaders::Iterator it(headers);
  std::vector<HeaderKeyValuePair> result;
  while (it.GetNext())
    result.push_back(HeaderKeyValuePair(it.name(), it.value()));
  return result;
}

std::vector<HeaderKeyValuePair> ToVector(const HttpResponseHeaders& headers) {
  void* iter = NULL;
  std::string name, value;
  std::vector<HeaderKeyValuePair> result;
  while (headers.EnumerateHeaderLines(&iter, &name, &value))
    result.push_back(HeaderKeyValuePair(name, value));
  return result;
}

// Simple builder for a DeterministicSocketData object to save repetitive code.
// It always sets the connect data to MockConnect(SYNCHRONOUS, OK), so it cannot
// be used in tests where the connect fails. In practice, those tests never have
// any read/write data and so can't benefit from it anyway.  The arrays are not
// copied. It is up to the caller to ensure they stay in scope until the test
// ends.
template <size_t reads_count, size_t writes_count>
scoped_ptr<DeterministicSocketData> BuildSocketData(
    MockRead (&reads)[reads_count],
    MockWrite (&writes)[writes_count]) {
  scoped_ptr<DeterministicSocketData> socket_data(
      new DeterministicSocketData(reads, reads_count, writes, writes_count));
  socket_data->set_connect_data(MockConnect(SYNCHRONOUS, OK));
  socket_data->SetStop(reads_count + writes_count);
  return socket_data.Pass();
}

// Builder for a DeterministicSocketData that expects nothing. This does not
// set the connect data, so the calling code must do that explicitly.
scoped_ptr<DeterministicSocketData> BuildNullSocketData() {
  return make_scoped_ptr(new DeterministicSocketData(NULL, 0, NULL, 0));
}

class MockWeakTimer : public base::MockTimer,
                      public base::SupportsWeakPtr<MockWeakTimer> {
 public:
  MockWeakTimer(bool retain_user_task, bool is_repeating)
      : MockTimer(retain_user_task, is_repeating) {}
};

// A sub-class of WebSocketHandshakeStreamCreateHelper which always sets a
// deterministic key to use in the WebSocket handshake.
class DeterministicKeyWebSocketHandshakeStreamCreateHelper
    : public WebSocketHandshakeStreamCreateHelper {
 public:
  DeterministicKeyWebSocketHandshakeStreamCreateHelper(
      WebSocketStream::ConnectDelegate* connect_delegate,
      const std::vector<std::string>& requested_subprotocols)
      : WebSocketHandshakeStreamCreateHelper(connect_delegate,
                                             requested_subprotocols) {}

  void OnStreamCreated(WebSocketBasicHandshakeStream* stream) override {
    stream->SetWebSocketKeyForTesting("dGhlIHNhbXBsZSBub25jZQ==");
  }
};

class WebSocketStreamCreateTest : public ::testing::Test {
 public:
  WebSocketStreamCreateTest() : has_failed_(false), ssl_fatal_(false) {}
  ~WebSocketStreamCreateTest() override {
    // Permit any endpoint locks to be released.
    stream_request_.reset();
    stream_.reset();
    RunUntilIdle();
  }

  void CreateAndConnectCustomResponse(
      const std::string& socket_url,
      const std::string& socket_path,
      const std::vector<std::string>& sub_protocols,
      const std::string& origin,
      const std::string& extra_request_headers,
      const std::string& response_body,
      scoped_ptr<base::Timer> timer = scoped_ptr<base::Timer>()) {
    url_request_context_host_.SetExpectations(
        WebSocketStandardRequest(socket_path, origin, extra_request_headers),
        response_body);
    CreateAndConnectStream(socket_url, sub_protocols, origin, timer.Pass());
  }

  // |extra_request_headers| and |extra_response_headers| must end in "\r\n" or
  // errors like "Unable to perform synchronous IO while stopped" will occur.
  void CreateAndConnectStandard(const std::string& socket_url,
                                const std::string& socket_path,
                                const std::vector<std::string>& sub_protocols,
                                const std::string& origin,
                                const std::string& extra_request_headers,
                                const std::string& extra_response_headers,
                                scoped_ptr<base::Timer> timer =
                                scoped_ptr<base::Timer>()) {
    CreateAndConnectCustomResponse(
        socket_url,
        socket_path,
        sub_protocols,
        origin,
        extra_request_headers,
        WebSocketStandardResponse(extra_response_headers),
        timer.Pass());
  }

  void CreateAndConnectRawExpectations(
      const std::string& socket_url,
      const std::vector<std::string>& sub_protocols,
      const std::string& origin,
      scoped_ptr<DeterministicSocketData> socket_data,
      scoped_ptr<base::Timer> timer = scoped_ptr<base::Timer>()) {
    AddRawExpectations(socket_data.Pass());
    CreateAndConnectStream(socket_url, sub_protocols, origin, timer.Pass());
  }

  // Add additional raw expectations for sockets created before the final one.
  void AddRawExpectations(scoped_ptr<DeterministicSocketData> socket_data) {
    url_request_context_host_.AddRawExpectations(socket_data.Pass());
  }

  // A wrapper for CreateAndConnectStreamForTesting that knows about our default
  // parameters.
  void CreateAndConnectStream(const std::string& socket_url,
                              const std::vector<std::string>& sub_protocols,
                              const std::string& origin,
                              scoped_ptr<base::Timer> timer) {
    for (size_t i = 0; i < ssl_data_.size(); ++i) {
      scoped_ptr<SSLSocketDataProvider> ssl_data(ssl_data_[i]);
      ssl_data_[i] = NULL;
      url_request_context_host_.AddSSLSocketDataProvider(ssl_data.Pass());
    }
    ssl_data_.clear();
    scoped_ptr<WebSocketStream::ConnectDelegate> connect_delegate(
        new TestConnectDelegate(this));
    WebSocketStream::ConnectDelegate* delegate = connect_delegate.get();
    scoped_ptr<WebSocketHandshakeStreamCreateHelper> create_helper(
        new DeterministicKeyWebSocketHandshakeStreamCreateHelper(
            delegate, sub_protocols));
    stream_request_ = ::net::CreateAndConnectStreamForTesting(
        GURL(socket_url),
        create_helper.Pass(),
        url::Origin(origin),
        url_request_context_host_.GetURLRequestContext(),
        BoundNetLog(),
        connect_delegate.Pass(),
        timer ? timer.Pass() : scoped_ptr<base::Timer>(
            new base::Timer(false, false)));
  }

  static void RunUntilIdle() { base::RunLoop().RunUntilIdle(); }

  // A simple function to make the tests more readable. Creates an empty vector.
  static std::vector<std::string> NoSubProtocols() {
    return std::vector<std::string>();
  }

  const std::string& failure_message() const { return failure_message_; }
  bool has_failed() const { return has_failed_; }

  class TestConnectDelegate : public WebSocketStream::ConnectDelegate {
   public:
    explicit TestConnectDelegate(WebSocketStreamCreateTest* owner)
        : owner_(owner) {}

    void OnSuccess(scoped_ptr<WebSocketStream> stream) override {
      stream.swap(owner_->stream_);
    }

    void OnFailure(const std::string& message) override {
      owner_->has_failed_ = true;
      owner_->failure_message_ = message;
    }

    void OnStartOpeningHandshake(
        scoped_ptr<WebSocketHandshakeRequestInfo> request) override {
      // Can be called multiple times (in the case of HTTP auth). Last call
      // wins.
      owner_->request_info_ = request.Pass();
    }
    void OnFinishOpeningHandshake(
        scoped_ptr<WebSocketHandshakeResponseInfo> response) override {
      if (owner_->response_info_)
        ADD_FAILURE();
      owner_->response_info_ = response.Pass();
    }
    void OnSSLCertificateError(
        scoped_ptr<WebSocketEventInterface::SSLErrorCallbacks>
            ssl_error_callbacks,
        const SSLInfo& ssl_info,
        bool fatal) override {
      owner_->ssl_error_callbacks_ = ssl_error_callbacks.Pass();
      owner_->ssl_info_ = ssl_info;
      owner_->ssl_fatal_ = fatal;
    }

   private:
    WebSocketStreamCreateTest* owner_;
  };

  WebSocketTestURLRequestContextHost url_request_context_host_;
  scoped_ptr<WebSocketStreamRequest> stream_request_;
  // Only set if the connection succeeded.
  scoped_ptr<WebSocketStream> stream_;
  // Only set if the connection failed.
  std::string failure_message_;
  bool has_failed_;
  scoped_ptr<WebSocketHandshakeRequestInfo> request_info_;
  scoped_ptr<WebSocketHandshakeResponseInfo> response_info_;
  scoped_ptr<WebSocketEventInterface::SSLErrorCallbacks> ssl_error_callbacks_;
  SSLInfo ssl_info_;
  bool ssl_fatal_;
  ScopedVector<SSLSocketDataProvider> ssl_data_;
  ScopedWebSocketEndpointZeroUnlockDelay zero_unlock_delay_;
};

// There are enough tests of the Sec-WebSocket-Extensions header that they
// deserve their own test fixture.
class WebSocketStreamCreateExtensionTest : public WebSocketStreamCreateTest {
 public:
  // Performs a standard connect, with the value of the Sec-WebSocket-Extensions
  // header in the response set to |extensions_header_value|. Runs the event
  // loop to allow the connect to complete.
  void CreateAndConnectWithExtensions(
      const std::string& extensions_header_value) {
    CreateAndConnectStandard(
        "ws://localhost/testing_path",
        "/testing_path",
        NoSubProtocols(),
        "http://localhost",
        "",
        "Sec-WebSocket-Extensions: " + extensions_header_value + "\r\n");
    RunUntilIdle();
  }
};

// Common code to construct expectations for authentication tests that receive
// the auth challenge on one connection and then create a second connection to
// send the authenticated request on.
class CommonAuthTestHelper {
 public:
  CommonAuthTestHelper() : reads1_(), writes1_(), reads2_(), writes2_() {}

  scoped_ptr<DeterministicSocketData> BuildSocketData1(
      const std::string& response) {
    request1_ = WebSocketStandardRequest("/", "http://localhost", "");
    writes1_[0] = MockWrite(SYNCHRONOUS, 0, request1_.c_str());
    response1_ = response;
    reads1_[0] = MockRead(SYNCHRONOUS, 1, response1_.c_str());
    reads1_[1] = MockRead(SYNCHRONOUS, OK, 2);  // Close connection

    return BuildSocketData(reads1_, writes1_);
  }

  scoped_ptr<DeterministicSocketData> BuildSocketData2(
      const std::string& request,
      const std::string& response) {
    request2_ = request;
    response2_ = response;
    writes2_[0] = MockWrite(SYNCHRONOUS, 0, request2_.c_str());
    reads2_[0] = MockRead(SYNCHRONOUS, 1, response2_.c_str());
    return BuildSocketData(reads2_, writes2_);
  }

 private:
  // These need to be object-scoped since they have to remain valid until all
  // socket operations in the test are complete.
  std::string request1_;
  std::string request2_;
  std::string response1_;
  std::string response2_;
  MockRead reads1_[2];
  MockWrite writes1_[1];
  MockRead reads2_[1];
  MockWrite writes2_[1];

  DISALLOW_COPY_AND_ASSIGN(CommonAuthTestHelper);
};

// Data and methods for BasicAuth tests.
class WebSocketStreamCreateBasicAuthTest : public WebSocketStreamCreateTest {
 protected:
  void CreateAndConnectAuthHandshake(const std::string& url,
                                     const std::string& base64_user_pass,
                                     const std::string& response2) {
    AddRawExpectations(helper_.BuildSocketData1(kUnauthorizedResponse));

    static const char request2format[] =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: Upgrade\r\n"
        "Pragma: no-cache\r\n"
        "Cache-Control: no-cache\r\n"
        "Authorization: Basic %s\r\n"
        "Upgrade: websocket\r\n"
        "Origin: http://localhost\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent:\r\n"
        "Accept-Encoding: gzip, deflate\r\n"
        "Accept-Language: en-us,fr\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Extensions: permessage-deflate; "
        "client_max_window_bits\r\n"
        "\r\n";
    const std::string request =
        base::StringPrintf(request2format, base64_user_pass.c_str());
    CreateAndConnectRawExpectations(
        url,
        NoSubProtocols(),
        "http://localhost",
        helper_.BuildSocketData2(request, response2));
  }

  static const char kUnauthorizedResponse[];

  CommonAuthTestHelper helper_;
};

class WebSocketStreamCreateDigestAuthTest : public WebSocketStreamCreateTest {
 protected:
  static const char kUnauthorizedResponse[];
  static const char kAuthorizedRequest[];

  CommonAuthTestHelper helper_;
};

const char WebSocketStreamCreateBasicAuthTest::kUnauthorizedResponse[] =
    "HTTP/1.1 401 Unauthorized\r\n"
    "Content-Length: 0\r\n"
    "WWW-Authenticate: Basic realm=\"camelot\"\r\n"
    "\r\n";

// These negotiation values are borrowed from
// http_auth_handler_digest_unittest.cc. Feel free to come up with new ones if
// you are bored. Only the weakest (no qop) variants of Digest authentication
// can be tested by this method, because the others involve random input.
const char WebSocketStreamCreateDigestAuthTest::kUnauthorizedResponse[] =
    "HTTP/1.1 401 Unauthorized\r\n"
    "Content-Length: 0\r\n"
    "WWW-Authenticate: Digest realm=\"Oblivion\", nonce=\"nonce-value\"\r\n"
    "\r\n";

const char WebSocketStreamCreateDigestAuthTest::kAuthorizedRequest[] =
    "GET / HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "Connection: Upgrade\r\n"
    "Pragma: no-cache\r\n"
    "Cache-Control: no-cache\r\n"
    "Authorization: Digest username=\"FooBar\", realm=\"Oblivion\", "
    "nonce=\"nonce-value\", uri=\"/\", "
    "response=\"f72ff54ebde2f928860f806ec04acd1b\"\r\n"
    "Upgrade: websocket\r\n"
    "Origin: http://localhost\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "User-Agent:\r\n"
    "Accept-Encoding: gzip, deflate\r\n"
    "Accept-Language: en-us,fr\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; "
    "client_max_window_bits\r\n"
    "\r\n";

class WebSocketStreamCreateUMATest : public ::testing::Test {
 public:
  // This enum should match with the enum in Delegate in websocket_stream.cc.
  enum HandshakeResult {
    INCOMPLETE,
    CONNECTED,
    FAILED,
    NUM_HANDSHAKE_RESULT_TYPES,
  };

  class StreamCreation : public WebSocketStreamCreateTest {
    void TestBody() override {}
  };

  scoped_ptr<base::HistogramSamples> GetSamples(const std::string& name) {
    base::HistogramBase* histogram =
        base::StatisticsRecorder::FindHistogram(name);
    return histogram ? histogram->SnapshotSamples()
                     : scoped_ptr<base::HistogramSamples>();
  }
};

// Confirm that the basic case works as expected.
TEST_F(WebSocketStreamCreateTest, SimpleSuccess) {
  CreateAndConnectStandard(
      "ws://localhost/", "/", NoSubProtocols(), "http://localhost", "", "");
  EXPECT_FALSE(request_info_);
  EXPECT_FALSE(response_info_);
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_TRUE(stream_);
  EXPECT_TRUE(request_info_);
  EXPECT_TRUE(response_info_);
}

TEST_F(WebSocketStreamCreateTest, HandshakeInfo) {
  static const char kResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "foo: bar, baz\r\n"
      "hoge: fuga\r\n"
      "hoge: piyo\r\n"
      "\r\n";

  CreateAndConnectCustomResponse(
      "ws://localhost/",
      "/",
      NoSubProtocols(),
      "http://localhost",
      "",
      kResponse);
  EXPECT_FALSE(request_info_);
  EXPECT_FALSE(response_info_);
  RunUntilIdle();
  EXPECT_TRUE(stream_);
  ASSERT_TRUE(request_info_);
  ASSERT_TRUE(response_info_);
  std::vector<HeaderKeyValuePair> request_headers =
      ToVector(request_info_->headers);
  // We examine the contents of request_info_ and response_info_
  // mainly only in this test case.
  EXPECT_EQ(GURL("ws://localhost/"), request_info_->url);
  EXPECT_EQ(GURL("ws://localhost/"), response_info_->url);
  EXPECT_EQ(101, response_info_->status_code);
  EXPECT_EQ("Switching Protocols", response_info_->status_text);
  ASSERT_EQ(12u, request_headers.size());
  EXPECT_EQ(HeaderKeyValuePair("Host", "localhost"), request_headers[0]);
  EXPECT_EQ(HeaderKeyValuePair("Connection", "Upgrade"), request_headers[1]);
  EXPECT_EQ(HeaderKeyValuePair("Pragma", "no-cache"), request_headers[2]);
  EXPECT_EQ(HeaderKeyValuePair("Cache-Control", "no-cache"),
            request_headers[3]);
  EXPECT_EQ(HeaderKeyValuePair("Upgrade", "websocket"), request_headers[4]);
  EXPECT_EQ(HeaderKeyValuePair("Origin", "http://localhost"),
            request_headers[5]);
  EXPECT_EQ(HeaderKeyValuePair("Sec-WebSocket-Version", "13"),
            request_headers[6]);
  EXPECT_EQ(HeaderKeyValuePair("User-Agent", ""), request_headers[7]);
  EXPECT_EQ(HeaderKeyValuePair("Accept-Encoding", "gzip, deflate"),
            request_headers[8]);
  EXPECT_EQ(HeaderKeyValuePair("Accept-Language", "en-us,fr"),
            request_headers[9]);
  EXPECT_EQ("Sec-WebSocket-Key",  request_headers[10].first);
  EXPECT_EQ(HeaderKeyValuePair("Sec-WebSocket-Extensions",
                               "permessage-deflate; client_max_window_bits"),
            request_headers[11]);

  std::vector<HeaderKeyValuePair> response_headers =
      ToVector(*response_info_->headers.get());
  ASSERT_EQ(6u, response_headers.size());
  // Sort the headers for ease of verification.
  std::sort(response_headers.begin(), response_headers.end());

  EXPECT_EQ(HeaderKeyValuePair("Connection", "Upgrade"), response_headers[0]);
  EXPECT_EQ("Sec-WebSocket-Accept", response_headers[1].first);
  EXPECT_EQ(HeaderKeyValuePair("Upgrade", "websocket"), response_headers[2]);
  EXPECT_EQ(HeaderKeyValuePair("foo", "bar, baz"), response_headers[3]);
  EXPECT_EQ(HeaderKeyValuePair("hoge", "fuga"), response_headers[4]);
  EXPECT_EQ(HeaderKeyValuePair("hoge", "piyo"), response_headers[5]);
}

// Confirm that the stream isn't established until the message loop runs.
TEST_F(WebSocketStreamCreateTest, NeedsToRunLoop) {
  CreateAndConnectStandard(
      "ws://localhost/", "/", NoSubProtocols(), "http://localhost", "", "");
  EXPECT_FALSE(has_failed());
  EXPECT_FALSE(stream_);
}

// Check the path is used.
TEST_F(WebSocketStreamCreateTest, PathIsUsed) {
  CreateAndConnectStandard("ws://localhost/testing_path",
                           "/testing_path",
                           NoSubProtocols(),
                           "http://localhost",
                           "",
                           "");
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_TRUE(stream_);
}

// Check that the origin is used.
TEST_F(WebSocketStreamCreateTest, OriginIsUsed) {
  CreateAndConnectStandard("ws://localhost/testing_path",
                           "/testing_path",
                           NoSubProtocols(),
                           "http://google.com",
                           "",
                           "");
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_TRUE(stream_);
}

// Check that sub-protocols are sent and parsed.
TEST_F(WebSocketStreamCreateTest, SubProtocolIsUsed) {
  std::vector<std::string> sub_protocols;
  sub_protocols.push_back("chatv11.chromium.org");
  sub_protocols.push_back("chatv20.chromium.org");
  CreateAndConnectStandard("ws://localhost/testing_path",
                           "/testing_path",
                           sub_protocols,
                           "http://google.com",
                           "Sec-WebSocket-Protocol: chatv11.chromium.org, "
                           "chatv20.chromium.org\r\n",
                           "Sec-WebSocket-Protocol: chatv20.chromium.org\r\n");
  RunUntilIdle();
  EXPECT_TRUE(stream_);
  EXPECT_FALSE(has_failed());
  EXPECT_EQ("chatv20.chromium.org", stream_->GetSubProtocol());
}

// Unsolicited sub-protocols are rejected.
TEST_F(WebSocketStreamCreateTest, UnsolicitedSubProtocol) {
  CreateAndConnectStandard("ws://localhost/testing_path",
                           "/testing_path",
                           NoSubProtocols(),
                           "http://google.com",
                           "",
                           "Sec-WebSocket-Protocol: chatv20.chromium.org\r\n");
  RunUntilIdle();
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "Response must not include 'Sec-WebSocket-Protocol' header "
            "if not present in request: chatv20.chromium.org",
            failure_message());
}

// Missing sub-protocol response is rejected.
TEST_F(WebSocketStreamCreateTest, UnacceptedSubProtocol) {
  std::vector<std::string> sub_protocols;
  sub_protocols.push_back("chat.example.com");
  CreateAndConnectStandard("ws://localhost/testing_path",
                           "/testing_path",
                           sub_protocols,
                           "http://localhost",
                           "Sec-WebSocket-Protocol: chat.example.com\r\n",
                           "");
  RunUntilIdle();
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "Sent non-empty 'Sec-WebSocket-Protocol' header "
            "but no response was received",
            failure_message());
}

// Only one sub-protocol can be accepted.
TEST_F(WebSocketStreamCreateTest, MultipleSubProtocolsInResponse) {
  std::vector<std::string> sub_protocols;
  sub_protocols.push_back("chatv11.chromium.org");
  sub_protocols.push_back("chatv20.chromium.org");
  CreateAndConnectStandard("ws://localhost/testing_path",
                           "/testing_path",
                           sub_protocols,
                           "http://google.com",
                           "Sec-WebSocket-Protocol: chatv11.chromium.org, "
                           "chatv20.chromium.org\r\n",
                           "Sec-WebSocket-Protocol: chatv11.chromium.org, "
                           "chatv20.chromium.org\r\n");
  RunUntilIdle();
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "'Sec-WebSocket-Protocol' header must not appear "
            "more than once in a response",
            failure_message());
}

// Unmatched sub-protocol should be rejected.
TEST_F(WebSocketStreamCreateTest, UnmatchedSubProtocolInResponse) {
  std::vector<std::string> sub_protocols;
  sub_protocols.push_back("chatv11.chromium.org");
  sub_protocols.push_back("chatv20.chromium.org");
  CreateAndConnectStandard("ws://localhost/testing_path",
                           "/testing_path",
                           sub_protocols,
                           "http://google.com",
                           "Sec-WebSocket-Protocol: chatv11.chromium.org, "
                           "chatv20.chromium.org\r\n",
                           "Sec-WebSocket-Protocol: chatv21.chromium.org\r\n");
  RunUntilIdle();
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "'Sec-WebSocket-Protocol' header value 'chatv21.chromium.org' "
            "in response does not match any of sent values",
            failure_message());
}

// permessage-deflate extension basic success case.
TEST_F(WebSocketStreamCreateExtensionTest, PerMessageDeflateSuccess) {
  CreateAndConnectWithExtensions("permessage-deflate");
  EXPECT_TRUE(stream_);
  EXPECT_FALSE(has_failed());
}

// permessage-deflate extensions success with all parameters.
TEST_F(WebSocketStreamCreateExtensionTest, PerMessageDeflateParamsSuccess) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; client_no_context_takeover; "
      "server_max_window_bits=11; client_max_window_bits=13; "
      "server_no_context_takeover");
  EXPECT_TRUE(stream_);
  EXPECT_FALSE(has_failed());
}

// Verify that incoming messages are actually decompressed with
// permessage-deflate enabled.
TEST_F(WebSocketStreamCreateExtensionTest, PerMessageDeflateInflates) {
  CreateAndConnectCustomResponse(
      "ws://localhost/testing_path",
      "/testing_path",
      NoSubProtocols(),
      "http://localhost",
      "",
      WebSocketStandardResponse(
          "Sec-WebSocket-Extensions: permessage-deflate\r\n") +
          std::string(
              "\xc1\x07"  // WebSocket header (FIN + RSV1, Text payload 7 bytes)
              "\xf2\x48\xcd\xc9\xc9\x07\x00",  // "Hello" DEFLATE compressed
              9));
  RunUntilIdle();

  ASSERT_TRUE(stream_);
  ScopedVector<WebSocketFrame> frames;
  CompletionCallback callback;
  ASSERT_EQ(OK, stream_->ReadFrames(&frames, callback));
  ASSERT_EQ(1U, frames.size());
  ASSERT_EQ(5U, frames[0]->header.payload_length);
  EXPECT_EQ("Hello", std::string(frames[0]->data->data(), 5));
}

// Unknown extension in the response is rejected
TEST_F(WebSocketStreamCreateExtensionTest, UnknownExtension) {
  CreateAndConnectWithExtensions("x-unknown-extension");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "Found an unsupported extension 'x-unknown-extension' "
            "in 'Sec-WebSocket-Extensions' header",
            failure_message());
}

// Malformed extensions are rejected (this file does not cover all possible
// parse failures, as the parser is covered thoroughly by its own unit tests).
TEST_F(WebSocketStreamCreateExtensionTest, MalformedExtension) {
  CreateAndConnectWithExtensions(";");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: 'Sec-WebSocket-Extensions' header "
      "value is rejected by the parser: ;",
      failure_message());
}

// The permessage-deflate extension may only be specified once.
TEST_F(WebSocketStreamCreateExtensionTest, OnlyOnePerMessageDeflateAllowed) {
  CreateAndConnectWithExtensions(
      "permessage-deflate, permessage-deflate; client_max_window_bits=10");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: "
      "Received duplicate permessage-deflate response",
      failure_message());
}

// permessage-deflate parameters may not be duplicated.
TEST_F(WebSocketStreamCreateExtensionTest, NoDuplicateParameters) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; client_no_context_takeover; "
      "client_no_context_takeover");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "Received duplicate permessage-deflate extension parameter "
      "client_no_context_takeover",
      failure_message());
}

// permessage-deflate parameters must start with "client_" or "server_"
TEST_F(WebSocketStreamCreateExtensionTest, BadParameterPrefix) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; absurd_no_context_takeover");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "Received an unexpected permessage-deflate extension parameter",
      failure_message());
}

// permessage-deflate parameters must be either *_no_context_takeover or
// *_max_window_bits
TEST_F(WebSocketStreamCreateExtensionTest, BadParameterSuffix) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; client_max_content_bits=5");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "Received an unexpected permessage-deflate extension parameter",
      failure_message());
}

// *_no_context_takeover parameters must not have an argument
TEST_F(WebSocketStreamCreateExtensionTest, BadParameterValue) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; client_no_context_takeover=true");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "Received invalid client_no_context_takeover parameter",
      failure_message());
}

// *_max_window_bits must have an argument
TEST_F(WebSocketStreamCreateExtensionTest, NoMaxWindowBitsArgument) {
  CreateAndConnectWithExtensions("permessage-deflate; client_max_window_bits");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "client_max_window_bits must have value",
      failure_message());
}

// *_max_window_bits must be an integer
TEST_F(WebSocketStreamCreateExtensionTest, MaxWindowBitsValueInteger) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; server_max_window_bits=banana");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "Received invalid server_max_window_bits parameter",
      failure_message());
}

// *_max_window_bits must be >= 8
TEST_F(WebSocketStreamCreateExtensionTest, MaxWindowBitsValueTooSmall) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; server_max_window_bits=7");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "Received invalid server_max_window_bits parameter",
      failure_message());
}

// *_max_window_bits must be <= 15
TEST_F(WebSocketStreamCreateExtensionTest, MaxWindowBitsValueTooBig) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; client_max_window_bits=16");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "Received invalid client_max_window_bits parameter",
      failure_message());
}

// *_max_window_bits must not start with 0
TEST_F(WebSocketStreamCreateExtensionTest, MaxWindowBitsValueStartsWithZero) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; client_max_window_bits=08");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "Received invalid client_max_window_bits parameter",
      failure_message());
}

// *_max_window_bits must not start with +
TEST_F(WebSocketStreamCreateExtensionTest, MaxWindowBitsValueStartsWithPlus) {
  CreateAndConnectWithExtensions(
      "permessage-deflate; server_max_window_bits=+9");
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ(
      "Error during WebSocket handshake: Error in permessage-deflate: "
      "Received invalid server_max_window_bits parameter",
      failure_message());
}

// TODO(ricea): Check that WebSocketDeflateStream is initialised with the
// arguments from the server. This is difficult because the data written to the
// socket is randomly masked.

// Additional Sec-WebSocket-Accept headers should be rejected.
TEST_F(WebSocketStreamCreateTest, DoubleAccept) {
  CreateAndConnectStandard(
      "ws://localhost/",
      "/",
      NoSubProtocols(),
      "http://localhost",
      "",
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n");
  RunUntilIdle();
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "'Sec-WebSocket-Accept' header must not appear "
            "more than once in a response",
            failure_message());
}

// Response code 200 must be rejected.
TEST_F(WebSocketStreamCreateTest, InvalidStatusCode) {
  static const char kInvalidStatusCodeResponse[] =
      "HTTP/1.1 200 OK\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "\r\n";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kInvalidStatusCodeResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: Unexpected response code: 200",
            failure_message());
}

// Redirects are not followed (according to the WHATWG WebSocket API, which
// overrides RFC6455 for browser applications).
TEST_F(WebSocketStreamCreateTest, RedirectsRejected) {
  static const char kRedirectResponse[] =
      "HTTP/1.1 302 Moved Temporarily\r\n"
      "Content-Type: text/html\r\n"
      "Content-Length: 34\r\n"
      "Connection: keep-alive\r\n"
      "Location: ws://localhost/other\r\n"
      "\r\n"
      "<title>Moved</title><h1>Moved</h1>";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kRedirectResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: Unexpected response code: 302",
            failure_message());
}

// Malformed responses should be rejected. HttpStreamParser will accept just
// about any garbage in the middle of the headers. To make it give up, the junk
// has to be at the start of the response. Even then, it just gets treated as an
// HTTP/0.9 response.
TEST_F(WebSocketStreamCreateTest, MalformedResponse) {
  static const char kMalformedResponse[] =
      "220 mx.google.com ESMTP\r\n"
      "HTTP/1.1 101 OK\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "\r\n";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kMalformedResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: Invalid status line",
            failure_message());
}

// Upgrade header must be present.
TEST_F(WebSocketStreamCreateTest, MissingUpgradeHeader) {
  static const char kMissingUpgradeResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "\r\n";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kMissingUpgradeResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: 'Upgrade' header is missing",
            failure_message());
}

// There must only be one upgrade header.
TEST_F(WebSocketStreamCreateTest, DoubleUpgradeHeader) {
  CreateAndConnectStandard(
      "ws://localhost/",
      "/",
      NoSubProtocols(),
      "http://localhost",
      "", "Upgrade: HTTP/2.0\r\n");
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "'Upgrade' header must not appear more than once in a response",
            failure_message());
}

// There must only be one correct upgrade header.
TEST_F(WebSocketStreamCreateTest, IncorrectUpgradeHeader) {
  static const char kMissingUpgradeResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "Upgrade: hogefuga\r\n"
      "\r\n";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kMissingUpgradeResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "'Upgrade' header value is not 'WebSocket': hogefuga",
            failure_message());
}

// Connection header must be present.
TEST_F(WebSocketStreamCreateTest, MissingConnectionHeader) {
  static const char kMissingConnectionResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "\r\n";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kMissingConnectionResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "'Connection' header is missing",
            failure_message());
}

// Connection header must contain "Upgrade".
TEST_F(WebSocketStreamCreateTest, IncorrectConnectionHeader) {
  static const char kMissingConnectionResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "Connection: hogefuga\r\n"
      "\r\n";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kMissingConnectionResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "'Connection' header value must contain 'Upgrade'",
            failure_message());
}

// Connection header is permitted to contain other tokens.
TEST_F(WebSocketStreamCreateTest, AdditionalTokenInConnectionHeader) {
  static const char kAdditionalConnectionTokenResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade, Keep-Alive\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "\r\n";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kAdditionalConnectionTokenResponse);
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_TRUE(stream_);
}

// Sec-WebSocket-Accept header must be present.
TEST_F(WebSocketStreamCreateTest, MissingSecWebSocketAccept) {
  static const char kMissingAcceptResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "\r\n";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kMissingAcceptResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "'Sec-WebSocket-Accept' header is missing",
            failure_message());
}

// Sec-WebSocket-Accept header must match the key that was sent.
TEST_F(WebSocketStreamCreateTest, WrongSecWebSocketAccept) {
  static const char kIncorrectAcceptResponse[] =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: x/byyPZ2tOFvJCGkkugcKvqhhPk=\r\n"
      "\r\n";
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kIncorrectAcceptResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error during WebSocket handshake: "
            "Incorrect 'Sec-WebSocket-Accept' header value",
            failure_message());
}

// Cancellation works.
TEST_F(WebSocketStreamCreateTest, Cancellation) {
  CreateAndConnectStandard(
      "ws://localhost/", "/", NoSubProtocols(), "http://localhost", "", "");
  stream_request_.reset();
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_FALSE(stream_);
  EXPECT_FALSE(request_info_);
  EXPECT_FALSE(response_info_);
}

// Connect failure must look just like negotiation failure.
TEST_F(WebSocketStreamCreateTest, ConnectionFailure) {
  scoped_ptr<DeterministicSocketData> socket_data(BuildNullSocketData());
  socket_data->set_connect_data(
      MockConnect(SYNCHRONOUS, ERR_CONNECTION_REFUSED));
  CreateAndConnectRawExpectations("ws://localhost/", NoSubProtocols(),
                                  "http://localhost", socket_data.Pass());
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error in connection establishment: net::ERR_CONNECTION_REFUSED",
            failure_message());
  EXPECT_FALSE(request_info_);
  EXPECT_FALSE(response_info_);
}

// Connect timeout must look just like any other failure.
TEST_F(WebSocketStreamCreateTest, ConnectionTimeout) {
  scoped_ptr<DeterministicSocketData> socket_data(BuildNullSocketData());
  socket_data->set_connect_data(
      MockConnect(ASYNC, ERR_CONNECTION_TIMED_OUT));
  CreateAndConnectRawExpectations("ws://localhost/", NoSubProtocols(),
                                  "http://localhost", socket_data.Pass());
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error in connection establishment: net::ERR_CONNECTION_TIMED_OUT",
            failure_message());
}

// The server doesn't respond to the opening handshake.
TEST_F(WebSocketStreamCreateTest, HandshakeTimeout) {
  scoped_ptr<DeterministicSocketData> socket_data(BuildNullSocketData());
  socket_data->set_connect_data(MockConnect(SYNCHRONOUS, ERR_IO_PENDING));
  scoped_ptr<MockWeakTimer> timer(new MockWeakTimer(false, false));
  base::WeakPtr<MockWeakTimer> weak_timer = timer->AsWeakPtr();
  CreateAndConnectRawExpectations("ws://localhost/",
                                  NoSubProtocols(),
                                  "http://localhost",
                                  socket_data.Pass(),
                                  timer.Pass());
  EXPECT_FALSE(has_failed());
  ASSERT_TRUE(weak_timer.get());
  EXPECT_TRUE(weak_timer->IsRunning());

  weak_timer->Fire();
  RunUntilIdle();

  EXPECT_TRUE(has_failed());
  EXPECT_EQ("WebSocket opening handshake timed out", failure_message());
  ASSERT_TRUE(weak_timer.get());
  EXPECT_FALSE(weak_timer->IsRunning());
}

// When the connection establishes the timer should be stopped.
TEST_F(WebSocketStreamCreateTest, HandshakeTimerOnSuccess) {
  scoped_ptr<MockWeakTimer> timer(new MockWeakTimer(false, false));
  base::WeakPtr<MockWeakTimer> weak_timer = timer->AsWeakPtr();

  CreateAndConnectStandard("ws://localhost/",
                           "/",
                           NoSubProtocols(),
                           "http://localhost",
                           "",
                           "",
                           timer.Pass());
  ASSERT_TRUE(weak_timer);
  EXPECT_TRUE(weak_timer->IsRunning());

  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_TRUE(stream_);
  ASSERT_TRUE(weak_timer);
  EXPECT_FALSE(weak_timer->IsRunning());
}

// When the connection fails the timer should be stopped.
TEST_F(WebSocketStreamCreateTest, HandshakeTimerOnFailure) {
  scoped_ptr<DeterministicSocketData> socket_data(BuildNullSocketData());
  socket_data->set_connect_data(
      MockConnect(SYNCHRONOUS, ERR_CONNECTION_REFUSED));
  scoped_ptr<MockWeakTimer> timer(new MockWeakTimer(false, false));
  base::WeakPtr<MockWeakTimer> weak_timer = timer->AsWeakPtr();
  CreateAndConnectRawExpectations("ws://localhost/",
                                  NoSubProtocols(),
                                  "http://localhost",
                                  socket_data.Pass(),
                                  timer.Pass());
  ASSERT_TRUE(weak_timer.get());
  EXPECT_TRUE(weak_timer->IsRunning());

  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("Error in connection establishment: net::ERR_CONNECTION_REFUSED",
            failure_message());
  ASSERT_TRUE(weak_timer.get());
  EXPECT_FALSE(weak_timer->IsRunning());
}

// Cancellation during connect works.
TEST_F(WebSocketStreamCreateTest, CancellationDuringConnect) {
  scoped_ptr<DeterministicSocketData> socket_data(BuildNullSocketData());
  socket_data->set_connect_data(MockConnect(SYNCHRONOUS, ERR_IO_PENDING));
  CreateAndConnectRawExpectations("ws://localhost/",
                                  NoSubProtocols(),
                                  "http://localhost",
                                  socket_data.Pass());
  stream_request_.reset();
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_FALSE(stream_);
}

// Cancellation during write of the request headers works.
TEST_F(WebSocketStreamCreateTest, CancellationDuringWrite) {
  // We seem to need at least two operations in order to use SetStop().
  MockWrite writes[] = {MockWrite(ASYNC, 0, "GET / HTTP/"),
                        MockWrite(ASYNC, 1, "1.1\r\n")};
  // We keep a copy of the pointer so that we can call RunFor() on it later.
  DeterministicSocketData* socket_data(
      new DeterministicSocketData(NULL, 0, writes, arraysize(writes)));
  socket_data->set_connect_data(MockConnect(SYNCHRONOUS, OK));
  socket_data->SetStop(1);
  CreateAndConnectRawExpectations("ws://localhost/",
                                  NoSubProtocols(),
                                  "http://localhost",
                                  make_scoped_ptr(socket_data));
  socket_data->Run();
  stream_request_.reset();
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(request_info_);
  EXPECT_FALSE(response_info_);
}

// Cancellation during read of the response headers works.
TEST_F(WebSocketStreamCreateTest, CancellationDuringRead) {
  std::string request = WebSocketStandardRequest("/", "http://localhost", "");
  MockWrite writes[] = {MockWrite(ASYNC, 0, request.c_str())};
  MockRead reads[] = {
    MockRead(ASYNC, 1, "HTTP/1.1 101 Switching Protocols\r\nUpgr"),
  };
  scoped_ptr<DeterministicSocketData> socket_data(
      BuildSocketData(reads, writes));
  socket_data->SetStop(1);
  DeterministicSocketData* socket_data_raw_ptr = socket_data.get();
  CreateAndConnectRawExpectations("ws://localhost/",
                                  NoSubProtocols(),
                                  "http://localhost",
                                  socket_data.Pass());
  socket_data_raw_ptr->Run();
  stream_request_.reset();
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_FALSE(stream_);
  EXPECT_TRUE(request_info_);
  EXPECT_FALSE(response_info_);
}

// Over-size response headers (> 256KB) should not cause a crash.  This is a
// regression test for crbug.com/339456. It is based on the layout test
// "cookie-flood.html".
TEST_F(WebSocketStreamCreateTest, VeryLargeResponseHeaders) {
  std::string set_cookie_headers;
  set_cookie_headers.reserve(45 * 10000);
  for (int i = 0; i < 10000; ++i) {
    set_cookie_headers +=
        base::StringPrintf("Set-Cookie: WK-websocket-test-flood-%d=1\r\n", i);
  }
  CreateAndConnectStandard("ws://localhost/", "/", NoSubProtocols(),
                           "http://localhost", "", set_cookie_headers);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_FALSE(response_info_);
}

// If the remote host closes the connection without sending headers, we should
// log the console message "Connection closed before receiving a handshake
// response".
TEST_F(WebSocketStreamCreateTest, NoResponse) {
  std::string request = WebSocketStandardRequest("/", "http://localhost", "");
  MockWrite writes[] = {MockWrite(ASYNC, request.data(), request.size(), 0)};
  MockRead reads[] = {MockRead(ASYNC, 0, 1)};
  scoped_ptr<DeterministicSocketData> socket_data(
      BuildSocketData(reads, writes));
  DeterministicSocketData* socket_data_raw_ptr = socket_data.get();
  CreateAndConnectRawExpectations("ws://localhost/",
                                  NoSubProtocols(),
                                  "http://localhost",
                                  socket_data.Pass());
  socket_data_raw_ptr->RunFor(2);
  EXPECT_TRUE(has_failed());
  EXPECT_FALSE(stream_);
  EXPECT_FALSE(response_info_);
  EXPECT_EQ("Connection closed before receiving a handshake response",
            failure_message());
}

TEST_F(WebSocketStreamCreateTest, SelfSignedCertificateFailure) {
  ssl_data_.push_back(
      new SSLSocketDataProvider(ASYNC, ERR_CERT_AUTHORITY_INVALID));
  ssl_data_[0]->cert =
      ImportCertFromFile(GetTestCertsDirectory(), "unittest.selfsigned.der");
  ASSERT_TRUE(ssl_data_[0]->cert.get());
  scoped_ptr<DeterministicSocketData> raw_socket_data(BuildNullSocketData());
  CreateAndConnectRawExpectations("wss://localhost/",
                                  NoSubProtocols(),
                                  "http://localhost",
                                  raw_socket_data.Pass());
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  ASSERT_TRUE(ssl_error_callbacks_);
  ssl_error_callbacks_->CancelSSLRequest(ERR_CERT_AUTHORITY_INVALID,
                                         &ssl_info_);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
}

TEST_F(WebSocketStreamCreateTest, SelfSignedCertificateSuccess) {
  scoped_ptr<SSLSocketDataProvider> ssl_data(
      new SSLSocketDataProvider(ASYNC, ERR_CERT_AUTHORITY_INVALID));
  ssl_data->cert =
      ImportCertFromFile(GetTestCertsDirectory(), "unittest.selfsigned.der");
  ASSERT_TRUE(ssl_data->cert.get());
  ssl_data_.push_back(ssl_data.release());
  ssl_data.reset(new SSLSocketDataProvider(ASYNC, OK));
  ssl_data_.push_back(ssl_data.release());
  url_request_context_host_.AddRawExpectations(BuildNullSocketData());
  CreateAndConnectStandard(
      "wss://localhost/", "/", NoSubProtocols(), "http://localhost", "", "");
  RunUntilIdle();
  ASSERT_TRUE(ssl_error_callbacks_);
  ssl_error_callbacks_->ContinueSSLRequest();
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_TRUE(stream_);
}

// If the server requests authorisation, but we have no credentials, the
// connection should fail cleanly.
TEST_F(WebSocketStreamCreateBasicAuthTest, FailureNoCredentials) {
  CreateAndConnectCustomResponse("ws://localhost/",
                                 "/",
                                 NoSubProtocols(),
                                 "http://localhost",
                                 "",
                                 kUnauthorizedResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_EQ("HTTP Authentication failed; no valid credentials available",
            failure_message());
  EXPECT_TRUE(response_info_);
}

TEST_F(WebSocketStreamCreateBasicAuthTest, SuccessPasswordInUrl) {
  CreateAndConnectAuthHandshake("ws://foo:bar@localhost/",
                                "Zm9vOmJhcg==",
                                WebSocketStandardResponse(std::string()));
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_TRUE(stream_);
  ASSERT_TRUE(response_info_);
  EXPECT_EQ(101, response_info_->status_code);
}

TEST_F(WebSocketStreamCreateBasicAuthTest, FailureIncorrectPasswordInUrl) {
  CreateAndConnectAuthHandshake(
      "ws://foo:baz@localhost/", "Zm9vOmJheg==", kUnauthorizedResponse);
  RunUntilIdle();
  EXPECT_TRUE(has_failed());
  EXPECT_TRUE(response_info_);
}

// Digest auth has the same connection semantics as Basic auth, so we can
// generally assume that whatever works for Basic auth will also work for
// Digest. There's just one test here, to confirm that it works at all.
TEST_F(WebSocketStreamCreateDigestAuthTest, DigestPasswordInUrl) {
  AddRawExpectations(helper_.BuildSocketData1(kUnauthorizedResponse));

  CreateAndConnectRawExpectations(
      "ws://FooBar:pass@localhost/",
      NoSubProtocols(),
      "http://localhost",
      helper_.BuildSocketData2(kAuthorizedRequest,
                               WebSocketStandardResponse(std::string())));
  RunUntilIdle();
  EXPECT_FALSE(has_failed());
  EXPECT_TRUE(stream_);
  ASSERT_TRUE(response_info_);
  EXPECT_EQ(101, response_info_->status_code);
}

TEST_F(WebSocketStreamCreateUMATest, Incomplete) {
  const std::string name("Net.WebSocket.HandshakeResult");
  scoped_ptr<base::HistogramSamples> original(GetSamples(name));

  {
    StreamCreation creation;
    creation.CreateAndConnectStandard("ws://localhost/",
                                      "/",
                                      creation.NoSubProtocols(),
                                      "http://localhost",
                                      "",
                                      "");
  }

  scoped_ptr<base::HistogramSamples> samples(GetSamples(name));
  ASSERT_TRUE(samples);
  if (original) {
    samples->Subtract(*original);  // Cancel the original values.
  }
  EXPECT_EQ(1, samples->GetCount(INCOMPLETE));
  EXPECT_EQ(0, samples->GetCount(CONNECTED));
  EXPECT_EQ(0, samples->GetCount(FAILED));
}

TEST_F(WebSocketStreamCreateUMATest, Connected) {
  const std::string name("Net.WebSocket.HandshakeResult");
  scoped_ptr<base::HistogramSamples> original(GetSamples(name));

  {
    StreamCreation creation;
    creation.CreateAndConnectStandard("ws://localhost/",
                                      "/",
                                      creation.NoSubProtocols(),
                                      "http://localhost",
                                      "",
                                      "");
    creation.RunUntilIdle();
  }

  scoped_ptr<base::HistogramSamples> samples(GetSamples(name));
  ASSERT_TRUE(samples);
  if (original) {
    samples->Subtract(*original);  // Cancel the original values.
  }
  EXPECT_EQ(0, samples->GetCount(INCOMPLETE));
  EXPECT_EQ(1, samples->GetCount(CONNECTED));
  EXPECT_EQ(0, samples->GetCount(FAILED));
}

TEST_F(WebSocketStreamCreateUMATest, Failed) {
  const std::string name("Net.WebSocket.HandshakeResult");
  scoped_ptr<base::HistogramSamples> original(GetSamples(name));

  {
    StreamCreation creation;
    static const char kInvalidStatusCodeResponse[] =
        "HTTP/1.1 200 OK\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
        "\r\n";
    creation.CreateAndConnectCustomResponse("ws://localhost/",
                                            "/",
                                            creation.NoSubProtocols(),
                                            "http://localhost",
                                            "",
                                            kInvalidStatusCodeResponse);
    creation.RunUntilIdle();
  }

  scoped_ptr<base::HistogramSamples> samples(GetSamples(name));
  ASSERT_TRUE(samples);
  if (original) {
    samples->Subtract(*original);  // Cancel the original values.
  }
  EXPECT_EQ(1, samples->GetCount(INCOMPLETE));
  EXPECT_EQ(0, samples->GetCount(CONNECTED));
  EXPECT_EQ(0, samples->GetCount(FAILED));
}

}  // namespace
}  // namespace net
