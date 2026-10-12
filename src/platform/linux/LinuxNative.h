#pragma once
//
// FullTerminal — Linux (X11) yerel platform katmani
//
// Win32 tarafinda isletim sisteminin verdigi ama Xlib'de karsiligi olmayan
// parcalar burada: acilir menuler (TrackPopupMenuEx), mesaj kutulari
// (MessageBoxW), sistem tepsisi (XEmbed), pencere goster/gizle/yerlesim,
// imlec konumu, monitor/calisma alani, zil, dikkat cekme, pencere simgesi,
// global kisayol (XGrabKey) ve sistem DPI'i.
//
// Bagimliliklar yalnizca libX11 + FreeType + Fontconfig (GUI ikilisinin
// zaten kullandiklari). Coklu monitor bilgisi icin libXinerama varsa calisma
// aninda dlopen ile yuklenir; yoksa tum ekran tek monitor sayilir.
//
#if !defined(_WIN32)
#include "core/Platform.h"
#include <string>
#include <vector>

union _XEvent;

namespace ft::lnx {

// Win32 SC_* sistem menusu komutlari (Alt+Bosluk menusu)
constexpr UINT kScSize     = 0xF000;
constexpr UINT kScMove     = 0xF010;
constexpr UINT kScMinimize = 0xF020;
constexpr UINT kScMaximize = 0xF030;
constexpr UINT kScClose    = 0xF060;
constexpr UINT kScRestore  = 0xF120;

// Platform.h'deki g_linuxHooks tablosunu doldurur. X baglantisi acmaz;
// GUI'siz kiplerde (--mcp) de cagrilabilir: mesaj kutusu kendi baglantisini acar.
void InstallHooks();

// Ana pencere (LinuxWindowHandle*) kaydi. nullptr: pencere kapandi.
void SetMainWindow(HWND hwnd);

// Ana dongu her X olayini once buraya verir. Tepsi/kisayol/kok pencere olayi
// tuketildiyse true. Global kisayol tetiklendiyse *hotkeyId doldurulur (yoksa 0).
bool HandleEvent(union _XEvent* ev, int* hotkeyId);

// Diger thread'lerden gelen zil / dikkat isteklerini ana thread'de uygular.
void PumpDeferred();

// Pencere boyutu onbellegi (GetClientRect her cagrida sunucuya gitmesin).
void NoteClientSize(int w, int h);

// Odak geldi: dikkat (urgency) isaretini temizle.
void ClearAttention();

// Modal kutu/menu acikken ana pencereye birikmis girdi olaylari kapaninca
// islenmesin (Win32 sahibi devre disi birakir). Bu zamandan eski girdi atilir.
unsigned long ModalInputFence();

// Sistem olcegi: FULLTERMINAL_DPI > Xft.dpi (RESOURCE_MANAGER) > GDK_SCALE > QT_SCALE_FACTOR > 96
UINT SystemDpi(void* display);

// Alt+Bosluk pencere menusu. Secilen SC_* komutunu dondurur (iptal: 0).
UINT ShowWindowMenu(HWND hwnd);
// SC_* komutunu normal pencere icin uygular (Guake kipi cagirandadir).
void ApplyWindowCommand(HWND hwnd, UINT sc);

// _NET_WM_ICON (16..128 px) ayarlar.
void SetWindowIcon(HWND hwnd, HICON icon);

// Basit ARGB olcekleme (alan ortalamasi, on carpimli).
std::vector<uint32_t> ScaleArgb(const uint32_t* src, int srcSize, int dstSize);

} // namespace ft::lnx

#endif // !defined(_WIN32)
