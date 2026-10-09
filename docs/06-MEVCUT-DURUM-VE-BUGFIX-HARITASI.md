# FullTerminal: mevcut ürün ve bug fix haritası

İnceleme ve düzeltme tarihi: 2026-10-08. Başlangıç kod tabanı: `9e9529e`. Bu rapor inceleme günündeki durumu kaydeder; yayımlanan güncel kapsam için [1.1.0 sürüm notlarına](releases/v1.1.0.md) bakın.

Kaynaklar: SkyWorks/SkyCortex FULLTERMINAL projesindeki 15 görev, Herdr dönüşüm wiki sayfası (inceleme sırasında v13), mevcut kaynak kodu, derleme betikleri ve CI. Görevlerin tamamı DONE işaretliydi; kodla karşılaştırma eksik GUI entegrasyonlarını ortaya çıkardı. Bu çalışma sonunda iki derleme yolu ve 14 CTest takımı çalıştırıldı.

## Mimari

- C++20, saf Win32, statik CRT; Qt/Electron/Tauri yok.
- D3D11/DXGI/DirectComposition üzerinde Direct2D ve DirectWrite çizimi.
- `MainWindow`: pencere olayları, girdi, oturumlar, çizim koordinasyonu. Screens, Sidebar, TabBar, Quake, Mcp, AgentRibbon, Swarm ve Fleet ayrı derleme birimleri; aynı sınıfın ortak durumunu kullanıyor.
- `Ui`: elle çizilen anlık kip Field/TextArea/NumField/HexColorField kontrolleri; pano, seçim, imleç ve odak yönetimi.
- `TerminalTab`: `PtySessionClient` üzerinden daemon'a bağlanır; çıktı kuyruğu, VT ayrıştırıcı, Screen, SSH aşamaları, ajan tespiti ve kayıt kancaları GUI'dedir. ConPTY'nin sahibi ayrı `--daemon` sürecidir.
- `PaneLayout`: sekme başına BSP ağacı; panel bölme, kapatma, odak ve zoom. `m_tabs` ile `m_tabLayouts` birlikte yönetiliyor.
- `Inventory`, `K8sManager`, `SftpModel`, `SnippetModel`, `TunnelModel`: ürün modelleri. Ayarlar ve envanter exe yanındaki portable_data altında. Gizli envanter değerleri DPAPI ile korunuyor.
- SSH/SFTP/tüneller Windows OpenSSH alt süreçlerini kullanıyor; uygulama içi libssh2 motoru değil. WSL, Docker ve kubectl hedefleri ilgili kurulu araçlara bağlı.
- Aynı exe GUI, `--mcp` stdio, `--mcp-http`, `--daemon`/`--ftagent` ve `--replay <dosya>` kiplerini barındırır. HTTP GUI'ye bağlı ayrı süreç/Job içinde çalışır; terminal daemon'ı GUI'den bağımsızdır.

## Kodda bulunan ürün kapsamı

| Alan | Mevcut kapsam |
| --- | --- |
| Yerel terminal | Windows PowerShell, PowerShell 7, CMD, Git Bash, MSYS2, Nushell, WSL, özel profil; kurulum keşfi |
| Oturumlar | Çoklu sekme, sekme yeniden adlandırma, iç içe yatay/dikey panel, odak gezintisi, zoom, oturum düzeni kaydetme/geri yükleme |
| Terminal motoru | ANSI/VT, 256 renk/truecolor, alternatif ekran, scrollback, seçim/pano, fare raporlama, paste guard |
| Masaüstü | Frameless pencere, DPI, tray, kapanınca arka plan, tek örnek, Guake/Visor global kısayol ve animasyon; monitör/boyut/hizalama/odak kaybında gizleme ayarları |
| Görünüm | Font/boyut, %10–100 terminal opaklığı, renk şemaları/özel renk, accent, imleç, dil seçimi |
| SSH | Host/kimlik envanteri, parola/anahtar/sertifika alanları, jump host, hızlı bağlantı, aşamalı bağlantı ekranı ve loglar |
| SFTP | Yerel/uzak çift panel, gezinme/filtre, upload/download, klasör işlemleri, silme/yeniden adlandırma, uzak dosya açma, transfer banner |
| Güvenlik araçları | Anahtar üretme servisi, known_hosts yönetimi, DPAPI kimlik saklama |
| Tüneller | Yerel -L, uzak -R, SOCKS -D; başlat/durdur, süreç durumu, kalıcı kurallar |
| Kubernetes | YAML/kubeconfig tarama ve import, bağlantı hedefleri, terminal KUBECONFIG ortamı, WSL yol dönüşümü, snippet üzerinden kubectl apply |
| Snippets | Kategoriler, arama, ekleme/düzenleme/silme, kopyalama/çalıştırma, YAML alanı |
| Ajan çalışma alanı | Claude/Antigravity/Codex/Cursor/Gemini/genel istem tespiti, idle/working/blocked; sekme uyarıları, onay/ret ribbon, fleet drawer, 4 swarm şablonu, broadcast |
| Kabuk semantiği | OSC 8 ve düz URL tespiti, Ctrl+tık bağlantı, OSC 133 komut işaretleri/çıkış kodu/gezinti, hata bağlamını ajan promptu olarak panoya verme |
| Kayıt | Terminal giriş/çıkış/boyut kayıt kancaları; .ftrec ve .cast v2; kayıt listesi, dosya seçme, oynatma/duraklatma, seek, restart, 0.25×–4× hız, MCP kayıt kontrolü |
| MCP | ft_systems, ft_exec, ft_fs, ft_vault, ft_pane_list, ft_pane_split, ft_pane_prompt, ft_pane_read, ft_pane_wait, ft_swarm_spawn, ft_session_record; stdio ve loopback HTTP, GUI IPC, izin/onay/audit |

## Bu çalışmada kapatılan eksikler

1. **Daemon GUI'ye bağlandı.** Restore sessions açıkken çıkışta panel ağacı ve oturum kimlikleri atomik kaydedilir, canlı oturumlar detach edilir. Açılışta aynı sürece bağlanılır; daemon artık yoksa eski profil/host üzerinden yeni oturum açılır. Sekme/panel kapatma oturumu sonlandırır. Başlatma komutu ve ortam değişkenleri daemon belleğinden geri alınır; session.json'a sır taşıyabilecek komut/ortam yazılmaz. Named pipe kullanıcı ACL'si, kurulum ad alanı, uzak istemci reddi ve mesaj boyutu sınırına sahiptir.
2. **Kayıt oynatma UI'ye bağlandı.** Logs → Recordings üzerinden dosya seçilir veya listeden oynatılır. Geri sarma parser/ekran durumunu sıfırlar. Unicode kaçışları, eksik/büyük/bozuk dosyalar, zaman sırası ve terminal boyutları doğrulanır. Kayıt kaydetme gerçek disk yazma sonucunu döndürür.
3. **MCP HTTP uygulandı.** Yalnızca 127.0.0.1, Bearer token, Host/Origin doğrulaması, stateless JSON POST, bildirimlere 202. Port/token değişikliğinde GUI süreci yeniden başlatır. Token DPAPI ile saklanır; eski düz metin token ilk başarılı kayıtta taşınır. Kayıt başlatma/durdurma yazma ve onay politikasına dahil edildi. GUI IPC yanıt sınırı/zaman aşımı ve izin kontrolü eklendi; başka kurulumun penceresine bağlanma önlendi.
4. **Ürün metinleri düzeltildi.** README'de Hintçe, C++20, aktif sekme içi broadcast ve Windows/harici araç bağımlılıkları doğru ifade edildi. Ölçülmemiş gecikme garantisi kaldırıldı. Mimari belgesinde hedef ile mevcut uygulama ayrıldı; MCP rehberi 11 araca ve HTTP'ye güncellendi.
5. **CMake onarıldı.** Eksik I18n.cpp, comdlg32 ve yeni kaynaklar eklendi; gömülü manifest çakışması giderildi. Ortak çekirdek kütüphanesi GUI ve testler tarafından kullanılıyor. Ninja + MSVC yolu doğrulandı.
6. **CI test kapsamı genişletildi.** Yedi eski giriş betiği korunarak tek `tests/run_all_tests.bat` betiğine bağlandı. Release CI artık 13 C++ takımı ve gerçek HTTP sürecini sınayan Python testi çalıştıracak şekilde düzenlendi. Python yalnızca test bağımlılığıdır.

## Doğrulama — 2026-10-08

- Baz durum: eski `build.bat` ve yedi eski test betiği başarılıydı; bunlar eksik entegrasyonları kapsamıyordu.
- Son durum: `build.bat` ve CMake/Ninja Release derlemesi başarılı. `ctest --test-dir build/ninja --output-on-failure` ve temiz `tests/run_all_tests.bat`: **14/14 geçti**. Son boyut taşması kontrolü eklendikten sonra CMake yeniden derlendi, ilgili kayıt/replay regresyon testi tekrar geçti. Release testlerinde assert'ler etkindir.
- Daemon testi ayrı süreç açar; TerminalTab girdi/çıktı, detach/reattach sonrasında kabuk değişkeninin korunması, başlatma bilgisinin korunması, resize, çıkış kodu 7, GUI bağlanmadan biten komutun son çıktısı/çıkış kodu 9 ve cleanup kontrol edilir.
- Replay: binary/cast roundtrip, Unicode, geri sarma/yeniden başlatma ve bozuk dosya retleri. Yerleşik `--screenshot` ile gerçek replay ekranı çizildi; renkli çıktı, Türkçe karakterler ve kontrol yerleşimi görsel incelendi (`build/replay-preview/replay.png`). Bu ekran görüntüsü etkileşimli fare/kısayol otomasyonu değildir.
- HTTP: gerçek izole exe süreci üzerinde initialize, 11 araç şeması, notification, hatalı token/Host/Origin/protokol, read-only retleri ve kapanma. Gerçek dış sunucuda komut çalıştırılmadı.
- DPAPI testi token'ın dosyada açık metin bulunmadığını, tekrar çözüldüğünü ve temizlenebildiğini doğrular. Sandbox hesabının DPAPI profili kullanılamadığından bu test gerçek Windows kullanıcı bağlamında çalıştırıldı. Kişisel portable_data/host dosyaları testlerde kullanılmadı.
- Yerel loglar: `build/final-msvc-build.log`, `build/ninja-build.log`, `build/final-test-runner.log`. GitHub Actions uzaktan tetiklenmedi.

## Kalan ürün sınırları

- Reattach son 256 KiB ham terminal çıktısını tekrar oynatır; sınırsız veya bütün VT durumunu birebir saklayan snapshot değildir. Windows yeniden başlatılırsa eski süreç devam etmez. Tek oturuma aynı anda tek GUI istemcisi bağlanır.
- HTTP sıralı, stateless JSON taşımasıdır; SSE aboneliği ve dış MCP sunucularına bağlanan MCP client uygulanmadı. GUI kapanınca HTTP kapanır; daemon oturumları restore ayarı açıksa yaşamaya devam eder.
- Gerçek SSH/SFTP hostları, uzak Kubernetes kümeleri, farklı Windows sürümleri ve çoklu monitörde Guake davranışı bu turda uçtan uca sınanmadı. Yeni replay/HTTP kontrollerinde İngilizce/Türkçe metin kullanılır; 16 dilin tamamına yeni çeviri eklenmedi.
- DPAPI sırları Windows kullanıcısına bağlıdır. MCP vault ile GUI envanterinin eşzamanlı düzenlenmesi hâlâ ayrı bir inceleme alanıdır; MCP rehberi bu sınırlamayı belirtir.
- Harici servis kataloğundaki teknoloji kaydı kaynak kodla uyuşmayabilir. Bu rapor yerel kod ve test sonuçlarını esas alır; harici katalog bu incelemede değiştirilmedi.
