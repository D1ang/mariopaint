/*
 * Mario Paint — Static Recompilation
 *
 * Entry point: loads the ROM, configures SNES Mouse on port 1, registers
 * recompiled functions, and picks one of three modes (docs/architecture.md):
 *
 *   default          timed recomp: the genuine ROM in LakeSnes's cycle-timed
 *                    frame loop, with every function in k_verified running as
 *                    native C in place of its ROM routine
 *   MP_REALFRAME=1   no native code: the oracle the harness compares against
 *   MP_NATIVE=1      the native-driven boot chain below, interpreter fallback
 *
 * Native-driven frame architecture:
 *   mp_01E2CE (frame sync) is the frame driver. Whenever game code
 *   calls it — during init, the main loop, or fade effects — it
 *   drives one complete frame cycle:
 *     1. snesrecomp_begin_frame() — SDL event pump, input
 *     2. snesrecomp_trigger_vblank() — PPU VBlank processing
 *     3. mp_0080D4() — NMI handler (DMA, PPU writes, joypad)
 *     4. snesrecomp_end_frame() — render, present, 60Hz sync
 *
 *   This means mp_00865A's infinite loop runs naturally, with
 *   mp_01E2CE yielding to the frame driver each iteration.
 *   g_quit is set when the user closes the window, causing
 *   mp_00865A to break its loop and return to main().
 *
 * Unrecompiled subroutines fall through to snesrecomp's 65816
 * interpreter (recomp_interp_*), so func_table_call never dead-ends
 * on an address we haven't translated yet.
 */

#include <snesrecomp/snesrecomp.h>
#include <snesrecomp/platform.h>
#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <SDL.h>   /* SDL2main: renames main to SDL_main (mingw links need it) */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

/* Global quit flag — set by mp_01E2CE when window is closed */
bool g_quit = false;

/*
 * Recompiled functions that pass tools/conformance.py: every checked call
 * byte-identical to the ROM (WRAM, VRAM, CGRAM, OAM), none skipped. These run
 * natively by default. Suffix L = returns with RTL; F = waits for frames
 * (checked frame by frame, runs as a framed intercept). Regenerate with conformance.py (scratch/conf/verified.txt)
 * and paste; never add a function here that has not passed.
 */
static const char k_verified[] =
    "00815B,008187,0081CA,00823C,00833B,00837D,00849D,0084AF,008683L,"
    "00878F,0087EE,008994LF,0089B1,0089C3,008A12L,008A16,008A39,"
    "008A75L,008AFFL,008B03,008B14L,008B18,008B29,008B3CL,008B44L,"
    "008B48,008D2C,008E0B,009001,009082,009107,0091C7,009378,0096A1F,"
    "0096AB,0096E1F,009760F,009853F,0098C3F,0098F8F,009928F,00998BF,"
    "009A35F,009B63F,009B96F,009BBAF,009C1BLF,009C25F,009D7D,009DFB,"
    "009FC4F,00A097LF,00A0B3,00A22D,00A25B,00A2CCF,00A363F,00A44DLF,"
    "00A451F,00A4ADLF,00A4B4F,00A525F,00A87EL,00A982F,00AAC7F,00AAFB,"
    "00AB26,00AB8A,00ACDC,00ADFB,00AF2C,00AF5A,00B051,00B0D3,00B1C2,"
    "00B23C,00B25E,00B305,00B393,00B5CB,00B66C,00B6F4,00B780,00B7D6,"
    "00B910,00B926,00BA69F,00BA78,00C3DE,00C410L,00C414,00C42AF,00C490,"
    "00C6C6F,00C6EFF,00C873,00D514F,00D52C,00D590,00D5F5L,00D659,"
    "00D6D3L,00D73C,00D824,00D842,00DE8E,00DF1C,00DF2C,00DF95,00DFC4,"
    "00E0D4,00E167,00E233,00E25C,00E64FF,00E7D9F,00EB3B,00ED76,00F054F,"
    "00F0A4,00F1FD,00F39E,00F3C9,00F3DEF,00F623F,00F6AC,00F7D4F,00F921,"
    "00FA35F,00FAB5F,00FB47,00FB5C,00FC93,00FCF4,00FD06,00FD18,00FD83,"
    "00FDFE,00FE21,00FE54,00FE9E,00FEB1,00FF36,00FF5FF,0182B1,0182F6,"
    "01834B,0183B6F,0185A3F,018C43,018CB7,018CBFF,018F1C,018F52F,"
    "01904A,019130,01916E,0191AC,0191FE,019244,01934F,01962CL,01972D,"
    "019DFEL,01A274,01A2B7,01A2E7,01A30D,01A373,01A44D,01A54F,01A5BB,"
    "01A5E4F,01A60E,01A819,01A8C4,01A93D,01AA48,01AAAD,01AAE3,01AB05,"
    "01AB61,01AC70,01ACD5,01AD78,01AD9C,01AF14,01AFDB,01B255,01B5D1,"
    "01B5DC,01B698,01B925,01BAD1,01BB1A,01BBBE,01BC57,01BC7B,01BD01,"
    "01BD87,01BDE6,01BE40,01BF29,01BF99,01C012F,01C0D3F,01C3C4,01C51D,"
    "01C5F8F,01C622F,01C95EF,01C9CE,01C9EF,01CA98,01CAAE,01CB10,01CD86,"
    "01CDE1LF,01D2BFL,01D308L,01D328L,01D348L,01D368L,01D3ABL,01D3BALF,"
    "01D409F,01D56DL,01D597L,01D75BL,01D7F0,01D810L,01D874,01D8CE,"
    "01D9A9,01D9E1L,01DA0D,01DA68,01DA73,01DA88,01DABF,01DAF9,01DCB9L,"
    "01DD79L,01DDB8L,01DDE1L,01DE97L,01DEB2L,01DECDL,01DF25L,01DFC1,"
    "01DFD3L,01E024L,01E033L,01E042L,01E060L,01E06FL,01E09BL,01E103L,"
    "01E1ABL,01E20CL,01E238L,01E298,01E2F3L,01E30EL,01E3ACL,01E429L,"
    "01E460L,01E500L,01E59BL,01E60CL,01E66BL,01E6CAL,01E6D0L,01E747L,"
    "01E87BL,01E88AL,01E895L,01E8F6L,01E93AL,01E998L,01EB81LF,01EBB5LF,"
    "01EC4BF,01ECAFF,01ECD5,01ED03,01EDAB,01EDCA,01EFB9,01F018,01F825L,"
    "01F84FL,01F91EL,01FA68L,0FC000L";

/* Routines that present a frame: $01:E2CE waits for the NMI; $01:E794 and
 * $01:E7C9 fade the screen in/out over several frames. */
static const char k_frame_drivers[] = "01E2CEL,01E794L,01E7C9L";

/* "01D9E1L,008B48" -> fn(addr, is_long) for each; suffix L = returns with RTL,
 * F = waits for frames (an intercept list gives those to framed_fn). */
static void add_addr_list2(const char *list, void (*fn)(uint32_t, bool), void (*framed_fn)(uint32_t, bool)) {
    while (list && *list) {
        /* Six hex digits (F is a suffix here, not a digit). */
        char digits[7] = {0};
        int nd = 0;
        while (nd < 6 && isxdigit((unsigned char)list[nd])) { digits[nd] = list[nd]; nd++; }
        if (nd == 0) break;
        unsigned long addr = strtoul(digits, NULL, 16);
        char *end = (char *)list + nd;
        bool is_long = false, framed = false;
        for (;; end++) {
            if (*end == 'L' || *end == 'l') is_long = true;
            else if (*end == 'F' || *end == 'f') framed = true;
            else break;
        }
        (framed && framed_fn ? framed_fn : fn)((uint32_t)addr, is_long);
        list = (*end == ',') ? end + 1 : end;
    }
}
static void add_addr_list(const char *list, void (*fn)(uint32_t, bool)) { add_addr_list2(list, fn, NULL); }

#ifdef MP_HAVE_GEN
void gen_register_all(void);
#endif

static void dump_profile(void) {
    recomp_timed_profile_dump(atoi(getenv("MP_PROFILE")));
}

int main(int argc, char *argv[]) {
    argc = snesrecomp_parse_args(argc, argv);   /* --headless, --record out.mp4 */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [--headless] [--record out.mp4] <mario_paint.sfc>\n", argv[0]);
        return 1;
    }

    /* Initialize snesrecomp (LakeSnes + SDL2 + ImGui menu) */
    if (!snesrecomp_init("Mario Paint", 3)) {
        fprintf(stderr, "Failed to initialize snesrecomp\n");
        return 1;
    }

    /* Load the ROM */
    if (!snesrecomp_load_rom(argv[1])) {
        fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
        snesrecomp_shutdown();
        return 1;
    }

    /* Configure port 1 as SNES Mouse (Mario Paint requires it) */
    recomp_input_set_device(1, SNES_INPUT_MOUSE);
    /* Cursor X/Y, so scripted "@x:@y" mouse steps steer the genuine ROM too. */
    recomp_input_set_mouse_cursor_addr(0x04DC, 0x04DE);

    /* Register all recompiled functions */
    mp_register_all();
#ifdef MP_HAVE_GEN
    /* Generated routines (gen/, from your ROM) are not validated yet, so they
     * are opt-in: MP_GEN=1 registers them over the hand-ports at the same
     * addresses. */
    if (getenv("MP_GEN")) gen_register_all();
#endif

    /* Anything not yet recompiled runs the original ROM code on the
     * LakeSnes CPU instead of silently doing nothing. */
    recomp_interp_set_enabled(true);

    /*
     * MP_INTERP_FUNCS="018000,0087EE" — hand specific addresses back to the
     * interpreter even though a recompiled version exists. Registering NULL
     * makes func_table_lookup miss, so dispatch falls through to the genuine
     * ROM code. This is the A/B lever for "is our translation of X wrong?":
     * run it interpreted and see if the symptom goes away.
     */
    {
        const char *list = getenv("MP_INTERP_FUNCS");
        while (list && *list) {
            char *end;
            unsigned long addr = strtoul(list, &end, 16);
            if (end == list) break;
            func_table_register((uint32_t)addr, NULL);
            printf("mp: $%06lX handed to the interpreter\n", addr);
            list = (*end == ',') ? end + 1 : end;
        }
    }

    /*
     * Timed loop (default, MP_REALFRAME=1, MP_TIMED, MP_VALIDATE): LakeSnes runs
     * the genuine ROM cycle-accurately; native bodies replace ROM routines at
     * their entry through the opcode hook. MP_REALFRAME=1 alone is the ground
     * truth the recomp is validated against.
     */
    if (!getenv("MP_NATIVE")) {
        bool real = getenv("MP_REALFRAME") != NULL;
        printf("Mario Paint recomp: %s\n", real ? "real-frame mode (genuine ROM, no native code)"
                                                : "timed recomp (verified functions native)");
        if (!real && !getenv("MP_VALIDATE") && !getenv("MP_TIMED"))
            add_addr_list2(k_verified, recomp_timed_add_intercept, recomp_timed_add_framed_intercept);
        /*
         * MP_TIMED="01D9E1L,008B48" — run exactly these natively instead of
         * k_verified (suffix L = returns with RTL). Add MP_REALFRAME=1 to start
         * from no native code at all.
         *
         * MP_VALIDATE="..." (same format) — lockstep validation instead: every
         * call runs the native body on a saved copy of the machine and compares
         * it with the genuine routine, which is what actually runs. Reports one
         * VALIDATE line per function at exit.
         *
         * MP_PROFILE=N — at exit, dump the N hottest JSR/JSL targets with the
         * M/X flags they are entered with (what to recompile next).
         */
        add_addr_list2(getenv("MP_TIMED"), recomp_timed_add_intercept, recomp_timed_add_framed_intercept);
        add_addr_list(getenv("MP_VALIDATE"), recomp_timed_add_validate);
        /* Frame drivers: a native body under validation that calls one waits
         * there for the genuine frame (checked frame by frame); a framed
         * intercept (suffix F) waits there while the timed CPU runs it. */
        add_addr_list(k_frame_drivers, recomp_timed_add_frame_driver);
        if (getenv("MP_VALIDATE")) atexit(recomp_timed_validate_report);
        if (!real || getenv("MP_TIMED") || getenv("MP_VALIDATE") || getenv("MP_PROFILE"))
            recomp_timed_recomp_enable();
        if (getenv("MP_PROFILE")) {
            recomp_timed_profile_enable();
            atexit(dump_profile);
        }
        while (snesrecomp_realframe_begin())
            snesrecomp_realframe_end();
        snesrecomp_shutdown();
        return 0;
    }

    /*
     * Let interpreted ROM code reach the recompiled frame driver.
     *
     * The interpreter is a plain cpu_runOpcode loop: a JSL inside interpreted
     * code is executed by the emulated CPU and never routed through the
     * dispatch table, so a recompiled C function is unreachable from it. That
     * matters for $01E2CE, which *is* our frame driver (begin_frame ->
     * trigger_vblank -> NMI -> end_frame). Without this, any routine we hand to
     * the interpreter runs its whole vblank-waiting loop without presenting a
     * single frame — the title screen ran entirely invisibly and the window sat
     * frozen until control came back.
     *
     * The opcode-fetch hook that timed-recomp interception uses works here too
     * (LakeSnes calls it from cpu_runOpcode, which the interpreter drives), so
     * registering $01E2CE as an intercept makes interpreted code call the
     * native frame driver and present frames normally.
     *
     * Recomp path only — real-frame mode must run the genuine ROM untouched.
     *
     * $018260 is the important one. The ROM's title loop has no frame sync in
     * it at all — it spins polling the mouse bytes at $04C6/$04C8/$04CA and
     * bails to the demo after $800 idle iterations. On hardware an NMI drives
     * the frame underneath it; interpreted here, it burns all 2048 iterations
     * instantly with nothing drawn and no input possible, so the title screen
     * flashed past invisibly. Our recompiled mp_018260 drives a frame per
     * iteration, which is what makes the screen appear and respond.
     *
     * The fades are the same story: their ROM loops step brightness one step
     * per vblank, so interpreted they finish instantly with nothing drawn and
     * the screen left mid-fade. The recompiled versions drive a frame per step.
     */
    recomp_timed_add_intercept(0x018260, false);  /* title loop,  JSR -> RTS */
    recomp_timed_add_intercept(0x01E2CE, true);   /* frame sync,  JSL -> RTL */
    recomp_timed_add_intercept(0x01E794, true);   /* fade in,     JSL -> RTL */
    recomp_timed_add_intercept(0x01E7C9, true);   /* fade out,    JSL -> RTL */
    recomp_timed_recomp_enable();

    printf("Mario Paint recomp: running boot chain\n");

    /*
     * Run the full boot chain. mp_01E2CE drives frames internally,
     * so the entire sequence works naturally:
     *   mp_008000 → mp_008013 → mp_0084D5 → mp_00865A (infinite loop)
     *
     * mp_00865A runs until g_quit is set (window close).
     */
    mp_008000();

    snesrecomp_shutdown();
    printf("Mario Paint recomp: shutdown complete\n");
    return 0;
}
