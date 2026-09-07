/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/player_mpv.h"
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(long ms) {
    struct timespec ts = {.tv_sec = ms / 1000L, .tv_nsec = (ms % 1000L) * 1000000L};
    nanosleep(&ts, NULL);
}

static bool file_contains(const char *path, const char *needle) {
    FILE *fp = fopen(path, "r");
    if (!fp) return false;
    char buf[16384] = {0};
    size_t got = fread(buf, 1u, sizeof(buf) - 1u, fp);
    buf[got] = '\0';
    fclose(fp);
    return strstr(buf, needle) != NULL;
}

int main(void) {
    char fake_path[256], args_path[256], cmd_path[256];
    snprintf(fake_path, sizeof(fake_path), "/tmp/vip-fake-mpv-%ld.py", (long)getpid());
    snprintf(args_path, sizeof(args_path), "/tmp/vip-fake-mpv-args-%ld.txt", (long)getpid());
    snprintf(cmd_path, sizeof(cmd_path), "/tmp/vip-fake-mpv-cmd-%ld.txt", (long)getpid());

    FILE *fp = fopen(fake_path, "w");
    TEST_CHECK(fp != NULL);
    fputs("#!/usr/bin/env python3\n"
          "import json, os, socket, sys\n"
          "args_path=os.environ['VIP_FAKE_MPV_ARGS']\n"
          "cmd_path=os.environ['VIP_FAKE_MPV_CMDS']\n"
          "open(args_path,'w').write('\\n'.join(sys.argv[1:])+'\\n')\n"
          "ipc=None\n"
          "for a in sys.argv[1:]:\n"
          "    if a.startswith('--input-ipc-server='): ipc=a.split('=',1)[1]\n"
          "if not ipc: sys.exit(3)\n"
          "try: os.unlink(ipc)\n"
          "except FileNotFoundError: pass\n"
          "srv=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); srv.bind(ipc); srv.listen(1)\n"
          "conn,_=srv.accept(); f=conn.makefile('r', encoding='utf-8')\n"
          "def emit(obj): conn.sendall((json.dumps(obj,separators=(',',':'))+'\\n').encode())\n"
          "with open(cmd_path,'a',encoding='utf-8') as log:\n"
          "  for line in f:\n"
          "    log.write(line); log.flush()\n"
          "    try: root=json.loads(line); cmd=root.get('command',[])\n"
          "    except Exception: continue\n"
          "    if not cmd: continue\n"
          "    if cmd[0]=='loadfile':\n"
          "      url=cmd[1] if len(cmd)>1 else ''\n"
          "      emit({'event':'start-file'})\n"
          "      if 'missing' in url:\n"
          "        print('Failed to open stream https://secret.invalid/user/pass/file', flush=True)\n"
          "        emit({'event':'end-file','reason':'error','error':-13})\n"
          "      else:\n"
          "        emit({'event':'file-loaded'})\n"
          "        emit({'event':'property-change','name':'duration','data':100.0})\n"
          "        emit({'event':'property-change','name':'time-pos','data':0.0})\n"
          "        emit({'event':'property-change','name':'seekable','data':True})\n"
          "        emit({'event':'property-change','name':'vid','data':1})\n"
          "        emit({'event':'property-change','name':'video-codec','data':'h264'})\n"
          "        emit({'event':'property-change','name':'width','data':1920})\n"
          "        emit({'event':'property-change','name':'height','data':1080})\n"
          "        emit({'event':'property-change','name':'current-vo','data':'gpu'})\n"
          "        emit({'event':'property-change','name':'hwdec-current','data':'vaapi'})\n"
          "        emit({'event':'playback-restart'})\n"
          "    elif cmd[0]=='stop': emit({'event':'end-file','reason':'stop'})\n"
          "    elif cmd[0]=='quit': break\n"
          "conn.close(); srv.close()\n"
          "try: os.unlink(ipc)\n"
          "except FileNotFoundError: pass\n", fp);
    TEST_CHECK(fclose(fp) == 0);
    TEST_CHECK(chmod(fake_path, 0700) == 0);
    TEST_CHECK(setenv("VIP_FAKE_MPV_ARGS", args_path, 1) == 0);
    TEST_CHECK(setenv("VIP_FAKE_MPV_CMDS", cmd_path, 1) == 0);

    vip_error_t error = {0};
    vip_mpv_player_t *player = NULL;
    vip_mpv_player_config_t config = {
        .mpv_path = fake_path,
        .window_id = 123u,
        .audio = false,
        .startup_grace_ms = 50u,
    };
    TEST_CHECK(vip_mpv_player_create(&player, &config, &error) == VIP_OK);
    TEST_CHECK(player != NULL);

    TEST_CHECK(vip_mpv_player_load_at(player, "http://example.invalid/ok.ts", 12.5, &error) == VIP_OK);
    bool playing = false;
    for (int i = 0; i < 80; ++i) {
        if (vip_mpv_player_state(player) == VIP_PLAYER_PLAYING) { playing = true; break; }
        sleep_ms(25);
    }
    TEST_CHECK(playing);
    TEST_CHECK(vip_mpv_player_is_running(player));

    vip_mpv_player_snapshot_t snap = {0};
    vip_mpv_player_snapshot(player, &snap);
    TEST_CHECK(snap.has_video);
    TEST_CHECK(snap.video_width == 1920);
    TEST_CHECK(snap.video_height == 1080);
    TEST_CHECK(strcmp(snap.video_codec, "h264") == 0);
    TEST_CHECK(strcmp(snap.vo, "gpu") == 0);
    TEST_CHECK(strcmp(snap.hwdec, "vaapi") == 0);

    TEST_CHECK(file_contains(args_path, "--idle=yes"));
    TEST_CHECK(file_contains(args_path, "--vo=gpu"));
    TEST_CHECK(file_contains(args_path, "--gpu-context=x11"));
    TEST_CHECK(file_contains(args_path, "--hwdec=auto-safe"));
    TEST_CHECK(file_contains(args_path, "--force-window=immediate"));
    TEST_CHECK(file_contains(args_path, "--no-border"));
    TEST_CHECK(!file_contains(args_path, "--wid="));
    TEST_CHECK(!file_contains(args_path, "example.invalid"));
    TEST_CHECK(!file_contains(args_path, "--start="));
    TEST_CHECK(!file_contains(args_path, "--playlist="));

    bool saw_load = false, saw_seek = false;
    for (int i = 0; i < 80; ++i) {
        saw_load = file_contains(cmd_path, "\"loadfile\"");
        saw_seek = file_contains(cmd_path, "12.5");
        if (saw_load && saw_seek) break;
        sleep_ms(25);
    }
    TEST_CHECK(saw_load);
    TEST_CHECK(saw_seek);

    vip_mpv_player_set_paused(player, true);
    TEST_CHECK(vip_mpv_player_is_paused(player));
    vip_mpv_player_set_paused(player, false);
    TEST_CHECK(!vip_mpv_player_is_paused(player));

    vip_mpv_player_stop(player);
    TEST_CHECK(vip_mpv_player_state(player) == VIP_PLAYER_STOPPED);

    /* The same persistent mpv runtime must accept another file after stop. */
    TEST_CHECK(vip_mpv_player_load(player, "http://example.invalid/missing.ts", &error) == VIP_OK);
    bool failed = false;
    for (int i = 0; i < 80; ++i) {
        if (vip_mpv_player_state(player) == VIP_PLAYER_ERROR) { failed = true; break; }
        sleep_ms(25);
    }
    TEST_CHECK(failed);
    TEST_CHECK(!vip_mpv_player_is_running(player));
    const char *last_error = vip_mpv_player_last_error(player);
    TEST_CHECK(strstr(last_error, "Failed to open stream") != NULL);
    TEST_CHECK(strstr(last_error, "secret.invalid") == NULL);
    TEST_CHECK(strstr(last_error, "[URL ocultada]") != NULL);

    vip_mpv_player_destroy(player);
    unlink(fake_path);
    unlink(args_path);
    unlink(cmd_path);
    unsetenv("VIP_FAKE_MPV_ARGS");
    unsetenv("VIP_FAKE_MPV_CMDS");
    return 0;
}
