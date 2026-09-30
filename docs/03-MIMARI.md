# FullTerminal — Teknik Mimari ve Yol Haritası

| | |
|---|---|
| Belge | 03 / Mimari |
| Sürüm | 0.2 |
| Tarih | 2026-09-09 |
| İlgili | [02-ISTER-LISTESI.md](02-ISTER-LISTESI.md) |

## 0. Kilitlenen kararlar

| Karar | Seçim | Gerekçe |
|---|---|---|
| Dil | C++23 | Zorunlu kısıt |
| UI çatısı | **Yok. Saf Win32.** Qt kullanılmayacak | Gety ve GhostView'in ev tarzı |
| Çizim | **D3D11 + DXGI + DirectComposition**, terminal yüzeyi için DirectWrite glyph atlası | 120 FPS ve 100 MB/s çıktı hedefi |
| Uygulama kabuğu | Aynı D3D11 cihazı üzerinde Direct2D interop | Gety'nin `Direct2DRenderer` kalıbı yeniden kullanılır |
| Ağ | WinHTTP (HTTPS ve WebSocket) | Gety'de kanıtlanmış, OpenSSL veya curl gerektirmez |
| Bağımlılık | **Vendor edilmiş statik kaynak.** Paket yöneticisi yok, DLL yok | Tek taşınabilir exe kuralı korunuyor |
| Platform | Windows öncelikli, çekirdek platformdan bağımsız | Farklılaşma zaten Windows'ta |
| CRT | Statik `/MT`, `/MTd` | Redistributable gerektirmez |

Referans projeler: `E:\0_SkySoft\Gety` ve `E:\0_SkySoft\PhotoViewer` (GhostView).
Bu iki projenin kalıpları taban alınacak, sıfırdan icat edilmeyecek.

---

## 1. Referans projelerden devralınanlar

Yeniden yazılmayacak, taşınacak olanlar:

| Kaynak | Ne | Nereye |
|---|---|---|
| `Gety/src/ui/Theme.h` | Obsidian koyu palet, `#00a2ff` cyan vurgu, DPI yardımcıları | `src/ui/Theme.h` |
| `Gety/src/ui/Direct2DRenderer` | Elle yazılmış widget katmanı: hit-test, layout dikdörtgenleri, hover ve pressed durumları, modern scrollbar, sekmeler | `src/ui/shell/` |
| `Gety/src/ui/MainWindow` | Çerçevesiz pencere, özel başlık çubuğu, sürüklenebilir header, DPI değişimi | `src/ui/MainWindow` |
| `Gety/src/core/I18n` | TR ve EN yerelleştirme altyapısı | `src/core/I18n` |
| `Gety/src/core/Config` | INI tabanlı taşınabilir ayar | `src/core/Config` |
| `Gety/src/engine/WinHttpUtils` | WinHTTP sarmalayıcı | `src/net/http/` |
| `Gety/src/engine/SegmentWorker` | Çok parçalı transfer ve iş parçacığı havuzu kalıbı | `src/transport/sftp/` transfer kuyruğu |
| `GhostView/src/ViewerApp` | D3D11 + DXGI + DirectComposition kurulumu, swapchain, resize | `src/render/Device` |
| `GhostView/src/ThumbnailBar` | Arka plan iş parçacığı + asenkron kaynak yükleme kalıbı | Scrollback ve SFTP listeleme |
| `Gety/src/ui/Dialogs` | Modal diyalog çizim kalıbı | Ayarlar, host düzenleme, onay pencereleri |
| `Gety/CMakeLists.txt` | `/MT`, `/utf-8`, statik link, POST_BUILD kopyalama | Kök `CMakeLists.txt` |

---

## 2. Katmanlar

```
┌────────────────────────────────────────────────────────────────┐
│  ui/            Win32 pencere, çerçevesiz kabuk, diyaloglar    │
│                 D2D ile çizilen kenar çubuğu, sekmeler, panel  │
├────────────────────────────────────────────────────────────────┤
│  render/        D3D11 cihazı, DXGI swapchain, DirectComposition│
│    ├ atlas/     DirectWrite glyph atlası, shaping, fallback    │
│    └ grid/      Hücre ızgarası instanced çizimi, hasar takibi  │
├────────────────────────────────────────────────────────────────┤
│  session/       Oturum yaşam döngüsü, çoğullama, kayıt         │
│  vt/            VT ayrıştırıcı, ekran ızgarası, scrollback     │
├────────────────────────────────────────────────────────────────┤
│  transport/                                                    │
│    ├ pty/       ConPTY (Win) | forkpty (POSIX, sonra)          │
│    ├ ssh/       libssh2 sarmalayıcı, kanal, agent, forward     │
│    ├ sftp/      SFTP istemcisi, transfer kuyruğu               │
│    ├ k8s/       WinHTTP REST + WebSocket exec/attach           │
│    └ serial/    COM portu                                      │
├────────────────────────────────────────────────────────────────┤
│  model/         Vault, Group, Host, Identity, Snippet          │
│  vault/         Şifreleme, anahtar türetme, kilit              │
│  policy/        İzin motoru, denetim izi                       │
│  envmap/        YAML, Helm, .env → ortam değişkeni çözücü      │
├────────────────────────────────────────────────────────────────┤
│  mcp/           JSON-RPC sunucusu ve istemcisi, araç kaydı     │
├────────────────────────────────────────────────────────────────┤
│  core/          İş parçacığı havuzu, log, config, I18n, hata   │
└────────────────────────────────────────────────────────────────┘
```

**Kural:** `vt`, `model`, `policy`, `envmap`, `mcp` katmanlarında **hiçbir Win32
UI çağrısı yoktur.** Düz C++23. Bu sayede birim testleri konsolda saniyeler
içinde koşar, ileride POSIX portu ve headless daemon aynı kodu kullanır.
`transport` altındaki her arka uç bir arayüzün arkasında durur.

---

## 3. Süreç modeli

İki binary:

**`ftagent.exe`** — arka plan daemon. SSH bağlantılarını, ConPTY süreçlerini,
tünelleri ve MCP sunucusunu tutar. GUI kapansa bile oturumlar yaşar
(FR-PROD-021). Claude Code, GUI açık olmadan buraya bağlanabilir (FR-MCP-003).

**`FullTerminal.exe`** — GUI. Named pipe ile agent'a bağlanır, yoksa başlatır.
GUI çökerse oturumlar hayatta kalır.

İletişim: yerel named pipe, uzunluk önekli ikili çerçeveler. Aynı makinede,
yalnızca aynı kullanıcı oturumuna açık (pipe ACL ile).

M0 ve M1'de tek süreç olarak geliştirilir, ayrıştırma M3'te yapılır. Ama sınır
baştan çizilir, yoksa sonradan ayırmak imkânsız olur.

---

## 4. Bağımlılıklar

Hepsi kaynak olarak `third_party/` altına girer ve statik derlenir.
Paket yöneticisi yok. Çalışma zamanında hiçbir DLL gerekmez.

| Alan | Seçim | Lisans | Boyut | Not |
|---|---|---|---|---|
| SSH protokolü | **libssh2** | BSD-3 | ~200 KB | Kripto arka ucu bizim sağladığımız katmana bağlanır |
| Kripto | **libsodium** (veya Monocypher) | ISC / CC0 | ~300 KB / ~30 KB | Ed25519, X25519, ChaCha20-Poly1305, Argon2id. Monocypher tek dosya, çok daha küçük, ama Argon2id ve bazı primitifler için ek iş |
| Simetrik ve hash | **Windows CNG** (`bcrypt.dll`) | Sistem | 0 | AES-GCM, AES-CTR, SHA-2, HMAC donanım hızlandırmalı. Sistemde var, vendor edilmez |
| Veritabanı | **SQLite amalgamation** | Public domain | ~700 KB | Tek `.c` dosyası, ev tarzına birebir uyar |
| JSON | **nlohmann/json** tek başlık | MIT | 0 (başlık) | MCP ve k8s API |
| YAML | **rapidyaml** | MIT | ~150 KB | Kubeconfig ve manifest |
| Unicode | **Windows yerleşik ICU** (`icu.dll`, Win10 1703+), yedek **utf8proc** | Sistem / MIT | 0 / ~200 KB | Grapheme cluster segmentasyonu |
| Metin şekillendirme | **DirectWrite** | Sistem | 0 | Ligatür, font fallback, bidi. HarfBuzz gerekmez |
| Ağ | **WinHTTP** | Sistem | 0 | HTTPS ve WebSocket. Schannel ile TLS 1.3 |
| Test | **doctest** tek başlık | MIT | 0 | Catch2'den hafif, derleme süresini şişirmez |

Tahmini toplam exe boyutu: 3 ile 5 MB arası. NFR-010'un (80 MB) çok altında.

**Kural:** yeni bir bağımlılık eklemek bir karar gerektirir. Ölçüt üç madde:
statik derlenebiliyor mu, lisansı uygun mu, Windows API'sinin zaten yaptığı bir
işi mi tekrarlıyor.

---

## 5. Çizim mimarisi

### 5.1 Tek cihaz, iki yüzey

GhostView'in kurulumu genişletiliyor. Tek bir `ID3D11Device` var. Üstünde:

- **Terminal yüzeyi** — kendi glyph atlas motorumuz, doğrudan D3D11.
- **Uygulama kabuğu** — `ID2D1Device` interop ile aynı cihaz üzerinde. Gety'nin
  `Direct2DRenderer` kalıbı buraya taşınır.

İkisi DirectComposition ile katmanlanır. Kabuk yeniden çizilmeden terminal
yüzeyi güncellenebilir, ki asıl kazanç bu.

### 5.2 Glyph atlası

Windows Terminal'in AtlasEngine yaklaşımı:

1. DirectWrite bir metin çalıştırmasını (text run) şekillendirir. Ligatür, font
   fallback ve karmaşık yazı sistemleri burada çözülür.
2. Her benzersiz `(glyph id, font, boyut, ağırlık)` dörtlüsü bir kez rasterize
   edilip doku atlasına yazılır. `IDWriteFontFace::CreateGlyphRunAnalysis` veya
   renkli glyph'ler için `IDWriteFontFace4::GetGlyphImageData`.
3. Çizim, hücre başına bir instance olan tek bir instanced draw çağrısı.
   Vertex shader hücre koordinatını, pixel shader atlas UV'sini ve rengi alır.
4. Atlas dolarsa LRU ile tahliye edilir.

Hasar takibi: her karede yalnızca değişen hücre aralıkları instance
tamponuna yazılır. Tam ekran yeniden çizim sadece resize ve tema değişiminde.

### 5.3 Aşırı çıktı davranışı (FR-TERM-029)

Ayrıştırıcı ile çizici ayrı hızlarda çalışır. `yes` veya devasa bir log
akarken ayrıştırıcı tam hızda ekran modelini günceller, çizici vsync'e
sabitlenir ve sadece son duruma bakar. Kullanıcı için kritik olan terminalin
donmaması, her ara kareyi görmesi değil.

---

## 6. Kritik teknik konular

### 6.1 ConPTY
`CreatePseudoConsole` Windows 10 1809 ve üstünde var. Bilinen tuzaklar:
yeniden boyutlandırmada tam ekran yeniden çizim gönderiyor, `chcp` ve kod
sayfası etkileşimi kirli, kapanışta `ClosePseudoConsole` ile süreç sonlandırma
sırası yanlış olursa kilitleniyor. M0'ın ilk işi bu alanın teknik doğrulaması.

### 6.2 libssh2 kripto arka ucu
libssh2'nin hazır arka uçları arasında WinCNG var, ama Ed25519 ve
`curve25519-sha256` desteği sınırlı. Bizim planımız karma bir arka uç:
AES-GCM, AES-CTR, SHA-2 ve HMAC için Windows CNG, Ed25519 ve X25519 için
libsodium. Bu arka ucu libssh2'nin `crypto.h` sözleşmesine göre kendimiz
yazıyoruz. **Bu bir doğrulama maddesi, M1'in ilk işi.** Çıkmaz olursa B planı
libssh2'yi tamamen libsodium arka ucuyla derlemek.

### 6.3 Ortam değişkeni enjeksiyonu (FR-K8S-016)
Üç strateji, host bazında otomatik seçim ve sonucun kalıcı öğrenilmesi:

1. **`SendEnv`** — SSH protokolünün resmî yolu. Sunucunun `sshd_config`
   dosyasında `AcceptEnv` ile izin vermesi gerekiyor, çoğu sunucuda kapalı.
2. **`export` ön bloğu** — bağlantı kurulunca, kullanıcı prompt'u görmeden
   `export KEY='value'` satırları gönderilir. Her yerde çalışır. Değerlerin
   shell geçmişine düşmesi baştaki boşlukla, oturum loguna yazılması sır
   maskelemesiyle önlenir.
3. **Geçici dosya + `source` + `shred`** — çok sayıda veya çok uzun değişkende.

### 6.4 Kubernetes exec
`kubectl exec` eşdeğeri, API sunucusuna WebSocket ile bağlanıp
`v4.channel.k8s.io` alt protokolünü konuşmak. Her mesajın ilk baytı kanal
numarası: 0 stdin, 1 stdout, 2 stderr, 3 hata, 4 yeniden boyutlandırma.
WinHTTP WebSocket API'si (`WinHttpWebSocketCompleteUpgrade`) bunu karşılıyor.
`exec` credential plugin'ler için harici süreç çalıştırma ve token
önbellekleme gerekiyor.

### 6.5 MCP sunucusu
MCP, JSON-RPC 2.0 üzerine oturan küçük bir protokol. Olgun bir C++ SDK
varsaymıyoruz; `initialize`, `tools/list`, `tools/call`, `resources/list`,
`resources/read` mesajlarını kendimiz uyguluyoruz. Taşıma stdio ve Streamable
HTTP olarak ayrılıyor. Spesifikasyonun güncel sürümü uygulamadan önce okunacak.

**Güvenlik ilkesi:** MCP araçları üretim sunucularında komut çalıştırabildiği
için politika motoru araç kaydından önce devrede. Çağrı yolunda tek bir kapı
var ve hiçbir araç onu atlayamıyor.

### 6.6 Sır yönetimi
Sırlar `SecureString` tipinde tutulur: `VirtualLock` ile kilitli sayfa, yıkıcıda
`SecureZeroMemory`, kopyalama yasak, sadece taşıma. Log katmanı bu tipi derleme
zamanında yazdıramaz. Çekirdek dökümü devre dışı.

---

## 7. Depo düzeni

```
FullTerminal/
├─ CMakeLists.txt
├─ build.bat                 doğrudan cl.exe ile derleme, Gety tarzı
├─ docs/
├─ third_party/              libssh2, libsodium, sqlite, rapidyaml, json
├─ src/
│  ├─ core/                  Config, I18n, log, iş parçacığı havuzu
│  ├─ vt/                    ayrıştırıcı, ızgara, scrollback
│  ├─ render/                D3D11 cihaz, atlas, ızgara çizici
│  ├─ ui/                    MainWindow, kabuk, diyaloglar, Theme.h
│  ├─ transport/{pty,ssh,sftp,k8s,serial}/
│  ├─ net/http/              WinHTTP sarmalayıcı
│  ├─ model/  vault/  policy/  envmap/  mcp/  session/
│  ├─ agent/                 ftagent.exe giriş noktası
│  └─ main.cpp               FullTerminal.exe giriş noktası
├─ resources/                ikon, manifest, .rc
├─ tests/{unit,fuzz}/
└─ portable_data/            ayarlar ve vault, Gety tarzı taşınabilir
```

---

## 8. Yol haritası

Her milestone'un çıktısı çalışan bir binary. Süreler tek geliştirici
varsayımıyla kaba tahmin.

### M0 — Terminal iskeleti (4–6 hafta)
CMake ve `build.bat`, CI. **İlk hafta ConPTY teknik doğrulaması.** VT
ayrıştırıcı ve ekran ızgarası. D3D11 cihaz ve glyph atlası. Sekme ve bölme.
PowerShell, CMD ve WSL profilleri açılıyor. Kopyala yapıştır, arama, tema.
**Kabul:** `FR-TERM-001..004, 009, 011, 018..020, 025, 029`,
`FR-LOCAL-001..008`, `NFR-003, NFR-007`.

### M1 — SSH ve envanter (5–7 hafta)
**İlk hafta libssh2 kripto arka ucu doğrulaması.** Parola ve anahtar kimlik
doğrulama, known hosts. Vault, grup, host modeli ve miras. Keychain ve
şifreleme. `~/.ssh/config` içe aktarma. Hızlı geçiş paleti.
**Kabul:** `FR-SSH-001..002, 013..015`, `FR-INV-001..006, 010`,
`FR-SEC-001..003, 008..009`, `FR-IMP-001, 009`, `NFR-002, 004, 005`.

### M2 — Dosya ve tünel (4–5 hafta)
SFTP istemcisi, çift panel, transfer kuyruğu, uzak dosya düzenleme. Üç tip port
yönlendirme ve kural yöneticisi. Jump host zinciri.
**Kabul:** `FR-FILE-001..008, 013..014`, `FR-TUN-001..008`, `FR-SSH-010..011`.

### M3 — Üretkenlik (4–6 hafta)
Süreç ayrıştırması: `ftagent.exe` doğuyor. Workspace, split view, broadcast,
şablonlar. Snippet ve değişken motoru. Tetikleyiciler. Oturum logları. Shell
entegrasyonu, OSC 133, autocomplete.
**Kabul:** `FR-PROD-001..017, 021..022`, `FR-TERM-014..015`,
`FR-LOCAL-012, 014, 018`.

### M4 — Kubernetes (4–5 hafta)
Kubeconfig, context ağacı, pod exec, log takibi, port-forward. Manifest tabanlı
ortam içe aktarma ve önizleme ekranı. Üretim işaretleme.
**Kabul:** `FR-K8S-001..006, 010..018, 022, 030..031, 034`.

### M5 — MCP ve AI (4–6 hafta)
Politika motoru ve denetim izi. MCP sunucusu, araç seti, onay akışı, headless
mod. MCP istemcisi. AI paneli.
**Kabul:** `FR-MCP-001..010, 020..029`, `FR-SEC-010`, `FR-AI-001..008`.

### M6 — Olgunlaşma (sürekli)
Senkron, yerelleştirme genişletme, erişilebilirlik, POSIX portu, eklenti
ortamı, kalan P2 maddeleri.

---

## 9. En büyük riskler

| Risk | Etki | Azaltma |
|---|---|---|
| ConPTY köşe durumları beklenenden çok zaman yer | M0 kayar | M0'ın ilk haftası teknik doğrulama, erken karar |
| Glyph atlası motoru karmaşık çıkar, ligatür ve fallback köşe durumları | M0 kayar | Önce tek fontlu, ligatürsüz sürüm çalışsın. Ligatür ve fallback M0 sonunda eklensin |
| libssh2 karma kripto arka ucu tutmaz | M1 kayar | M1'in ilk haftası doğrulama. B planı tamamen libsodium arka ucu |
| VT emülasyonunda uyumsuzluk, vim ve tmux bozulur | Ürün güvenilmez olur | `vttest` ve gerçek uygulama regresyon takımı, M0'dan itibaren CI'da |
| MCP ile üretim sunucusunda kaza | Güven kaybı, veri kaybı | Varsayılan salt okunur, insan onayı, yıkıcı komut tespiti, denetim izi |
| Kapsam çok geniş, hiçbir şey bitmez | Proje ölür | Her milestone'da çalışan binary, P0 dışına taşma yok |

---

## 10. Kalan açık kararlar

| # | Soru | Öneri |
|---|---|---|
| 1 | Kripto kütüphanesi libsodium mu Monocypher mı | Monocypher tek dosya ve 30 KB, ama Argon2id ve bazı primitifler eksik. Önce libsodium ile çalıştır, sonra küçültmeyi değerlendir |
| 2 | Lisans: kapalı, açık çekirdek, tamamen açık | Açık çekirdek |
| 3 | Senkron: hiç yok, kendi sunucusu, dosya tabanlı | M6'ya kadar dosya tabanlı |
| 4 | Ticarileştirme modeli | Sonra kararlaştırılır, mimari engel olmasın |

## 11. Sonraki adım

**M0 teknik doğrulaması.** İki küçük prototip:

1. ConPTY ile PowerShell, CMD ve bir WSL dağıtımı açan, çıktıyı ham olarak
   pencereye basan minimal uygulama. Resize, kapanış ve kod sayfası
   davranışları ölçülür.
2. D3D11 üzerinde DirectWrite glyph atlası ile sabit genişlikli bir ızgara
   çizen prototip. 100 MB/s çıktı ve 120 FPS ölçülür.

İkisi çalışınca birleştirilir ve M0'ın gövdesi başlar.
