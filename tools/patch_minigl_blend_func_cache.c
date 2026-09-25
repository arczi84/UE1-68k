#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    /* GLBlendFunc starts by comparing the requested source factor with its
     * cached value.  The classic backend can lose the corresponding Warp3D
     * state while the cache still matches, exactly like the GL_BLEND enable
     * cache.  Make every call program both factors while retaining the cache
     * fields for state queries. */
    static const unsigned char signature[] = {
        0xb4, 0xaa, 0x12, 0x08,
        0x67, 0x00, 0x00, 0xe0,
        0x25, 0x42, 0x12, 0x08,
        0x25, 0x43, 0x12, 0x0c
    };
    FILE *in;
    FILE *out;
    unsigned char *data;
    long size;
    long match = -1;
    long i;
    int matches = 0;

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

    for (i = 0; i <= size - (long)sizeof(signature); ++i) {
        if (memcmp(data + i, signature, sizeof(signature)) == 0) {
            match = i;
            ++matches;
        }
    }
    if (matches != 1) {
        fprintf(stderr, "expected one GLBlendFunc cache signature, found %d\n", matches);
        free(data);
        return 1;
    }

    /* Replace BEQ.W with two 680x0 NOPs. */
    data[match + 4] = 0x4e;
    data[match + 5] = 0x71;
    data[match + 6] = 0x4e;
    data[match + 7] = 0x71;

    out = fopen(argv[2], "wb");
    if (!out || fwrite(data, 1, (size_t)size, out) != (size_t)size ||
        fclose(out) != 0) {
        perror(argv[2]);
        free(data);
        return 1;
    }
    printf("disabled GLBlendFunc factor-cache early-out at file offset 0x%lx\n",
           match + 4);
    free(data);
    return 0;
}
