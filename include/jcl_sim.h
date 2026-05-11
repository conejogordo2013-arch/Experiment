#ifndef JCL_SIM_H
#define JCL_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jcl_crypto.h"

#define JCL_MAX_APDU_DATA 255
#define JCL_MAX_FILE_DATA 256
#define JCL_IMSI_LEN 15
#define JCL_ROM_SIZE 512
#define JCL_EEPROM_SIZE 1024
#define JCL_RAM_SIZE 256
#define JCL_ATR_MAX_LEN 32
#define JCL_JCCS_LEN 48
#define JCL_MASK_LEN 128
#define JCL_JCD_LEN 8
#define JCL_CCC_LEN 8
#define JCL_CJC_LEN 16
#define JCL_JCC_LEN (JCL_JCD_LEN + JCL_CCC_LEN + JCL_CJC_LEN)
#define JCL_JCID_LEN 16
#define JCL_NET_KEY_LEN 16
#define JCL_JKHO_LEN 32
#define JCL_JCDL_LEN 48
#define JCL_JA_LEN 48
#define JCL_KJ_LEN 96
#define JCL_SECURE_KEY_LEN 96
#define JCL_PIN_LEN 16
#define JCL_PUK_LEN 32

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

typedef enum {
    JCL_MEM_ROM = 0x00,
    JCL_MEM_EEPROM = 0x01,
    JCL_MEM_RAM = 0x02,
} jcl_memory_area_t;

typedef enum {
    JCL_LIFE_BOOT,
    JCL_LIFE_READY,
    JCL_LIFE_AUTH_PENDING,
    JCL_LIFE_AUTH_FAILED,
    JCL_LIFE_ACTIVE,
    JCL_LIFE_TEMP_BLOCKED,
    JCL_LIFE_MAINTENANCE,
    JCL_LIFE_LOCKED,
    JCL_LIFE_JC_BRICKED,
} jcl_life_t;

typedef struct {
    uint8_t jccs[JCL_JCCS_LEN];
    uint8_t mask[JCL_MASK_LEN];
    bool destroyed;
} jcl_core_t;

typedef struct {
    uint8_t jcd[JCL_JCD_LEN];
    uint8_t ccc[JCL_CCC_LEN];
    uint8_t cjc[JCL_CJC_LEN];
    uint8_t jcc[JCL_JCC_LEN];
    uint8_t jcid[JCL_JCID_LEN];
} jcl_identity_t;

typedef struct {
    uint8_t jcvl[24];
    bool has_jcvl;
    uint8_t jcm[JCL_NET_KEY_LEN];
    uint8_t jcd[JCL_NET_KEY_LEN];
    uint8_t jcf[JCL_NET_KEY_LEN];
    uint8_t jcz[JCL_NET_KEY_LEN];
} jcl_network_t;

typedef struct {
    uint8_t jkho[JCL_JKHO_LEN];
} jcl_jci_crypto_t;

typedef struct {
    uint8_t auth_key[JCL_JCDL_LEN];
    uint8_t jcdl[JCL_JCDL_LEN];
    bool has_jcdl;
    uint8_t jclsc1[JCL_JCDL_LEN];
    uint8_t jclsc2[JCL_JCDL_LEN];
    uint8_t jcx[JCL_SECURE_KEY_LEN];
    bool has_jcx;
} jcl_jci_ram_t;

typedef struct {
    uint16_t pc;
    uint8_t sp;
    uint8_t acc;
    uint8_t flags;
    bool halted;
    uint64_t cycles;
} jcl_cpu_t;

typedef struct {
    bool vcc;
    bool rst;
    bool clk;
    bool io;
    uint32_t clock_hz;
    uint64_t edges;
} jcl_contact_bus_t;

typedef struct {
    uint8_t rom[JCL_ROM_SIZE];
    uint8_t eeprom[JCL_EEPROM_SIZE];
    uint8_t ram[JCL_RAM_SIZE];
    uint16_t eeprom_wear[JCL_EEPROM_SIZE];
    uint8_t atr[JCL_ATR_MAX_LEN];
    size_t atr_len;
    jcl_cpu_t cpu;
    jcl_contact_bus_t bus;
    uint8_t last_apdu_ins;
    uint16_t last_status;
    uint32_t resets;
} jcl_hardware_t;

typedef struct {
    uint16_t fid;
    char name[24];
    uint16_t eeprom_offset;
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
    jcl_hardware_t hw;
    jcl_life_t life;
    char mode[8];
    jcl_core_t core;
    jcl_identity_t ident;
    jcl_network_t net;
    jcl_jci_crypto_t jci_crypto;
    jcl_jci_ram_t jci_ram;
    char jns[JCL_PIN_LEN + 1];
    char jul[JCL_PUK_LEN + 1];
    unsigned puk_retries_left;
    unsigned auth_fail_count;
    uint32_t auth_count;
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
const char *jcl_life_text(jcl_life_t life);
void jcl_print_response(const jcl_response_t *response);

#endif
