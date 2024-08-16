#pragma once
// namespace eprosima{
// namespace fastrtps {
// namespace rtps {
// namespace Time_t{
// void Time_t(int, unsigned int){}
// }
// }
// }
// }
#include "DDSServer.h"
#include "Softbus.h"
#include "Util.h"

#define MAPPING_FILE "mapping"
#define ENCLAVE_FILE_EXTENSION ".signed.so"

#ifdef __cplusplus
extern "C"
#endif
    int
    SM2_Verify(unsigned char *message,
               int len,
               unsigned char Px[],
               unsigned char Py[],
               unsigned char R[],
               unsigned char S[]);
typedef SoftbusServer TeeServer;
typedef SoftbusClient<> TeeClient;
enum class SIDE {
    Client,
    Server
};
enum class MODE {
    Normal,
    ComputeNode,
    Migrate,
    Transparent
};

struct DistributedTeeConfig {
    SIDE side;
    MODE mode;
    std::string name;
    std::string version;
};

struct DistributedTeeContext {
    DistributedTeeConfig config;
    // each server is responsible for one service
    std::vector<TeeServer *> servers;
    TeeClient *client = nullptr;
    int enclave_id = 0;

    // the original ecall function
    void *ecall_enclave;
    std::vector<char> sealed_shared_key;
    std::string shared_key;
};

template <typename Func>
void Z_publish_secure_function(DistributedTeeContext *context,
                               std::string name,
                               Func &&func)
{
    /* context->server->publish_service(name, std::forward<Func>(func)); */
    auto *server = new TeeServer;
    server->publish_service(name, std::forward<Func>(func));
    context->servers.push_back(server);
}

template <typename T> struct return_type;

template <typename R, typename... Args> struct return_type<R (*)(Args...)> {
    using type = R;
};

template <typename R, typename... Args> struct return_type<R(Args...)> {
    using type = R;
};

template <typename R, typename... Args> struct return_type<R (&)(Args...)> {
    using type = R;
};

template <typename R, typename... Args> struct return_type<R(Args...) const> {
    using type = R;
};

template <typename R, typename... Args>
struct return_type<std::function<R(Args...)>> {
    using type = R;
};

int distributed_tee_ecall_enclave(void *enclave,
                                  uint32_t function_id,
                                  const void *input_buffer,
                                  size_t input_buffer_size,
                                  void *output_buffer,
                                  size_t output_buffer_size,
                                  void *ms,
                                  const void *ocall_table);

extern DistributedTeeContext *g_current_dtee_context;

inline bool exist_local_tee()
{
    if (is_module_installed("penglai")) {
        return true;
    }
    return false;
}

/*
 * LookLocal: whether to look up for local tee, if true and there is a local
 * tee, the func will be executed locally
 */
template <bool LookLocal, typename Func, typename... Args>
typename return_type<Func>::type
Z_call_remote_secure_function(DistributedTeeContext *context,
                              std::string name,
                              Func &&func,
                              Args &&...args)
{
    if constexpr (LookLocal) {
        if (exist_local_tee()) {
            return func(std::forward<Args>(args)...);
        }
    }
    if (context->config.side == SIDE::Server) {
        // exit(-1);
        return func(std::forward<Args>(args)...);
    }

    if (context->config.mode == MODE::Normal) {
        return context->client->call_service<typename return_type<Func>::type>(
            name, ENCLAVE_UNRELATED, std::forward<Args>(args)...);
    }

    if (context->config.mode == MODE::Migrate) {
        return func(std::forward<Args>(args)...);
    }

    return {};
}

// interfaces:
#ifdef __TEE
DistributedTeeContext *
init_distributed_tee_context(DistributedTeeConfig config);
void destroy_distributed_tee_context(DistributedTeeContext *context);
#else
#define init_distributed_tee_context(...) 0
#define destroy_distributed_tee_context(...)
#endif

#ifdef __TEE
#define publish_secure_function(context, func)                                 \
    {                                                                          \
        Z_publish_secure_function(context, #func, func);                       \
    }
#define call_remote_secure_function(context, func, ...)                        \
    Z_call_remote_secure_function<false>(context, #func, func, __VA_ARGS__)
void tee_server_run(DistributedTeeContext *context);
#define call_distributed_secure_function(context, func, ...)                   \
    Z_call_remote_secure_function<true>(context, #func, func, __VA_ARGS__)
#else
#define publish_secure_function(context, func)                                 
#define call_remote_secure_function(context, func, ...)                        
#define tee_server_run(context)
#define call_distributed_secure_function(context, func, ...)                   
#endif
