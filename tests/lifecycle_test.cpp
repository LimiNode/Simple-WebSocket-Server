#include "assert.hpp"
#include "server_ws.hpp"
#include <atomic>
#include <chrono>
#include <thread>

#ifdef ASIO_STANDALONE
namespace test_asio = asio;
#else
namespace test_asio = SimpleWeb::asio;
#endif

class TrackingServer : public SimpleWeb::SocketServer<SimpleWeb::WS> {
public:
  std::size_t tracked_connection_count() {
    SimpleWeb::LockGuard lock(connections_mutex);
    return connections.size();
  }
};

int main() {
  TrackingServer server;
  server.config.address = "127.0.0.1";
  server.config.port = 0;
  auto &rejected_endpoint = server.endpoint["^/reject$"];
  rejected_endpoint.on_handshake = [](std::shared_ptr<TrackingServer::Connection>, SimpleWeb::CaseInsensitiveMultimap &) {
    return SimpleWeb::StatusCode::client_error_forbidden;
  };

  std::atomic<unsigned short> port(0);
  std::thread server_thread([&server, &port]() {
    server.start([&port](unsigned short assigned_port) { port = assigned_port; });
  });

  for(int i = 0; i < 500 && port.load() == 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT(port.load() != 0);

  SimpleWeb::io_context context;
  test_asio::ip::tcp::socket unknown_endpoint(context);
  SimpleWeb::error_code ec;
  unknown_endpoint.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port.load()), ec);
  ASSERT(!ec);
  const std::string unknown_request = "GET /unknown HTTP/1.1\r\nHost: localhost\r\n\r\n";
  unknown_endpoint.write_some(test_asio::buffer(unknown_request), ec);
  ASSERT(!ec);
  char byte = 0;
  unknown_endpoint.read_some(test_asio::buffer(&byte, 1), ec);
  ASSERT(ec);
  for(int i = 0; i < 500 && server.tracked_connection_count() != 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  ASSERT(server.tracked_connection_count() == 0);

  test_asio::ip::tcp::socket rejected(context);
  rejected.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port.load()), ec);
  ASSERT(!ec);
  const std::string rejected_request = "GET /reject HTTP/1.1\r\nHost: localhost\r\n\r\n";
  rejected.write_some(test_asio::buffer(rejected_request), ec);
  ASSERT(!ec);
  rejected.read_some(test_asio::buffer(&byte, 1), ec);
  while(!ec)
    rejected.read_some(test_asio::buffer(&byte, 1), ec);
  ASSERT(ec);
  for(int i = 0; i < 500 && server.tracked_connection_count() != 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  ASSERT(server.tracked_connection_count() == 0);

  test_asio::ip::tcp::socket socket(context);
  socket.connect(test_asio::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port.load()), ec);
  ASSERT(!ec);

  const std::string partial_request = "GET / HTTP/1.1\r\nHost: localhost\r\n";
  socket.write_some(test_asio::buffer(partial_request), ec);
  ASSERT(!ec);

  for(int i = 0; i < 500 && server.tracked_connection_count() == 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  ASSERT(server.tracked_connection_count() != 0);

  server.stop();
  server_thread.join();
  ASSERT(server.tracked_connection_count() == 0);
}
