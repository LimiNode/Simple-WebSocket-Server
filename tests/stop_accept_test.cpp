#include "assert.hpp"
#include "server_ws.hpp"
#include <memory>

#ifdef ASIO_STANDALONE
namespace test_asio = asio;
#else
namespace test_asio = SimpleWeb::asio;
#endif

class CountingServer : public SimpleWeb::SocketServer<SimpleWeb::WS> {
public:
  int accept_calls = 0;
  bool stop_on_rearm = false;

  unsigned short bound_port() const {
    return acceptor->local_endpoint().port();
  }

  std::size_t pending_connection_count() {
    SimpleWeb::LockGuard lock(connections_mutex);
    return connections.size();
  }

protected:
  void accept(const std::shared_ptr<SimpleWeb::ScopeRunner> &runner) override {
    ++accept_calls;
    if(stop_on_rearm && accept_calls == 2)
      stop_accept();
    SimpleWeb::SocketServer<SimpleWeb::WS>::accept(runner);
  }
};

int main() {
  auto context = std::make_shared<SimpleWeb::io_context>();
  CountingServer server;
  server.io_service = context;
  server.config.address = "127.0.0.1";
  server.config.port = 0;
  server.start();
  ASSERT(server.accept_calls == 1);
  const unsigned short port = server.bound_port();

  SimpleWeb::io_context peer_context;
  test_asio::ip::tcp::socket first_peer(peer_context);
  SimpleWeb::error_code ec;
  first_peer.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port), ec);
  ASSERT(!ec);

  // Let the successful accept callback run. The override calls stop_accept()
  // in the re-arm window, after the callback has succeeded but before the
  // concrete accept() implementation can queue another operation.
  server.stop_on_rearm = true;
  ASSERT(context->run_one() == 1);
  ASSERT(server.accept_calls == 2);
  ASSERT(server.pending_connection_count() == 1);

  test_asio::ip::tcp::socket second_peer(peer_context);
  second_peer.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port), ec);
  // The acceptor was closed during the re-arm window. The second peer may
  // complete its TCP connect into the kernel backlog (or fail immediately),
  // but it must not be registered by the server. No accept completion should
  // have been queued by the rejected re-arm attempt.
  ASSERT(context->poll() == 0);
  ASSERT(server.accept_calls == 2);
  ASSERT(server.pending_connection_count() == 1);

  server.stop();
  context->restart();
  context->run();
}
