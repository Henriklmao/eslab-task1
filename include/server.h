
/**
 * @file server.h
 * @brief Header file for the CommServer class.
 *
 * This file declares the CommServer class, which implements a communication
 * server for interacting with the inverted pendulum simulation. It utilizes
 * Boost.Asio for asynchronous I/O operations and Boost.Beast for handling HTTP
 * requests, responses, and WebSocket sessions. The CommServer class facilitates
 * communication with the simulation, allowing control and monitoring of
 * simulation parameters over network.
 *
 * @author Utkarsh Raj
 * @date 10-April-2024
 */

#include "boost/asio/ip/tcp.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/config.hpp>
#include <json.hpp>

#include "controller.h"
#include "simulator.h"
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

class WebSocketSession;
class HttpSession;

/**
 * @brief Class for managing communication frontend and simulation backend.
 *
 * The CommServer class implements a communication server that handles HTTP and
 * WebSocket requests to control and monitor the inverted pendulum simulation.
 * It listens for incoming connections, processes requests, and sends
 * corresponding responses.
 */
class CommServer {
  Simulator &sim; ///< Reference to the simulator object

  net::io_context ioc{1};                ///< io context required for all I/O
  net::steady_timer snapshot_timer{ioc}; ///< Timer for live snapshot pushes

  net::ip::address address{
      net::ip::make_address("0.0.0.0")}; ///< Binds on all interfaces
  unsigned short port{8000};             ///< Server Port
  tcp::acceptor
      acceptor; ///< The acceptor is used to listen for incoming connections
  std::mutex sessions_mutex;
  std::vector<std::weak_ptr<WebSocketSession>> websocket_sessions;

public:
  /**
   * @brief Constructor for CommServer.
   *
   * Initializes the CommServer with the specified simulator object and listens
   * for incoming connections on the specified IP address and port.
   *
   * @param sim Reference to the simulator object.
   */
  CommServer(Simulator &sim) : sim(sim), acceptor(ioc, {address, port}) {}
  /**
   * @brief Starts the communication server.
   *
   * This method starts the communication server in a separate thread.
   */

  void start_server();

private:
  friend class HttpSession;
  friend class WebSocketSession;

  /**
   * @brief Starts an asynchronous accept operation.
   */
  void do_accept();

  /**
   * @brief Starts periodic simulation snapshot broadcasts.
   */
  void start_snapshot_timer();

  /**
   * @brief Runs the communication server loop.
   *
   * This method continuously listens for incoming connections, accepts
   * them, and handles HTTP/WebSocket requests.
   */
  void run_server();
  /**
   * @brief Handles an incoming HTTP request.
   *
   * This method processes an already-read HTTP request and sends an appropriate
   * HTTP response.
   *
   * @param socket The socket for communicating with the client.
   * @param req The HTTP request to process.
   */
  http::response<http::string_body>
  handle_request(const http::request<http::string_body> &req);

  void register_websocket(const std::shared_ptr<WebSocketSession> &session);
  void unregister_websocket(const WebSocketSession *session);
  void broadcast_snapshot(const std::string &event = "");

  json make_sample_payload();
  json make_status_payload();
  json make_pid_payload();
  json make_params_payload();
  json make_snapshot_payload(const std::string &event = "");
  json apply_websocket_command(const json &command,
                               std::string &broadcast_event);
};
