#define _GNU_SOURCE
#include <assert.h>
#include <string.h>
// Exercise the actual probe decoder without requiring a GPU or entering EGL.
#define main support_probe_main
#include "linux_support.c"
#undef main
int aud_init(AudLogFn log, AudGetProcFn proc, const wchar_t *dll, const wchar_t *data) { return 0; }
int aud_configure(int a, int b, int c, int d, int e, int f, int g, int h) { return 0; }
int aud_rr_available(void) { return 0; }
void aud_shutdown(void) {}
int main(void)
{
    assert(setlocale(LC_ALL, "C"));
    wchar_t path[128];
    assert(decode_runtime_path(path, 128, "/tmp/caf\xc3\xa9/\xe6\xb5\x8b\xe8\xaf\x95/\xf0\x9f\x8e\xae"));
    assert(!wcscmp(path, L"/tmp/caf\u00e9/\u6d4b\u8bd5/\U0001f3ae"));
    assert(!strcmp(setlocale(LC_CTYPE, NULL), "C"));
    assert(!strcmp(setlocale(LC_NUMERIC, NULL), "C"));
    assert(decode_runtime_path(path, 4, "abc") && path[3] == 0);
    assert(!decode_runtime_path(path, 3, "abc") && path[0] == 0);
    assert(!decode_runtime_path(path, 128, "/tmp/\xc3\x28") && path[0] == 0);
    assert(!decode_runtime_path(path, 0, "abc"));
    puts("PASS: probe UTF-8 paths, locale isolation, invalid encoding and truncation");
}
