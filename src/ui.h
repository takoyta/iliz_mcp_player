#pragma once
#include "app.h"

void GetLayout(Layout *l);
int VisibleRows(const Layout *l);
void PlaceSearch();
void ApplyShape();
void ApplyTopmost();
void ToggleTopmost();
void ToggleShuffle();
void ToggleMute();
void TrayAdd();
void TrayDel();
void TrayTip();
void RestoreWnd(HWND wnd);
void ArmTimer();
void LoadSettings(int *x, int *y);
void SaveGeom();
int Run();
