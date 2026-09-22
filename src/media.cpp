#include "media.h"
#include "player.h"

#include <roapi.h>
#include <windows.media.h>
#include <SystemMediaTransportControlsInterop.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <wrl/wrappers/corewrappers.h>

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::FtmBase;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::Wrappers::HStringReference;
using ABI::Windows::Foundation::ITypedEventHandler;
using namespace ABI::Windows::Media;

static ComPtr<ISystemMediaTransportControls> g_smtc;
static EventRegistrationToken g_smtc_btn;
static BOOL g_ro_ours;

class SmtcBtnHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    ITypedEventHandler<SystemMediaTransportControls *, SystemMediaTransportControlsButtonPressedEventArgs *>,
    FtmBase>
{
public:
    IFACEMETHODIMP Invoke(ISystemMediaTransportControls *,
                          ISystemMediaTransportControlsButtonPressedEventArgs *args) override
    {
        SystemMediaTransportControlsButton btn;
        if (SUCCEEDED(args->get_Button(&btn)) && g_wnd)
            PostMessageW(g_wnd, WM_MEDIA, (WPARAM)btn, 0);
        return S_OK;
    }
};

static BOOL InitSmtc(HWND wnd)
{
    ComPtr<ISystemMediaTransportControlsInterop> interop;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_Media_SystemMediaTransportControls).Get(),
        IID_PPV_ARGS(&interop));
    if (FAILED(hr))
        return FALSE;
    hr = interop->GetForWindow(wnd, IID_PPV_ARGS(&g_smtc));
    if (FAILED(hr) || !g_smtc)
        return FALSE;
    g_smtc->put_IsEnabled(TRUE);
    g_smtc->put_IsPlayEnabled(TRUE);
    g_smtc->put_IsPauseEnabled(TRUE);
    g_smtc->put_IsStopEnabled(TRUE);
    g_smtc->put_IsNextEnabled(TRUE);
    g_smtc->put_IsPreviousEnabled(TRUE);
    g_smtc->put_IsFastForwardEnabled(TRUE);
    g_smtc->put_IsRewindEnabled(TRUE);
    ComPtr<SmtcBtnHandler> handler = Make<SmtcBtnHandler>();
    if (!handler)
        return FALSE;
    return SUCCEEDED(g_smtc->add_ButtonPressed(handler.Get(), &g_smtc_btn));
}

void MediaKeysInit(HWND wnd)
{
    HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
    if (SUCCEEDED(hr))
        g_ro_ours = TRUE;
    if ((g_ro_ours || hr == RPC_E_CHANGED_MODE) && InitSmtc(wnd)) {
        MediaSync();
        return;
    }
    RegisterHotKey(wnd, 1, MOD_NOREPEAT, VK_MEDIA_PLAY_PAUSE);
    RegisterHotKey(wnd, 2, MOD_NOREPEAT, VK_MEDIA_STOP);
    RegisterHotKey(wnd, 3, MOD_NOREPEAT, VK_MEDIA_NEXT_TRACK);
    RegisterHotKey(wnd, 4, MOD_NOREPEAT, VK_MEDIA_PREV_TRACK);
}

void MediaKeysShutdown()
{
    if (g_wnd) {
        UnregisterHotKey(g_wnd, 1);
        UnregisterHotKey(g_wnd, 2);
        UnregisterHotKey(g_wnd, 3);
        UnregisterHotKey(g_wnd, 4);
    }
    if (g_smtc) {
        g_smtc->remove_ButtonPressed(g_smtc_btn);
        g_smtc->put_IsEnabled(FALSE);
        g_smtc.Reset();
    }
    if (g_ro_ours) {
        RoUninitialize();
        g_ro_ours = FALSE;
    }
}

void MediaSync()
{
    if (!g_smtc)
        return;
    DWORD st = g_stream ? BASS_ChannelIsActive(g_stream) : BASS_ACTIVE_STOPPED;
    MediaPlaybackStatus status = MediaPlaybackStatus_Closed;
    if (st == BASS_ACTIVE_PLAYING)
        status = MediaPlaybackStatus_Playing;
    else if (st == BASS_ACTIVE_PAUSED)
        status = MediaPlaybackStatus_Paused;
    else if (g_stream)
        status = MediaPlaybackStatus_Stopped;
    g_smtc->put_PlaybackStatus(status);

    ComPtr<ISystemMediaTransportControlsDisplayUpdater> upd;
    if (FAILED(g_smtc->get_DisplayUpdater(&upd)) || !upd)
        return;
    if (g_cur < 0) {
        upd->ClearAll();
        upd->Update();
        return;
    }
    upd->put_Type(MediaPlaybackType_Music);
    ComPtr<IMusicDisplayProperties> music;
    if (FAILED(upd->get_MusicProperties(&music)) || !music)
        return;
    HSTRING title = 0, artist = 0;
    const wchar_t *t = g_tracks[g_cur].title;
    const wchar_t *a = g_tracks[g_cur].artist;
    if (t && t[0])
        WindowsCreateString(t, (UINT32)lstrlenW(t), &title);
    if (a && a[0])
        WindowsCreateString(a, (UINT32)lstrlenW(a), &artist);
    music->put_Title(title);
    music->put_Artist(artist);
    WindowsDeleteString(title);
    WindowsDeleteString(artist);
    upd->Update();
}

BOOL MediaCommand(int cmd)
{
    switch (cmd) {
    case APPCOMMAND_MEDIA_PLAY:
        if (!g_stream) {
            if (g_sel >= 0)
                PlayIndex(g_sel, TRUE);
            break;
        }
        if (BASS_ChannelIsActive(g_stream) != BASS_ACTIVE_PLAYING)
            TogglePause();
        break;
    case APPCOMMAND_MEDIA_PAUSE:
        if (g_stream && BASS_ChannelIsActive(g_stream) == BASS_ACTIVE_PLAYING)
            TogglePause();
        break;
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
        TogglePause();
        break;
    case APPCOMMAND_MEDIA_STOP:
        StopPlayback();
        break;
    case APPCOMMAND_MEDIA_NEXTTRACK:
        PlayNext(TRUE);
        break;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
        PlayPrev();
        break;
    case APPCOMMAND_MEDIA_FAST_FORWARD:
        SeekBy(5);
        break;
    case APPCOMMAND_MEDIA_REWIND:
        SeekBy(-5);
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

void MediaSmtcButton(WPARAM btn)
{
    switch ((SystemMediaTransportControlsButton)btn) {
    case SystemMediaTransportControlsButton_Play:
        MediaCommand(APPCOMMAND_MEDIA_PLAY);
        break;
    case SystemMediaTransportControlsButton_Pause:
        MediaCommand(APPCOMMAND_MEDIA_PAUSE);
        break;
    case SystemMediaTransportControlsButton_Stop:
        MediaCommand(APPCOMMAND_MEDIA_STOP);
        break;
    case SystemMediaTransportControlsButton_Next:
        MediaCommand(APPCOMMAND_MEDIA_NEXTTRACK);
        break;
    case SystemMediaTransportControlsButton_Previous:
        MediaCommand(APPCOMMAND_MEDIA_PREVIOUSTRACK);
        break;
    case SystemMediaTransportControlsButton_FastForward:
        MediaCommand(APPCOMMAND_MEDIA_FAST_FORWARD);
        break;
    case SystemMediaTransportControlsButton_Rewind:
        MediaCommand(APPCOMMAND_MEDIA_REWIND);
        break;
    default:
        break;
    }
}
