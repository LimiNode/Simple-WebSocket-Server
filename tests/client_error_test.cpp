#include "assert.hpp"
#include "client_ws.hpp"
#include <atomic>
#include <thread>

#ifdef ASIO_STANDALONE
namespace test_asio = asio;
#else
namespace test_asio = SimpleWeb::asio;
#endif

class TrackingClient : public SimpleWeb::SocketClient<SimpleWeb::WS> {
public:
  explicit TrackingClient(const std::string &server_port_path) : SimpleWeb::SocketClient<SimpleWeb::WS>(server_port_path) {}

  bool has_connection() {
    SimpleWeb::LockGuard lock(connection_mutex);
    return static_cast<bool>(connection);
  }
};

int main() {
  SimpleWeb::io_context server_context;
  test_asio::ip::tcp::acceptor acceptor(server_context, test_asio::ip::tcp::endpoint(test_asio::ip::tcp::v4(), 0));
  const auto port = acceptor.local_endpoint().port();

  std::thread server_thread([&]() {
    test_asio::ip::tcp::socket socket(server_context);
    SimpleWeb::error_code ec;
    acceptor.accept(socket, ec);
    ASSERT(!ec);
    const std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    test_asio::write(socket, test_asio::buffer(response), ec);
    ASSERT(!ec);
  });

  TrackingClient client("127.0.0.1:" + std::to_string(port));
  std::atomic<int> errors(0);
  client.on_error = [&client, &errors](std::shared_ptr<TrackingClient::Connection>, const SimpleWeb::error_code &) {
    ++errors;
    client.stop();
  };
  std::thread client_thread([&client]() { client.start(); });

  server_thread.join();
  client_thread.join();
  ASSERT(errors == 1);
  ASSERT(!client.has_connection());
}
