Simple-WebSocket-Server
=================

A very simple, fast, multithreaded, platform independent WebSocket (WS) and WebSocket Secure (WSS) server and client library implemented using C++11, Asio (both Boost.Asio and standalone Asio can be used) and OpenSSL. Created to be an easy way to make WebSocket endpoints in C++.

This repository is a maintained downstream continuation of eidheim/Simple-WebSocket-Server. It preserves the original project history while providing compatibility fixes, CI, releases, and integration maintenance for current Asio, Boost.Asio, OpenSSL, and Kurlyk.

## Versioning

Tags with the `-ln.N` suffix are LimiNode-maintained downstream releases.
They are not releases published by the original upstream project.
The `v2.0.3-ln.1` release is represented as CMake package version `2.0.3.1`.

### Downstream compatibility notes

Incoming WebSocket messages are limited to 16 MiB by default, and HTTP upgrade
buffers are limited to 16 KiB. Applications requiring larger messages or
handshake headers must set `Config::max_message_size` or
`Config::max_handshake_size` explicitly.

Applications must keep the owning server or client alive until all callbacks
that have already entered the library have returned. Calling `stop()` from a
callback is supported as a cancellation request for future handlers, but it
is not a quiescence barrier for callbacks already running on other threads.
Before destroying the owner, an external caller must complete the normal
`stop()`/destructor barrier. Destroying the owner synchronously from inside
its own callback is not a supported ownership pattern in C++.

See https://gitlab.com/eidheim/Simple-Web-Server for an easy way to make REST resources available from C++ applications. Also, feel free to check out the new C++ IDE supporting C++11/14/17: https://gitlab.com/cppit/jucipp. 

### Features

* RFC 6455 mostly supported: text/binary frames, fragmented messages, ping-pong, connection close with status and reason.
* Asynchronous message handling
* Thread pool if needed
* Platform independent
* WebSocket Secure support
* Timeouts, if any of SocketServer::timeout_request and SocketServer::timeout_idle are >0 (default: SocketServer::timeout_request=5 seconds, and SocketServer::timeout_idle=0 seconds; no timeout on idle connections)
* Simple way to add WebSocket endpoints using regex for path, and anonymous functions
* An easy to use WebSocket and WebSocket Secure client library
* C++ bindings to the following OpenSSL methods: Base64, MD5, SHA1, SHA256 and SHA512 (found in crypto.hpp)

### Usage

See [ws_examples.cpp](ws_examples.cpp) or [wss_examples.cpp](wss_examples.cpp) for example usage. 

### Dependencies

* Boost.Asio or standalone Asio
* OpenSSL libraries

### Compile

Compile with a C++11 supported compiler:

```sh
mkdir build
cd build
cmake ..
make
cd ..
```

#### Run server and client examples

### WS

```sh
./build/ws_examples
```

### WSS

Before running the WSS-examples, an RSA private key (server.key) and an SSL certificate (server.crt) must be created.

Then:
```
./build/wss_examples
```
