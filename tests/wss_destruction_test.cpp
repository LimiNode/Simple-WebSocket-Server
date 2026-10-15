#include "assert.hpp"
#include "server_wss.hpp"
#include <memory>

#ifdef ASIO_STANDALONE
namespace test_asio = asio;
#else
namespace test_asio = SimpleWeb::asio;
#endif

class TestWssServer : public SimpleWeb::SocketServer<SimpleWeb::WSS> {
public:
  TestWssServer(const std::string &certificate_file, const std::string &private_key_file)
      : SimpleWeb::SocketServer<SimpleWeb::WSS>(certificate_file, private_key_file) {}

  unsigned short bound_port() const {
    return acceptor->local_endpoint().port();
  }
};

int main(int argc, char **argv) {
  ASSERT(argc == 3);

  auto context = std::make_shared<SimpleWeb::io_context>();
  test_asio::ip::tcp::socket peer(*context);
  SimpleWeb::error_code ec;

  {
    TestWssServer server(argv[1], argv[2]);
    server.io_service = context;
    server.config.address = "127.0.0.1";
    server.config.port = 0;
    server.start();

    peer.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), server.bound_port()), ec);
    ASSERT(!ec);

    // Run the successful TCP accept. The server now has a pending TLS
    // handshake, but the peer deliberately sends no TLS bytes.
    ASSERT(context->run_one() == 1);
  }

  // The server and its TLS context are gone. The queued handshake completion
  // must only close the connection and must not dereference the old server.
  context->restart();
  context->run();
  peer.close(ec);
}
