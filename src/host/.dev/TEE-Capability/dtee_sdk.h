#include "distributed_tee.h"

#ifdef __TEE

extern "C" int ecall_proxy(const char *enclave_filename, uint32_t fid,
                           char *in_buf, int in_buf_size, char *out_buf,
                           int out_buf_size);
#else
inline int ecall_proxy(const char *enclave_filename, uint32_t fid, char *in_buf,
                       int in_buf_size, char *out_buf, int out_buf_size) {
  std::cout << "NO TEE" << std::endl;
  return 0;
}
#endif

auto get_enclave_version_mapping()
    -> std::unordered_map<std::string, std::string>;

inline PackedMigrateCallResult _Z_task_handler(std::string enclave_name,
                                               uint32_t function_id,
                                               std::vector<char> in_buf,
                                               std::vector<char> out_buf) {
  printf("fid: %d, in_buf_len: %lu, out_buf_len: %lu\n", function_id,
         in_buf.size(), out_buf.size());
  auto map = get_enclave_version_mapping();
  if (map.find(enclave_name) == map.end()) {
    printf("NO SUCH ENCLAVE: %s\n", enclave_name.c_str());
    return {.res = -1};
  }
  std::string enclave_filename = enclave_name + ENCLAVE_FILE_EXTENSION;
  int res = ecall_proxy(enclave_filename.c_str(), function_id, in_buf.data(),
                        in_buf.size(), out_buf.data(), out_buf.size());
  std::cout << "ECALL RES: " << res << std::endl;
  PackedMigrateCallResult result = {.res = res, .out_buf = out_buf};
  return result;
}

#ifdef __TEE
#define dtee_server_run(ctx)                                                   \
  if (ctx->config.side == SIDE::Server &&                                      \
      ctx->config.mode == MODE::ComputeNode) {                                 \
    publish_secure_function(ctx, _Z_task_handler);                             \
  }                                                                            \
  tee_server_run(ctx);
#else
#define dtee_server_run(ctx)
#endif
