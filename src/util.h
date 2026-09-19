#pragma once
#include "app.h"

HANDLE Heap();
int S(int v);
int Abs(int v);
int Clamp(int v, int lo, int hi);
void Copy(wchar_t *dst, const wchar_t *src, int cap);
int Len(const wchar_t *s);
void Append(wchar_t *dst, int cap, const wchar_t *src);
BOOL EqI(const wchar_t *a, const wchar_t *b);
BOOL ContainsI(const wchar_t *hay, const wchar_t *needle);
const wchar_t *FileName(const wchar_t *path);
BOOL IsAudio(const wchar_t *path);
BOOL WorkArea(const RECT *r, RECT *out);
