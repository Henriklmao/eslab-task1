
/**
 * @file server.cpp
 * @brief Implementation file for the CommServer class.
 *
 * This file contains the implementation of the methods declared in the server.h
 * header file. It provides the functionality for handling incoming HTTP and
 * WebSocket requests and managing communication with the inverted pendulum
 * simulation.
 *
 * @author Utkarsh Raj
 * @date 10-April-2024
 */

#include "server.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <utility>

namespace {
void set_cors_headers(http::response<http::string_body> &res) {
  res.set(boost::beast::http::field::access_control_allow_origin, "*");
  res.set(boost::beast::http::field::access_control_allow_methods,
          "GET, POST, OPTIONS");
  res.set(boost::beast::http::field::access_control_allow_headers,
          "Content-Type");
}

http::response<http::string_body>
make_json_response(const json &payload, unsigned version, bool keep_alive,
                   http::status status = http::status::ok) {
  http::response<http::string_body> res{status, version};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "application/json");
  set_cors_headers(res);
  res.keep_alive(keep_alive);
  res.body() = payload.dump();
  res.prepare_payload();
  return res;
}

http::response<http::string_body>
make_text_response(const std::string &body, unsigned version, bool keep_alive,
                   http::status status = http::status::ok) {
  http::response<http::string_body> res{status, version};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "text/plain");
  set_cors_headers(res);
  res.keep_alive(keep_alive);
  res.body() = body;
  res.prepare_payload();
  return res;
}
} // namespace

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
  websocket::stream<tcp::socket> ws;
  beast::flat_buffer buffer;
  CommServer &server;
  std::deque<std::string> write_queue;
  bool closed{false};
  bool registered{false};

public:
  WebSocketSession(CommServer &server, tcp::socket socket)
      : ws(std::move(socket)), server(server) {}

  ~WebSocketSession() { close(); }

  void run(http::request<http::string_body> req) {
    ws.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws.set_option(
        websocket::stream_base::decorator([](websocket::response_type &res) {
          res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
          res.set(http::field::access_control_allow_origin, "*");
        }));

    auto self = shared_from_this();
    ws.async_accept(req, [self](beast::error_code ec) { self->on_accept(ec); });
  }

  void send(std::string message) {
    auto self = shared_from_this();
    net::post(ws.get_executor(),
              [self, message = std::move(message)]() mutable {
                if (self->closed) {
                  return;
                }

                const bool write_in_progress = !self->write_queue.empty();
                self->write_queue.push_back(std::move(message));

                if (!write_in_progress) {
                  self->do_write();
                }
              });
  }

private:
  void on_accept(beast::error_code ec) {
    if (ec) {
      return;
    }

    registered = true;
    server.register_websocket(shared_from_this());
    send(server.make_snapshot_payload().dump());
    do_read();
  }

  void do_read() {
    if (closed) {
      return;
    }

    buffer.consume(buffer.size());
    auto self = shared_from_this();
    ws.async_read(buffer, [self](beast::error_code ec, std::size_t) {
      self->on_read(ec);
    });
  }

  void on_read(beast::error_code ec) {
    if (ec) {
      close();
      return;
    }

    std::string broadcast_event;
    json response;

    try {
      const json command = json::parse(beast::buffers_to_string(buffer.data()));
      response = server.apply_websocket_command(command, broadcast_event);

      if (command.contains("id")) {
        response["id"] = command["id"];
      }
    } catch (const std::exception &error) {
      response["type"] = "error";
      response["message"] = error.what();
    }

    send(response.dump());

    if (!broadcast_event.empty()) {
      server.broadcast_snapshot(broadcast_event);
    }

    do_read();
  }

  void do_write() {
    if (closed || write_queue.empty()) {
      return;
    }

    ws.text(true);
    auto self = shared_from_this();
    ws.async_write(
        net::buffer(write_queue.front()),
        [self](beast::error_code ec, std::size_t) { self->on_write(ec); });
  }

  void on_write(beast::error_code ec) {
    if (ec) {
      close();
      return;
    }

    write_queue.pop_front();

    if (!write_queue.empty()) {
      do_write();
    }
  }

  void close() {
    if (closed) {
      return;
    }

    closed = true;

    if (registered) {
      registered = false;
      server.unregister_websocket(this);
    }
  }
};

class HttpSession : public std::enable_shared_from_this<HttpSession> {
  tcp::socket socket;
  beast::flat_buffer buffer;
  http::request<http::string_body> req;
  CommServer &server;

public:
  HttpSession(CommServer &server, tcp::socket socket)
      : socket(std::move(socket)), server(server) {}

  void run() {
    auto self = shared_from_this();
    http::async_read(
        socket, buffer, req,
        [self](beast::error_code ec, std::size_t) { self->on_read(ec); });
  }

private:
  void on_read(beast::error_code ec) {
    if (ec) {
      return;
    }

    if (websocket::is_upgrade(req)) {
      if (req.target() == "/ws") {
        std::make_shared<WebSocketSession>(server, std::move(socket))
            ->run(std::move(req));
        return;
      }

      write_response(make_text_response("Invalid WebSocket endpoint",
                                        req.version(), req.keep_alive(),
                                        http::status::bad_request));
      return;
    }

    write_response(server.handle_request(req));
  }

  void write_response(http::response<http::string_body> res) {
    auto response =
        std::make_shared<http::response<http::string_body>>(std::move(res));
    auto self = shared_from_this();
    http::async_write(socket, *response,
                      [self, response](beast::error_code, std::size_t) {
                        beast::error_code ec;
                        self->socket.shutdown(tcp::socket::shutdown_send, ec);
                      });
  }
};

void CommServer::start_server() {
  std::thread comm_thread(&CommServer::run_server, this);
  comm_thread.join();
}

void CommServer::run_server() {
  do_accept();
  start_snapshot_timer();
  ioc.run();
}

void CommServer::do_accept() {
  acceptor.async_accept([this](beast::error_code ec, tcp::socket socket) {
    if (!ec) {
      std::make_shared<HttpSession>(*this, std::move(socket))->run();
    }

    do_accept();
  });
}

void CommServer::start_snapshot_timer() {
  snapshot_timer.expires_after(std::chrono::milliseconds(100));
  snapshot_timer.async_wait([this](beast::error_code ec) {
    if (ec) {
      return;
    }

    if (sim.g_start.load() && !sim.g_pause.load()) {
      broadcast_snapshot();
    }

    start_snapshot_timer();
  });
}

json CommServer::make_sample_payload() {
  json sample;
  {
    std::lock_guard<std::mutex> lock(sim.g_start_mutex);
    sample["time"] = std::round(sim.T * 100) / 100;
    sample["x"] = std::round(sim.x[0] * 100) / 100;
    sample["theta"] = sim.theta[sim.i];
    sample["x_dot"] = sim.x_dot[0];
    sample["theta_dot"] = sim.theta_dot[0];
    sample["x_dot_dot"] = sim.x_dot_dot[0];
    sample["theta_dot_dot"] = sim.theta_dot_dot[0];
    sample["force"] = sim.F;
    sample["energy"] = sim.E;
    sample["ref"] = sim.m_params.ref_angle;
    sample["pause"] = sim.g_pause.load();
  }
  return sample;
}

json CommServer::make_status_payload() {
  json status;
  status["pause"] = sim.g_pause.load();
  status["start"] = sim.g_start.load();
  return status;
}

json CommServer::make_pid_payload() {
  json pid;
  {
    std::lock_guard<std::mutex> lock(sim.g_start_mutex);
    const auto params = sim.m_controller->get_params();
    pid["kp"] = params.kp;
    pid["kd"] = params.kd;
    pid["ki"] = params.ki;
  }
  return pid;
}

json CommServer::make_params_payload() {
  json params;
  {
    std::lock_guard<std::mutex> lock(sim.g_start_mutex);
    params["ref"] = sim.m_params.ref_angle;
    params["delay"] = sim.m_params.delay;
    params["jitter"] = sim.m_params.jitter;
  }
  return params;
}

json CommServer::make_snapshot_payload(const std::string &event) {
  json snapshot;
  snapshot["type"] = "snapshot";
  if (!event.empty()) {
    snapshot["event"] = event;
  }
  snapshot["sample"] = make_sample_payload();
  snapshot["status"] = make_status_payload();
  snapshot["pid"] = make_pid_payload();
  snapshot["params"] = make_params_payload();
  return snapshot;
}

json CommServer::apply_websocket_command(const json &command,
                                         std::string &broadcast_event) {
  const std::string type = command.value("type", "getSnapshot");
  broadcast_event.clear();

  if (type == "getSnapshot") {
    return make_snapshot_payload();
  }

  if (type == "getStatus") {
    json response;
    response["type"] = "status";
    response["status"] = make_status_payload();
    return response;
  }

  if (type == "getSample") {
    json response;
    response["type"] = "sample";
    response["sample"] = make_sample_payload();
    return response;
  }

  if (type == "getPid") {
    json response;
    response["type"] = "pid";
    response["pid"] = make_pid_payload();
    return response;
  }

  if (type == "getParams") {
    json response;
    response["type"] = "params";
    response["params"] = make_params_payload();
    return response;
  }

  if (type == "setPid") {
    const json pid = command.at("pid");
    {
      std::lock_guard<std::mutex> lock(sim.g_start_mutex);
      const auto current = sim.m_controller->get_params();
      sim.m_controller->update_params(pid.value("kp", current.kp),
                                      pid.value("ki", current.ki),
                                      pid.value("kd", current.kd));
    }
    broadcast_event = "pid";
    return make_snapshot_payload(broadcast_event);
  }

  if (type == "setParams") {
    const json params = command.at("params");
    {
      std::lock_guard<std::mutex> lock(sim.g_start_mutex);
      sim.update_params(params.value("ref", sim.m_params.ref_angle),
                        params.value("delay", sim.m_params.delay),
                        params.value("jitter", sim.m_params.jitter));
    }
    broadcast_event = "params";
    return make_snapshot_payload(broadcast_event);
  }

  if (type == "reset") {
    {
      std::lock_guard<std::mutex> lock(sim.g_start_mutex);
      sim.reset_simulator();
    }
    broadcast_event = "reset";
    return make_snapshot_payload(broadcast_event);
  }

  if (type == "toggleStartStop") {
    if (!sim.g_start.load()) {
      std::lock_guard<std::mutex> start_lock(sim.g_start_mutex);
      sim.g_start = true;
      sim.g_start_cv.notify_one();
    }
    {
      std::lock_guard<std::mutex> lock(sim.g_pause_mutex);
      sim.g_pause = !sim.g_pause;
      sim.g_pause_cv.notify_one();
    }
    broadcast_event = "startstop";
    return make_snapshot_payload(broadcast_event);
  }

  throw std::invalid_argument("Unknown WebSocket command type: " + type);
}

void CommServer::register_websocket(
    const std::shared_ptr<WebSocketSession> &session) {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  websocket_sessions.erase(
      std::remove_if(websocket_sessions.begin(), websocket_sessions.end(),
                     [](const std::weak_ptr<WebSocketSession> &candidate) {
                       return candidate.expired();
                     }),
      websocket_sessions.end());
  websocket_sessions.push_back(session);
}

void CommServer::unregister_websocket(const WebSocketSession *session) {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  websocket_sessions.erase(
      std::remove_if(
          websocket_sessions.begin(), websocket_sessions.end(),
          [session](const std::weak_ptr<WebSocketSession> &candidate) {
            const auto shared = candidate.lock();
            return !shared || shared.get() == session;
          }),
      websocket_sessions.end());
}

void CommServer::broadcast_snapshot(const std::string &event) {
  const std::string message = make_snapshot_payload(event).dump();
  std::vector<std::shared_ptr<WebSocketSession>> sessions;

  {
    std::lock_guard<std::mutex> lock(sessions_mutex);
    websocket_sessions.erase(
        std::remove_if(websocket_sessions.begin(), websocket_sessions.end(),
                       [](const std::weak_ptr<WebSocketSession> &candidate) {
                         return candidate.expired();
                       }),
        websocket_sessions.end());

    sessions.reserve(websocket_sessions.size());
    for (const auto &candidate : websocket_sessions) {
      if (auto session = candidate.lock()) {
        sessions.push_back(std::move(session));
      }
    }
  }

  for (const auto &session : sessions) {
    session->send(message);
  }
}

http::response<http::string_body>
CommServer::handle_request(const http::request<http::string_body> &req) {

  if (req.method() == http::verb::get) {
    if (req.target() == "/sim") {
      return make_json_response(make_sample_payload(), req.version(),
                                req.keep_alive());
    }
    if (req.target() == "/status") {
      return make_json_response(make_status_payload(), req.version(),
                                req.keep_alive());
    }
    if (req.target() == "/pid") {
      return make_json_response(make_pid_payload(), req.version(),
                                req.keep_alive());
    }
    if (req.target() == "/params") {
      return make_json_response(make_params_payload(), req.version(),
                                req.keep_alive());
    }
  }

  if (req.method() == http::verb::post) {
    std::string broadcast_event;

    if (req.target() == "/pid") {
      json pid = json::parse(req.body());
      std::lock_guard<std::mutex> lock(sim.g_start_mutex);
      sim.m_controller->update_params(pid["kp"], pid["ki"], pid["kd"]);
      broadcast_event = "pid";
    } else if (req.target() == "/reset") {
      std::lock_guard<std::mutex> lock(sim.g_start_mutex);
      sim.reset_simulator();
      broadcast_event = "reset";
    } else if (req.target() == "/startstop") {
      if (!sim.g_start.load()) {
        std::lock_guard<std::mutex> start_lock(sim.g_start_mutex);
        sim.g_start = true;
        sim.g_start_cv.notify_one();
      }
      std::lock_guard<std::mutex> lock(sim.g_pause_mutex);
      sim.g_pause = !sim.g_pause;
      sim.g_pause_cv.notify_one();
      broadcast_event = "startstop";
    } else if (req.target() == "/params") {
      json params = json::parse(req.body());
      std::lock_guard<std::mutex> lock(sim.g_start_mutex);
      sim.update_params(params["ref"], params["delay"], params["jitter"]);
      broadcast_event = "params";
    } else {
      return make_text_response("Invalid request-target", req.version(),
                                req.keep_alive(), http::status::bad_request);
    }

    broadcast_snapshot(broadcast_event);

    return make_text_response("Accepted", req.version(), req.keep_alive());
  }

  if (req.method() == http::verb::options) {
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    set_cors_headers(res);
    res.keep_alive(req.keep_alive());
    res.prepare_payload();
    return res;
  }

  return make_text_response("Invalid request-target", req.version(),
                            req.keep_alive(), http::status::bad_request);
}
