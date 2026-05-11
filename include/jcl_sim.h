#ifndef JCL_SIM_H
#define JCL_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jcl_crypto.h"

#define JCL_MAX_APDU_DATA 255
#define JCL_MAX_FILE_DATA 256
#define JCL_IMSI_LEN 15

typedef enum {
    JCL_SW_OK = 0x9000,
    JCL_SW_VERIFY_FAIL = 0x63C0,
    JCL_SW_SECURITY_STATUS_NOT_SATISFIED = 0x6982,
    JCL_SW_FILE_NOT_FOUND = 0x6A82,
    JCL_SW_WRONG_LENGTH = 0x6700,
    JCL_SW_INS_NOT_SUPPORTED = 0x6D00,
    JCL_SW_CLA_NOT_SUPPORTED = 0x6E00,
    JCL_SW_INCORRECT_P1P2 = 0x6A86,
} jcl_status_t;

typedef struct {
    uint16_t fid;
    char name[24];
    uint8_t data[JCL_MAX_FILE_DATA];
    size_t len;
    bool requires_pin;
    bool writable;
} jcl_file_t;

typedef struct {
    char iccid[21];
    char imsi[JCL_IMSI_LEN + 1];
    char operator_name[32];
    uint8_t master_key[JCL_KEY_LEN];
    uint8_t session_key[JCL_KEY_LEN];
    char pin[9];
    unsigned retries_left;
    bool pin_verified;
    uint16_t selected_fid;
    jcl_file_t files[8];
    size_t file_count;
} jcl_sim_t;

typedef struct {
    uint8_t cla;
    uint8_t ins;
    uint8_t p1;
    uint8_t p2;
    uint8_t lc;
    uint8_t data[JCL_MAX_APDU_DATA];
    uint8_t le;
    bool has_le;
} jcl_apdu_t;

typedef struct {
    uint8_t data[JCL_MAX_APDU_DATA];
    size_t len;
    jcl_status_t sw;
} jcl_response_t;

void jcl_sim_init(jcl_sim_t *sim);
bool jcl_parse_hex_apdu(const char *hex, jcl_apdu_t *apdu);
jcl_response_t jcl_sim_process(jcl_sim_t *sim, const jcl_apdu_t *apdu);
const char *jcl_status_text(jcl_status_t sw);
void jcl_print_response(const jcl_response_t *response);

#endif
