/* Read "item frame phase close bubble" per line. Emit one CRC32 per line and
 * optionally append every complete panel image to argv[2], in wire order. */
#include "deepseek_review.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const baji_rect_t panel = {0, 0, DSK_SIDE, DSK_SIDE};
static uint8_t screen[DSK_SIDE * DSK_SIDE * 2];
static uint32_t table[256];

static uint8_t *load(const char *path, size_t *size)
{
    FILE *in = fopen(path, "rb");
    if (!in) return NULL;
    uint8_t *data = NULL;
    if (fseek(in, 0, SEEK_END) == 0) {
        long end = ftell(in);
        if (end >= 0 && fseek(in, 0, SEEK_SET) == 0) {
            *size = (size_t)end;
            data = malloc(*size);
            if (data && fread(data, 1, *size, in) != *size) {
                free(data);
                data = NULL;
            }
        }
    }
    fclose(in);
    return data;
}

int main(int argc, char **argv)
{
    bool blended = argc > 1 && strcmp(argv[1], "--blend") == 0;
    if (blended) { argv++; argc--; }
    if (argc < 2 || argc > 3) return 2;
    size_t size = 0;
    uint8_t *data = load(argv[1], &size);
    dsk_art_t art;
    if (!dsk_art_open(&art, data, size)) {
        fputs("invalid or unavailable DSK2 pack\n", stderr);
        free(data);
        return 1;
    }
    FILE *raw = argc == 3 ? fopen(argv[2], "wb") : NULL;
    if (argc == 3 && !raw) { free(data); return 1; }
    dsk_canvas_t canvas;
    dsk_canvas_init(&canvas, &art);
    for (unsigned i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (unsigned j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
        table[i] = crc;
    }
    unsigned item, frame, phase, closeup, bubble, mix = 0;
    int status = 0, fields;
    int expected = blended ? 6 : 5;
    while ((fields = blended ? scanf("%u%u%u%u%u%u", &item, &frame, &phase, &closeup, &bubble, &mix) :
                               scanf("%u%u%u%u%u", &item, &frame, &phase, &closeup, &bubble)) == expected) {
        if (item >= DSK_ITEMS || frame > 255 || phase >= DSK_PHASES ||
            closeup > 1 || bubble >= art.bubble_steps ||
            !dsk_canvas_select_blend(&canvas, (dsk_frame_t){item, frame, phase, closeup, bubble}, mix)) {
            fputs("invalid selection\n", stderr);
            status = 2;
            break;
        }
        dsk_paint(&panel, screen, &canvas);
        uint32_t crc = UINT32_MAX;
        for (size_t i = 0; i < sizeof(screen); i++) crc = (crc >> 8) ^ table[(crc ^ screen[i]) & 255];
        printf("%08lx\n", (unsigned long)~crc);
        if (raw && fwrite(screen, sizeof(screen), 1, raw) != 1) { status = 1; break; }
    }
    if (fields != EOF && fields != expected) status = 2;
    if (raw && fclose(raw) != 0) status = 1;
    if (fflush(stdout) != 0) status = 1;
    dsk_art_close(&art);
    free(data);
    return status;
}
