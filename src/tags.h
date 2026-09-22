#pragma once
#include "app.h"

struct TagItem {
    wchar_t k[48];
    wchar_t v[META_CAP];
};

void ReadTags(const wchar_t *path, wchar_t *title, wchar_t *artist, wchar_t *album, int cap);
BOOL RepairLegacyMeta(wchar_t *s, int cap);
int DumpTags(const wchar_t *path, TagItem *items, int cap);
