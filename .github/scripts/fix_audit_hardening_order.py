from pathlib import Path

p = Path("src/thumbnails/thumbnails.c")
s = p.read_text()
block = '''typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf env;
} thumbnail_jpeg_error_t;

static void thumbnail_jpeg_fail(j_common_ptr cinfo) {
    thumbnail_jpeg_error_t *err = (thumbnail_jpeg_error_t *)cinfo->err;
    longjmp(err->env, 1);
}

'''
if s.count(block) != 1:
    raise SystemExit(f"expected one jpeg handler block, found {s.count(block)}")
s = s.replace(block, "", 1)
anchor = '''#define THUMB_IMAGE_MAX_BYTES (12u * 1024u * 1024u)

'''
if s.count(anchor) != 1:
    raise SystemExit(f"jpeg handler anchor mismatch: {s.count(anchor)}")
s = s.replace(anchor, anchor + block, 1)
p.write_text(s)
print("jpeg handler moved before first use")
