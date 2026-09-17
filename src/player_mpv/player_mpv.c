/* SPDX-License-Identifier: MIT */
/*
 * Persistent mpv adapter.
 *
 * A single idle mpv process is controlled through JSON IPC. Media URLs are
 * sent over the private Unix socket instead of argv. On X11, mpv is embedded
 * directly into the application's video container with --wid.
 */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/player_mpv.h"

#include <json-c/json.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VIP_MPV_LOG_CAP 4096u
#define VIP_MPV_IPC_BUF_CAP 32768u
#define VIP_MPV_CONNECT_TIMEOUT_MS 2500

struct vip_mpv_player {
    pthread_mutex_t mutex;
    pthread_mutex_t write_mutex;
    pthread_t monitor;
    bool monitor_started;
    bool runtime_running;
    bool shutting_down;
    bool media_running;
    bool paused;
    bool buffering;
    bool seekable;
    bool natural_end;
    pid_t pid;
    int log_fd;
    int ipc_fd;
    char ipc_path[108];
    char *mpv_path;
    unsigned long window_id;
    bool audio;
    vip_player_state_t state;
    double position_seconds;
    double duration_seconds;
    double percent_pos;
    double volume;
    double cache_duration_seconds;
    double pending_start_seconds;
    bool debug;
    bool has_video;
    int video_width;
    int video_height;
    char video_codec[64];
    char vo[64];
    char hwdec[64];
    char last_event[64];
    bool exit_code_valid;
    int exit_code;
    uint64_t serial;
    char last_error[512];
    char recent_log[VIP_MPV_LOG_CAP];
    size_t recent_log_len;
};

static atomic_uint_fast64_t g_socket_counter = 1;

/* Implement the env_enabled helper. */
static bool env_enabled(const char *name) {
    const char *value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0 && strcasecmp(value, "false") != 0;
}

/* Implement the debug_log helper. */
static void debug_log(vip_mpv_player_t *player, const char *fmt, ...) {
    if (!player || !player->debug || !fmt)
        return;
    fprintf(stderr, "[mpv-debug] ");
    va_list ap;
    va_start(ap, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    vfprintf(stderr, fmt, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    va_end(ap);
    fputc('\n', stderr);
}

/* Implement the monotonic_ms helper. */
static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000L);
}

/* Implement the sleep_ms helper. */
static void sleep_ms(long ms) {
    struct timespec ts = {.tv_sec = ms / 1000L, .tv_nsec = (ms % 1000L) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

/* Set nonblocking. */
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Return whether url start. */
static bool is_url_start(const char *s, size_t remaining, size_t *prefix_len) {
    if (remaining >= 7u && memcmp(s, "http://", 7u) == 0) {
        *prefix_len = 7u;
        return true;
    }
    if (remaining >= 8u && memcmp(s, "https://", 8u) == 0) {
        *prefix_len = 8u;
        return true;
    }
    return false;
}

/* Implement the url_delimiter helper. */
static bool url_delimiter(unsigned char c) {
    return isspace(c) || c == '\'' || c == '"' || c == '<' || c == '>' || c == ')' || c == ']';
}

/* Append char ring. */
static void append_char_ring(char *dst, size_t cap, size_t *len, char c) {
    if (cap < 2u)
        return;
    if (*len + 1u >= cap) {
        size_t drop = cap / 4u;
        if (drop < 1u)
            drop = 1u;
        memmove(dst, dst + drop, *len - drop);
        *len -= drop;
    }
    dst[(*len)++] = c;
    dst[*len] = '\0';
}

/* Append text ring. */
static void append_text_ring(char *dst, size_t cap, size_t *len, const char *text) {
    for (const char *p = text; p && *p; ++p)
        append_char_ring(dst, cap, len, *p);
}

/* mpv diagnostics can echo the complete stream URL (and therefore Xtream
 * credentials).  Redact URL-shaped spans before anything reaches recent_log. */
static void append_sanitized(char *dst, size_t cap, size_t *len, const char *src, size_t src_len) {
    size_t i = 0u;
    while (i < src_len) {
        size_t prefix_len = 0u;
        if (is_url_start(src + i, src_len - i, &prefix_len)) {
            append_text_ring(dst, cap, len, "[URL ocultada]");
            i += prefix_len;
            while (i < src_len && !url_delimiter((unsigned char)src[i]))
                ++i;
            continue;
        }
        char c = src[i++];
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
        append_char_ring(dst, cap, len, c);
    }
}

/* Trim text. */
static void trim_text(char *text) {
    if (!text)
        return;
    size_t len = strlen(text);
    while (len > 0u && isspace((unsigned char)text[len - 1u]))
        text[--len] = '\0';
    size_t start = 0u;
    while (text[start] && isspace((unsigned char)text[start]))
        ++start;
    if (start > 0u)
        memmove(text, text + start, strlen(text + start) + 1u);
}

/* Implement the touch_state_locked helper. */
static void touch_state_locked(vip_mpv_player_t *player, vip_player_state_t state) {
    if (player->state != state) {
        player->state = state;
        ++player->serial;
    }
}

/* Update state from flags locked. */
static void update_state_from_flags_locked(vip_mpv_player_t *player) {
    if (!player->media_running)
        return;
    if (player->buffering)
        touch_state_locked(player, VIP_PLAYER_BUFFERING);
    else if (player->paused)
        touch_state_locked(player, VIP_PLAYER_PAUSED);
    else
        touch_state_locked(player, VIP_PLAYER_PLAYING);
}

/* Reset media snapshot locked. */
static void reset_media_snapshot_locked(vip_mpv_player_t *player, double start_seconds) {
    player->paused = false;
    player->buffering = false;
    player->seekable = false;
    player->natural_end = false;
    player->position_seconds = start_seconds;
    player->duration_seconds = 0.0;
    player->percent_pos = 0.0;
    player->cache_duration_seconds = 0.0;
    player->pending_start_seconds = start_seconds;
    player->has_video = false;
    player->video_width = 0;
    player->video_height = 0;
    player->video_codec[0] = '\0';
    player->vo[0] = '\0';
    player->hwdec[0] = '\0';
    player->last_event[0] = '\0';
    player->exit_code_valid = false;
    player->exit_code = 0;
    player->last_error[0] = '\0';
    player->recent_log[0] = '\0';
    player->recent_log_len = 0u;
}

/* Write all fd. */
static int write_all_fd(int fd, const char *text, size_t len) {
    size_t offset = 0u;
    while (offset < len) {
        ssize_t count = write(fd, text + offset, len - offset);
        if (count > 0) {
            offset += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd = {.fd = fd, .events = POLLOUT};
            if (poll(&pfd, 1, 250) > 0)
                continue;
        }
        return -1;
    }
    return 0;
}

/* Implement the player_write_line helper. */
static int player_write_line(vip_mpv_player_t *player, const char *text) {
    if (!player || !text)
        return -1;
    pthread_mutex_lock(&player->mutex);
    int fd = player->ipc_fd;
    bool available = player->runtime_running && fd >= 0;
    pthread_mutex_unlock(&player->mutex);
    if (!available)
        return -1;

    size_t len = strlen(text);
    pthread_mutex_lock(&player->write_mutex);
    int rc = write_all_fd(fd, text, len);
    if (rc == 0)
        rc = write_all_fd(fd, "\n", 1u);
    pthread_mutex_unlock(&player->write_mutex);
    return rc;
}

/* Serialize all IPC writes: UI actions and the monitor thread can issue
 * commands concurrently, but each JSON object must remain one complete line. */
static int player_send_command(vip_mpv_player_t *player, json_object *command) {
    if (!command)
        return -1;
    json_object *root = json_object_new_object();
    if (!root) {
        json_object_put(command);
        return -1;
    }
    json_object_object_add(root, "command", command);
    const char *text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    int rc = text ? player_write_line(player, text) : -1;
    json_object_put(root);
    return rc;
}

/* Implement the command_array helper. */
static json_object *command_array(const char *name) {
    json_object *array = json_object_new_array();
    if (!array)
        return NULL;
    json_object *name_obj = json_object_new_string(name);
    if (!name_obj) {
        json_object_put(array);
        return NULL;
    }
    json_object_array_add(array, name_obj);
    return array;
}

/* Send observe. */
static int send_observe(vip_mpv_player_t *player, int id, const char *property) {
    json_object *cmd = command_array("observe_property");
    if (!cmd)
        return -1;
    json_object_array_add(cmd, json_object_new_int(id));
    json_object_array_add(cmd, json_object_new_string(property));
    return player_send_command(player, cmd);
}

/* Send observers. */
static void send_observers(vip_mpv_player_t *player) {
    static const char *properties[] = {"pause",
                                       "time-pos",
                                       "duration",
                                       "percent-pos",
                                       "paused-for-cache",
                                       "seekable",
                                       "volume",
                                       "demuxer-cache-duration",
                                       "vid",
                                       "video-codec",
                                       "width",
                                       "height",
                                       "current-vo",
                                       "hwdec-current"};
    for (size_t i = 0u; i < sizeof(properties) / sizeof(properties[0]); ++i)
        (void)send_observe(player, (int)i + 1, properties[i]);
}

/* Send loadfile. */
static int send_loadfile(vip_mpv_player_t *player, const char *url) {
    json_object *cmd = command_array("loadfile");
    if (!cmd)
        return -1;
    json_object_array_add(cmd, json_object_new_string(url));
    json_object_array_add(cmd, json_object_new_string("replace"));
    return player_send_command(player, cmd);
}

/* Send stop. */
static int send_stop(vip_mpv_player_t *player) {
    json_object *cmd = command_array("stop");
    return cmd ? player_send_command(player, cmd) : -1;
}

/* Send quit. */
static int send_quit(vip_mpv_player_t *player) {
    json_object *cmd = command_array("quit");
    return cmd ? player_send_command(player, cmd) : -1;
}

/* Send set pause. */
static int send_set_pause(vip_mpv_player_t *player, bool paused) {
    json_object *cmd = command_array("set_property");
    if (!cmd)
        return -1;
    json_object_array_add(cmd, json_object_new_string("pause"));
    json_object_array_add(cmd, json_object_new_boolean(paused));
    return player_send_command(player, cmd);
}

/* Send set volume. */
static int send_set_volume(vip_mpv_player_t *player, double volume) {
    json_object *cmd = command_array("set_property");
    if (!cmd)
        return -1;
    json_object_array_add(cmd, json_object_new_string("volume"));
    json_object_array_add(cmd, json_object_new_double(volume));
    return player_send_command(player, cmd);
}

/* Send seek. */
static int send_seek(vip_mpv_player_t *player, double seconds, bool relative) {
    json_object *cmd = command_array("seek");
    if (!cmd)
        return -1;
    json_object_array_add(cmd, json_object_new_double(seconds));
    json_object_array_add(cmd, json_object_new_string(relative ? "relative+exact" : "absolute+exact"));
    return player_send_command(player, cmd);
}

/* Create ipc path. */
static bool make_ipc_path(vip_mpv_player_t *player) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    struct stat st;
    if (!runtime || !runtime[0] || stat(runtime, &st) != 0 || !S_ISDIR(st.st_mode) ||
        access(runtime, W_OK | X_OK) != 0)
        runtime = "/tmp";
    uint64_t n = atomic_fetch_add(&g_socket_counter, 1u);
    int written = snprintf(player->ipc_path, sizeof(player->ipc_path), "%s/viptv-mpv-%ld-%llu.sock", runtime,
                           (long)getpid(), (unsigned long long)n);
    if (written < 0 || (size_t)written >= sizeof(player->ipc_path)) {
        written = snprintf(player->ipc_path, sizeof(player->ipc_path), "/tmp/viptv-mpv-%ld-%llu.sock",
                           (long)getpid(), (unsigned long long)n);
    }
    return written > 0 && (size_t)written < sizeof(player->ipc_path);
}

/* Implement the try_connect_ipc helper. */
static int try_connect_ipc(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    set_nonblocking(fd);
    return fd;
}

/* Terminate and reap. */
static void terminate_and_reap(pid_t pid) {
    if (pid <= 0)
        return;
    int status = 0;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid || (result < 0 && errno == ECHILD))
        return;
    (void)kill(pid, SIGTERM);
    for (unsigned i = 0u; i < 20u; ++i) {
        result = waitpid(pid, &status, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD))
            return;
        sleep_ms(25);
    }
    (void)kill(pid, SIGKILL);
    (void)waitpid(pid, &status, 0);
}

/* Spawn runtime. */
static int spawn_runtime(vip_mpv_player_t *player, int *log_read_fd) {
    const char *renderer = getenv("VIPTV_MPV_RENDERER");
    bool use_gpu_next = renderer && (!strcasecmp(renderer, "next") || !strcasecmp(renderer, "gpu-next"));
    bool use_x11_vo = renderer && (!strcasecmp(renderer, "x11") || !strcasecmp(renderer, "software-x11"));
    const char *requested_hwdec = getenv("VIPTV_MPV_HWDEC");
    const char *hwdec = "auto-safe";
    if (requested_hwdec && (!strcmp(requested_hwdec, "no") || !strcmp(requested_hwdec, "auto") ||
                            !strcmp(requested_hwdec, "auto-safe") || !strcmp(requested_hwdec, "auto-copy") ||
                            !strcmp(requested_hwdec, "auto-copy-safe") || !strcmp(requested_hwdec, "vaapi") ||
                            !strcmp(requested_hwdec, "vaapi-copy") || !strcmp(requested_hwdec, "vulkan") ||
                            !strcmp(requested_hwdec, "vulkan-copy")))
        hwdec = requested_hwdec;

    const char *vo_arg = use_x11_vo ? "--vo=x11" : (use_gpu_next ? "--vo=gpu-next,gpu" : "--vo=gpu");
    const char *context_arg =
        use_x11_vo ? NULL : (use_gpu_next ? "--gpu-context=x11egl,x11,x11vk" : "--gpu-context=x11");
    debug_log(player, "runtime persistente renderer=%s hwdec=%s embed=wid parent=%lu",
              use_x11_vo ? "x11" : (use_gpu_next ? "gpu-next-x11" : "gpu-x11"), hwdec, player->window_id);

    int log_pipe[2] = {-1, -1};
    if (pipe(log_pipe) != 0)
        return -1;
    if (!make_ipc_path(player)) {
        close(log_pipe[0]);
        close(log_pipe[1]);
        errno = ENAMETOOLONG;
        return -1;
    }
    (void)unlink(player->ipc_path);

    pid_t pid = fork();
    if (pid < 0) {
        close(log_pipe[0]);
        close(log_pipe[1]);
        return -1;
    }
    if (pid == 0) {
        if (dup2(log_pipe[1], STDOUT_FILENO) < 0)
            _exit(126);
        if (dup2(log_pipe[1], STDERR_FILENO) < 0)
            _exit(126);
        close(log_pipe[0]);
        if (log_pipe[1] > STDERR_FILENO)
            close(log_pipe[1]);

        char ipc_arg[160];
        char hwdec_arg[96];
        char title_arg[96];
        char wid_arg[96];
        snprintf(ipc_arg, sizeof(ipc_arg), "--input-ipc-server=%s", player->ipc_path);
        snprintf(hwdec_arg, sizeof(hwdec_arg), "--hwdec=%s", hwdec);
        snprintf(title_arg, sizeof(title_arg), "--title=visual-iptv-mpv-%ld", (long)getpid());
        snprintf(wid_arg, sizeof(wid_arg), "--wid=%lu", player->window_id);
        char *const audio_arg = player->audio ? "--audio=auto" : "--no-audio";
        char *argv[36];
        size_t ai = 0u;
        argv[ai++] = player->mpv_path;
        argv[ai++] = "--no-config";
        argv[ai++] = "--idle=yes";
        argv[ai++] = "--force-window=immediate";
        argv[ai++] = "--no-border";
        argv[ai++] = wid_arg;
        argv[ai++] = title_arg;
        argv[ai++] = "--keep-open=no";
        argv[ai++] = "--osc=no";
        argv[ai++] = "--osd-level=0";
        argv[ai++] = "--input-terminal=no";
        argv[ai++] = "--input-default-bindings=no";
        argv[ai++] = player->debug ? "--msg-level=all=info" : "--msg-level=all=warn";
        argv[ai++] = (char *)vo_arg;
        if (context_arg)
            argv[ai++] = (char *)context_arg;
        argv[ai++] = hwdec_arg;
        argv[ai++] = "--video-sync=audio";
        argv[ai++] = "--interpolation=no";
        argv[ai++] = ipc_arg;
        argv[ai++] = audio_arg;
        argv[ai] = NULL;
        if (strchr(player->mpv_path, '/'))
            execv(player->mpv_path, argv);
        else
            execvp(player->mpv_path, argv);
        dprintf(STDERR_FILENO, "exec mpv falhou: %s\n", strerror(errno));
        _exit(127);
    }

    close(log_pipe[1]);
    set_nonblocking(log_pipe[0]);
    *log_read_fd = log_pipe[0];
    return (int)pid;
}

/* Implement the drain_mpv_log helper. */
static void drain_mpv_log(vip_mpv_player_t *player, int fd) {
    char tmp[768];
    if (fd < 0)
        return;
    for (;;) {
        ssize_t count = read(fd, tmp, sizeof(tmp));
        if (count > 0) {
            char clean[1024] = {0};
            size_t clean_len = 0u;
            append_sanitized(clean, sizeof(clean), &clean_len, tmp, (size_t)count);
            trim_text(clean);
            if (clean[0]) {
                pthread_mutex_lock(&player->mutex);
                append_sanitized(player->recent_log, sizeof(player->recent_log), &player->recent_log_len, tmp,
                                 (size_t)count);
                if (player->state == VIP_PLAYER_ERROR && player->recent_log[0]) {
                    char error_text[VIP_MPV_LOG_CAP];
                    snprintf(error_text, sizeof(error_text), "%s", player->recent_log);
                    trim_text(error_text);
                    if (error_text[0])
                        snprintf(player->last_error, sizeof(player->last_error), "%.511s", error_text);
                }
                pthread_mutex_unlock(&player->mutex);
                debug_log(player, "mpv: %s", clean);
            }
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        break;
    }
}

/* Copy json string. */
static void copy_json_string(char *dst, size_t cap, json_object *data) {
    if (!dst || cap == 0u)
        return;
    dst[0] = '\0';
    if (!data || json_object_get_type(data) == json_type_null)
        return;
    const char *text = json_object_get_string(data);
    if (text)
        snprintf(dst, cap, "%s", text);
}

/* Set last event locked. */
static void set_last_event_locked(vip_mpv_player_t *player, const char *event) {
    if (!player || !event)
        return;
    snprintf(player->last_event, sizeof(player->last_event), "%s", event);
}

/* IPC is event-driven.  Property observations update a mutex-protected
 * snapshot that the X11 thread can read without parsing JSON itself. */
static void handle_ipc_line(vip_mpv_player_t *player, const char *line) {
    json_object *root = json_tokener_parse(line);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root)
            json_object_put(root);
        return;
    }
    json_object *event_obj = NULL;
    const char *event = json_object_object_get_ex(root, "event", &event_obj) && event_obj
                            ? json_object_get_string(event_obj)
                            : NULL;
    double deferred_seek = 0.0;

    pthread_mutex_lock(&player->mutex);
    if (event)
        set_last_event_locked(player, event);
    if (event && strcmp(event, "start-file") == 0) {
        player->media_running = true;
        touch_state_locked(player, VIP_PLAYER_OPENING);
    } else if (event && strcmp(event, "file-loaded") == 0) {
        player->media_running = true;
        deferred_seek = player->pending_start_seconds;
        player->pending_start_seconds = 0.0;
        update_state_from_flags_locked(player);
        ++player->serial;
    } else if (event && strcmp(event, "playback-restart") == 0) {
        update_state_from_flags_locked(player);
        ++player->serial;
    } else if (event && strcmp(event, "end-file") == 0) {
        json_object *reason_obj = NULL;
        const char *reason = json_object_object_get_ex(root, "reason", &reason_obj) && reason_obj
                                 ? json_object_get_string(reason_obj)
                                 : NULL;
        player->media_running = false;
        player->buffering = false;
        player->paused = false;
        if (reason && strcmp(reason, "eof") == 0) {
            player->natural_end = true;
            touch_state_locked(player, VIP_PLAYER_STOPPED);
        } else if (reason && (strcmp(reason, "stop") == 0 || strcmp(reason, "quit") == 0)) {
            touch_state_locked(player, VIP_PLAYER_STOPPED);
        } else {
            trim_text(player->recent_log);
            if (player->recent_log[0])
                snprintf(player->last_error, sizeof(player->last_error), "%.511s", player->recent_log);
            else if (reason)
                snprintf(player->last_error, sizeof(player->last_error), "mpv encerrou o arquivo: %s",
                         reason);
            else
                snprintf(player->last_error, sizeof(player->last_error), "mpv não conseguiu abrir a mídia");
            touch_state_locked(player, VIP_PLAYER_ERROR);
        }
    } else if (event && strcmp(event, "property-change") == 0) {
        json_object *name_obj = NULL, *data = NULL;
        const char *name = json_object_object_get_ex(root, "name", &name_obj) && name_obj
                               ? json_object_get_string(name_obj)
                               : NULL;
        (void)json_object_object_get_ex(root, "data", &data);
        if (name) {
            if (strcmp(name, "pause") == 0 && data && json_object_get_type(data) != json_type_null)
                player->paused = json_object_get_boolean(data) != 0;
            else if (strcmp(name, "time-pos") == 0 && data && json_object_get_type(data) != json_type_null)
                player->position_seconds = json_object_get_double(data);
            else if (strcmp(name, "duration") == 0 && data && json_object_get_type(data) != json_type_null)
                player->duration_seconds = json_object_get_double(data);
            else if (strcmp(name, "percent-pos") == 0 && data && json_object_get_type(data) != json_type_null)
                player->percent_pos = json_object_get_double(data);
            else if (strcmp(name, "paused-for-cache") == 0 && data &&
                     json_object_get_type(data) != json_type_null)
                player->buffering = json_object_get_boolean(data) != 0;
            else if (strcmp(name, "seekable") == 0 && data && json_object_get_type(data) != json_type_null)
                player->seekable = json_object_get_boolean(data) != 0;
            else if (strcmp(name, "volume") == 0 && data && json_object_get_type(data) != json_type_null)
                player->volume = json_object_get_double(data);
            else if (strcmp(name, "demuxer-cache-duration") == 0 && data &&
                     json_object_get_type(data) != json_type_null)
                player->cache_duration_seconds = json_object_get_double(data);
            else if (strcmp(name, "vid") == 0 && data && json_object_get_type(data) != json_type_null) {
                if (json_object_get_type(data) == json_type_int)
                    player->has_video = json_object_get_int64(data) > 0;
                else {
                    const char *vid = json_object_get_string(data);
                    player->has_video = vid && strcmp(vid, "no") != 0 && strcmp(vid, "false") != 0;
                }
            } else if (strcmp(name, "video-codec") == 0)
                copy_json_string(player->video_codec, sizeof(player->video_codec), data);
            else if (strcmp(name, "width") == 0 && data && json_object_get_type(data) != json_type_null)
                player->video_width = json_object_get_int(data);
            else if (strcmp(name, "height") == 0 && data && json_object_get_type(data) != json_type_null)
                player->video_height = json_object_get_int(data);
            else if (strcmp(name, "current-vo") == 0)
                copy_json_string(player->vo, sizeof(player->vo), data);
            else if (strcmp(name, "hwdec-current") == 0)
                copy_json_string(player->hwdec, sizeof(player->hwdec), data);
            ++player->serial;
            update_state_from_flags_locked(player);
        }
    }

    bool debug = player->debug;
    char last_event[64], codec[64], vo[64], hwdec[64];
    int width = player->video_width, height = player->video_height;
    bool has_video = player->has_video;
    snprintf(last_event, sizeof(last_event), "%s", player->last_event);
    snprintf(codec, sizeof(codec), "%s", player->video_codec);
    snprintf(vo, sizeof(vo), "%s", player->vo);
    snprintf(hwdec, sizeof(hwdec), "%s", player->hwdec);
    pthread_mutex_unlock(&player->mutex);

    if (deferred_seek > 0.0) {
        debug_log(player, "retomada via IPC seek=%.3f", deferred_seek);
        (void)send_seek(player, deferred_seek, false);
    }
    if (debug && event &&
        (!strcmp(event, "start-file") || !strcmp(event, "file-loaded") || !strcmp(event, "video-reconfig") ||
         !strcmp(event, "playback-restart") || !strcmp(event, "end-file"))) {
        debug_log(player, "event=%s video=%s codec=%s size=%dx%d vo=%s hwdec=%s", last_event,
                  has_video ? "yes" : "no", codec[0] ? codec : "?", width, height, vo[0] ? vo : "?",
                  hwdec[0] ? hwdec : "no/unknown");
    }
    json_object_put(root);
}

/* Consume ipc. */
static void consume_ipc(vip_mpv_player_t *player, char *buf, size_t *len) {
    pthread_mutex_lock(&player->mutex);
    int fd = player->ipc_fd;
    pthread_mutex_unlock(&player->mutex);
    if (fd < 0)
        return;

    for (;;) {
        /* A complete line larger than the fixed IPC buffer is discarded rather
         * than allowing stale bytes to be treated as a later JSON message. */
        if (*len >= VIP_MPV_IPC_BUF_CAP - 1u) {
            *len = 0u;
            buf[0] = '\0';
        }

        size_t writable = VIP_MPV_IPC_BUF_CAP - *len - 1u;
        ssize_t n = read(fd, buf + *len, writable);
        if (n > 0) {
            *len += (size_t)n;
            buf[*len] = '\0';

            size_t consumed = 0u;
            for (size_t i = 0u; i < *len; ++i) {
                if (buf[i] != '\n')
                    continue;

                buf[i] = '\0';
                if (i > consumed)
                    handle_ipc_line(player, buf + consumed);
                consumed = i + 1u;
            }

            /* Keep only the initialized partial line at the end of the buffer. */
            if (consumed != 0u) {
                if (consumed < *len) {
                    size_t remaining = *len - consumed;
                    memmove(buf, buf + consumed, remaining);
                    *len = remaining;
                } else {
                    *len = 0u;
                }
                buf[*len] = '\0';
            }
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
}

/* One monitor thread owns runtime observation: socket reads, mpv stderr,
 * process liveness and native-window reparent/resize synchronization. */
static void *monitor_main(void *userdata) {
    vip_mpv_player_t *player = userdata;
    char ipc_buf[VIP_MPV_IPC_BUF_CAP] = {0};
    size_t ipc_len = 0u;
    int status = 0;
    bool status_valid = false;

    for (;;) {
        pthread_mutex_lock(&player->mutex);
        pid_t pid = player->pid;
        int log_fd = player->log_fd;
        int ipc_fd = player->ipc_fd;
        bool shutting_down = player->shutting_down;
        char path[108];
        snprintf(path, sizeof(path), "%s", player->ipc_path);
        pthread_mutex_unlock(&player->mutex);
        if (pid <= 0)
            break;

        if (ipc_fd < 0 && !shutting_down) {
            int connected = try_connect_ipc(path);
            if (connected >= 0) {
                bool accepted = false;
                pthread_mutex_lock(&player->mutex);
                if (player->ipc_fd < 0) {
                    player->ipc_fd = connected;
                    ++player->serial;
                    accepted = true;
                }
                pthread_mutex_unlock(&player->mutex);
                if (!accepted)
                    close(connected);
                else {
                    debug_log(player, "IPC conectado; mpv persistente pronto");
                    send_observers(player);
                }
            }
        }

        pthread_mutex_lock(&player->mutex);
        ipc_fd = player->ipc_fd;
        log_fd = player->log_fd;
        pthread_mutex_unlock(&player->mutex);
        struct pollfd pfds[2];
        nfds_t nfds = 0u;
        if (log_fd >= 0)
            pfds[nfds++] = (struct pollfd){.fd = log_fd, .events = POLLIN | POLLHUP | POLLERR};
        if (ipc_fd >= 0)
            pfds[nfds++] = (struct pollfd){.fd = ipc_fd, .events = POLLIN | POLLHUP | POLLERR};
        if (nfds > 0u)
            (void)poll(pfds, nfds, 50);
        else
            sleep_ms(50);
        if (log_fd >= 0)
            drain_mpv_log(player, log_fd);
        if (ipc_fd >= 0)
            consume_ipc(player, ipc_buf, &ipc_len);

        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            status_valid = true;
            break;
        }
        if (result < 0 && errno != EINTR)
            break;
        if (shutting_down)
            break;
    }

    pthread_mutex_lock(&player->mutex);
    int log_fd = player->log_fd;
    int ipc_fd = player->ipc_fd;
    pid_t pid = player->pid;
    bool shutting_down = player->shutting_down;
    player->log_fd = -1;
    player->ipc_fd = -1;
    player->pid = -1;
    pthread_mutex_unlock(&player->mutex);

    if (ipc_fd >= 0)
        close(ipc_fd);
    if (log_fd >= 0) {
        drain_mpv_log(player, log_fd);
        close(log_fd);
    }
    if (pid > 0 && !status_valid) {
        terminate_and_reap(pid);
        status = 0;
    }
    (void)unlink(player->ipc_path);

    pthread_mutex_lock(&player->mutex);
    player->runtime_running = false;
    player->media_running = false;
    if (status_valid && WIFEXITED(status)) {
        player->exit_code_valid = true;
        player->exit_code = WEXITSTATUS(status);
    }
    if (!shutting_down) {
        trim_text(player->recent_log);
        if (player->recent_log[0])
            snprintf(player->last_error, sizeof(player->last_error), "%.511s", player->recent_log);
        else if (status_valid && WIFEXITED(status) && WEXITSTATUS(status) == 127)
            snprintf(player->last_error, sizeof(player->last_error),
                     "mpv não encontrado; instale o pacote mpv");
        else
            snprintf(player->last_error, sizeof(player->last_error), "processo mpv encerrou inesperadamente");
        touch_state_locked(player, VIP_PLAYER_ERROR);
    }
    pthread_mutex_unlock(&player->mutex);

    if (player->debug && status_valid) {
        if (WIFEXITED(status))
            debug_log(player, "runtime-exit-code=%d", WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            debug_log(player, "runtime-terminated-by-signal=%d", WTERMSIG(status));
    }
    return NULL;
}

/* Join old monitor. */
static void join_old_monitor(vip_mpv_player_t *player) {
    pthread_mutex_lock(&player->mutex);
    bool joinable = player->monitor_started && !player->runtime_running;
    pthread_t thread = player->monitor;
    pthread_mutex_unlock(&player->mutex);
    if (joinable) {
        pthread_join(thread, NULL);
        pthread_mutex_lock(&player->mutex);
        player->monitor_started = false;
        pthread_mutex_unlock(&player->mutex);
    }
}

/* Implement the shutdown_runtime helper. */
static void shutdown_runtime(vip_mpv_player_t *player) {
    if (!player)
        return;
    pthread_mutex_lock(&player->mutex);
    bool joinable = player->monitor_started;
    pthread_t thread = player->monitor;
    pid_t pid = player->pid;
    player->shutting_down = true;
    pthread_mutex_unlock(&player->mutex);

    (void)send_quit(player);
    if (pid > 0)
        (void)kill(pid, SIGTERM);
    if (joinable)
        pthread_join(thread, NULL);

    pthread_mutex_lock(&player->mutex);
    player->monitor_started = false;
    player->runtime_running = false;
    player->shutting_down = false;
    player->pid = -1;
    if (player->ipc_fd >= 0)
        close(player->ipc_fd);
    if (player->log_fd >= 0)
        close(player->log_fd);
    player->ipc_fd = -1;
    player->log_fd = -1;
    pthread_mutex_unlock(&player->mutex);
    if (player->ipc_path[0])
        (void)unlink(player->ipc_path);
}

/* Ensure runtime. */
static vip_status_t ensure_runtime(vip_mpv_player_t *player, vip_error_t *error) {
    join_old_monitor(player);
    pthread_mutex_lock(&player->mutex);
    bool running = player->runtime_running;
    pthread_mutex_unlock(&player->mutex);
    if (!running) {
        int log_fd = -1;
        int child = spawn_runtime(player, &log_fd);
        if (child < 0) {
            vip_error_set(error, VIP_ERR_PLAYER, "não foi possível iniciar mpv: %s", strerror(errno));
            return VIP_ERR_PLAYER;
        }
        pthread_mutex_lock(&player->mutex);
        player->pid = (pid_t)child;
        player->log_fd = log_fd;
        player->ipc_fd = -1;
        player->runtime_running = true;
        player->shutting_down = false;
        player->exit_code_valid = false;
        player->exit_code = 0;
        pthread_mutex_unlock(&player->mutex);
        if (pthread_create(&player->monitor, NULL, monitor_main, player) != 0) {
            (void)kill((pid_t)child, SIGTERM);
            terminate_and_reap((pid_t)child);
            close(log_fd);
            pthread_mutex_lock(&player->mutex);
            player->pid = -1;
            player->log_fd = -1;
            player->runtime_running = false;
            touch_state_locked(player, VIP_PLAYER_ERROR);
            pthread_mutex_unlock(&player->mutex);
            vip_error_set(error, VIP_ERR_PLAYER, "não foi possível criar monitor do mpv");
            return VIP_ERR_PLAYER;
        }
        pthread_mutex_lock(&player->mutex);
        player->monitor_started = true;
        pthread_mutex_unlock(&player->mutex);
    }

    int64_t deadline = monotonic_ms() + VIP_MPV_CONNECT_TIMEOUT_MS;
    for (;;) {
        pthread_mutex_lock(&player->mutex);
        bool ready = player->runtime_running && player->ipc_fd >= 0;
        bool alive = player->runtime_running;
        char last_error[512];
        snprintf(last_error, sizeof(last_error), "%s", player->last_error);
        pthread_mutex_unlock(&player->mutex);
        if (ready) {
            vip_error_clear(error);
            return VIP_OK;
        }
        if (!alive) {
            vip_error_set(error, VIP_ERR_PLAYER, "%s",
                          last_error[0] ? last_error : "mpv encerrou antes de abrir o IPC");
            return VIP_ERR_PLAYER;
        }
        if (monotonic_ms() >= deadline)
            break;
        sleep_ms(10);
    }
    vip_error_set(error, VIP_ERR_PLAYER, "mpv iniciou, mas o IPC não ficou disponível em %d ms",
                  VIP_MPV_CONNECT_TIMEOUT_MS);
    return VIP_ERR_PLAYER;
}

/* Implement the vip_mpv_player_create helper. */
vip_status_t vip_mpv_player_create(vip_mpv_player_t **out, const vip_mpv_player_config_t *config,
                                   vip_error_t *error) {
    if (!out || !config || config->window_id == 0u) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "configuração inválida do player mpv");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    (void)signal(SIGPIPE, SIG_IGN);
    vip_mpv_player_t *player = calloc(1, sizeof(*player));
    if (!player) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para player mpv");
        return VIP_ERR_NOMEM;
    }
    pthread_mutex_init(&player->mutex, NULL);
    pthread_mutex_init(&player->write_mutex, NULL);
    player->pid = -1;
    player->log_fd = -1;
    player->ipc_fd = -1;
    player->window_id = config->window_id;
    player->audio = config->audio;
    player->debug = env_enabled("VIPTV_MPV_DEBUG");
    player->state = VIP_PLAYER_IDLE;
    player->volume = 100.0;
    const char *path = config->mpv_path && config->mpv_path[0] ? config->mpv_path : "mpv";
    player->mpv_path = vip_strdup(path);
    if (!player->mpv_path) {
        pthread_mutex_destroy(&player->write_mutex);
        pthread_mutex_destroy(&player->mutex);
        free(player);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para caminho do mpv");
        return VIP_ERR_NOMEM;
    }
    if (player->debug)
        debug_log(player, "diagnóstico habilitado; mpv embutido via --wid + overlay de entrada + IPC JSON");
    *out = player;
    vip_error_clear(error);
    return VIP_OK;
}

/* Implement the vip_mpv_player_stop helper. */
void vip_mpv_player_stop(vip_mpv_player_t *player) {
    if (!player)
        return;
    pthread_mutex_lock(&player->mutex);
    bool media_running = player->media_running;
    bool runtime_running = player->runtime_running;
    player->media_running = false;
    player->pending_start_seconds = 0.0;
    player->paused = false;
    player->buffering = false;
    player->seekable = false;
    player->natural_end = false;
    player->position_seconds = 0.0;
    player->duration_seconds = 0.0;
    player->percent_pos = 0.0;
    player->cache_duration_seconds = 0.0;
    touch_state_locked(player, VIP_PLAYER_STOPPED);
    pthread_mutex_unlock(&player->mutex);
    if (media_running && runtime_running)
        (void)send_stop(player);
}

/* Implement the vip_mpv_player_destroy helper. */
void vip_mpv_player_destroy(vip_mpv_player_t *player) {
    if (!player)
        return;
    vip_mpv_player_stop(player);
    shutdown_runtime(player);
    free(player->mpv_path);
    pthread_mutex_destroy(&player->write_mutex);
    pthread_mutex_destroy(&player->mutex);
    free(player);
}

/* Implement the vip_mpv_player_load_at helper. */
vip_status_t vip_mpv_player_load_at(vip_mpv_player_t *player, const char *url, double start_seconds,
                                    vip_error_t *error) {
    if (!player || !url || !url[0]) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "URL de reprodução inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    if (start_seconds < 0.0)
        start_seconds = 0.0;
    vip_status_t ready = ensure_runtime(player, error);
    if (ready != VIP_OK)
        return ready;

    pthread_mutex_lock(&player->mutex);
    bool had_media = player->media_running;
    pthread_mutex_unlock(&player->mutex);
    if (had_media)
        (void)send_stop(player);

    pthread_mutex_lock(&player->mutex);
    reset_media_snapshot_locked(player, start_seconds);
    player->media_running = true;
    touch_state_locked(player, VIP_PLAYER_OPENING);
    pthread_mutex_unlock(&player->mutex);

    if (send_loadfile(player, url) != 0) {
        pthread_mutex_lock(&player->mutex);
        player->media_running = false;
        snprintf(player->last_error, sizeof(player->last_error), "falha ao enviar loadfile ao mpv via IPC");
        touch_state_locked(player, VIP_PLAYER_ERROR);
        pthread_mutex_unlock(&player->mutex);
        vip_error_set(error, VIP_ERR_PLAYER, "falha ao enviar mídia ao mpv via IPC");
        return VIP_ERR_PLAYER;
    }
    debug_log(player, "loadfile enviado via IPC%s",
              start_seconds > 0.0 ? "; retomada será aplicada após file-loaded" : "");
    vip_error_clear(error);
    return VIP_OK;
}

/* Implement the vip_mpv_player_load helper. */
vip_status_t vip_mpv_player_load(vip_mpv_player_t *player, const char *url, vip_error_t *error) {
    return vip_mpv_player_load_at(player, url, 0.0, error);
}

/* Implement the vip_mpv_player_set_paused helper. */
void vip_mpv_player_set_paused(vip_mpv_player_t *player, bool paused) {
    if (!player)
        return;
    pthread_mutex_lock(&player->mutex);
    bool media_running = player->media_running;
    player->paused = paused;
    if (media_running)
        update_state_from_flags_locked(player);
    pthread_mutex_unlock(&player->mutex);
    if (media_running)
        (void)send_set_pause(player, paused);
}

/* Implement the vip_mpv_player_is_paused helper. */
bool vip_mpv_player_is_paused(vip_mpv_player_t *player) {
    if (!player)
        return false;
    pthread_mutex_lock(&player->mutex);
    bool value = player->paused;
    pthread_mutex_unlock(&player->mutex);
    return value;
}

/* Implement the vip_mpv_player_is_running helper. */
bool vip_mpv_player_is_running(vip_mpv_player_t *player) {
    if (!player)
        return false;
    pthread_mutex_lock(&player->mutex);
    bool value = player->media_running;
    pthread_mutex_unlock(&player->mutex);
    return value;
}

/* Implement the vip_mpv_player_state helper. */
vip_player_state_t vip_mpv_player_state(vip_mpv_player_t *player) {
    if (!player)
        return VIP_PLAYER_ERROR;
    pthread_mutex_lock(&player->mutex);
    vip_player_state_t value = player->state;
    pthread_mutex_unlock(&player->mutex);
    return value;
}

/* Implement the vip_mpv_player_snapshot helper. */
void vip_mpv_player_snapshot(vip_mpv_player_t *player, vip_mpv_player_snapshot_t *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!player) {
        out->state = VIP_PLAYER_ERROR;
        return;
    }
    pthread_mutex_lock(&player->mutex);
    out->state = player->state;
    out->running = player->media_running;
    out->paused = player->paused;
    out->buffering = player->buffering;
    out->seekable = player->seekable;
    out->natural_end = player->natural_end;
    out->position_seconds = player->position_seconds;
    out->duration_seconds = player->duration_seconds;
    out->percent_pos = player->percent_pos;
    out->volume = player->volume;
    out->cache_duration_seconds = player->cache_duration_seconds;
    out->has_video = player->has_video;
    out->video_width = player->video_width;
    out->video_height = player->video_height;
    snprintf(out->video_codec, sizeof(out->video_codec), "%s", player->video_codec);
    snprintf(out->vo, sizeof(out->vo), "%s", player->vo);
    snprintf(out->hwdec, sizeof(out->hwdec), "%s", player->hwdec);
    snprintf(out->last_event, sizeof(out->last_event), "%s", player->last_event);
    out->exit_code_valid = player->exit_code_valid;
    out->exit_code = player->exit_code;
    out->serial = player->serial;
    pthread_mutex_unlock(&player->mutex);
}

/* Implement the vip_mpv_player_seek helper. */
vip_status_t vip_mpv_player_seek(vip_mpv_player_t *player, double position_seconds, vip_error_t *error) {
    if (!player || position_seconds < 0.0) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "posição de seek inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    if (send_seek(player, position_seconds, false) != 0) {
        vip_error_set(error, VIP_ERR_PLAYER, "player não está disponível para seek");
        return VIP_ERR_PLAYER;
    }
    pthread_mutex_lock(&player->mutex);
    player->position_seconds = position_seconds;
    ++player->serial;
    pthread_mutex_unlock(&player->mutex);
    vip_error_clear(error);
    return VIP_OK;
}

/* Implement the vip_mpv_player_seek_relative helper. */
vip_status_t vip_mpv_player_seek_relative(vip_mpv_player_t *player, double delta_seconds,
                                          vip_error_t *error) {
    if (!player) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "player inválido");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    if (send_seek(player, delta_seconds, true) != 0) {
        vip_error_set(error, VIP_ERR_PLAYER, "player não está disponível para seek");
        return VIP_ERR_PLAYER;
    }
    vip_error_clear(error);
    return VIP_OK;
}

/* Implement the vip_mpv_player_set_volume helper. */
vip_status_t vip_mpv_player_set_volume(vip_mpv_player_t *player, double volume, vip_error_t *error) {
    if (!player) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "player inválido");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    if (volume < 0.0)
        volume = 0.0;
    if (volume > 130.0)
        volume = 130.0;
    if (send_set_volume(player, volume) != 0) {
        vip_error_set(error, VIP_ERR_PLAYER, "player não está disponível para volume");
        return VIP_ERR_PLAYER;
    }
    pthread_mutex_lock(&player->mutex);
    player->volume = volume;
    ++player->serial;
    pthread_mutex_unlock(&player->mutex);
    vip_error_clear(error);
    return VIP_OK;
}

/* Implement the vip_mpv_player_state_name helper. */
const char *vip_mpv_player_state_name(vip_player_state_t state) {
    switch (state) {
    case VIP_PLAYER_IDLE:
        return "Pronto";
    case VIP_PLAYER_OPENING:
        return "Abrindo";
    case VIP_PLAYER_BUFFERING:
        return "Buffering";
    case VIP_PLAYER_PLAYING:
        return "Reproduzindo";
    case VIP_PLAYER_PAUSED:
        return "Pausado";
    case VIP_PLAYER_ERROR:
        return "Erro";
    case VIP_PLAYER_RECONNECTING:
        return "Reconectando";
    case VIP_PLAYER_STOPPED:
        return "Parado";
    default:
        return "Desconhecido";
    }
}

/* Implement the vip_mpv_player_last_error helper. */
const char *vip_mpv_player_last_error(vip_mpv_player_t *player) {
    if (!player)
        return "player mpv ausente";
    static _Thread_local char copy[512];
    pthread_mutex_lock(&player->mutex);
    snprintf(copy, sizeof(copy), "%s", player->last_error);
    pthread_mutex_unlock(&player->mutex);
    return copy;
}
