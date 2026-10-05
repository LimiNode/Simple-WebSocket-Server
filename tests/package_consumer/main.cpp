#include <client_ws.hpp>

int main() {
  SimpleWeb::SocketClient<SimpleWeb::WS> client("localhost:80/");
  (void)client;
  return 0;
}
