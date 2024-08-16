#ifndef SOFTBUS_H
#define SOFTBUS_H
/*
 * Copyright (c) 2023 IPADS, Shanghai Jiao Tong University.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "DDSClient.h"
#include "DDSServer.h"
#include "Serialization.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#define NET_CALL_TIMEOUT 100

template <typename T> struct type_xx {
  typedef T type;
};

template <> struct type_xx<void> {
  typedef int8_t type;
};

template <typename T> class value_t {
public:
  typedef typename type_xx<T>::type type;
  typedef std::string msg_type;
  typedef uint16_t code_type;

  value_t() {
    code_ = 0;
    msg_.clear();
  }
  bool valid() { return (code_ == 0 ? true : false); }
  int error_code() { return code_; }
  std::string error_msg() { return msg_; }
  type val() { return val_; }

  void set_val(const type &val) { val_ = val; }
  void set_code(code_type code) { code_ = code; }
  void set_msg(msg_type msg) { msg_ = msg; }

  friend Serialization &operator>>(Serialization &in, value_t<T> &d) {
    in >> d.code_ >> d.msg_;
    if (d.code_ == 0) {
      in >> d.val_;
    }
    return in;
  }
  friend Serialization &operator<<(Serialization &out, value_t<T> d) {
    out << d.code_ << d.msg_ << d.val_;
    return out;
  }

private:
  code_type code_;
  msg_type msg_;
  type val_;
};

class SoftbusServer {
public:
  SoftbusServer() {}

  template <typename ServerProxy = DDSServer> // ServerProxy must implement:
                                              // init, serve, publish_service
  ServerProxy *run() {
    ServerProxy *server_proxy = new ServerProxy;
    server_proxy->init();
    server_proxy->publish_service(service_names[0]);
    /* for (const auto &name : service_names) { */
    /*   server.publish_service(name); */
    /* } */
    server_proxy->serve(this);
    return server_proxy;
  }

  // call function template class using tuple as parameter
  template <typename Function, typename Tuple, std::size_t... Index>
  decltype(auto) invoke_impl(Function &&func, Tuple &&t,
                             std::index_sequence<Index...>) {
    return func(std::get<Index>(std::forward<Tuple>(t))...);
  }

  template <typename Function, typename Tuple>
  decltype(auto) invoke(Function &&func, Tuple &&t) {
    constexpr auto size =
        std::tuple_size<typename std::decay<Tuple>::type>::value;
    return invoke_impl(std::forward<Function>(func), std::forward<Tuple>(t),
                       std::make_index_sequence<size>{});
  }

  // used when the return value is void
  template <typename R, typename F, typename ArgsTuple>
  typename std::enable_if<std::is_same<R, void>::value,
                          typename type_xx<R>::type>::type
  call_helper(F f, ArgsTuple args) {
    invoke(f, args);
    return 0;
  }

  template <typename R, typename F, typename ArgsTuple>
  typename std::enable_if<!std::is_same<R, void>::value,
                          typename type_xx<R>::type>::type
  call_helper(F f, ArgsTuple args) {
    return invoke(f, args);
  }

  template <typename R, typename... Params>
  void service_proxy_(std::function<R(Params... ps)> service,
                      Serialization *serialization, const char *data, int len) {
    using args_type = std::tuple<typename std::decay<Params>::type...>;
    //
    Serialization param_serialization(StreamBuffer(data, len));
    constexpr auto N =
        std::tuple_size<typename std::decay<args_type>::type>::value;
    args_type args =
        param_serialization.get_tuple<args_type>(std::make_index_sequence<N>{});

    typename type_xx<R>::type r = call_helper<R>(service, args);
    (*serialization) << r;
  }

  template <typename R, typename... Params>
  void service_proxy_(R (*service)(Params...), Serialization *serialization,
                      const char *data, int len) {
    service_proxy_(std::function<R(Params...)>(service), serialization, data,
                   len);
  }

  template <typename S>
  void service_proxy(S service, Serialization *serialization, const char *data,
                     int len) {
    service_proxy_(service, serialization, data, len);
  }

  template <typename S>
  void publish_service(std::string service_name, S service) {
    service_to_func[service_name] = std::bind(
        &SoftbusServer::service_proxy<S>, this, service, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3);
    service_names.push_back(service_name);
  }

  Serialization *call_(std::string name, const char *data, int len) {
    Serialization *ds = new Serialization();
    if (service_to_func.find(name) == service_to_func.end()) {
      printf("Service %s not found\n", name.c_str());
      return ds;
    }
    auto fun = service_to_func[name];
    fun(ds, data, len);
    ds->reset();
    return ds;
  }

  typedef void (*ServiceFunc)(Serialization *, const char *, int);
  void add_service_to_func(std::string service_name, ServiceFunc func) {
    service_to_func[service_name] = func;
  }

private:
  std::map<std::string, std::function<void(Serialization *, const char *, int)>>
      service_to_func;
  std::vector<std::string> service_names;
};

template <typename ClientProxy = DDSClient> // ClientProxy must implement: init,
                                            // call_service, isReady
class SoftbusClient {
public:
  SoftbusClient() {}

  ~SoftbusClient() {}

  template <typename Arg>
  void package_params(Serialization &ds, const Arg &arg) {
    ds << arg;
  }

  template <typename Arg, typename... Args>
  void package_params(Serialization &ds, const Arg &arg, const Args &...args) {
    ds << arg;
    package_params(ds, args...);
  }

  template <typename V> V net_call(Serialization &ds, int enclave_id = -1) {
    /* std::cout << "NET CALL " << enclave_id << std::endl; */
    /* Serialization *result = m_client.call_service(&ds, enclave_id); */
    /* V val; */
    /* (*result) >> val; */
    /* return val; */
  }

  template <typename V, typename... Params>
  V call_service(std::string service_name, int enclave_id, Params... params) {
    if (m_client_map.count(service_name) <= 0) {
      m_client_map.insert({service_name, std::make_unique<ClientProxy>()});
      m_client_map[service_name]->init(service_name);
    }
    printf("CALL SERVICE: %s\n", service_name.c_str());

    Serialization ds;
    ds << service_name;
    package_params(ds, params...);

    while (!m_client_map[service_name]->isReady()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(NET_CALL_TIMEOUT));
    }

    Serialization *result =
        m_client_map[service_name]->call_service(&ds, enclave_id);
    V val;
    (*result) >> val;
    return val;
  }

  ClientProxy &get_client_proxy(const std::string &service) {
    return *m_client_map[service];
  }

private:
  std::unordered_map<std::string, std::unique_ptr<ClientProxy>> m_client_map;
};

#endif
