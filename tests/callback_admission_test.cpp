#include "assert.hpp"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace AdmissionTest {
  std::function<void()> open_pause, upgrade_pause, handshake_pause;
  void before_open() { if(open_pause) open_pause(); }
  void before_upgrade() { if(upgrade_pause) upgrade_pause(); }
  void before_handshake() { if(handshake_pause) handshake_pause(); }
}

// This header differs from server_ws.hpp only by the three pauses above.
#include "admission_server_ws.hpp"
#include "server_wss.hpp"

#ifdef ASIO_STANDALONE
namespace test_asio = asio;
#else
namespace test_asio = SimpleWeb::asio;
#endif
using Tcp = test_asio::ip::tcp;

void send_request(SimpleWeb::WS &peer, const std::string &request);
void send_request(SimpleWeb::WSS &peer, const std::string &request);

class Event {
  std::mutex mutex;
  std::condition_variable condition;
  bool ready = false;
public:
  void set() {
    std::lock_guard<std::mutex> lock(mutex);
    ready = true;
    condition.notify_all();
  }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT(condition.wait_for(lock, std::chrono::seconds(5), [this] { return ready; }));
  }
};

template<class Socket>
class TestServer : public SimpleWeb::SocketServer<Socket> {
public:
  template<class... Args>
  TestServer(Args &&...args) : SimpleWeb::SocketServer<Socket>(std::forward<Args>(args)...) {}

  unsigned short port() const { return this->acceptor->local_endpoint().port(); }
  // Acquiring the lifecycle mutex proves stop's registry cleanup has completed,
  // not merely that its first atomic store has happened.
  bool cancellation_finished() const {
    std::lock_guard<std::mutex> lock(this->start_stop_mutex);
    return this->stopping.load();
  }
  std::size_t tracked() const {
    SimpleWeb::LockGuard lock(this->connections_mutex);
    return this->connections.size();
  }
};

template<class Server, class Peer>
void test_open_admission(Server &server, Peer &peer, const std::shared_ptr<SimpleWeb::io_context> &context) {
  server.io_service = context;
  server.config.address = "127.0.0.1";
  server.config.port = 0;
  Event admitted, resume, callback_entered, callback_return;
  AdmissionTest::open_pause = [&admitted, &resume] { admitted.set(); resume.wait(); };
  int opens = 0;
  server.endpoint["^/$"].on_open = [&server, &opens, &callback_entered, &callback_return](std::shared_ptr<typename Server::Connection>) {
    ASSERT(server.cancellation_finished());
    ASSERT(server.get_connections().empty());
    ++opens;
    callback_entered.set();
    callback_return.wait();
  };
  server.start();
  std::thread worker([context] { context->run(); });
  peer.lowest_layer().connect(Tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), server.port()));
  const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
                              "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                              "Sec-WebSocket-Version: 13\r\n\r\n";
  // The TLS overload performs its TLS handshake before writing HTTP.
  send_request(peer, request);
  admitted.wait(); // successful handshake; final generation check has passed
  auto stopped = std::async(std::launch::async, [&server] { server.stop(); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while(!server.cancellation_finished() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  ASSERT(server.cancellation_finished());
  ASSERT(server.tracked() == 0);
  ASSERT(server.get_connections().empty());
  ASSERT(stopped.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
  // Leave cancelled completions queued for the post-destruction drain.
  context->stop();
  resume.set(); // actual user on_open begins AFTER cancellation/registry clear
  callback_entered.wait();
  ASSERT(stopped.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
  callback_return.set();
  ASSERT(stopped.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  stopped.get();
  worker.join();
  ASSERT(opens == 1);
  ASSERT(server.get_connections().empty());
  ASSERT(server.tracked() == 0);
  SimpleWeb::error_code ec;
  peer.lowest_layer().close(ec);
  AdmissionTest::open_pause = nullptr;
}

void send_request(SimpleWeb::WS &peer, const std::string &request) {
  test_asio::write(peer, test_asio::buffer(request));
}
void send_request(SimpleWeb::WSS &peer, const std::string &request) {
  peer.handshake(test_asio::ssl::stream_base::client);
  test_asio::write(peer, test_asio::buffer(request));
}

void test_upgrade_admission(bool admitted) {
  SimpleWeb::io_context context;
  TestServer<SimpleWeb::WS> server;
  Tcp::acceptor listener(context, Tcp::endpoint(Tcp::v4(), 0));
  Tcp::socket peer(context);
  peer.connect(Tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), listener.local_endpoint().port()));
  std::unique_ptr<SimpleWeb::WS> socket(new SimpleWeb::WS(context));
  listener.accept(*socket);
  auto connection = std::make_shared<TestServer<SimpleWeb::WS>::Connection>(std::move(socket));
  connection->path = "/";
  connection->header.emplace("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
  int handshakes = 0, opens = 0;
  server.endpoint["^/$"].on_handshake = [&handshakes](std::shared_ptr<TestServer<SimpleWeb::WS>::Connection>, SimpleWeb::CaseInsensitiveMultimap &) {
    ++handshakes;
    return SimpleWeb::StatusCode::information_switching_protocols;
  };
  server.endpoint["^/$"].on_open = [&opens](std::shared_ptr<TestServer<SimpleWeb::WS>::Connection>) { ++opens; };
  Event captured_runner, resume;
  auto pause = [&captured_runner, &resume] { captured_runner.set(); resume.wait(); };
  if(admitted)
    AdmissionTest::handshake_pause = pause;
  else
    AdmissionTest::upgrade_pause = pause;
  auto upgrade = std::async(std::launch::async, [&server, connection] { server.upgrade(connection); });
  captured_runner.wait();
  auto stopped = std::async(std::launch::async, [&server] { server.stop(); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while(!server.cancellation_finished() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  ASSERT(server.cancellation_finished());
  if(admitted) // registered, but synchronous handshake setup has not begun
    ASSERT(stopped.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
  else // no ScopeRunner lock yet; stop must finish before we resume upgrade
    ASSERT(stopped.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  resume.set();
  ASSERT(upgrade.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  upgrade.get();
  ASSERT(stopped.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  stopped.get();
  ASSERT(handshakes == (admitted ? 1 : 0));
  context.run();
  ASSERT(handshakes == (admitted ? 1 : 0) && opens == 0);
  ASSERT(server.tracked() == 0 && server.get_connections().empty());
  AdmissionTest::upgrade_pause = nullptr;
  AdmissionTest::handshake_pause = nullptr;
}

int main(int argc, char **argv) {
  ASSERT(argc == 3);
  {
    SimpleWeb::io_context context;
    SimpleWeb::WS peer(context);
    auto server_context = std::make_shared<SimpleWeb::io_context>();
    {
      TestServer<SimpleWeb::WS> server;
      test_open_admission(server, peer, server_context);
    }
    server_context->restart();
    server_context->run(); // drain after destruction under ASan
  }
  {
    SimpleWeb::io_context context;
    test_asio::ssl::context tls(test_asio::ssl::context::tls_client);
    SimpleWeb::WSS peer(context, tls);
    auto server_context = std::make_shared<SimpleWeb::io_context>();
    {
      TestServer<SimpleWeb::WSS> server(argv[1], argv[2]);
      test_open_admission(server, peer, server_context);
    }
    server_context->restart();
    server_context->run();
  }
  test_upgrade_admission(false);
  test_upgrade_admission(true);
}
