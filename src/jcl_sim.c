#include "jcl_sim.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JCL_CLA 0x80
#define INS_SELECT 0xA4
#define INS_READ_BINARY 0xB0
#define INS_UPDATE_BINARY 0xD6
#define INS_VERIFY 0x20
#define INS_INTERNAL_AUTHENTICATE 0x88
#define INS_GET_STATUS 0xF2

static void add_file(jcl_sim_t *sim, uint16_t fid, const char *name,
                     const uint8_t *data, size_t len, bool requires_pin,
                     bool writable) {
    if (sim->file_count >= sizeof(sim->files) / sizeof(sim->files[0])) {
        return;
    }

    jcl_file_t *file = &sim->files[sim->file_count++];
    file->fid = fid;
    snprintf(file->name, sizeof(file->name), "%s", name);
    file->len = len > JCL_MAX_FILE_DATA ? JCL_MAX_FILE_DATA : len;
    memcpy(file->data, data, file->len);
    file->requires_pin = requires_pin;
    file->writable = writable;
}

static jcl_file_t *find_file(jcl_sim_t *sim, uint16_t fid) {
    for (size_t i = 0; i < sim->file_count; ++i) {
        if (sim->files[i].fid == fid) {
            return &sim->files[i];
        }
    }
    return NULL;
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

static void response_status(jcl_response_t *response, jcl_status_t sw) {
    response->len = 0;
    response->sw = sw;
}

void jcl_sim_init(jcl_sim_t *sim) {
    memset(sim, 0, sizeof(*sim));
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

    const uint8_t mf[] = "JCL-MF Alpha Root";
    const uint8_t imsi[] = "001010123456789";
    const uint8_t spn[] = "JuanCarlosLegals LabNet";
    const uint8_t sms[] = "Toy SMS storage: empty";

    add_file(sim, 0x3F00, "MF", mf, sizeof(mf) - 1, false, false);
    add_file(sim, 0x6F07, "EF-IMSI", imsi, sizeof(imsi) - 1, true, false);
    add_file(sim, 0x6F46, "EF-SPN", spn, sizeof(spn) - 1, false, false);
    add_file(sim, 0x6F3C, "EF-SMS", sms, sizeof(sms) - 1, true, true);
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

jcl_response_t jcl_sim_process(jcl_sim_t *sim, const jcl_apdu_t *apdu) {
    jcl_response_t response;
    response_status(&response, JCL_SW_OK);

    if (apdu->cla != JCL_CLA && apdu->cla != 0x00) {
        response_status(&response, JCL_SW_CLA_NOT_SUPPORTED);
        return response;
    }

    switch (apdu->ins) {
    case INS_SELECT: {
        if (apdu->lc != 2) {
            response_status(&response, JCL_SW_WRONG_LENGTH);
            return response;
        }
        uint16_t fid = ((uint16_t)apdu->data[0] << 8) | apdu->data[1];
        jcl_file_t *file = find_file(sim, fid);
        if (file == NULL) {
            response_status(&response, JCL_SW_FILE_NOT_FOUND);
            return response;
        }
        sim->selected_fid = fid;
        response.len = (size_t)snprintf((char *)response.data, sizeof(response.data),
                                        "FID=%04X NAME=%s LEN=%zu", file->fid,
                                        file->name, file->len);
        return response;
    }
    case INS_READ_BINARY: {
        jcl_file_t *file = find_file(sim, sim->selected_fid);
        if (file == NULL) {
            response_status(&response, JCL_SW_FILE_NOT_FOUND);
            return response;
        }
        if (file->requires_pin && !sim->pin_verified) {
            response_status(&response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
            return response;
        }
        size_t offset = ((size_t)apdu->p1 << 8) | apdu->p2;
        if (offset > file->len) {
            response_status(&response, JCL_SW_INCORRECT_P1P2);
            return response;
        }
        size_t remaining = file->len - offset;
        size_t want = apdu->has_le && apdu->le != 0 ? apdu->le : remaining;
        response.len = want < remaining ? want : remaining;
        memcpy(response.data, file->data + offset, response.len);
        return response;
    }
    case INS_UPDATE_BINARY: {
        jcl_file_t *file = find_file(sim, sim->selected_fid);
        if (file == NULL) {
            response_status(&response, JCL_SW_FILE_NOT_FOUND);
            return response;
        }
        if (!file->writable) {
            response_status(&response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
            return response;
        }
        if (file->requires_pin && !sim->pin_verified) {
            response_status(&response, JCL_SW_SECURITY_STATUS_NOT_SATISFIED);
            return response;
        }
        size_t offset = ((size_t)apdu->p1 << 8) | apdu->p2;
        if (offset + apdu->lc > JCL_MAX_FILE_DATA) {
            response_status(&response, JCL_SW_WRONG_LENGTH);
            return response;
        }
        memcpy(file->data + offset, apdu->data, apdu->lc);
        if (offset + apdu->lc > file->len) {
            file->len = offset + apdu->lc;
        }
        return response;
    }
    case INS_VERIFY: {
        if (apdu->lc == 0 || apdu->lc >= sizeof(sim->pin)) {
            response_status(&response, JCL_SW_WRONG_LENGTH);
            return response;
        }
        char entered[sizeof(sim->pin)] = {0};
        memcpy(entered, apdu->data, apdu->lc);
        if (sim->retries_left == 0 || strcmp(entered, sim->pin) != 0) {
            if (sim->retries_left > 0) {
                sim->retries_left--;
            }
            response_status(&response, (jcl_status_t)(JCL_SW_VERIFY_FAIL | sim->retries_left));
            return response;
        }
        sim->pin_verified = true;
        sim->retries_left = 3;
        return response;
    }
    case INS_INTERNAL_AUTHENTICATE: {
        if (apdu->lc == 0) {
            response_status(&response, JCL_SW_WRONG_LENGTH);
            return response;
        }
        uint8_t sres[JCL_MAC_LEN];
        jcl_derive_response(sim->master_key, apdu->data, apdu->lc, sres, sim->session_key);
        memcpy(response.data, sres, JCL_MAC_LEN);
        memcpy(response.data + JCL_MAC_LEN, sim->session_key, JCL_KEY_LEN);
        response.len = JCL_MAC_LEN + JCL_KEY_LEN;
        return response;
    }
    case INS_GET_STATUS: {
        response.len = (size_t)snprintf((char *)response.data, sizeof(response.data),
                                        "ICCID=%s IMSI=%s OP=%s PIN=%s RETRIES=%u FID=%04X",
                                        sim->iccid, sim->imsi, sim->operator_name,
                                        sim->pin_verified ? "verified" : "required",
                                        sim->retries_left, sim->selected_fid);
        return response;
    }
    default:
        response_status(&response, JCL_SW_INS_NOT_SUPPORTED);
        return response;
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
