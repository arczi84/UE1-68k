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
    /*
     * CGX_ListModes() compares SDL BitsPerPixel against P96's significant
     * colour depth.  Accept the exact value first; if it differs, retry after
     * mapping SDL's 32-bit storage request to P96's 24-bit colour depth.
     */
    static const unsigned char depth_compare[] = {
        0x42, 0x80, 0x10, 0x2a, 0x00, 0x04,
        0xb0, 0x82, 0x57, 0xc0, 0x49, 0xc0, 0x60, 0x06,
        0x42, 0x80, 0x60, 0x02, 0x70, 0xff
    };
    static const unsigned char bpp_compare[] = {
        0x42, 0x80, 0x10, 0x2a, 0x00, 0x04,
        0xb0, 0x82, 0x67, 0x02, 0x51, 0x80, 0xb0, 0x82,
        0x57, 0xc0, 0x49, 0xc0, 0x4e, 0x71
    };

    FILE *in;
    FILE *out;
    unsigned char *data;
    long size;
    long compare_at;

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

    compare_at = find_unique(data, size, depth_compare, sizeof(depth_compare));
    if (compare_at < 0) {
        fprintf(stderr, "unique CGX_ListModes signatures not found\n");
        free(data);
        return 1;
    }

    memcpy(data + compare_at, bpp_compare, sizeof(bpp_compare));

    out = fopen(argv[2], "wb");
    if (!out || fwrite(data, 1, (size_t)size, out) != (size_t)size ||
        fclose(out) != 0) {
        perror(argv[2]);
        free(data);
        return 1;
    }

    printf("patched P96 24-depth/32-bpp alias at 0x%lx\n", compare_at);
    free(data);
    return 0;
}
