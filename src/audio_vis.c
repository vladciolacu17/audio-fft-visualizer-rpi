// audio_vis.c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <fftw3.h>

#define PERIPH_BASE   0x3F000000UL
#define GPIO_OFFSET   0x200000
#define GPIO_BASE     (PERIPH_BASE + GPIO_OFFSET)
#define GPIO_LEN      0x1000

#define GPFSEL0       0x00
#define GPSET0        0x1C
#define GPCLR0        0x28
#define GPLEV0        0x34

#define PIN_CS    8
#define PIN_CLK   11
#define PIN_MOSI  10
#define PIN_MISO  9

#define FS          10000
#define PERIOD_NS   (1000000000L / FS)
#define NSAMPLES    256
#define NFFT        1024
#define NBARS       48

static volatile uint32_t *gpio = NULL;

static void delay_ns(long ns) {
    struct timespec ts;
    ts.tv_sec  = 0;
    ts.tv_nsec = ns;
    nanosleep(&ts, NULL);
}

static int gpio_map(void) {
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) { perror("open /dev/mem"); return -1; }
    void *map = mmap(NULL, GPIO_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, GPIO_BASE);
    close(mem_fd);
    if (map == MAP_FAILED) { perror("mmap"); return -1; }
    gpio = (volatile uint32_t *)map;
    return 0;
}

static void gpio_fsel(int pin, int mode) {
    int reg = pin / 10;
    int shift = (pin % 10) * 3;
    volatile uint32_t *fsel = gpio + (GPFSEL0/4) + reg;
    uint32_t val = *fsel;
    val &= ~(0x7 << shift);
    if (mode == 1) val |= (0x1 << shift);
    *fsel = val;
}

static void gpio_set(int pin) { *(gpio + GPSET0/4) = (1u << pin); }
static void gpio_clr(int pin) { *(gpio + GPCLR0/4) = (1u << pin); }
static int  gpio_read(int pin){ return ((*(gpio + GPLEV0/4)) & (1u << pin)) ? 1 : 0; }
static void gpio_write(int pin, int level) { if (level) gpio_set(pin); else gpio_clr(pin); }

static void spi_clk_pulse(void) {
    gpio_write(PIN_CLK, 1); delay_ns(2000);
    gpio_write(PIN_CLK, 0); delay_ns(2000);
}

static int adc0832_read_ch0(void) {
    int value = 0;

    gpio_write(PIN_CLK, 0);
    gpio_write(PIN_CS, 1);
    delay_ns(2000);

    gpio_write(PIN_CS, 0);
    delay_ns(2000);

    gpio_write(PIN_MOSI, 1); spi_clk_pulse();
    gpio_write(PIN_MOSI, 1); spi_clk_pulse();
    gpio_write(PIN_MOSI, 0); spi_clk_pulse();
    spi_clk_pulse();

    for (int i = 0; i < 8; i++) {
        gpio_write(PIN_CLK, 1);
        delay_ns(1000);
        int bit = gpio_read(PIN_MISO);
        value = (value << 1) | (bit & 1);
        gpio_write(PIN_CLK, 0);
        delay_ns(1000);
    }

    gpio_write(PIN_CS, 1);
    delay_ns(2000);
    return value & 0xFF;
}

static void hann_window(double *w, int n) {
    for (int i = 0; i < n; i++) {
        w[i] = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)(n - 1)));
    }
}

int main(void) {
    if (gpio_map() < 0) return 1;

    gpio_fsel(PIN_CS, 1);
    gpio_fsel(PIN_CLK, 1);
    gpio_fsel(PIN_MOSI, 1);
    gpio_fsel(PIN_MISO, 0);

    gpio_write(PIN_CS, 1);
    gpio_write(PIN_CLK, 0);
    gpio_write(PIN_MOSI, 0);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

    const int W = 1200, H = 600;
    SDL_Window *win = SDL_CreateWindow("Pi Zero2W ADC0832 Audio Visualizer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    if (!win) return 1;
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) return 1;

    double *x = (double*)fftw_alloc_real(NFFT);
    fftw_complex *X = (fftw_complex*)fftw_alloc_complex(NFFT/2 + 1);
    fftw_plan plan = fftw_plan_dft_r2c_1d(NFFT, x, X, FFTW_MEASURE);
    double *winv = (double*)malloc(sizeof(double) * NSAMPLES);
    hann_window(winv, NSAMPLES);

    double mags[NFFT/2 + 1], edges[NBARS + 1];
    double fmin = 20.0, fmax = (double)FS / 2.0;
    for (int i = 0; i <= NBARS; i++) {
        double t = (double)i / (double)NBARS;
        edges[i] = fmin * pow(fmax / fmin, t);
    }

    int samples[NSAMPLES];
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);

    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) running = 0;

        for (int n = 0; n < NSAMPLES; n++) {
            t.tv_nsec += PERIOD_NS;
            while (t.tv_nsec >= 1000000000L) { t.tv_nsec -= 1000000000L; t.tv_sec += 1; }
            int v = adc0832_read_ch0();
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            samples[n] = v;
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
        }

        for (int i = 0; i < NSAMPLES; i++) {
            double s = ((double)samples[i] - 128.0) / 128.0;
            x[i] = s * winv[i];
        }
        for (int i = NSAMPLES; i < NFFT; i++) x[i] = 0.0;
        fftw_execute(plan);

        double maxmag = 1e-9;
        for (int k = 0; k <= NFFT/2; k++) {
            double re = X[k][0], im = X[k][1];
            mags[k] = sqrt(re*re + im*im);
            if (mags[k] > maxmag) maxmag = mags[k];
        }
        for (int k = 0; k <= NFFT/2; k++) mags[k] /= maxmag;

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);

        int leftW = W / 2, y0 = H / 2;
        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
        for (int i = 1; i < NSAMPLES; i++) {
            int x1 = (i - 1) * (leftW - 1) / (NSAMPLES - 1);
            int x2 = i * (leftW - 1) / (NSAMPLES - 1);
            int s1 = samples[i - 1] - 128, s2 = samples[i] - 128;
            int y1 = y0 - (s1 * (H/2 - 20) / 128);
            int y2 = y0 - (s2 * (H/2 - 20) / 128);
            SDL_RenderDrawLine(ren, x1, y1, x2, y2);
        }

        int barW = (W - leftW) / NBARS;
        for (int b = 0; b < NBARS; b++) {
            int k0 = (int)floor(edges[b] * NFFT / FS);
            int k1 = (int)ceil(edges[b + 1] * NFFT / FS);
            if (k0 < 1) k0 = 1;
            if (k1 > NFFT/2) k1 = NFFT/2;
            double peak = 0.0;
            for (int k = k0; k <= k1; k++) if (mags[k] > peak) peak = mags[k];
            int h = (int)(peak * (H - 40));
            SDL_Rect rc = { leftW + b * barW + 2, H - 20 - h, barW - 4, h };
            SDL_SetRenderDrawColor(ren, 100, 220, 100, 255);
            SDL_RenderFillRect(ren, &rc);
        }

        SDL_RenderPresent(ren);
    }

    fftw_destroy_plan(plan);
    fftw_free(x);
    fftw_free(X);
    free(winv);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
