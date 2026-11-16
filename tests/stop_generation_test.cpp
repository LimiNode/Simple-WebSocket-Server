#include "assert.hpp"
#include "server_ws.hpp"
#include <atomic>
#include <memory>

#ifdef ASIO_STANDALONE
namespace test_asio = asio;
#else
namespace test_asio = SimpleWeb::asio;
#endif

class GenerationServer : public SimpleWeb::SocketServer<SimpleWeb::WS> {
public:
  std::shared_ptr<Connection> captured_connection;
  std::atomic<int> open_callbacks{0};
  bool stop_from_on_open = false;

  unsigned short bound_port() const {
    return acceptor->local_endpoint().port();
  }

  bool replay_open() {
    return connection_open(captured_connection, endpoint["^/$"]);
  }
};

int main() {
  auto context = std::make_shared<SimpleWeb::io_context>();
  GenerationServer server;
  server.io_service = context;
  server.config.address = "127.0.0.1";
  server.config.port = 0;

  auto &root = server.endpoint["^/$"];
  root.on_handshake = [&server](std::shared_ptr<GenerationServer::Connection> connection, SimpleWeb::CaseInsensitiveMultimap &) {
    server.captured_connection = std::move(connection);
    return SimpleWeb::StatusCode::information_switching_protocols;
  };
  root.on_open = [&server](std::shared_ptr<GenerationServer::Connection>) {
    ++server.open_callbacks;
    if(server.stop_from_on_open)
      server.stop();
  };

  server.start();
  const auto port = server.bound_port();

  test_asio::ip::tcp::socket peer(*context);
  SimpleWeb::error_code ec;
  peer.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port), ec);
  ASSERT(!ec);
  ASSERT(context->run_one() == 1); // accept

  const std::string request = "GET / HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Upgrade: websocket\r\n"
                              "Connection: Upgrade\r\n"
                              "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                              "Sec-WebSocket-Version: 13\r\n\r\n";
  test_asio::write(peer, test_asio::buffer(request), ec);
  ASSERT(!ec);
  ASSERT(context->run_one() == 1); // handshake and on_handshake
  ASSERT(server.captured_connection != nullptr);

  // The handshake write completion is queued, but has not entered yet.
  // stop() must cancel that old generation before it can publish the socket.
  server.stop();
  ASSERT(server.get_connections().empty());
  ASSERT(!server.replay_open());
  ASSERT(server.get_connections().empty());
  ASSERT(server.open_callbacks.load() == 0);

  context->restart();
  context->poll();
  ASSERT(server.get_connections().empty());
  ASSERT(server.open_callbacks.load() == 0);

  // A new start must not accept publication from the old ScopeRunner.
  server.start();
  ASSERT(!server.replay_open());
  ASSERT(server.get_connections().empty());
  server.stop();

  // stop() from the owning on_open callback must remain non-blocking.
  context->poll();
  context->restart();
  server.stop_from_on_open = true;
  server.start();
  server.captured_connection.reset();
  test_asio::ip::tcp::socket second_peer(*context);
  second_peer.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), server.bound_port()), ec);
  ASSERT(!ec);
  ASSERT(context->run_one() == 1); // accept
  test_asio::write(second_peer, test_asio::buffer(request), ec);
  ASSERT(!ec);
  for(int i = 0; i < 4 && !server.captured_connection; ++i)
    ASSERT(context->run_one() == 1); // handshake and on_handshake
  ASSERT(server.captured_connection != nullptr);
  for(int i = 0; i < 4 && server.open_callbacks.load() == 0; ++i)
    ASSERT(context->run_one() == 1); // write completion, on_open, and stop()
  ASSERT(server.open_callbacks.load() == 1);
  ASSERT(server.get_connections().empty());

  peer.close(ec);
  second_peer.close(ec);
}
