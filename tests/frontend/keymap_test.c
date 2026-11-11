/*
 * N84 - key map file test: the shipped dist/n84.keys.tns is exactly the
 * default map as keymap_write prints it, it loads back to the defaults, and
 * bad lines are reported without dropping the good ones.
 *   keymap_test FILE        check FILE
 *   keymap_test --write     print the default key file (to regenerate it)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frontend/keymap_ti8x.h"

static int failures;
#define CHECK(cond, ...) do { if (cond) printf("  ok   "); else { printf("  FAIL "); failures++; } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

static char *slurp(FILE *f, size_t *n) {
    static char buf[16384];
    rewind(f);
    *n = fread(buf, 1, sizeof buf - 1, f);
    buf[*n] = 0;
    return buf;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--write")) { keymap_reset(); keymap_write(stdout); return 0; }
    if (argc != 2) { fprintf(stderr, "usage: keymap_test FILE | --write\n"); return 2; }

    uint8_t def[PK_COUNT];
    keymap_reset();
    for (int k = 1; k < PK_COUNT; k++) def[k] = keymap_get((plat_key_t)k);

    /* shipped file == generated defaults */
    FILE *t = tmpfile();
    keymap_write(t);
    size_t n1, n2;
    char *gen = strdup(slurp(t, &n1));
    fclose(t);
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    char *shipped = slurp(f, &n2);
    fclose(f);
    CHECK(n1 == n2 && !memcmp(gen, shipped, n1), "%s matches the default map (regenerate with --write)", argv[1]);
    free(gen);

    char err[96] = "";
    int r = keymap_load(argv[1], err, sizeof err);
    int same = 1;
    for (int k = 1; k < PK_COUNT; k++) same &= keymap_get((plat_key_t)k) == def[k];
    CHECK(r == 1 && same && keymap_custom(), "it loads back to the defaults");

    /* overrides, '=' form, comments, none, bad lines */
    char path[] = "/tmp/n84keysXXXXXX";
    int fd = mkstemp(path);
    f = fdopen(fd, "w");
    fprintf(f, "# test\nctrl ALPHA\nshift = 2nd   # swapped\na none\n= Y=\nfoo 2ND\n?! ENTER\nb bar\ntab\n");
    fclose(f);
    r = keymap_load(path, err, sizeof err);
    remove(path);
    CHECK(r == -1 && strstr(err, "line 6"), "first bad line reported: %s", err);
    CHECK(keymap_get(PK_CTRL) == TK_ALPHA && keymap_get(PK_SHIFT) == TK_2ND, "ctrl/shift swapped");
    CHECK(keymap_get(PK_A) == TK_NONE && keymap_get(PK_EQU) == TK_YEQU, "'none' and labels as values");
    CHECK(keymap_get(PK_B) == def[PK_B] && keymap_get(PK_QUESEXCL) == TK_NONE, "bad lines leave keys unchanged");
    CHECK(keymap_get(PK_ESC) == TK_CLEAR, "unlisted keys keep their default");

    r = keymap_load("/nonexistent/n84.keys.tns", err, sizeof err);
    CHECK(r == 0 && !keymap_custom() && keymap_get(PK_CTRL) == TK_2ND, "no file: defaults");

    printf(failures ? "keymap: %d FAILED\n" : "keymap: ALL OK\n", failures);
    return failures != 0;
}
