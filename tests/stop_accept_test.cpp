#include "assert.hpp"
#include "server_ws.hpp"
#include <memory>

class CountingServer : public SimpleWeb::SocketServer<SimpleWeb::WS> {
public:
  int accept_calls = 0;

protected:
  void accept(const std::shared_ptr<SimpleWeb::ScopeRunner> &runner) override {
    ++accept_calls;
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

  server.stop_accept();
  context->run();
  ASSERT(server.accept_calls == 1);
}
