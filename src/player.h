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
void ProbePending();
int AddPath(const wchar_t *path);
void SavePlaylist();
void LoadPlaylist();
BOOL PlayIndex(int i, BOOL show_err);
BOOL PlayNext(BOOL wrap);
void PlayPrev();
void TogglePause();
void StopPlayback();
void RemoveSel();
