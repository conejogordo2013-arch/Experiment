#include "jcl_sim.h"

#include <stdio.h>
#include <string.h>

static void print_help(const char *program) {
    printf("JuanCarlosLegals SIM Alpha emulator\n");
    printf("Usage:\n");
    printf("  %s --demo\n", program);
    printf("  %s '<hex APDU>' ['<hex APDU>' ...]\n\n", program);
    printf("Supported educational APDUs:\n");
    printf("  00 A4 00 00 02 <FID>       SELECT file\n");
    printf("  00 B0 <off_hi> <off_lo> 00 [Le] READ BINARY selected file\n");
    printf("  00 D6 <off_hi> <off_lo> Lc <data> UPDATE BINARY selected writable file\n");
    printf("  00 20 00 01 Lc <ASCII PIN> VERIFY PIN (default 1234)\n");
    printf("  80 88 00 00 Lc <RAND>      JuanCarlosLegals AUTH toy response\n");
    printf("  80 F2 00 00                GET STATUS\n");
}

static int run_apdu(jcl_sim_t *sim, const char *hex) {
    jcl_apdu_t apdu;
    if (!jcl_parse_hex_apdu(hex, &apdu)) {
        fprintf(stderr, "Invalid APDU hex: %s\n", hex);
        return 1;
    }

    printf("> %s\n", hex);
    jcl_response_t response = jcl_sim_process(sim, &apdu);
    jcl_print_response(&response);
    return response.sw == JCL_SW_OK ? 0 : 2;
}

static int run_demo(void) {
    const char *script[] = {
        "80 F2 00 00",
        "00 A4 00 00 02 6F 07",
        "00 B0 00 00 00 0F",
        "00 20 00 01 04 31 32 33 34",
        "00 B0 00 00 00 0F",
        "80 88 00 00 10 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF",
        "00 A4 00 00 02 6F 3C",
        "00 D6 00 00 05 48 6F 6C 61 21",
        "00 B0 00 00 00 05",
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
