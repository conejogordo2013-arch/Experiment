#include "jcl_sim.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define JCL_CLA 0x80
#define INS_VERIFY 0x20
#define INS_JCI 0x70
#define INS_GET_DATA 0xCA
#define INS_SELECT 0xA4
#define INS_READ_BINARY 0xB0
#define INS_UPDATE_BINARY 0xD6
#define INS_READ_MEMORY 0xE0
#define INS_WRITE_MEMORY 0xE2
#define INS_RESET_CARD 0xF0
#define INS_CLOCK 0xF1
#define INS_GET_STATUS 0xF2
#define INS_INTERNAL_AUTHENTICATE 0x88

#define CPU_OP_LDI 0x10
#define CPU_OP_STORE_RAM 0x20
#define CPU_OP_LOAD_RAM 0x21
#define CPU_OP_XOR_IMM 0x30
#define CPU_OP_ADD_IMM 0x31
#define CPU_OP_JNZ 0x40
#define CPU_OP_HALT 0xFF

static const uint8_t boot_rom[] = {
    CPU_OP_LDI, 'J', CPU_OP_STORE_RAM, 0x00,
    CPU_OP_LDI, 'C', CPU_OP_STORE_RAM, 0x01,
    CPU_OP_LDI, 'L', CPU_OP_STORE_RAM, 0x02,
    CPU_OP_LDI, 0x01, CPU_OP_STORE_RAM, 0x10,
    CPU_OP_LOAD_RAM, 0x10, CPU_OP_ADD_IMM, 0x01,
    CPU_OP_STORE_RAM, 0x10, CPU_OP_HALT,
};

static const uint8_t alpha_atr[] = {
    0x3B, 0x9F, 0x4A, 0x43, 0x4C, 0x2D, 0x41, 0x4C,
    0x50, 0x48, 0x41, 0x2D, 0x48, 0x57, 0x01, 0x00,
};

static const uint8_t jci_os[] = "JCL-OS/4.1";
static const uint8_t jci_std[] = "ISO-7816-JCI";

static void hw_tick(jcl_hardware_t *hw, unsigned cycles);

static void derive_label(const uint8_t *secret, size_t secret_len, const char *label,
                         uint8_t *out, size_t out_len) {
    jcl_expand_hash(secret, secret_len, NULL, 0, NULL, 0, (const uint8_t *)label,
                    strlen(label), out, out_len);
}

static void derive3(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len,
                    const uint8_t *c, size_t c_len, const char *label,
                    uint8_t *out, size_t out_len) {
    jcl_expand_hash(a, a_len, b, b_len, c, c_len, (const uint8_t *)label,
                    strlen(label), out, out_len);
}

static void init_fixed_bytes(const char *label, uint8_t *out, size_t out_len) {
    static const uint8_t seed[] = "JuanCarlosLegals-ISO-7816-JCI-Alpha";
    derive3(seed, sizeof(seed) - 1, NULL, 0, NULL, 0, label, out, out_len);
}

static void jci_core_destroy(jcl_sim_t *sim) {
    derive3(sim->core.mask, sizeof(sim->core.mask), (const uint8_t *)"BRICK", 5,
            NULL, 0, "JCCS-BRICKED", sim->core.jccs, sizeof(sim->core.jccs));
    sim->core.destroyed = true;
}

static void jci_brick(jcl_sim_t *sim) {
    sim->life = JCL_LIFE_JC_BRICKED;
    memset(&sim->jci_ram, 0, sizeof(sim->jci_ram));
    memset(sim->hw.eeprom, 0, sizeof(sim->hw.eeprom));
    memset(sim->files, 0, sizeof(sim->files));
    sim->file_count = 0;
    jci_core_destroy(sim);
}

static void jci_fail(jcl_sim_t *sim) {
    sim->auth_fail_count++;
    sim->life = JCL_LIFE_AUTH_FAILED;
    if (sim->auth_fail_count > 3U) {
        sim->life = JCL_LIFE_TEMP_BLOCKED;
    }
    if (sim->auth_fail_count > 6U) {
        jci_brick(sim);
    }
}

static void jci_init_model(jcl_sim_t *sim) {
    snprintf(sim->mode, sizeof(sim->mode), "%s", "PROD");
    sim->life = JCL_LIFE_BOOT;

    init_fixed_bytes("JCCS", sim->core.jccs, sizeof(sim->core.jccs));
    init_fixed_bytes("MASK", sim->core.mask, sizeof(sim->core.mask));
    init_fixed_bytes("JCD", sim->ident.jcd, sizeof(sim->ident.jcd));
    init_fixed_bytes("CCC", sim->ident.ccc, sizeof(sim->ident.ccc));
    init_fixed_bytes("CJC", sim->ident.cjc, sizeof(sim->ident.cjc));
    memcpy(sim->ident.jcc, sim->ident.jcd, sizeof(sim->ident.jcd));
    memcpy(sim->ident.jcc + sizeof(sim->ident.jcd), sim->ident.ccc, sizeof(sim->ident.ccc));
    memcpy(sim->ident.jcc + sizeof(sim->ident.jcd) + sizeof(sim->ident.ccc),
           sim->ident.cjc, sizeof(sim->ident.cjc));
    init_fixed_bytes("JCID", sim->ident.jcid, sizeof(sim->ident.jcid));

    init_fixed_bytes("JCm", sim->net.jcm, sizeof(sim->net.jcm));
    init_fixed_bytes("JCd", sim->net.jcd, sizeof(sim->net.jcd));
    init_fixed_bytes("JCf", sim->net.jcf, sizeof(sim->net.jcf));
    init_fixed_bytes("JCz", sim->net.jcz, sizeof(sim->net.jcz));
    derive3(sim->core.jccs, sizeof(sim->core.jccs), sim->net.jcd, sizeof(sim->net.jcd),
            NULL, 0, "JKHO", sim->jci_crypto.jkho, sizeof(sim->jci_crypto.jkho));

    derive_label(sim->core.jccs, sizeof(sim->core.jccs), "AUTH",
                 sim->jci_ram.auth_key, sizeof(sim->jci_ram.auth_key));
    snprintf(sim->jns, sizeof(sim->jns), "%s", "0000000000000000");
    snprintf(sim->jul, sizeof(sim->jul), "%s", "00000000000000000000000000000000");
    sim->puk_retries_left = 10;
    sim->auth_count = 0;
    sim->auth_fail_count = 0;
    sim->life = JCL_LIFE_READY;
}

static void jci_jc132(jcl_sim_t *sim, const uint8_t *jcdl, uint8_t out[64]) {
    derive3(sim->core.jccs, sizeof(sim->core.jccs), sim->net.jcd, sizeof(sim->net.jcd),
            jcdl, JCL_JCDL_LEN, "JC132", out, 64);
}

static void jci_jccc(jcl_sim_t *sim, const uint8_t *base, size_t base_len,
                     const char *label, uint8_t *out, size_t out_len) {
    derive3(base, base_len, sim->jci_crypto.jkho, sizeof(sim->jci_crypto.jkho),
            NULL, 0, label, out, out_len);
}

static void jci_issue_jcdl(jcl_sim_t *sim, uint8_t out[JCL_JCDL_LEN]) {
    sim->life = JCL_LIFE_AUTH_PENDING;
    derive3(sim->core.jccs, sizeof(sim->core.jccs), sim->net.jcm, sizeof(sim->net.jcm),
            (const uint8_t *)&sim->hw.cpu.cycles, sizeof(sim->hw.cpu.cycles),
            "JCVL", sim->net.jcvl, sizeof(sim->net.jcvl));
    sim->net.has_jcvl = true;
    derive3(sim->jci_ram.auth_key, sizeof(sim->jci_ram.auth_key), sim->net.jcvl,
            sizeof(sim->net.jcvl), (const uint8_t *)&sim->auth_count,
            sizeof(sim->auth_count), "JCDL", sim->jci_ram.jcdl, sizeof(sim->jci_ram.jcdl));
    sim->jci_ram.has_jcdl = true;
    memcpy(out, sim->jci_ram.jcdl, JCL_JCDL_LEN);
}

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static bool data_starts(const jcl_apdu_t *apdu, const char *cmd) {
    size_t len = strlen(cmd);
    return apdu->lc >= len && memcmp(apdu->data, cmd, len) == 0;
}


static void response_status(jcl_response_t *response, jcl_status_t sw) {
    response->len = 0;
    response->sw = sw;
}

static void set_status(jcl_sim_t *sim, jcl_response_t *response, jcl_status_t sw) {
    response_status(response, sw);
    sim->hw.last_status = sw;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void hw_reset(jcl_hardware_t *hw, bool cold) {
    if (cold) {
        memset(hw->ram, 0, sizeof(hw->ram));
        hw->bus.vcc = true;
        hw->bus.clock_hz = 3570000U;
    }

    hw->bus.rst = true;
    hw->bus.clk = false;
    hw->bus.io = true;
    hw->cpu.pc = 0;
    hw->cpu.sp = (uint8_t)(JCL_RAM_SIZE - 1U);
    hw->cpu.acc = 0;
    hw->cpu.flags = 0;
    hw->cpu.halted = false;
    hw->resets++;
    hw_tick(hw, 12);
}

static void hw_init(jcl_hardware_t *hw) {
    memset(hw, 0, sizeof(*hw));
    memset(hw->rom, 0xFF, sizeof(hw->rom));
    memset(hw->eeprom, 0xFF, sizeof(hw->eeprom));
    memcpy(hw->rom, boot_rom, sizeof(boot_rom));
    memcpy(hw->atr, alpha_atr, sizeof(alpha_atr));
    hw->atr_len = sizeof(alpha_atr);
    hw_reset(hw, true);
}

static bool memory_bounds(jcl_memory_area_t area, uint16_t offset, size_t len) {
    switch (area) {
    case JCL_MEM_ROM:
        return offset <= JCL_ROM_SIZE && len <= (size_t)(JCL_ROM_SIZE - offset);
    case JCL_MEM_EEPROM:
        return offset <= JCL_EEPROM_SIZE && len <= (size_t)(JCL_EEPROM_SIZE - offset);
    case JCL_MEM_RAM:
        return offset <= JCL_RAM_SIZE && len <= (size_t)(JCL_RAM_SIZE - offset);
    default:
        return false;
    }
}

static uint8_t *memory_ptr(jcl_hardware_t *hw, jcl_memory_area_t area, uint16_t offset) {
    switch (area) {
    case JCL_MEM_ROM:
        return hw->rom + offset;
    case JCL_MEM_EEPROM:
        return hw->eeprom + offset;
    case JCL_MEM_RAM:
        return hw->ram + offset;
    default:
        return NULL;
    }
}

static uint8_t hw_fetch(jcl_hardware_t *hw) {
    uint8_t value = hw->rom[hw->cpu.pc % JCL_ROM_SIZE];
    hw->cpu.pc = (uint16_t)((hw->cpu.pc + 1U) % JCL_ROM_SIZE);
    return value;
}

static void hw_tick(jcl_hardware_t *hw, unsigned cycles) {
    for (unsigned i = 0; i < cycles; ++i) {
        hw->bus.clk = !hw->bus.clk;
        hw->bus.edges++;
        hw->cpu.cycles++;

        if (hw->cpu.halted || !hw->bus.vcc || !hw->bus.rst) {
            continue;
        }

        uint8_t op = hw_fetch(hw);
        switch (op) {
        case CPU_OP_LDI:
            hw->cpu.acc = hw_fetch(hw);
            hw->cpu.flags = hw->cpu.acc == 0 ? 1U : 0U;
            break;
        case CPU_OP_STORE_RAM: {
            uint8_t addr = hw_fetch(hw);
            hw->ram[addr] = hw->cpu.acc;
            break;
        }
        case CPU_OP_LOAD_RAM: {
            uint8_t addr = hw_fetch(hw);
            hw->cpu.acc = hw->ram[addr];
            hw->cpu.flags = hw->cpu.acc == 0 ? 1U : 0U;
            break;
        }
        case CPU_OP_XOR_IMM:
            hw->cpu.acc ^= hw_fetch(hw);
            hw->cpu.flags = hw->cpu.acc == 0 ? 1U : 0U;
            break;
        case CPU_OP_ADD_IMM:
            hw->cpu.acc = (uint8_t)(hw->cpu.acc + hw_fetch(hw));
            hw->cpu.flags = hw->cpu.acc == 0 ? 1U : 0U;
            break;
        case CPU_OP_JNZ: {
            uint8_t addr = hw_fetch(hw);
            if ((hw->cpu.flags & 1U) == 0U) {
                hw->cpu.pc = addr;
            }
            break;
        }
        case CPU_OP_HALT:
            hw->cpu.halted = true;
            break;
        default:
            hw->cpu.flags |= 0x80U;
            hw->cpu.halted = true;
            break;
        }
    }
}

static void hw_record_apdu(jcl_hardware_t *hw, const jcl_apdu_t *apdu) {
    hw->last_apdu_ins = apdu->ins;
    hw->bus.io = false;
    hw_tick(hw, 2U + apdu->lc);
    hw->bus.io = true;
}

static void hw_write_eeprom(jcl_hardware_t *hw, uint16_t offset, const uint8_t *data, size_t len) {
    if (!memory_bounds(JCL_MEM_EEPROM, offset, len)) {
        return;
    }
    memcpy(hw->eeprom + offset, data, len);
    for (size_t i = 0; i < len; ++i) {
        if (hw->eeprom_wear[offset + i] < UINT16_MAX) {
            hw->eeprom_wear[offset + i]++;
        }
    }
}

static void add_file(jcl_sim_t *sim, uint16_t fid, const char *name,
                     uint16_t eeprom_offset, const uint8_t *data, size_t len,
                     bool requires_pin, bool writable) {
    if (sim->file_count >= sizeof(sim->files) / sizeof(sim->files[0])) {
        return;
    }

    jcl_file_t *file = &sim->files[sim->file_count++];
    file->fid = fid;
    snprintf(file->name, sizeof(file->name), "%s", name);
    file->eeprom_offset = eeprom_offset;
    file->len = len > JCL_MAX_FILE_DATA ? JCL_MAX_FILE_DATA : len;
    memcpy(file->data, data, file->len);
    file->requires_pin = requires_pin;
    file->writable = writable;
    hw_write_eeprom(&sim->hw, eeprom_offset, file->data, file->len);
}

static jcl_file_t *find_file(jcl_sim_t *sim, uint16_t fid) {
    for (size_t i = 0; i < sim->file_count; ++i) {
        if (sim->files[i].fid == fid) {
            return &sim->files[i];
        }
    }
    return NULL;
}

static void sync_file_to_eeprom(jcl_sim_t *sim, const jcl_file_t *file) {
    hw_write_eeprom(&sim->hw, file->eeprom_offset, file->data, file->len);
}

void jcl_sim_init(jcl_sim_t *sim) {
    memset(sim, 0, sizeof(*sim));
    hw_init(&sim->hw);
    jci_init_model(sim);
    memcpy(sim->hw.rom + 0x0040, jci_os, sizeof(jci_os) - 1);
    memcpy(sim->hw.rom + 0x0060, jci_std, sizeof(jci_std) - 1);
    snprintf(sim->iccid, sizeof(sim->iccid), "8988000000000000001");
    snprintf(sim->imsi, sizeof(sim->imsi), "001010123456789");
    snprintf(sim->operator_name, sizeof(sim->operator_name), "JuanCarlosLegals LabNet");
    snprintf(sim->pin, sizeof(sim->pin), "1234");
    sim->retries_left = 3;
    sim->selected_fid = 0x3F00;

    const uint8_t key[JCL_KEY_LEN] = {
        0x4a, 0x75, 0x61, 0x6e, 0x43, 0x61, 0x72, 0x6c,
        0x6f, 0x73, 0x4c, 0x65, 0x67, 0x61, 0x6c, 0x73,
    };
    memcpy(sim->master_key, key, sizeof(sim->master_key));
    memcpy(sim->session_key, key, sizeof(sim->session_key));
    hw_write_eeprom(&sim->hw, 0x0100, sim->master_key, sizeof(sim->master_key));
    hw_write_eeprom(&sim->hw, 0x0120, (const uint8_t *)sim->jns, strlen(sim->jns));
    hw_write_eeprom(&sim->hw, 0x0140, (const uint8_t *)sim->jul, strlen(sim->jul));
    hw_write_eeprom(&sim->hw, 0x0180, sim->ident.jcid, sizeof(sim->ident.jcid));
    hw_write_eeprom(&sim->hw, 0x0190, sim->ident.jcc, sizeof(sim->ident.jcc));

    const uint8_t mf[] = "JCL-MF Alpha Root";
    const uint8_t imsi[] = "001010123456789";
    const uint8_t spn[] = "JuanCarlosLegals LabNet";
    const uint8_t sms[] = "Toy SMS storage: empty";

    add_file(sim, 0x3F00, "MF", 0x0020, mf, sizeof(mf) - 1, false, false);
    add_file(sim, 0x6F07, "EF-IMSI", 0x0040, imsi, sizeof(imsi) - 1, true, false);
    add_file(sim, 0x6F46, "EF-SPN", 0x0060, spn, sizeof(spn) - 1, false, false);
    add_file(sim, 0x6F3C, "EF-SMS", 0x0080, sms, sizeof(sms) - 1, true, true);
}

bool jcl_parse_hex_apdu(const char *hex, jcl_apdu_t *apdu) {
    uint8_t bytes[5 + JCL_MAX_APDU_DATA + 1];
    size_t count = 0;
    int high = -1;

    memset(apdu, 0, sizeof(*apdu));

    for (const char *p = hex; *p != '\0'; ++p) {
        if (isspace((unsigned char)*p) || *p == ':') {
            continue;
        }
        int v = hex_value(*p);
        if (v < 0) {
            return false;
        }
        if (high < 0) {
            high = v;
        } else {
            if (count >= sizeof(bytes)) {
                return false;
            }
            bytes[count++] = (uint8_t)((high << 4) | v);
            high = -1;
        }
    }

    if (high >= 0 || count < 4) {
        return false;
    }

    apdu->cla = bytes[0];
    apdu->ins = bytes[1];
    apdu->p1 = bytes[2];
    apdu->p2 = bytes[3];

    if (count == 4) {
        return true;
    }

    apdu->lc = bytes[4];
    if (count < 5U + apdu->lc) {
        return false;
    }
    memcpy(apdu->data, &bytes[5], apdu->lc);

    if (count == 5U + apdu->lc + 1U) {
        apdu->has_le = true;
        apdu->le = bytes[count - 1];
    } else if (count != 5U + apdu->lc) {
        return false;
    }

    return true;
}

static jcl_response_t get_data(const jcl_sim_t *sim, const jcl_apdu_t *apdu) {
    jcl_response_t response;
    response_status(&response, JCL_SW_OK);

    switch (apdu->p2) {
    case 0x01:
        response.len = sim->hw.atr_len;
        memcpy(response.data, sim->hw.atr, response.len);
        break;
    case 0x02:
        response.len = (size_t)snprintf((char *)response.data, sizeof(response.data),
                                        "CPU PC=%04X SP=%02X ACC=%02X FLAGS=%02X HALT=%u CYC=%llu",
                                        sim->hw.cpu.pc, sim->hw.cpu.sp, sim->hw.cpu.acc,
                                        sim->hw.cpu.flags, sim->hw.cpu.halted ? 1U : 0U,
                                        (unsigned long long)sim->hw.cpu.cycles);
        break;
    case 0x03:
        response.len = (size_t)snprintf((char *)response.data, sizeof(response.data),
                                        "ROM=%u EEPROM=%u RAM=%u ATR=%zu",
                                        (unsigned)JCL_ROM_SIZE, (unsigned)JCL_EEPROM_SIZE,
                                        (unsigned)JCL_RAM_SIZE, sim->hw.atr_len);
        break;
    case 0x04:
        response.len = (size_t)snprintf((char *)response.data, sizeof(response.data),
                                        "VCC=%u RST=%u CLK=%u IO=%u HZ=%u EDGES=%llu RESETS=%u",
                                        sim->hw.bus.vcc ? 1U : 0U, sim->hw.bus.rst ? 1U : 0U,
                                        sim->hw.bus.clk ? 1U : 0U, sim->hw.bus.io ? 1U : 0U,
                                        sim->hw.bus.clock_hz,
                                        (unsigned long long)sim->hw.bus.edges,
                                        sim->hw.resets);
        break;
    default:
        response_status(&response, JCL_SW_INCORRECT_P1P2);
        break;
    }

    return response;
}

static jcl_response_t read_memory(jcl_sim_t *sim, const jcl_apdu_t *apdu) {
    jcl_response_t response;
    jcl_memory_area_t area = (jcl_memory_area_t)apdu->p1;
    uint16_t offset = apdu->p2;

    response_status(&response, JCL_SW_OK);
    if (apdu->lc != 1) {
        response_status(&response, JCL_SW_WRONG_LENGTH);
        return response;
    }
    size_t len = apdu->data[0];
    if (len == 0 || len > JCL_MAX_APDU_DATA || !memory_bounds(area, offset, len)) {
        response_status(&response, JCL_SW_INCORRECT_P1P2);
        return response;
    }
    if ((area == JCL_MEM_EEPROM || area == JCL_MEM_RAM) && !sim->pin_verified) {
        response_status(&response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
        return response;
    }

    memcpy(response.data, memory_ptr(&sim->hw, area, offset), len);
    response.len = len;
    return response;
}

static jcl_response_t write_memory(jcl_sim_t *sim, const jcl_apdu_t *apdu) {
    jcl_response_t response;
    jcl_memory_area_t area = (jcl_memory_area_t)apdu->p1;
    uint16_t offset = apdu->p2;

    response_status(&response, JCL_SW_OK);
    if (!sim->pin_verified) {
        response_status(&response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
        return response;
    }
    if (apdu->lc == 0 || area == JCL_MEM_ROM || !memory_bounds(area, offset, apdu->lc)) {
        response_status(&response, JCL_SW_INCORRECT_P1P2);
        return response;
    }
    if (area == JCL_MEM_EEPROM) {
        hw_write_eeprom(&sim->hw, offset, apdu->data, apdu->lc);
    } else {
        memcpy(memory_ptr(&sim->hw, area, offset), apdu->data, apdu->lc);
    }
    return response;
}


static jcl_response_t jci_process(jcl_sim_t *sim, const jcl_apdu_t *apdu) {
    jcl_response_t response;
    response_status(&response, JCL_SW_OK);

    if (data_starts(apdu, "STATE")) {
        response.len = (size_t)snprintf((char *)response.data, sizeof(response.data),
                                        "%s", jcl_life_text(sim->life));
        return response;
    }
    if (sim->life == JCL_LIFE_JC_BRICKED) {
        response_status(&response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
        return response;
    }

    if (data_starts(apdu, "JCR")) {
        response.data[0] = 'J';
        response.data[1] = 'C';
        response.data[2] = 'R';
        derive3(sim->core.jccs, sizeof(sim->core.jccs), sim->ident.jcid,
                sizeof(sim->ident.jcid), sim->net.jcf, sizeof(sim->net.jcf),
                "JCR", response.data + 3, 21);
        response.len = 24;
    } else if (data_starts(apdu, "JCID")) {
        memcpy(response.data, sim->ident.jcid, sizeof(sim->ident.jcid));
        response.len = sizeof(sim->ident.jcid);
    } else if (data_starts(apdu, "JCC")) {
        memcpy(response.data, sim->ident.jcc, sizeof(sim->ident.jcc));
        response.len = sizeof(sim->ident.jcc);
    } else if (data_starts(apdu, "AUTH1")) {
        jci_issue_jcdl(sim, response.data);
        response.len = JCL_JCDL_LEN;
    } else if (data_starts(apdu, "AUTH2")) {
        const size_t cmd_len = 5;
        const uint8_t *candidate = sim->jci_ram.jcdl;
        if (apdu->lc >= cmd_len + JCL_JCDL_LEN) {
            candidate = apdu->data + cmd_len;
        }
        if (!sim->jci_ram.has_jcdl || !bytes_equal(candidate, sim->jci_ram.jcdl, JCL_JCDL_LEN)) {
            jci_fail(sim);
            memcpy(response.data, "AUTH_FAIL", 9);
            response.len = 9;
            return response;
        }

        uint8_t base[64];
        uint8_t ja[JCL_JA_LEN];
        uint8_t kj[JCL_KJ_LEN];
        jci_jc132(sim, sim->jci_ram.jcdl, base);
        jci_jccc(sim, base, sizeof(base), "JA", ja, sizeof(ja));
        jci_jccc(sim, base, sizeof(base), "KJ", kj, sizeof(kj));
        derive_label(ja, sizeof(ja), "JCLSC1", sim->jci_ram.jclsc1,
                     sizeof(sim->jci_ram.jclsc1));
        derive_label(kj, sizeof(kj), "JCLSC2", sim->jci_ram.jclsc2,
                     sizeof(sim->jci_ram.jclsc2));
        memcpy(sim->jci_ram.jcx, kj, sizeof(kj));
        sim->jci_ram.has_jcx = true;
        sim->life = JCL_LIFE_ACTIVE;
        sim->auth_count++;
        sim->auth_fail_count = 0;
        memcpy(response.data, "AUTH_OK", 7);
        response.len = 7;
    } else if (data_starts(apdu, "SEND")) {
        const size_t cmd_len = 4;
        if (sim->life != JCL_LIFE_ACTIVE || !sim->jci_ram.has_jcx || apdu->lc < cmd_len) {
            response_status(&response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
            return response;
        }
        size_t len = apdu->lc - cmd_len;
        derive3(sim->jci_ram.jcx, sizeof(sim->jci_ram.jcx), apdu->data + cmd_len,
                len, NULL, 0, "JV6", response.data, len);
        response.len = len;
    } else if (data_starts(apdu, "JNS")) {
        const size_t cmd_len = 3;
        size_t len = apdu->lc > cmd_len ? apdu->lc - cmd_len : 0;
        if (len == strlen(sim->jns) && memcmp(apdu->data + cmd_len, sim->jns, len) == 0) {
            sim->retries_left = 3;
            sim->pin_verified = true;
            memcpy(response.data, "JNS_OK", 6);
            response.len = 6;
        } else {
            if (sim->retries_left > 0) {
                sim->retries_left--;
            }
            if (sim->retries_left == 0) {
                sim->life = JCL_LIFE_LOCKED;
            }
            memcpy(response.data, "JNS_FAIL", 8);
            response.len = 8;
        }
    } else if (data_starts(apdu, "JUL")) {
        const size_t cmd_len = 3;
        size_t len = apdu->lc > cmd_len ? apdu->lc - cmd_len : 0;
        if (len == strlen(sim->jul) && memcmp(apdu->data + cmd_len, sim->jul, len) == 0) {
            sim->puk_retries_left = 10;
            sim->retries_left = 3;
            sim->life = JCL_LIFE_READY;
            memcpy(response.data, "JUL_OK", 6);
            response.len = 6;
        } else {
            if (sim->puk_retries_left > 0) {
                sim->puk_retries_left--;
            }
            if (sim->puk_retries_left == 0) {
                jci_brick(sim);
            }
            memcpy(response.data, "JUL_FAIL", 8);
            response.len = 8;
        }
    } else {
        response_status(&response, JCL_SW_INS_NOT_SUPPORTED);
    }

    return response;
}

jcl_response_t jcl_sim_process(jcl_sim_t *sim, const jcl_apdu_t *apdu) {
    jcl_response_t response;
    response_status(&response, JCL_SW_OK);
    hw_record_apdu(&sim->hw, apdu);

    if (apdu->cla != JCL_CLA && apdu->cla != 0x00) {
        set_status(sim, &response, JCL_SW_CLA_NOT_SUPPORTED);
        return response;
    }
    if (sim->life == JCL_LIFE_JC_BRICKED && apdu->ins != INS_JCI) {
        set_status(sim, &response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
        return response;
    }

    switch (apdu->ins) {
    case INS_JCI:
        response = jci_process(sim, apdu);
        break;
    case INS_RESET_CARD:
        hw_reset(&sim->hw, apdu->p1 == 0x00);
        response.len = sim->hw.atr_len;
        memcpy(response.data, sim->hw.atr, response.len);
        break;
    case INS_CLOCK: {
        unsigned cycles = ((unsigned)apdu->p1 << 8) | apdu->p2;
        if (cycles == 0) {
            cycles = 1;
        }
        hw_tick(&sim->hw, cycles);
        response.len = (size_t)snprintf((char *)response.data, sizeof(response.data),
                                        "CYC=%llu PC=%04X ACC=%02X HALT=%u",
                                        (unsigned long long)sim->hw.cpu.cycles,
                                        sim->hw.cpu.pc, sim->hw.cpu.acc,
                                        sim->hw.cpu.halted ? 1U : 0U);
        break;
    }
    case INS_GET_DATA:
        response = get_data(sim, apdu);
        break;
    case INS_READ_MEMORY:
        response = read_memory(sim, apdu);
        break;
    case INS_WRITE_MEMORY:
        response = write_memory(sim, apdu);
        break;
    case INS_SELECT: {
        if (apdu->lc != 2) {
            set_status(sim, &response, JCL_SW_WRONG_LENGTH);
            return response;
        }
        uint16_t fid = ((uint16_t)apdu->data[0] << 8) | apdu->data[1];
        jcl_file_t *file = find_file(sim, fid);
        if (file == NULL) {
            set_status(sim, &response, JCL_SW_FILE_NOT_FOUND);
            return response;
        }
        sim->selected_fid = fid;
        response.len = (size_t)snprintf((char *)response.data, sizeof(response.data),
                                        "FID=%04X NAME=%s LEN=%zu EEPROM=%04X",
                                        file->fid, file->name, file->len,
                                        file->eeprom_offset);
        break;
    }
    case INS_READ_BINARY: {
        jcl_file_t *file = find_file(sim, sim->selected_fid);
        if (file == NULL) {
            set_status(sim, &response, JCL_SW_FILE_NOT_FOUND);
            return response;
        }
        if (file->requires_pin && !sim->pin_verified) {
            set_status(sim, &response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
            return response;
        }
        size_t offset = ((size_t)apdu->p1 << 8) | apdu->p2;
        if (offset > file->len) {
            set_status(sim, &response, JCL_SW_INCORRECT_P1P2);
            return response;
        }
        size_t remaining = file->len - offset;
        size_t want = apdu->has_le && apdu->le != 0 ? apdu->le : remaining;
        response.len = want < remaining ? want : remaining;
        memcpy(response.data, file->data + offset, response.len);
        break;
    }
    case INS_UPDATE_BINARY: {
        jcl_file_t *file = find_file(sim, sim->selected_fid);
        if (file == NULL) {
            set_status(sim, &response, JCL_SW_FILE_NOT_FOUND);
            return response;
        }
        if (!file->writable || (file->requires_pin && !sim->pin_verified)) {
            set_status(sim, &response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
            return response;
        }
        size_t offset = ((size_t)apdu->p1 << 8) | apdu->p2;
        if (offset + apdu->lc > JCL_MAX_FILE_DATA) {
            set_status(sim, &response, JCL_SW_WRONG_LENGTH);
            return response;
        }
        memcpy(file->data + offset, apdu->data, apdu->lc);
        if (offset + apdu->lc > file->len) {
            file->len = offset + apdu->lc;
        }
        sync_file_to_eeprom(sim, file);
        break;
    }
    case INS_VERIFY: {
        if (apdu->lc == 0 || apdu->lc >= sizeof(sim->pin)) {
            set_status(sim, &response, JCL_SW_WRONG_LENGTH);
            return response;
        }
        char entered[sizeof(sim->pin)] = {0};
        memcpy(entered, apdu->data, apdu->lc);
        if (sim->retries_left == 0 || strcmp(entered, sim->pin) != 0) {
            if (sim->retries_left > 0) {
                sim->retries_left--;
            }
            set_status(sim, &response, (jcl_status_t)(JCL_SW_VERIFY_FAIL | sim->retries_left));
            return response;
        }
        sim->pin_verified = true;
        sim->retries_left = 3;
        break;
    }
    case INS_INTERNAL_AUTHENTICATE: {
        if (apdu->lc == 0) {
            set_status(sim, &response, JCL_SW_WRONG_LENGTH);
            return response;
        }
        uint8_t sres[JCL_MAC_LEN];
        jcl_derive_response(sim->master_key, apdu->data, apdu->lc, sres, sim->session_key);
        memcpy(response.data, sres, JCL_MAC_LEN);
        memcpy(response.data + JCL_MAC_LEN, sim->session_key, JCL_KEY_LEN);
        response.len = JCL_MAC_LEN + JCL_KEY_LEN;
        break;
    }
    case INS_GET_STATUS:
        response.len = (size_t)snprintf((char *)response.data, sizeof(response.data),
                                        "ICCID=%s IMSI=%s OP=%s LIFE=%s PIN=%s JNS=%u JUL=%u AUTH=%u FID=%04X PC=%04X CYC=%llu ROM=%u EEPROM=%u RAM=%u",
                                        sim->iccid, sim->imsi, sim->operator_name,
                                        jcl_life_text(sim->life),
                                        sim->pin_verified ? "verified" : "required",
                                        sim->retries_left, sim->puk_retries_left,
                                        sim->auth_count, sim->selected_fid,
                                        sim->hw.cpu.pc,
                                        (unsigned long long)sim->hw.cpu.cycles,
                                        (unsigned)JCL_ROM_SIZE,
                                        (unsigned)JCL_EEPROM_SIZE,
                                        (unsigned)JCL_RAM_SIZE);
        break;
    default:
        set_status(sim, &response, JCL_SW_INS_NOT_SUPPORTED);
        return response;
    }

    sim->hw.last_status = response.sw;
    hw_tick(&sim->hw, 1);
    return response;
}


const char *jcl_life_text(jcl_life_t life) {
    switch (life) {
    case JCL_LIFE_BOOT:
        return "BOOT";
    case JCL_LIFE_READY:
        return "READY";
    case JCL_LIFE_AUTH_PENDING:
        return "AUTH_PENDING";
    case JCL_LIFE_AUTH_FAILED:
        return "AUTH_FAILED";
    case JCL_LIFE_ACTIVE:
        return "ACTIVE";
    case JCL_LIFE_TEMP_BLOCKED:
        return "TEMP_BLOCKED";
    case JCL_LIFE_MAINTENANCE:
        return "MAINTENANCE";
    case JCL_LIFE_LOCKED:
        return "LOCKED";
    case JCL_LIFE_JC_BRICKED:
        return "JC_BRICKED";
    default:
        return "UNKNOWN";
    }
}

const char *jcl_status_text(jcl_status_t sw) {
    switch (sw) {
    case JCL_SW_OK:
        return "OK";
    case JCL_SW_SECURITY_STATUS_NOT_SATISFIED:
        return "security status not satisfied";
    case JCL_SW_FILE_NOT_FOUND:
        return "file not found";
    case JCL_SW_WRONG_LENGTH:
        return "wrong length";
    case JCL_SW_INS_NOT_SUPPORTED:
        return "instruction not supported";
    case JCL_SW_CLA_NOT_SUPPORTED:
        return "class not supported";
    case JCL_SW_INCORRECT_P1P2:
        return "incorrect P1/P2";
    default:
        if ((sw & 0xFFF0U) == JCL_SW_VERIFY_FAIL) {
            return "PIN verification failed";
        }
        return "unknown status";
    }
}

void jcl_print_response(const jcl_response_t *response) {
    printf("DATA[%zu]=", response->len);
    for (size_t i = 0; i < response->len; ++i) {
        printf("%02X", response->data[i]);
    }
    if (response->len > 0) {
        printf("  ASCII=\"");
        for (size_t i = 0; i < response->len; ++i) {
            putchar(isprint(response->data[i]) ? response->data[i] : '.');
        }
        printf("\"");
    }
    printf("\nSW=%04X (%s)\n", response->sw, jcl_status_text(response->sw));
}
