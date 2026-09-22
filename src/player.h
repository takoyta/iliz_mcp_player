#pragma once
#include "app.h"

void ApplyVol();
unsigned RandU();
int ViewIndex(int track);
void ClampScroll();
void EnsureVisible(int i);
void RebuildView(BOOL reset_scroll);
void FilterChanged();
void MoveSelBy(int delta);
void PickOnly(int i);
void PickToggle(int i);
void PickRange(int i);
void PickAllView();
int PickedCount();
void RemovePicked(BOOL disk);
int ProbePending();
int AddPath(const wchar_t *path);
void SavePlaylist();
void LoadPlaylist();
BOOL PlayIndex(int i, BOOL show_err);
BOOL PlayById(int id, BOOL show_err);
BOOL PlayNext(BOOL wrap);
BOOL PlayPrev();
void TogglePause();
void StopPlayback();
void SeekBy(double delta);
void RemoveTrack(int i);
void RemoveSel();
void ToggleLike(int i);
void SetLike(int i, int liked);
int CollectQueue(int *out);
