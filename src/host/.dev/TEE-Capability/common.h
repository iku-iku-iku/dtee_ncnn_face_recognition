#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char in_char;
typedef char out_char;

#ifdef __TEE
extern "C" void eapp_print(const char *, ...);
#else
#define eapp_print printf
#endif

#define TEE_CHECK(x)                                                           \
  if (!(x)) {                                                                  \
    eapp_print("TEE_CHECK failed: %s in %s:%s\n", #x, __FILE__, __LINE__);     \
    return -1;                                                                 \
  }

#define TEE_ASSERT(x, ...)                                                     \
  if (!(x)) {                                                                  \
    eapp_print(__VA_ARGS__);                                                   \
  }

typedef struct _enclave_sealed_data_t {
  uint32_t data_body_len;
  uint8_t reserved[16];
  uint8_t data_body[];
} cc_enclave_sealed_data_t;
#ifndef __TEE
namespace {

inline uint32_t cc_enclave_get_sealed_data_size(const uint32_t add_len,
                                                const uint32_t seal_data_len) {
  return sizeof(cc_enclave_sealed_data_t) + seal_data_len + add_len;
}

inline uint32_t cc_enclave_seal_data(uint8_t *seal_data, uint32_t seal_data_len,
                                     cc_enclave_sealed_data_t *sealed_data,
                                     uint32_t sealed_data_len,
                                     uint8_t *additional_text,
                                     uint32_t additional_text_len) {
  memcpy(sealed_data->reserved, &seal_data_len, sizeof(uint32_t));
  memcpy(sealed_data->reserved + sizeof(uint32_t), &additional_text_len,
         sizeof(uint32_t));

  uint8_t *p = sealed_data->data_body;
  for (int i = 0; i < seal_data_len; i++) {
    *p++ = 0xff ^ seal_data[i];
  }
  for (int i = 0; i < additional_text_len; i++) {
    *p++ = 0xfe ^ additional_text[i];
  }
  return 0;
}

inline uint32_t cc_enclave_unseal_data(cc_enclave_sealed_data_t *sealed_data,
                                       uint8_t *decrypted_data,
                                       uint32_t *decrypted_data_len,
                                       uint8_t *additional_text,
                                       uint32_t *additional_text_len) {
  uint8_t *p = sealed_data->data_body;
  for (int i = 0; i < *decrypted_data_len; i++) {
    decrypted_data[i] = 0xff ^ *p++;
  }
  for (int i = 0; i < *additional_text_len; i++) {
    additional_text[i] = 0xfe ^ *p++;
  }
  return 0;
}

inline uint32_t
cc_enclave_get_add_text_size(const cc_enclave_sealed_data_t *sealed_data) {

  uint32_t add_text_len;
  memcpy(&add_text_len, sealed_data->reserved + sizeof(uint32_t),
         sizeof(uint32_t));
  return add_text_len;
}

inline uint32_t cc_enclave_get_encrypted_text_size(
    const cc_enclave_sealed_data_t *sealed_data) {
  uint32_t encrypted_text_len;
  memcpy(&encrypted_text_len, sealed_data->reserved, sizeof(uint32_t));
  return encrypted_text_len;
}

} // namespace
#else

extern "C" {

extern uint32_t cc_enclave_get_sealed_data_size(const uint32_t add_len,
                                                const uint32_t seal_data_len);

extern uint32_t cc_enclave_seal_data(uint8_t *seal_data, uint32_t seal_data_len,
                                     cc_enclave_sealed_data_t *sealed_data,
                                     uint32_t sealed_data_len,
                                     uint8_t *additional_text,
                                     uint32_t additional_text_len);

extern uint32_t cc_enclave_unseal_data(cc_enclave_sealed_data_t *sealed_data,
                                       uint8_t *decrypted_data,
                                       uint32_t *decrypted_data_len,
                                       uint8_t *additional_text,
                                       uint32_t *additional_text_len);

extern uint32_t
cc_enclave_get_add_text_size(const cc_enclave_sealed_data_t *sealed_data);

extern uint32_t
cc_enclave_get_encrypted_text_size(const cc_enclave_sealed_data_t *sealed_data);
}
#endif

namespace {

char additional_text[] = "add mac text";
inline int seal_data_inplace(char *buf, int buf_len, int data_len) {
  uint32_t ret;
  char *plaintext = buf;
  /* long data_len = plaintext_len; */
  long add_len = strlen((const char *)additional_text) + 1;

  uint32_t sealed_data_len = cc_enclave_get_sealed_data_size(add_len, data_len);
  if (sealed_data_len > buf_len) {
    eapp_print("sealed_data_len > buf_len (%d > %d)\n", sealed_data_len,
               buf_len);
    return -1;
  }

  if (sealed_data_len == UINT32_MAX)
    return -1;

  cc_enclave_sealed_data_t *sealed_data =
      (cc_enclave_sealed_data_t *)malloc(sealed_data_len);
  if (sealed_data == NULL)
    return -1;

  ret = cc_enclave_seal_data((uint8_t *)plaintext, data_len, sealed_data,
                             sealed_data_len, (uint8_t *)additional_text,
                             add_len);
  memcpy(buf, (const char *)sealed_data, sealed_data_len);
  return sealed_data_len;
}

inline int unseal_data_inplace(char *buf, int buf_len) {
  char *sealed_data = buf;
  uint32_t add_len = cc_enclave_get_add_text_size(
      (const cc_enclave_sealed_data_t *)sealed_data);
  uint32_t data_len = cc_enclave_get_encrypted_text_size(
      (const cc_enclave_sealed_data_t *)sealed_data);

  char *decrypted_data = (char *)malloc(data_len);
  char *demac_data = (char *)malloc(add_len);

  cc_enclave_unseal_data((cc_enclave_sealed_data_t *)sealed_data,
                         (uint8_t *)decrypted_data, &data_len,
                         (uint8_t *)demac_data, &add_len);

  if (strcmp(demac_data, additional_text) != 0) {
    return -1;
  }

  memcpy(buf, decrypted_data, data_len);
  return data_len;
}
} // namespace
