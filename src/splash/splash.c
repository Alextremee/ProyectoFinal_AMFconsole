/*
 * retroconsole-splash: vuelca un PNG al framebuffer /dev/fb0
 * y opcionalmente reproduce un WAV.
 * Uso: retroconsole-splash <imagen.png> [segundos]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <png.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <imagen.png> [segundos]\n", argv[0]);
        return 1;
    }
    int seconds = (argc >= 3) ? atoi(argv[2]) : 3;

    /* Abrir framebuffer */
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) { perror("open fb"); return 1; }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("ioctl fb"); close(fbfd); return 1;
    }

    long screensize = finfo.smem_len;
    unsigned char* fb = (unsigned char*)mmap(0, screensize,
        PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); close(fbfd); return 1; }

    /* Negro de fondo */
    memset(fb, 0, screensize);

    /* Cargar PNG */
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { perror("fopen"); munmap(fb, screensize); close(fbfd); return 1; }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, fp);
    png_read_info(png, info);

    int w = png_get_image_width(png, info);
    int h = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    png_bytep* rows = (png_bytep*)malloc(sizeof(png_bytep) * h);
    for (int y = 0; y < h; y++)
        rows[y] = (png_byte*)malloc(png_get_rowbytes(png, info));
    png_read_image(png, rows);
    fclose(fp);

    /* Centrar en pantalla */
    int fb_w = vinfo.xres;
    int fb_h = vinfo.yres;
    int bpp = vinfo.bits_per_pixel / 8;
    int off_x = (fb_w - w) / 2;
    int off_y = (fb_h - h) / 2;
    if (off_x < 0) off_x = 0;
    if (off_y < 0) off_y = 0;

    int max_w = (w < fb_w) ? w : fb_w;
    int max_h = (h < fb_h) ? h : fb_h;

    for (int y = 0; y < max_h; y++) {
        png_bytep row = rows[y];
        for (int x = 0; x < max_w; x++) {
            png_bytep px = &row[x * 4];
            int fb_pos = ((y + off_y) * finfo.line_length) + ((x + off_x) * bpp);
            if (fb_pos + bpp > screensize) continue;
            if (bpp == 4) {
                fb[fb_pos]     = px[2]; /* B */
                fb[fb_pos + 1] = px[1]; /* G */
                fb[fb_pos + 2] = px[0]; /* R */
                fb[fb_pos + 3] = 0xFF;  /* A */
            } else if (bpp == 2) {
                /* RGB565 */
                unsigned short rgb = ((px[0] & 0xF8) << 8) |
                                     ((px[1] & 0xFC) << 3) |
                                     (px[2] >> 3);
                *(unsigned short*)(fb + fb_pos) = rgb;
            }
        }
    }

    for (int y = 0; y < h; y++) free(rows[y]);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);

    sleep(seconds);

    munmap(fb, screensize);
    close(fbfd);
    return 0;
}
