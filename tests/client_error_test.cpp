#include "assert.hpp"
#include "client_ws.hpp"
#include <atomic>
#include <thread>

using namespace SimpleWeb;

class TrackingClient : public SocketClient<WS> {
public:
  explicit TrackingClient(const std::string &server_port_path) : SocketClient<WS>(server_port_path) {}

  bool has_connection() {
    LockGuard lock(connection_mutex);
    return static_cast<bool>(connection);
  }
};

int main() {
  io_context server_context;
  asio::ip::tcp::acceptor acceptor(server_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  const auto port = acceptor.local_endpoint().port();

  std::thread server_thread([&]() {
    asio::ip::tcp::socket socket(server_context);
    error_code ec;
    acceptor.accept(socket, ec);
    ASSERT(!ec);
    const std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    asio::write(socket, asio::buffer(response), ec);
    ASSERT(!ec);
  });

  TrackingClient client("127.0.0.1:" + std::to_string(port));
  std::atomic<int> errors(0);
  client.on_error = [&client, &errors](std::shared_ptr<TrackingClient::Connection>, const error_code &) {
    ++errors;
    client.stop();
  };
  std::thread client_thread([&client]() { client.start(); });

  server_thread.join();
  client_thread.join();
  ASSERT(errors == 1);
  ASSERT(!client.has_connection());
}
