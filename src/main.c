#include "jcl_sim.h"

#include <stdio.h>
#include <string.h>

static void print_help(const char *program) {
    printf("JuanCarlosLegals SIM Alpha emulator\n");
    printf("Usage:\n");
    printf("  %s --demo\n", program);
    printf("  %s '<hex APDU>' ['JCI:<command>[payload]' ...]\n\n", program);
    printf("Supported educational APDUs:\n");
    printf("  00 A4 00 00 02 <FID>       SELECT file\n");
    printf("  00 B0 <off_hi> <off_lo> 00 [Le] READ BINARY selected file\n");
    printf("  00 D6 <off_hi> <off_lo> Lc <data> UPDATE BINARY selected writable file\n");
    printf("  00 20 00 01 Lc <ASCII PIN> VERIFY PIN (default 1234)\n");
    printf("  80 88 00 00 Lc <RAND>      JuanCarlosLegals AUTH toy response\n");
    printf("  80 CA 00 01                GET DATA: ATR\n");
    printf("  80 CA 00 02                GET DATA: CPU registers\n");
    printf("  80 CA 00 03                GET DATA: memory map\n");
    printf("  80 CA 00 04                GET DATA: contact bus\n");
    printf("  80 E0 <area> <off> 01 <n>  READ MEMORY area 00 ROM, 01 EEPROM, 02 RAM\n");
    printf("  80 E2 <area> <off> Lc <data> WRITE MEMORY EEPROM/RAM after PIN\n");
    printf("  80 F0 00 00                RESET and return ATR\n");
    printf("  80 F1 <cycles_hi> <lo>     clock CPU cycles\n");
    printf("  80 F2 00 00                GET STATUS\n");
    printf("  JCI:STATE                  fictitious ISO-7816-JCI life state\n");
    printf("  JCI:AUTH1 / JCI:AUTH2      issue and consume JCDL challenge\n");
    printf("  JCI:JCR / JCI:JCID / JCI:JCC identity records\n");
    printf("  JCI:JNS<16 digits>         verify JuanCarlosLegals PIN\n");
    printf("  JCI:SEND<data>             secure-channel toy transform after AUTH2\n");
}

static int parse_command(const char *text, jcl_apdu_t *apdu) {
    const char prefix[] = "JCI:";
    if (strncmp(text, prefix, sizeof(prefix) - 1) == 0) {
        size_t len = strlen(text + sizeof(prefix) - 1);
        if (len > JCL_MAX_APDU_DATA) {
            return 0;
        }
        memset(apdu, 0, sizeof(*apdu));
        apdu->cla = 0x80;
        apdu->ins = 0x70;
        apdu->lc = (uint8_t)len;
        memcpy(apdu->data, text + sizeof(prefix) - 1, len);
        return 1;
    }
    return jcl_parse_hex_apdu(text, apdu) ? 1 : 0;
}

static int run_apdu(jcl_sim_t *sim, const char *hex) {
    jcl_apdu_t apdu;
    if (!parse_command(hex, &apdu)) {
        fprintf(stderr, "Invalid command/APDU: %s\n", hex);
        return 1;
    }

    printf("> %s\n", hex);
    jcl_response_t response = jcl_sim_process(sim, &apdu);
    jcl_print_response(&response);
    return response.sw == JCL_SW_OK ? 0 : 2;
}

static int run_demo(void) {
    const char *script[] = {
        "80 F0 00 00",
        "JCI:STATE",
        "JCI:JCR",
        "JCI:JCID",
        "JCI:JCC",
        "JCI:AUTH1",
        "JCI:AUTH2",
        "JCI:SENDhola-jci",
        "80 CA 00 02",
        "80 CA 00 03",
        "80 E0 00 00 01 10",
        "80 F2 00 00",
        "00 A4 00 00 02 6F 07",
        "00 B0 00 00 00 0F",
        "JCI:JNS0000000000000000",
        "00 20 00 01 04 31 32 33 34",
        "80 E0 02 00 01 04",
        "80 E2 02 20 04 54 45 53 54",
        "80 E0 02 20 01 04",
        "00 B0 00 00 00 0F",
        "80 88 00 00 10 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF",
        "00 A4 00 00 02 6F 3C",
        "00 D6 00 00 05 48 6F 6C 61 21",
        "00 B0 00 00 00 05",
        "80 E0 01 80 01 05",
    };
    jcl_sim_t sim;
    int rc = 0;

    jcl_sim_init(&sim);
    for (size_t i = 0; i < sizeof(script) / sizeof(script[0]); ++i) {
        int step = run_apdu(&sim, script[i]);
        if (step == 1) {
            rc = 1;
        }
        putchar('\n');
    }
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    if (strcmp(argv[1], "--demo") == 0) {
        return run_demo();
    }

    jcl_sim_t sim;
    int rc = 0;
    jcl_sim_init(&sim);

    for (int i = 1; i < argc; ++i) {
        int step = run_apdu(&sim, argv[i]);
        if (step == 1) {
            rc = 1;
        }
        putchar('\n');
    }

    return rc;
}
