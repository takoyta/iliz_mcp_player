#pragma once
#include "app.h"

BOOL StoreOpen();
void StoreClose();
BOOL StoreDataDir(wchar_t *out, int cap);
void StoreLoad();
void StoreSaveQueue();
void StoreUpsert(Track *t);
void StoreSetLiked(int id, int liked);
void StoreBumpPlays(int id);
void StoreSetSecs(int id, int secs);
void StoreDelete(int id);
int StoreSearch(const wchar_t *q, int *ids, int cap, int *total);
int IndexById(int id);
const wchar_t *TrackTitle(const Track *t);
void FillFromFileName(Track *t);
