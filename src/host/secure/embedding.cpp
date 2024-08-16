

#ifdef __cplusplus
extern "C" {
#endif
#include "distributed_face_recognition_u.h"
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

extern cc_enclave_t *g_enclave_context;
extern void z_create_enclave(const char*, bool is_proxy);
extern void z_destroy_enclave();

#ifdef __cplusplus
}
#endif

int img_recorder(char* arr, int id) {
  int retval;

  z_create_enclave("enclave.signed.so", false);

  cc_enclave_result_t __Z_res = __secure_img_recorder_impl(g_enclave_context, &retval , arr, id);
  if (__Z_res != CC_SUCCESS) {
    printf("Ecall enclave error\n");
    exit(-1);
  } 

  z_destroy_enclave();

  return retval;
}
int img_verifier(char* arr) {
  int retval;

  z_create_enclave("enclave.signed.so", false);

  cc_enclave_result_t __Z_res = __secure_img_verifier_impl(g_enclave_context, &retval , arr);
  if (__Z_res != CC_SUCCESS) {
    printf("Ecall enclave error\n");
    exit(-1);
  } 

  z_destroy_enclave();

  return retval;
}

