#include "assert.hpp"
#include "server_ws.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace SimpleWeb;

class TrackingServer : public SocketServer<WS> {
public:
  std::size_t tracked_connection_count() {
    LockGuard lock(connections_mutex);
    return connections.size();
  }
};

int main() {
  TrackingServer server;
  server.config.address = "127.0.0.1";
  server.config.port = 0;

  std::atomic<unsigned short> port(0);
  std::thread server_thread([&server, &port]() {
    server.start([&port](unsigned short assigned_port) { port = assigned_port; });
  });

  for(int i = 0; i < 500 && port.load() == 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT(port.load() != 0);

  io_context context;
  asio::ip::tcp::socket socket(context);
  error_code ec;
  socket.connect(asio::ip::tcp::endpoint(make_address("127.0.0.1"), port.load()), ec);
  ASSERT(!ec);

  const std::string partial_request = "GET / HTTP/1.1\r\nHost: localhost\r\n";
  socket.write_some(asio::buffer(partial_request), ec);
  ASSERT(!ec);

  for(int i = 0; i < 500 && server.tracked_connection_count() == 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  ASSERT(server.tracked_connection_count() != 0);

  server.stop();
  server_thread.join();
  ASSERT(server.tracked_connection_count() == 0);
}
