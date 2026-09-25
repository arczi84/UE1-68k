#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long find_unique(const unsigned char *data, long size,
                        const unsigned char *pattern, size_t pattern_size)
{
    long found = -1;
    int matches = 0;
    long i;

    for (i = 0; i <= size - (long)pattern_size; ++i) {
        if (memcmp(data + i, pattern, pattern_size) == 0) {
            found = i;
            ++matches;
        }
    }
    return matches == 1 ? found : -1;
}

int main(int argc, char **argv)
{
    /* CGX_GL_Init(): mglLockMode(context, MGL_LOCK_SMART). */
    static const unsigned char init_old[] = {
        0x48, 0x78, 0x00, 0xe6,
        0x2f, 0x39, 0x00, 0x0f, 0xe8, 0xa4,
        0x4e, 0xb9, 0x00, 0x28, 0x0f, 0xd4
    };
    /* Preserve instruction width while changing the second argument to FALSE. */
    static const unsigned char init_new[] = {
        0x42, 0xa7, 0x4e, 0x71,
        0x2f, 0x39, 0x00, 0x0f, 0xe8, 0xa4,
        0x4e, 0xb9, 0x00, 0x28, 0x0f, 0xd4
    };
    /* MGL wrapper: dispatch->MGLLockMode at table offset 0x01ac. */
    static const unsigned char wrapper_old[] = {
        0x20, 0x79, 0x00, 0x10, 0x08, 0xec,
        0x22, 0x68, 0x00, 0x10,
        0x2f, 0x51, 0x00, 0x04,
        0x22, 0x68, 0x01, 0xac,
        0x4e, 0xd1
    };
    /* Same ABI, dispatch->MGLEnableSync at table offset 0x018c. */
    static const unsigned char wrapper_new[] = {
        0x20, 0x79, 0x00, 0x10, 0x08, 0xec,
        0x22, 0x68, 0x00, 0x10,
        0x2f, 0x51, 0x00, 0x04,
        0x22, 0x68, 0x01, 0x8c,
        0x4e, 0xd1
    };
    FILE *in;
    FILE *out;
    unsigned char *data;
    long size;
    long init_at;
    long wrapper_at;

    if (argc != 3) {
        fprintf(stderr, "usage: %s input output\n", argv[0]);
        return 2;
    }

    in = fopen(argv[1], "rb");
    if (!in || fseek(in, 0, SEEK_END) != 0 || (size = ftell(in)) < 0 ||
        fseek(in, 0, SEEK_SET) != 0) {
        perror(argv[1]);
        return 1;
    }
    data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, in) != (size_t)size) {
        fprintf(stderr, "cannot read input\n");
        fclose(in);
        free(data);
        return 1;
    }
    fclose(in);

    init_at = find_unique(data, size, init_old, sizeof(init_old));
    wrapper_at = find_unique(data, size, wrapper_old, sizeof(wrapper_old));
    if (init_at < 0 || wrapper_at < 0) {
        fprintf(stderr, "unique MiniGL init signatures not found\n");
        free(data);
        return 1;
    }

    memcpy(data + init_at, init_new, sizeof(init_new));
    memcpy(data + wrapper_at, wrapper_new, sizeof(wrapper_new));

    out = fopen(argv[2], "wb");
    if (!out || fwrite(data, 1, (size_t)size, out) != (size_t)size ||
        fclose(out) != 0) {
        perror(argv[2]);
        free(data);
        return 1;
    }

    printf("patched mglEnableSync(FALSE) at 0x%lx, wrapper at 0x%lx\n",
           init_at, wrapper_at);
    free(data);
    return 0;
}
