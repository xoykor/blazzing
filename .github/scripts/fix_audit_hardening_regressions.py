from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)

# The application no longer cancels thumbnails on scroll/filter, but an
# explicit provider boundary must actually discard stale pending artwork.
p = Path("tests/test_thumbnail_policy.c")
s = p.read_text()
s = one(s,
'''    /* cancel_pending is intentionally non-destructive at the application
       policy layer. Simulate a scroll while a background artwork request is
       paused, then ensure it still runs after resume. */
    vip_thumbnail_scheduler_set_paused(scheduler, true);
    vip_thumbnail_request_t artwork = {
        .provider_id = "p",
        .channel_id = "artwork-kept-across-scroll",
        .logo_url = "https://example.invalid/poster.jpg",
        .stream_url = "http://stream.invalid/vod",
        .priority = 10000
    };
    TEST_STATUS(vip_thumbnail_scheduler_enqueue(scheduler, &artwork, &error), VIP_OK, &error);
    vip_thumbnail_scheduler_cancel_pending(scheduler);
    vip_thumbnail_scheduler_set_paused(scheduler, false);
    TEST_CHECK(wait_for_count(&state, 2, 1000L) == 2);
''',
'''    /* Viewport/search changes no longer call cancel_pending. An explicit
       provider boundary does, and must discard stale queued work. */
    vip_thumbnail_scheduler_set_paused(scheduler, true);
    vip_thumbnail_request_t artwork = {
        .provider_id = "old-provider",
        .channel_id = "stale-artwork",
        .logo_url = "https://example.invalid/poster.jpg",
        .stream_url = "http://stream.invalid/vod",
        .priority = 10000
    };
    TEST_STATUS(vip_thumbnail_scheduler_enqueue(scheduler, &artwork, &error), VIP_OK, &error);
    vip_thumbnail_scheduler_cancel_pending(scheduler);
    vip_thumbnail_scheduler_set_paused(scheduler, false);
    TEST_CHECK(wait_for_count(&state, 2, 200L) == 1);

    /* Scheduler remains usable immediately after a provider-boundary cancel. */
    artwork.provider_id = "new-provider";
    artwork.channel_id = "fresh-artwork";
    TEST_STATUS(vip_thumbnail_scheduler_enqueue(scheduler, &artwork, &error), VIP_OK, &error);
    TEST_CHECK(wait_for_count(&state, 2, 1000L) == 2);
''', "thumbnail cancel regression")
p.write_text(s)

# Remove helper made dead by the clearer external-service card copy.
p = Path("src/app/hub.c")
s = p.read_text()
s = one(s,
'''static int hub_text_width(hub_window_t *h, const char *text) {
    if (!text) return 0;
    if (h->font) return XTextWidth(h->font, text, (int)strlen(text));
    return (int)strlen(text) * 8;
}

''', "", "unused hub_text_width")
p.write_text(s)

# Secret Service pipe writes must handle short writes/EINTR; silently dropping
# part of a password could create a profile that can never authenticate.
p = Path("src/ui_x11/x11_app.c")
s = p.read_text()
anchor = '''/* Password persistence is delegated to Secret Service via secret-tool.
 * SQLite stores only non-secret profile fields. */
'''
helper = '''static bool write_all_fd(int fd, const char *data, size_t len) {
    size_t offset = 0u;
    while (offset < len) {
        ssize_t written = write(fd, data + offset, len - offset);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

'''
s = one(s, anchor, helper + anchor, "write_all_fd helper")
s = one(s,
'''    size_t len = strlen(password);
    (void)write(inpipe[1], password, len);
    (void)write(inpipe[1], "\\n", 1u);
    close(inpipe[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
''',
'''    size_t len = strlen(password);
    bool wrote = write_all_fd(inpipe[1], password, len) && write_all_fd(inpipe[1], "\\n", 1u);
    close(inpipe[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return wrote && WIFEXITED(status) && WEXITSTATUS(status) == 0;
''', "checked keyring write")
p.write_text(s)

print("hardening regression corrections applied")
