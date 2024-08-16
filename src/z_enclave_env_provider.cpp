

#include <TEE-Capability/distributed_tee.h>
#include <elf.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "distributed_face_recognition_u.h"
#include "enclave.h"
#define PRIVATE_KEY_SIZE 32
#define PUBLIC_KEY_SIZE 64
#define HASH_SIZE 32
#define SIGNATURE_SIZE 64

extern ocall_enclave_table_t ocall_table;

extern const char* g_forced_enclave_path;

#define NONCE 12345
#define ROUND_TO(x, align) (((x) + ((align)-1)) & ~((align)-1))
extern "C"
{
#include <miracl/miracl.h>
#include <miracl/mirdef.h>
#include <SM4_Enc.h>
    extern std::vector<char> (*get_report_func)(const char* enclave_path);
    //extern bool (*is_report_valid_func)(void* report, const char* enclave_path);
    extern bool (*is_report_valid_func)(const void *report,
                                 const void *pub_key,
                                 const void *pub_key_signature,
                                 const char *enclave_path);
 
    extern std::pair<std::string, std::string> (*make_key_pair_func)();
    extern std::string (*make_shared_key_func)(std::string pri_key,
                                               std::string in_pub_key);
    extern void (*sm4_encrypt)(const unsigned char *key,
                        unsigned char *buf,
                        int buf_len);
    extern void (*sm4_decrypt)(const unsigned char *key,
                        unsigned char *buf,
                        int buf_len);
#define SM2_WORDSIZE 8
#define SM2_NUMBITS 256
#define SM2_NUMWORD (SM2_NUMBITS / SM2_WORDSIZE)
}

extern "C"
{
    typedef struct
    {
        unsigned int state[8];
        unsigned int length;
        unsigned int curlen;
        unsigned char buf[64];
    } SM3_STATE;
    void SM3_init(SM3_STATE* md);
    void SM3_process(SM3_STATE* md, unsigned char buf[], int len);
    void SM3_done(SM3_STATE* md, unsigned char* hash);
}
void update_enclave_hash(void* hash, uintptr_t nonce_arg)
{
    SM3_STATE hash_ctx;
    uintptr_t nonce = nonce_arg;

    SM3_init(&hash_ctx);
    SM3_process(&hash_ctx, (unsigned char*)(hash), HASH_SIZE);
    SM3_process(&hash_ctx, (unsigned char*)(&nonce), sizeof(uintptr_t));
    SM3_done(&hash_ctx, (unsigned char*)hash);
}

bool is_same_bytes(char* p1, char* p2, int len)
{
    for (int i = 0; i < len; i++) {
        if (p1[i] != p2[i]) {
            return false;
        }
    }
    return true;
}

void printHex(unsigned char* c, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        printf("0x%02X, ", c[i]);
        if ((i % 4) == 3) printf(" ");

        if ((i % 16) == 15) printf("\n");
    }
    if ((i % 16) != 0) printf("\n");
}

typedef struct _enclave_css_t
{                                                /* 160 bytes */
    unsigned char enclave_hash[HASH_SIZE];       /* (32) */
    unsigned char signature[SIGNATURE_SIZE];     /* (64) */
    unsigned char user_pub_key[PUBLIC_KEY_SIZE]; /* (64) */
} enclave_css_t;

int get_meta_property(unsigned char* elf_ptr, unsigned long size,
                      unsigned long* meta_offset, unsigned long* meta_blocksize)
{
    Elf64_Ehdr elf_hdr;
    Elf64_Shdr* shdr;
    int i;
    bool found = false;

    memcpy(&elf_hdr, elf_ptr, sizeof(Elf64_Ehdr));
    shdr = (Elf64_Shdr*)(elf_ptr + elf_hdr.e_shoff);
    const char* shstrtab =
        (char*)(elf_ptr + (shdr + elf_hdr.e_shstrndx)->sh_offset);

    /* Loader section */
    for (i = 0; i < elf_hdr.e_shnum; i++, shdr++) {
        if (!strcmp(shstrtab + shdr->sh_name, ".note.penglaimeta")) {
            found = true;
            break;
        }
    }
    if (found == false) {
        printf(
            "ERROR: The enclave image should have '.note.penglaimeta' "
            "section\n");
        return -1;
    }
    /* We require that enclaves should have .note.penglaimeta section to store
     * the metadata information We limit this section is used for metadata only
     * and ISV should not extend this section.
     *
     * .note.penglaimeta layout:
     *
     * |  namesz         |
     * |  metadata size  |
     * |  type           |
     * |  name           |
     * |  metadata       |
     */

    Elf64_Nhdr* note = (Elf64_Nhdr*)(elf_ptr + shdr->sh_offset);
    if (note == NULL) {
        printf("ERROR: Nhdr is NULL\n");
        return -1;
    }

    if (shdr->sh_size !=
        ROUND_TO(sizeof(Elf64_Nhdr) + note->n_namesz + note->n_descsz,
                 shdr->sh_addralign)) {
        printf("ERROR: The '.note.penglaimeta' section size is not correct.\n");
        return -1;
    }

    const char* meta_name = "penglai_metadata";
    if (note->n_namesz != (strlen(meta_name) + 1) ||
        memcmp((void*)(elf_ptr + shdr->sh_offset + sizeof(Elf64_Nhdr)),
               meta_name, note->n_namesz)) {
        printf(
            "ERROR: The note in the '.note.penglaimeta' section must be named "
            "as \"penglai_metadata\"\n");
        return -1;
    }

    *meta_offset =
        (unsigned long)(shdr->sh_offset + sizeof(Elf64_Nhdr) + note->n_namesz);
    *meta_blocksize = note->n_descsz;

    return true;
}

cc_enclave_t g_enclave;
cc_enclave_t* g_enclave_context;
using cc_ecall_enclave_func_t = std::remove_const_t<decltype(std::declval<cc_enclave_ops>().cc_ecall_enclave)>;
#define REMOTE_HOOK_FUNC (cc_ecall_enclave_func_t) distributed_tee_ecall_enclave
#define LOCAL_HOOK_FUNC  (cc_ecall_enclave_func_t) local_tee_ecall_enclave

#define CC_ECALL_ENCLAVE \
    g_enclave_context->list_ops_node->ops_desc->ops->cc_ecall_enclave
extern "C"
{
    static cc_ecall_enclave_func_t cc_ecall_enclave;
    int local_tee_ecall_enclave(cc_enclave_t *enclave,
                                  uint32_t function_id,
                                  const void *input_buffer,
                                  size_t input_buffer_size,
                                  void *output_buffer,
                                  size_t output_buffer_size,
                                  void *ms,
                                  const void *ocall_table)
    {
        int fake_input_buffer_size = input_buffer_size + 4;
        int fake_output_buffer_size = output_buffer_size + 1;
        void *fake_input_buffer = malloc(fake_input_buffer_size);
        void *fake_output_buffer = malloc(fake_output_buffer_size);

        memcpy(fake_input_buffer, input_buffer, input_buffer_size);
        memcpy(fake_output_buffer, output_buffer, output_buffer_size);
        // avoid encryption and decryption
        memset((char*)fake_input_buffer + input_buffer_size, 0, 4);
        memset((char*)fake_output_buffer + output_buffer_size, 1, 1);

        if (!cc_ecall_enclave) {
            printf("not hooked successfully!\n");
        }
        int res = cc_ecall_enclave(enclave, function_id, fake_input_buffer, fake_input_buffer_size, fake_output_buffer, fake_output_buffer_size, ms, ocall_table);

        memcpy(output_buffer, fake_output_buffer, output_buffer_size);
        free(fake_input_buffer);
        free(fake_output_buffer);

        return res;
    }

    void hook_local_ecall_enclave(cc_ecall_enclave_func_t &origin) {
        cc_ecall_enclave = origin;
        origin = LOCAL_HOOK_FUNC;
    }

    static bool is_migrate()
    {
        if (g_current_dtee_context) {
            if (g_current_dtee_context->config.side == SIDE::Client &&
                g_current_dtee_context->config.mode == MODE::Migrate) {
                return true;
            }
        }
        return false;
    }

    static bool is_transparent()
    {
        if (g_current_dtee_context) {
            if (g_current_dtee_context->config.side == SIDE::Client &&
                g_current_dtee_context->config.mode == MODE::Transparent) {
                return true;
            }
        }
        return false;
    }

    // 迁移模式下，远程调用enclave
    struct cc_enclave_ops hook_enclave_ops = {.cc_ecall_enclave = REMOTE_HOOK_FUNC};

    struct cc_enclave_ops_desc hook_enclave_desc = {.ops = &hook_enclave_ops};

    struct list_ops_desc hook_enclave_list_ops_desc = {.ops_desc =
                                                           &hook_enclave_desc};

    cc_enclave_t hook_enclave = {.list_ops_node = &hook_enclave_list_ops_desc};

    struct signature_t
    {
        unsigned char r[PUBLIC_KEY_SIZE / 2];
        unsigned char s[PUBLIC_KEY_SIZE / 2];
    };

    struct pubkey_t
    {
        unsigned char xA[PUBLIC_KEY_SIZE / 2];
        unsigned char yA[PUBLIC_KEY_SIZE / 2];
    };

    struct sm_report_t
    {
        unsigned char hash[HASH_SIZE];
        unsigned char signature[SIGNATURE_SIZE];
        unsigned char sm_pub_key[PUBLIC_KEY_SIZE];
    };

    struct enclave_report_t
    {
        unsigned char hash[HASH_SIZE];
        unsigned char signature[SIGNATURE_SIZE];
        uintptr_t nonce;
    };

    struct report_t
    {
        struct sm_report_t sm;
        struct enclave_report_t enclave;
        unsigned char dev_pub_key[PUBLIC_KEY_SIZE];
    };

    struct penglai_enclave_user_param
    {
        unsigned long eid;
        unsigned long elf_ptr;
        long elf_size;
        long stack_size;
        unsigned long untrusted_mem_ptr;
        long untrusted_mem_size;
        long ocall_buf_size;
        int resume_type;
    };

    struct penglai_enclave_attest_param
    {
        unsigned long eid;
        unsigned long nonce;
        struct report_t report;
    };

    struct report_t current_report;

    struct PLenclave
    {
        struct elf_args* elffile;
        int eid;
        int fd;
        struct penglai_enclave_user_param user_param;
        struct penglai_enclave_attest_param attest_param;
    };

    const char SM2_p[32] = {0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    const char SM2_a[32] = {0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc};

    const char SM2_b[32] = {0x28, 0xe9, 0xfa, 0x9e, 0x9d, 0x9f, 0x5e, 0x34,
                            0x4d, 0x5a, 0x9e, 0x4b, 0xcf, 0x65, 0x09, 0xa7,
                            0xf3, 0x97, 0x89, 0xf5, 0x15, 0xab, 0x8f, 0x92,
                            0xdd, 0xbc, 0xbd, 0x41, 0x4d, 0x94, 0x0e, 0x93};

    const char SM2_Gx[32] = {0x32, 0xc4, 0xae, 0x2c, 0x1f, 0x19, 0x81, 0x19,
                             0x5f, 0x99, 0x04, 0x46, 0x6a, 0x39, 0xc9, 0x94,
                             0x8f, 0xe3, 0x0b, 0xbf, 0xf2, 0x66, 0x0b, 0xe1,
                             0x71, 0x5a, 0x45, 0x89, 0x33, 0x4c, 0x74, 0xc7};

    const char SM2_Gy[32] = {0xbc, 0x37, 0x36, 0xa2, 0xf4, 0xf6, 0x77, 0x9c,
                             0x59, 0xbd, 0xce, 0xe3, 0x6b, 0x69, 0x21, 0x53,
                             0xd0, 0xa9, 0x87, 0x7c, 0xc6, 0x2a, 0x47, 0x40,
                             0x02, 0xdf, 0x32, 0xe5, 0x21, 0x39, 0xf0, 0xa0};

    const char SM2_n[32] = {0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                            0x72, 0x03, 0xdf, 0x6b, 0x21, 0xc6, 0x05, 0x2b,
                            0x53, 0xbb, 0xf4, 0x09, 0x39, 0xd5, 0x41, 0x23};

    void str_to_point(std::string str, epoint* a)
    {
        char mem[MR_BIG_RESERVE(2)] = {0};
        big x, y;
        x = mirvar_mem(mem, 0);
        y = mirvar_mem(mem, 1);
        bytes_to_big(SM2_NUMWORD, str.c_str(), x);
        bytes_to_big(SM2_NUMWORD, str.c_str() + SM2_NUMWORD, y);

        epoint_set(x, y, 0, a);
    }

    std::string point_to_str(epoint* a)
    {
        char mem[MR_BIG_RESERVE(2)] = {0};
        big x, y;
        x = mirvar_mem(mem, 0);
        y = mirvar_mem(mem, 1);

        epoint_get(a, x, y);

        char buffer[2 * SM2_NUMWORD];
        big_to_bytes(SM2_NUMWORD, x, buffer, 1);

        big_to_bytes(SM2_NUMWORD, y, buffer + SM2_NUMWORD, 1);

        return std::string(buffer, buffer + 2 * SM2_NUMWORD);
    }

    std::pair<std::string, std::string> make_key_pair()
    {
        miracl* mip = mirsys(128, 16);
        irand(time(0));

        char mem[MR_BIG_RESERVE(7)] = {0};
        char mem_point[MR_ECP_RESERVE(2)] = {0};

        epoint *g, *pub;
        big p, a, b, n, gx, gy, pri;
        p = mirvar_mem(mem, 0);
        a = mirvar_mem(mem, 1);
        b = mirvar_mem(mem, 2);
        n = mirvar_mem(mem, 3);
        gx = mirvar_mem(mem, 4);
        gy = mirvar_mem(mem, 5);
        pri = mirvar_mem(mem, 6);

        bytes_to_big(SM2_NUMWORD, SM2_a, a);
        bytes_to_big(SM2_NUMWORD, SM2_b, b);
        bytes_to_big(SM2_NUMWORD, SM2_p, p);
        bytes_to_big(SM2_NUMWORD, SM2_Gx, gx);
        bytes_to_big(SM2_NUMWORD, SM2_Gy, gy);
        bytes_to_big(SM2_NUMWORD, SM2_n, n);

        g = epoint_init_mem(mem_point, 0);

        pub = epoint_init_mem(mem_point, 1);
        ecurve_init(a, b, p, MR_PROJECTIVE);

        if (!epoint_set(gx, gy, 0, g)) {
            {};
        }

        auto make_prikey = [n](big x) {
            bigrand(n, x);  // generate a big random number 0<=x<n
        };

        make_prikey(pri);
        ecurve_mult(pri, g, pub);

        auto get_key = [](big b) {
            char buffer[SM2_NUMWORD];
            big_to_bytes(SM2_NUMWORD, b, buffer, 1);
            return std::string(buffer, buffer + SM2_NUMWORD);
        };

        auto res = std::make_pair(get_key(pri), point_to_str(pub));
        mirexit();
        return res;
    }

    std::string make_shared_key(std::string pri_key, std::string in_pub_key)
    {
        miracl* mip = mirsys(128, 16);
        char mem[MR_BIG_RESERVE(1)] = {0};
        char mem_point[MR_ECP_RESERVE(2)] = {0};

        big pri, a, b, p;
        pri = mirvar_mem(mem, 0);
        a = mirvar_mem(mem, 1);
        b = mirvar_mem(mem, 2);
        p = mirvar_mem(mem, 3);
        bytes_to_big(SM2_NUMWORD, SM2_a, a);
        bytes_to_big(SM2_NUMWORD, SM2_b, b);
        bytes_to_big(SM2_NUMWORD, SM2_p, p);
        ecurve_init(a, b, p, MR_PROJECTIVE);

        epoint* pub = epoint_init_mem(mem_point, 0);
        str_to_point(std::move(in_pub_key), pub);

        epoint* shared = epoint_init_mem(mem_point, 1);
        bytes_to_big(SM2_NUMWORD, pri_key.c_str(), pri);
        ecurve_mult(pri, pub, shared);

        auto res = point_to_str(shared);
        mirexit();
        return res;
    }

    bool is_report_valid(const void *report_p,
                                 const void *pub_key,
                                 const void *pub_key_signature,
                                 const char *enclave_path)
    {
        struct report_t* report = (struct report_t*)report_p;

        // 0. check the pub_key signature
        {
            struct pubkey_t* sm_pub_key =
                (struct pubkey_t*)report->sm.sm_pub_key;
            struct signature_t* signature = (struct signature_t*)pub_key_signature;


            if (SM2_Verify((unsigned char*)pub_key, SIGNATURE_SIZE, sm_pub_key->xA,
                           sm_pub_key->yA, signature->r, signature->s)) {
                printf("INVALID PUB KEY\n");
auto print_str = [](int len, const char* name, const void* p) {
            printf("%s: \n", name);
            for (int i = 0; i < len; i++) printf("%02x ", ((unsigned char*)p)[i]);
            printf("\n");
};
print_str(64, "pub sig", signature);
print_str(64, "pub key", pub_key);
print_str(64, "sm pub key", sm_pub_key);
print_str(sizeof(*report), "report", report);
                return false;
            }
        }

        // 1. check the signature
        {
            struct pubkey_t* sm_pub_key =
                (struct pubkey_t*)report->sm.sm_pub_key;
            struct signature_t* signature =
                (struct signature_t*)report->enclave.signature;

            int fd;

            if (SM2_Verify(report->enclave.hash, HASH_SIZE, sm_pub_key->xA,
                           sm_pub_key->yA, signature->r, signature->s)) {
                return false;
            }
        }

        // 2. check the hash
        {
            bool is_valid = true;
            int fd = open(enclave_path, O_RDONLY);
            if (fd < 0) {
                printf("open enclave file failed\n");
                return false;
            }
            struct stat stat;
            fstat(fd, &stat);
            if (stat.st_size == 0) {
                printf("enclave file size is 0\n");
                return false;
            }

            // calculate the correct hash
            void* elf_ptr = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            unsigned long meta_offset, meta_size;
            if (-1 == get_meta_property((unsigned char*)elf_ptr, stat.st_size,
                                        &meta_offset, &meta_size)) {
                exit(-1);
            }
            char* elf_hash = (char*)malloc(HASH_SIZE);
            memcpy(elf_hash, (char*)elf_ptr + meta_offset, HASH_SIZE);
            update_enclave_hash(elf_hash, report->enclave.nonce);

            // compare with the received hash
            printf("expected hash:\n");
            printHex((unsigned char*)elf_hash, HASH_SIZE);
            printf("calculated hash:\n");
            printHex((unsigned char*)report->enclave.hash, HASH_SIZE);
            if (!is_same_bytes(elf_hash, (char*)report->enclave.hash,
                               HASH_SIZE)) {
                is_valid = false;
            }
            close(fd);
            free(elf_hash);
            if (!is_valid) {
                return false;
            }
        }

        return true;
    }

    void z_create_enclave(const char* enclave_path, bool is_proxy = false)
    {
        if (g_forced_enclave_path) enclave_path = g_forced_enclave_path;
        if (is_migrate() || is_transparent() && !exist_local_tee()) {
            g_enclave_context = &hook_enclave;
            return;
        }
        else {
            g_enclave_context = &g_enclave;
        }

        cc_enclave_result_t res = CC_FAIL;
        struct penglai_enclave_attest_param* attest_param;

        res = cc_enclave_create(enclave_path, AUTO_ENCLAVE_TYPE, 0,
                                SECGEAR_DEBUG_FLAG, NULL, 0, g_enclave_context);

        if (res != CC_SUCCESS) {
            printf("Create enclave error\n");
            exit(-1);
        }

        // TODO: add support for other enclaves
        attest_param =
            &((struct PLenclave*)g_enclave_context->private_data)->attest_param;
//        printf("eid %d nonce %d\n", attest_param->eid,
//               attest_param->report.enclave.nonce);
        memcpy(&current_report, &attest_param->report,
                sizeof(struct report_t));
//        if (is_report_valid(&attest_param->report, enclave_path)) {
//            printf("IS VALID REPORT\n");
//        }

        cc_ecall_enclave_func_t &func = const_cast<cc_ecall_enclave_func_t&>(g_enclave_context->list_ops_node->ops_desc->ops->cc_ecall_enclave);
        if (g_enclave_context == NULL ||
            g_enclave_context->list_ops_node == NULL ||
            g_enclave_context->list_ops_node->ops_desc == NULL ||
            g_enclave_context->list_ops_node->ops_desc->ops == NULL ||
            g_enclave_context->list_ops_node->ops_desc->ops->cc_ecall_enclave ==
                NULL) {
            printf("Create enclave error\n");
            goto end;
        }

        if (is_proxy) {
            printf("IS PROXY OF REMOTE CLIENT\n");
        } else {
            printf("IS LOCAL ENCLAVE\n");
        }

        if (!is_proxy && func != LOCAL_HOOK_FUNC) {
            cc_ecall_enclave = func;
            void* page_start = (void*)((uintptr_t)&func & ~(getpagesize() - 1));
            if (mprotect(page_start, getpagesize(), PROT_READ | PROT_WRITE) == 0) func = LOCAL_HOOK_FUNC;
        }


        return;

    end:
        exit(-1);
    }

    void z_destroy_enclave()
    {
        if (is_migrate()) {
            return;
        }
        if (g_enclave_context == &g_enclave) {
            cc_enclave_result_t res = cc_enclave_destroy(g_enclave_context);
            if (res != CC_SUCCESS) {
                printf("Destroy enclave error\n");
            }
            g_enclave_context = NULL;
        }
    }

    std::vector<char> get_report(const char* enclave_path)
    {
        z_create_enclave(enclave_path);
        std::vector<char> report(sizeof(struct report_t));
        memcpy(report.data(), &current_report, sizeof(struct report_t));
        z_destroy_enclave();
        return report;
    }


    extern int (*key_exchange_func)(char *in_key,
                             int in_key_len,
                             char *out_key,
                             int out_key_len,
                             char *out_sealed_shared_key,
                             int out_sealed_shared_key_len,
                             char *out_key_signature,
                             int out_key_signature_len);

    int key_exchange(char* in_key, int in_key_len, char* out_key,
                     int out_key_len, char *out_sealed_shared_key, int out_sealed_shared_key_len,
                     char *out_key_signature, int out_key_signature_len)
    {
        int retval;

        z_create_enclave("enclave.signed.so");

        cc_enclave_result_t __Z_res =
            __secure_key_exchange_impl(g_enclave_context, &retval, in_key,
                                       in_key_len, out_key, out_key_len, 
                                       out_sealed_shared_key, out_sealed_shared_key_len,
                                       out_key_signature, out_key_signature_len);
        if (__Z_res != CC_SUCCESS) {
            printf("Ecall enclave error\n");
            exit(-1);
        }

        z_destroy_enclave();

        return retval;
    }

    void _Z_encrypt(const unsigned char* key, unsigned char* buf, int buf_len)
    {
        unsigned char iv[16] = {0};
        if (buf_len % 16 != 0) {
            printf("BUF LEN % 16 != 0");
            exit(-1);
        }
        SM4_CBC_Encrypt(key, iv, buf, buf_len, buf, buf_len);
    }

    void _Z_decrypt(const unsigned char* key, unsigned char* buf, int buf_len)
    {
        unsigned char iv[16] = {0};
        if (buf_len % 16 != 0) {
            printf("BUF LEN % 16 != 0");
            exit(-1);
        }
        SM4_CBC_Decrypt(key, iv, buf, buf_len, buf, buf_len);
    }

    __attribute__((constructor)) void before_main()
    {
        sm4_encrypt = _Z_encrypt;
        sm4_decrypt = _Z_decrypt;
        key_exchange_func = key_exchange;
        get_report_func = get_report;
        is_report_valid_func = is_report_valid;
        make_key_pair_func = make_key_pair;
        make_shared_key_func = make_shared_key;
        pthread_rwlock_init(&(hook_enclave.rwlock), NULL);
        // g_enclave = (cc_enclave_t *)malloc(sizeof(cc_enclave_t));
        // if (!g_enclave) {
        //   // return CC_ERROR_OUT_OF_MEMORY;
        //   printf("Create g_enclave error\n");
        //   return;
        // }
        // memset(g_enclave, 0, sizeof(*g_enclave));
        // // z_create_enclave();
    }

    int ecall_proxy(const char* enclave_filename, uint32_t fid, char* in_buf,
                    int in_buf_size, char* out_buf, int out_buf_size)
    {
        z_create_enclave(enclave_filename, true);
        cc_enclave_result_t ret = CC_FAIL;
        uint32_t ms = 0;

        /* Call the cc_enclave function */
        cc_enclave_t* enclave = g_enclave_context;
        if (!enclave) {
            ret = CC_ERROR_BAD_PARAMETERS;
            goto exit;
        }
        if (pthread_rwlock_rdlock(&enclave->rwlock)) {
            ret = CC_ERROR_BUSY;
            goto exit;
        }
        if (!enclave->list_ops_node || !enclave->list_ops_node->ops_desc ||
            !enclave->list_ops_node->ops_desc->ops ||
            !enclave->list_ops_node->ops_desc->ops->cc_ecall_enclave) {
            ret = CC_ERROR_BAD_PARAMETERS;
            goto exit;
        }
        if ((ret = enclave->list_ops_node->ops_desc->ops->cc_ecall_enclave(
                 enclave, fid, in_buf, in_buf_size, out_buf, out_buf_size, &ms,
                 &ocall_table)) != CC_SUCCESS) {
            pthread_rwlock_unlock(&enclave->rwlock);
            goto exit;
        }
        if (pthread_rwlock_unlock(&enclave->rwlock)) {
            ret = CC_ERROR_BUSY;
            goto exit;
        }

        ret = CC_SUCCESS;

    exit:
        z_destroy_enclave();
        return ret;
    }
}
