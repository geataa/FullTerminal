# M0 — Teknik Doğrulama Raporu

| | |
|---|---|
| Belge | 04 / M0 Raporu |
| Tarih | 2026-09-09 |
| Durum | M0 gövdesi çalışıyor, ölçümler alındı |

---

## 1. Ne çalışıyor

Tek `FullTerminal.exe`, **503 KB**, harici DLL yok, statik CRT, taşınabilir.

**Terminal çekirdeği**
- DEC ANSI durum makinesi. CSI, OSC, DCS, SGR, özel modlar (DECSET/DECRST).
- Ekran modeli: birincil ve alternatif tampon, kaydırma bölgesi, ertelenmiş sarma, scrollback.
- 24 bit truecolor, 256 renk, ANSI 16, kalın, italik, altı çizili, üstü çizili, ters, sönük.
- UTF-8 akış çözücü, geniş karakter ve birleşen işaret genişlik tablosu.
- Seçim, kopyala, yapıştır, bracketed paste, fare tekerleğiyle scrollback.

**Çizim**
- D3D11 + DXGI + DirectComposition. D2D interop ile uygulama kabuğu.
- Terminal metni `DrawGlyphRun` ile çiziliyor: aynı biçimdeki hücreler tek çağrıda.
- Glyph indisleri yüz başına önbellekte. Fontta olmayan karakter için sistem yedeklemesi.

**Yerel terminaller**
- ConPTY arka ucu, `kernel32`'den dinamik çözülüyor.
- Otomatik profil keşfi: PowerShell 7, Windows PowerShell, Komut İstemi, WSL dağıtımları
  (kayıt defterinden, sürüm ve varsayılan işaretiyle), Git Bash, MSYS2, Nushell.
- Profil başına ikon rozeti, renk, başlangıç dizini, ortam değişkenleri.

**Uygulama kabuğu**
- Çerçevesiz pencere, kendi başlık çubuğu, sekme şeridi, Windows 11 yuvarlak köşe.
- Kenar çubuğu: Terminal, Hostlar, Kimlikler, Ayarlar.
- Elle yazılmış widget seti: buton, metin alanı, onay kutusu, segment seçici, liste satırı.

**SSH ve envanter**
- Host envanteri: etiket, adres, port, kullanıcı, doğrulama türü, anahtar yolu, grup,
  etiketler, jump host, not, üretim işareti.
- Kimlikler (Keychain) ayrı ekran, birden çok host tarafından paylaşılır.
- Parolalar diske **DPAPI** ile şifreli yazılıyor, düz metin yok (FR-SEC-009).
- Bağlantı: Windows'un yerleşik OpenSSH istemcisi ConPTY içinde başlatılıyor.
  Parola istemi görülürse kayıtlı parola bir kez otomatik gönderiliyor.
- Ayarlar: font ailesi ve boyutu, imleç stili, yanıp sönme, seçince kopyala, zil,
  varsayılan profil, scrollback satır sayısı. Hepsi `portable_data/` altında.

## 2. Ölçümler

| Ölçüt | Hedef (NFR) | Ölçülen | Durum |
|---|---|---|---|
| Kurulu boyut | 80 MB altı | 0,5 MB | geçti |
| Kare hızı | 60 FPS | 60 FPS (vsync) | geçti |
| VT ayrıştırıcı + ekran modeli | 100 MB/s | 65–77 MB/s | yaklaştı |
| ConPTY üzerinden gerçek çıktı | — | ~2 MB/s | ConPTY tavanı |

**Ayrıştırıcı ölçümü:** `tests/bench_vt.cpp`, 93 MB metin, 126x36 ızgara, 64 KB parçalar.
Üç turda 76,8 / 71,1 / 65,1 MB/s.

```bash
bin\bench_vt.exe bin\bench.txt 126 36
```

## 3. Bulgular

**ConPTY gerçek darboğaz, biz değiliz.** Aynı 93 MB'lık dosya `type` ile terminale
akıtıldığında uygulama 2 MB/s görüyor. Ayrıştırıcı tek başına 70 MB/s yapıyor.
Aradaki fark tamamen ConPTY'nin konsol çıktısını VT'ye yeniden kodlamasından geliyor.
Sonuç: kendi HLSL glyph atlas motorumuza geçmek için acele etmeye gerek yok, çünkü
şu anki darboğaz çizim değil. Karar M3'e ertelendi.

**Ayrıştırıcıda kalan %30.** Hedefe ulaşmak için en büyük aday scrollback satır
ayırmaları. Her satır ayrı bir `std::vector<Cell>` ayırıyor; 800 bin satırda bu
ciddi bir yük. Havuzlanmış ayırıcı ya da halka tampon bunu çözer.

**`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE_HANDLE` diye bir sabit yok.** Doğrusu
`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` (0x00020016). Eski SDK'lar için kendimiz
tanımlıyoruz.

**`NTDDI_VERSION` açıkça verilmeli.** `_WIN32_WINNT=0x0A00` tek başına
`NTDDI_WIN10` demek, ConPTY tipleri ise `NTDDI_WIN10_RS5` (0x0A000006) gerektiriyor.
Verilmezse `HPCON` tanımsız kalıyor.

**Çerçevesiz pencerede `SWP_FRAMECHANGED` şart.** `WM_NCCALCSIZE`'ın sıfır dönmesi
tek başına yetmiyor; çerçeve ölçüleri pencere oluşturulurken önbelleklendiği için
bir kez frame değişikliği bildirilmezse işletim sisteminin başlık çubuğu bizimkinin
üstünde durmaya devam ediyor.

**ConPTY kapatma sırası.** Çalışan sıra: girdi borusunun yazma ucunu kapat, sürecin
çıkmasını bekle (takılırsa öldür), `ClosePseudoConsole`, okuyucuyu iptal et ve
birleştir, kalan handle'ları kapat. Başka sırada kilitleniyor.

**DPI farkındalığı ölçüm scriptlerini de ilgilendiriyor.** Uygulama PerMonitorV2;
DPI-farkında olmayan bir PowerShell scripti `GetClientRect`'ten sanallaştırılmış
mantıksal piksel alıyor ve ekran görüntüsü yanlış boyutta çıkıyor. Doğrulama
scriptlerinde `SetProcessDpiAwarenessContext(-4)` çağrılmalı.

## 4. Bilinen eksikler

| Konu | Durum |
|---|---|
| Birleşen işaretler (grapheme cluster) | Atlanıyor, tek `char32_t` tutuluyor |
| Yeniden boyutlandırmada satır akıtma | Yok, alttan hizalanıyor (FR-TERM-008) |
| Scrollback diske taşma | Yok, 10.000 satırda kesiliyor (FR-TERM-007) |
| Ligatür | DirectWrite şekillendirmesi kullanılmıyor, glyph indisi doğrudan alınıyor |
| Sixel, Kitty grafik | Yok |
| Bölünmüş panel | Yok, sadece sekme |
| Kendi SSH istemcimiz | Yok, `ssh.exe` sarmalanıyor. libssh2 M1'de |
| SFTP, port yönlendirme | Yok, M2 |

## 5. Sıradaki adım

M1'in ilk işi: **libssh2 karma kripto arka ucu doğrulaması.** Windows CNG'de
Ed25519 imzalama olmadığı için simetrik ve hash tarafı CNG, Ed25519 ve X25519
libsodium olacak. Tutmazsa B planı tamamen libsodium arka ucu.

Ondan sonra kendi SSH istemcimiz `ssh.exe` sarmalayıcısının yerini alır ve
anahtar yönetimi, agent, port yönlendirme, SFTP kapıları açılır.
