#include "assert.hpp"
#include "server_ws.hpp"
#include <memory>

int main() {
  auto context = std::make_shared<SimpleWeb::io_context>();
  {
    SimpleWeb::SocketServer<SimpleWeb::WS> server;
    server.io_service = context;
    server.config.address = "127.0.0.1";
    server.config.port = 0;
    server.start();
  }

  // The cancelled accept handler is still queued in the externally-owned
  // context. Running it after server destruction must be safe.
  context->run();
}
