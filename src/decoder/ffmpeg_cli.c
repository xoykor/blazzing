/* SPDX-License-Identifier: MIT */
/*
 * FFmpeg CLI thumbnail backend.
 *
 * FFmpeg is spawned directly with argv (not through a shell), and raw RGB is
 * read through a pipe.  The backend enforces timeouts and rejects useless
 * near-black/near-uniform candidate frames before returning one to the cache.
 */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/decoder.h"
#include "decoder_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VIP_FFMPEG_DEFAULT_TIMEOUT_MS 12000u
#define VIP_FFMPEG_DEFAULT_FRAMES 3u
#define VIP_FFMPEG_DEFAULT_WIDTH 320u
#define VIP_FFMPEG_DEFAULT_HEIGHT 180u

typedef struct {
    char *ffmpeg_path;
    unsigned timeout_ms;
    unsigned candidate_frames;
    size_t width;
    size_t height;
} ffmpeg_impl_t;

static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * INT64_C(1000) + (int64_t)(ts.tv_nsec / 1000000L);
}

static void terminate_child(pid_t pid) {
    if (pid <= 0) return;
    if (kill(pid, SIGTERM) == 0) {
        for (unsigned i = 0; i < 10; ++i) {
            int status = 0;
            pid_t rc = waitpid(pid, &status, WNOHANG);
            if (rc == pid || rc < 0) return;
            struct timespec pause = {.tv_sec = 0, .tv_nsec = 20000000L};
            nanosleep(&pause, NULL);
        }
    }
    (void)kill(pid, SIGKILL);
    (void)waitpid(pid, NULL, 0);
}

static bool safe_mul3(size_t a, size_t b, size_t *out) {
    if (a == 0 || b == 0 || a > SIZE_MAX / b) return false;
    size_t ab = a * b;
    if (ab > SIZE_MAX / 3u) return false;
    *out = ab * 3u;
    return true;
}

/* Sample the image instead of scanning every pixel: this rejects blank
 * startup frames cheaply while keeping thumbnail generation bounded. */
static bool frame_is_useful(const uint8_t *rgb, size_t width, size_t height, size_t stride) {
    size_t sx = width / 64u;
    size_t sy = height / 36u;
    if (sx == 0) sx = 1;
    if (sy == 0) sy = 1;
    double n = 0.0, sum = 0.0, sumsq = 0.0;
    for (size_t y = 0; y < height; y += sy) {
        const uint8_t *row = rgb + y * stride;
        for (size_t x = 0; x < width; x += sx) {
            const uint8_t *pixel = row + x * 3u;
            double lum = 0.2126 * (double)pixel[0] + 0.7152 * (double)pixel[1] + 0.0722 * (double)pixel[2];
            n += 1.0;
            sum += lum;
            sumsq += lum * lum;
        }
    }
    if (n == 0.0) return false;
    double mean = sum / n;
    double variance = sumsq / n - mean * mean;
    return mean >= 7.0 && variance >= 18.0;
}

/* execv/execvp receives source as one argv element.  No shell is involved,
 * so playlist URLs and local paths cannot become shell syntax. */
static int spawn_ffmpeg(const ffmpeg_impl_t *impl, const char *source, int *stdout_fd) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            (void)dup2(devnull, STDIN_FILENO);
            (void)dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(126);
        if (pipefd[1] != STDOUT_FILENO) close(pipefd[1]);

        char frame_count[16];
        char filter[192];
        char rw_timeout[32];
        snprintf(frame_count, sizeof(frame_count), "%u", impl->candidate_frames);
        snprintf(filter, sizeof(filter),
                 "scale=%zu:%zu:force_original_aspect_ratio=decrease,"
                 "pad=%zu:%zu:(ow-iw)/2:(oh-ih)/2",
                 impl->width, impl->height, impl->width, impl->height);
        unsigned long long timeout_us = (unsigned long long)impl->timeout_ms * 1000ULL;
        snprintf(rw_timeout, sizeof(rw_timeout), "%llu", timeout_us);

        char *const argv[] = {
            impl->ffmpeg_path,
            "-hide_banner",
            "-loglevel", "error",
            "-nostdin",
            "-rw_timeout", rw_timeout,
            "-i", (char *)source,
            "-an", "-sn", "-dn",
            "-threads", "1",
            "-vf", filter,
            "-frames:v", frame_count,
            "-f", "rawvideo",
            "-pix_fmt", "rgb24",
            "pipe:1",
            NULL
        };
        if (strchr(impl->ffmpeg_path, '/')) execv(impl->ffmpeg_path, argv);
        else execvp(impl->ffmpeg_path, argv);
        _exit(127);
    }

    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) (void)fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    *stdout_fd = pipefd[0];
    return (int)pid;
}

static vip_status_t ffmpeg_capture(void *userdata,
                                   const char *source,
                                   vip_rgb_frame_t *frame_out,
                                   vip_error_t *error) {
    ffmpeg_impl_t *impl = userdata;
    size_t frame_bytes = 0;
    if (!impl || !source || !frame_out || !safe_mul3(impl->width, impl->height, &frame_bytes)) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "configuração inválida do decoder FFmpeg");
        return VIP_ERR_INVALID_ARGUMENT;
    }

    uint8_t *candidate = malloc(frame_bytes);
    if (!candidate) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para frame do FFmpeg");
        return VIP_ERR_NOMEM;
    }

    int fd = -1;
    int pid_int = spawn_ffmpeg(impl, source, &fd);
    if (pid_int < 0) {
        free(candidate);
        vip_error_set(error, VIP_ERR_IO, "não foi possível iniciar o FFmpeg: %s", strerror(errno));
        return VIP_ERR_IO;
    }
    pid_t pid = (pid_t)pid_int;
    size_t offset = 0;
    unsigned frames_seen = 0;
    bool valid = false;
    bool timed_out = false;
    int64_t deadline = monotonic_ms() + (int64_t)impl->timeout_ms;

    while (!valid && frames_seen < impl->candidate_frames) {
        int64_t remaining = deadline - monotonic_ms();
        if (remaining <= 0) {
            timed_out = true;
            break;
        }
        int wait_ms = remaining > 100 ? 100 : (int)remaining;
        struct pollfd pfd = {.fd = fd, .events = POLLIN | POLLHUP};
        int prc = poll(&pfd, 1, wait_ms);
        if (prc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (prc == 0) continue;
        if (pfd.revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(fd, candidate + offset, frame_bytes - offset);
            if (n > 0) {
                offset += (size_t)n;
                if (offset == frame_bytes) {
                    ++frames_seen;
                    if (frame_is_useful(candidate, impl->width, impl->height, impl->width * 3u)) {
                        valid = true;
                        break;
                    }
                    offset = 0;
                }
                continue;
            }
            if (n == 0) break;
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
        }
    }

    close(fd);
    terminate_child(pid);

    if (!valid) {
        free(candidate);
        if (timed_out) {
            vip_error_set(error, VIP_ERR_IO, "FFmpeg excedeu o limite de %u ms sem frame válido", impl->timeout_ms);
            return VIP_ERR_IO;
        }
        vip_error_set(error, VIP_ERR_INVALID_FRAME,
                      "FFmpeg não produziu frame visualmente útil (%u candidato(s))", frames_seen);
        return VIP_ERR_INVALID_FRAME;
    }

    frame_out->data = candidate;
    frame_out->width = impl->width;
    frame_out->height = impl->height;
    frame_out->stride = impl->width * 3u;
    vip_error_clear(error);
    return VIP_OK;
}

static void ffmpeg_destroy(void *userdata) {
    ffmpeg_impl_t *impl = userdata;
    if (!impl) return;
    free(impl->ffmpeg_path);
    free(impl);
}

vip_status_t vip_ffmpeg_decoder_create(vip_thumbnail_decoder_t **out,
                                       const vip_ffmpeg_decoder_config_t *config,
                                       vip_error_t *error) {
    if (!out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "saída do decoder ausente");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;

    ffmpeg_impl_t *impl = calloc(1, sizeof(*impl));
    vip_thumbnail_decoder_t *decoder = calloc(1, sizeof(*decoder));
    if (!impl || !decoder) {
        free(impl);
        free(decoder);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para decoder FFmpeg");
        return VIP_ERR_NOMEM;
    }

    const char *path = config && config->ffmpeg_path && config->ffmpeg_path[0]
                         ? config->ffmpeg_path : "ffmpeg";
    impl->ffmpeg_path = vip_strdup(path);
    impl->timeout_ms = config && config->timeout_ms ? config->timeout_ms : VIP_FFMPEG_DEFAULT_TIMEOUT_MS;
    impl->candidate_frames = config && config->candidate_frames
                               ? config->candidate_frames : VIP_FFMPEG_DEFAULT_FRAMES;
    impl->width = config && config->output_width ? config->output_width : VIP_FFMPEG_DEFAULT_WIDTH;
    impl->height = config && config->output_height ? config->output_height : VIP_FFMPEG_DEFAULT_HEIGHT;

    bool path_ok = impl->ffmpeg_path != NULL;
    bool limits_ok = impl->candidate_frames <= 30u && impl->width <= 4096u && impl->height <= 4096u;
    if (!path_ok || !limits_ok) {
        vip_status_t st = path_ok ? VIP_ERR_INVALID_ARGUMENT : VIP_ERR_NOMEM;
        vip_error_set(error, st, path_ok ? "configuração FFmpeg fora dos limites"
                                        : "sem memória para caminho do FFmpeg");
        ffmpeg_destroy(impl);
        free(decoder);
        return st;
    }

    decoder->name = "ffmpeg-cli";
    decoder->impl = impl;
    decoder->capture_impl = ffmpeg_capture;
    decoder->destroy_impl = ffmpeg_destroy;
    *out = decoder;
    vip_error_clear(error);
    return VIP_OK;
}
