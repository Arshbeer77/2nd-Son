/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "vendor/runtime.h"
/* Thread creation is not exposed by this bounded, single-thread probe. */
void runtime_thread_keys_cleanup(void) {
    fputs("STOP: thread-key lifecycle is not integrated\n",stderr); ExitProcess(24);
}
static const struct { const char *encoded, *name; } names[] = {
#include "import_names.inc"
};
const char *second_son_import_name(const char *encoded) {
    for (size_t i=0; i<sizeof(names)/sizeof(names[0]); ++i)
        if (!strcmp(encoded,names[i].encoded)) return names[i].name;
    return "<unknown>";
}
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *encoded) {
    const char *name=second_son_import_name(encoded);
    for (size_t i=0; i<count; ++i)
        if (!strcmp(name,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}
