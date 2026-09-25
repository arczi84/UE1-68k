#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    /* In the inlined vid_OpenWindow path, MiniGL fills the public-screen,
     * inner-width and inner-height tag values, reloads IntuitionBase, then
     * calls OpenWindowTagList.  At this point d5/d6 still contain the offx/
     * offy arguments passed to MGLCreateContext. Replace the redundant base
     * reload with WA_Left/WA_Top assignments from d5/d6. IntuitionBase is
     * already in a6 from the immediately preceding LockPubScreen call. */
    static const unsigned char before[] = {
        0x2f, 0x40, 0x00, 0x48,
        0x2f, 0x44, 0x00, 0x50,
        0x2f, 0x47, 0x00, 0x58,
        0x2c, 0x79, 0x00, 0x08, 0xc3, 0xf0,
        0x43, 0xef, 0x00, 0x44,
        0x41, 0xf8, 0x00, 0x00
    };
    static const unsigned char after[] = {
        0x2f, 0x40, 0x00, 0x48, /* screen */
        0x2f, 0x44, 0x00, 0x50, /* inner width */
        0x2f, 0x47, 0x00, 0x58, /* inner height */
        0x2f, 0x45, 0x00, 0x60, /* WA_Left = offx */
        0x2f, 0x46, 0x00, 0x68, /* WA_Top  = offy */
        0x43, 0xef, 0x00, 0x44, /* a1 = tag list */
        0x91, 0xc8              /* a0 = NULL */
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

    for (i = 0; i <= size - (long)sizeof(before); ++i) {
        if (memcmp(data + i, before, sizeof(before)) == 0) {
            match = i;
            ++matches;
        }
    }
    if (matches != 1) {
        fprintf(stderr, "expected one MiniGL window-tag signature, found %d\n",
                matches);
        free(data);
        return 1;
    }
    memcpy(data + match, after, sizeof(after));

    out = fopen(argv[2], "wb");
    if (!out || fwrite(data, 1, (size_t)size, out) != (size_t)size ||
        fclose(out) != 0) {
        perror(argv[2]);
        free(data);
        return 1;
    }
    printf("patched MiniGL initial WA_Left/WA_Top at file offset 0x%lx\n",
           match);
    free(data);
    return 0;
}
