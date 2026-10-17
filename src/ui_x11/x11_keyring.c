/* SPDX-License-Identifier: MIT */
/*
 * Keyring integration via Secret Service and secret-tool.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "x11_keyring.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Secret Service attributes are passed directly to execv, but keep the
 * profile identifier deliberately narrow as an extra boundary check. */
static bool keyring_profile_id_is_safe(const char *profile_id) {
    if (!profile_id || !profile_id[0])
        return false;
    size_t len = strlen(profile_id);
    if (len > 128u)
        return false;
    for (size_t i = 0u; i < len; ++i) {
        unsigned char c = (unsigned char)profile_id[i];
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.' || c == ':'))
            return false;
    }
    return true;
}

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>

bool keyring_store_password(const char *profile_id, const char *password) {
    if (!keyring_profile_id_is_safe(profile_id) || !password || !password[0])
        return false;
    char target[192];
    int n = snprintf(target, sizeof(target), "visual-iptv:%s", profile_id);
    if (n <= 0 || (size_t)n >= sizeof(target))
        return false;
    CREDENTIALA cred;
    memset(&cred, 0, sizeof(cred));
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = target;
    cred.CredentialBlobSize = (DWORD)strlen(password);
    cred.CredentialBlob = (LPBYTE)(uintptr_t)password;
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = (LPSTR)(uintptr_t)profile_id;
    return CredWriteA(&cred, 0) != FALSE;
}

bool keyring_lookup_password(const char *profile_id, char *out, size_t cap) {
    if (!out || cap == 0u)
        return false;
    out[0] = '\0';
    if (!keyring_profile_id_is_safe(profile_id))
        return false;
    char target[192];
    int n = snprintf(target, sizeof(target), "visual-iptv:%s", profile_id);
    if (n <= 0 || (size_t)n >= sizeof(target))
        return false;
    PCREDENTIALA pcred = NULL;
    if (!CredReadA(target, CRED_TYPE_GENERIC, 0, &pcred) || !pcred)
        return false;
    bool ok = false;
    if (pcred->CredentialBlob && pcred->CredentialBlobSize > 0 &&
        (size_t)pcred->CredentialBlobSize < cap) {
        memcpy(out, pcred->CredentialBlob, pcred->CredentialBlobSize);
        out[pcred->CredentialBlobSize] = '\0';
        ok = out[0] != '\0';
    }
    CredFree(pcred);
    return ok;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Resolve secret-tool without invoking a shell or trusting a mutable PATH. */
static bool find_secret_tool(char *out, size_t cap) {
    static const char *const candidates[] = {
        "/usr/bin/secret-tool",
        "/bin/secret-tool",
        "/usr/local/bin/secret-tool",
    };
    if (!out || cap == 0u)
        return false;
    out[0] = '\0';
    for (size_t i = 0u; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (access(candidates[i], X_OK) != 0)
            continue;
        int n = snprintf(out, cap, "%s", candidates[i]);
        if (n > 0 && (size_t)n < cap)
            return true;
    }
    out[0] = '\0';
    return false;
}

/* Write all bytes to file descriptor. */
static bool write_all_fd(int fd, const char *data, size_t len) {
    size_t offset = 0u;
    while (offset < len) {
        ssize_t written = write(fd, data + offset, len - offset);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

/* Spawn secret-tool directly with a fixed executable path and explicit file
 * actions.  No shell is involved and no executable lookup uses PATH. */
static bool spawn_secret_tool(const char *secret_tool, char *const argv[],
                              int stdin_fd, int stdout_fd, pid_t *pid_out) {
    if (!secret_tool || !secret_tool[0] || !argv || !pid_out)
        return false;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        return false;

    int rc = 0;
    if (stdin_fd >= 0)
        rc = posix_spawn_file_actions_adddup2(&actions, stdin_fd, STDIN_FILENO);
    if (rc == 0 && stdout_fd >= 0)
        rc = posix_spawn_file_actions_adddup2(&actions, stdout_fd, STDOUT_FILENO);
    if (rc == 0)
        rc = posix_spawn_file_actions_addopen(
            &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = -1;
    if (rc == 0)
        rc = posix_spawn(&pid, secret_tool, &actions, NULL, argv, environ);

    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0)
        return false;

    *pid_out = pid;
    return true;
}

/* Password persistence is delegated to Secret Service via secret-tool.
 * SQLite stores only non-secret profile fields. */
bool keyring_store_password(const char *profile_id, const char *password) {
    char secret_tool[256];
    if (!keyring_profile_id_is_safe(profile_id) || !password || !password[0] ||
        !find_secret_tool(secret_tool, sizeof(secret_tool)))
        return false;

    int inpipe[2];
    if (pipe2(inpipe, O_CLOEXEC) != 0)
        return false;

    char *const argv[] = {
        "secret-tool", "store", "--label=Blazzing", "application", "visual-iptv",
        "profile", (char *)profile_id, NULL
    };
    pid_t pid = -1;
    bool spawned = spawn_secret_tool(secret_tool, argv, inpipe[0], -1, &pid);
    close(inpipe[0]);
    if (!spawned) {
        close(inpipe[1]);
        return false;
    }

    size_t len = strlen(password);
    bool wrote = write_all_fd(inpipe[1], password, len) &&
                 write_all_fd(inpipe[1], "\n", 1u);
    close(inpipe[1]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return wrote && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool wait_secret_tool(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static ssize_t trim_secret_output(char *out, ssize_t n) {
    if (n <= 0)
        return 0;
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        out[--n] = '\0';
    return n;
}

/* Look up password from the Secret Service via secret-tool. */
bool keyring_lookup_password(const char *profile_id, char *out, size_t cap) {
    if (!out || cap == 0u)
        return false;
    out[0] = '\0';

    char secret_tool[256];
    if (!keyring_profile_id_is_safe(profile_id) || !find_secret_tool(secret_tool, sizeof(secret_tool)))
        return false;

    int outpipe[2];
    if (pipe2(outpipe, O_CLOEXEC) != 0)
        return false;

    char *const argv[] = {
        "secret-tool", "lookup", "application", "visual-iptv",
        "profile", (char *)profile_id, NULL
    };
    pid_t pid = -1;
    bool spawned = spawn_secret_tool(secret_tool, argv, -1, outpipe[1], &pid);
    close(outpipe[1]);
    if (!spawned) {
        close(outpipe[0]);
        return false;
    }

    ssize_t n = read(outpipe[0], out, cap - 1u);
    close(outpipe[0]);
    if (!wait_secret_tool(pid) || n <= 0) {
        out[0] = '\0';
        return false;
    }
    return trim_secret_output(out, n) > 0;
}

#endif
