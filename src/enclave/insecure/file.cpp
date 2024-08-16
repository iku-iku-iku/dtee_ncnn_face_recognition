
#ifdef __cplusplus
extern "C" {
#endif
#include "distributed_face_recognition_t.h"
#include "enclave.h"
#ifdef __cplusplus
}
#endif
#include "string.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

extern "C" int write_file(char* in_filename, int in_filename_len, char* in_content, int in_content_len) {
  int retval;

  cc_enclave_result_t __Z_res = __insecure_write_file_impl(&retval , in_filename, in_filename_len, in_content, in_content_len);
  if (__Z_res != CC_SUCCESS) {
    printf("Ecall enclave error\n");
    exit(-1);
  }

  return retval;
}
extern "C" int get_emb_list(char* out_list) {
  int retval;

  cc_enclave_result_t __Z_res = __insecure_get_emb_list_impl(&retval , out_list);
  if (__Z_res != CC_SUCCESS) {
    printf("Ecall enclave error\n");
    exit(-1);
  }

  return retval;
}
extern "C" int read_file(char* in_filename, int in_filename_len, char* out_content, int out_content_len) {
  int retval;

  cc_enclave_result_t __Z_res = __insecure_read_file_impl(&retval , in_filename, in_filename_len, out_content, out_content_len);
  if (__Z_res != CC_SUCCESS) {
    printf("Ecall enclave error\n");
    exit(-1);
  }

  return retval;
}
