#include "assert.hpp"
#include "server_ws.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

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

class BlockingOpenServer : public SimpleWeb::SocketServer<SimpleWeb::WS> {
public:
  std::atomic<bool> open_entered{false};
  std::atomic<bool> saw_stopping{false};
  std::mutex event_mutex;
  std::condition_variable event;

  bool is_stopping() const noexcept {
    return stopping.load();
  }

  unsigned short bound_port() const {
    return acceptor->local_endpoint().port();
  }
};

class UpgradeServer : public SimpleWeb::SocketServer<SimpleWeb::WS> {
public:
  bool is_stopping() const noexcept {
    return stopping.load();
  }
};

static void test_external_stop_during_open() {
  auto context = std::make_shared<SimpleWeb::io_context>();
  BlockingOpenServer server;
  server.io_service = context;
  server.config.address = "127.0.0.1";
  server.config.port = 0;

  auto &root = server.endpoint["^/$"];
  root.on_handshake = [](std::shared_ptr<BlockingOpenServer::Connection>, SimpleWeb::CaseInsensitiveMultimap &) {
    return SimpleWeb::StatusCode::information_switching_protocols;
  };
  root.on_open = [&server](std::shared_ptr<BlockingOpenServer::Connection>) {
    server.open_entered.store(true);
    server.event.notify_all();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(!server.is_stopping() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    server.saw_stopping.store(server.is_stopping());
    server.event.notify_all();
  };

  server.start();
  const auto port = server.bound_port();
  std::thread io_thread([&context] { context->run(); });

  test_asio::ip::tcp::socket peer(*context);
  SimpleWeb::error_code ec;
  peer.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port), ec);
  ASSERT(!ec);
  const std::string request = "GET / HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Upgrade: websocket\r\n"
                              "Connection: Upgrade\r\n"
                              "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                              "Sec-WebSocket-Version: 13\r\n\r\n";
  test_asio::write(peer, test_asio::buffer(request), ec);
  ASSERT(!ec);

  {
    std::unique_lock<std::mutex> lock(server.event_mutex);
    ASSERT(server.event.wait_for(lock, std::chrono::seconds(2), [&server] { return server.open_entered.load(); }));
  }

  // stop() must be able to publish cancellation while on_open is running.
  // The callback observes stopping and returns, allowing ScopeRunner to drain.
  server.stop();
  ASSERT(server.saw_stopping.load());
  ASSERT(server.get_connections().empty());

  peer.close(ec);
  io_thread.join();
}

static void test_concurrent_upgrade_stop() {
  auto context = std::make_shared<SimpleWeb::io_context>();
  UpgradeServer server;
  auto &root = server.endpoint["^/$"];
  std::atomic<bool> handshake_entered{false};
  std::mutex event_mutex;
  std::condition_variable event;

  root.on_handshake = [&](std::shared_ptr<UpgradeServer::Connection>, SimpleWeb::CaseInsensitiveMultimap &) {
    handshake_entered.store(true);
    event.notify_all();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(!server.is_stopping() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return SimpleWeb::StatusCode::information_switching_protocols;
  };

  test_asio::ip::tcp::acceptor listener(*context, test_asio::ip::tcp::endpoint(test_asio::ip::tcp::v4(), 0));
  const auto port = listener.local_endpoint().port();
  test_asio::ip::tcp::socket peer(*context);
  SimpleWeb::error_code ec;
  peer.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port), ec);
  ASSERT(!ec);
  test_asio::ip::tcp::socket accepted(*context);
  listener.accept(accepted, ec);
  ASSERT(!ec);

  std::unique_ptr<SimpleWeb::WS> socket(new SimpleWeb::WS(std::move(accepted)));
  auto connection = std::make_shared<UpgradeServer::Connection>(std::move(socket));
  connection->path = "/";
  connection->header.emplace("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");

  std::thread upgrade_thread([&server, connection] { server.upgrade(connection); });
  {
    std::unique_lock<std::mutex> lock(event_mutex);
    ASSERT(event.wait_for(lock, std::chrono::seconds(2), [&handshake_entered] { return handshake_entered.load(); }));
  }

  server.stop();
  upgrade_thread.join();
  ASSERT(server.get_connections().empty());
  peer.close(ec);
}

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

  SimpleWeb::restart(*context);
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
  SimpleWeb::restart(*context);
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

  test_external_stop_during_open();
  test_concurrent_upgrade_stop();
}
