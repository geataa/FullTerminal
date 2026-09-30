# FullTerminal — İster Listesi

| | |
|---|---|
| Belge | 02 / İster Listesi |
| Sürüm | 0.1 (taslak) |
| Tarih | 2026-09-09 |
| İlgili | [01-URUN-VIZYONU.md](01-URUN-VIZYONU.md), [03-MIMARI.md](03-MIMARI.md) |

## Öncelik anahtarı

| Kod | Anlamı |
|---|---|
| **P0** | MVP. Bu olmadan ürün çalışmaz. |
| **P1** | 1.0 sürümü. Termius paritesi için gerekli. |
| **P2** | 1.x. Farklılaşma ve derinlik. |
| **P3** | Sonraki yıl. Fikir havuzu. |

Kaynak sütunu: `parite` Termius'ta var, `fark` Termius'ta yok, `istek` senin
açıkça belirttiğin madde.

---

# E1 — Terminal Çekirdeği

VT ayrıştırıcı, ekran modeli ve çizim. Ürünün kalbi, en çok özen isteyen kısım.

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-TERM-001 | DEC ANSI durum makinesine uygun VT ayrıştırıcı. Bilinmeyen dizileri sessizce yut, asla çökme. | P0 | parite |
| FR-TERM-002 | `xterm-256color` tam desteği, ayrıca `xterm`, `linux`, `vt100`, `vt220` emülasyon modu seçimi. | P0 | parite |
| FR-TERM-003 | 24 bit truecolor (SGR 38;2 / 48;2), 256 renk paleti, temel 16 renk yeniden eşleme. | P0 | parite |
| FR-TERM-004 | Alternatif ekran arabelleği (vim, less, htop doğru çalışsın). | P0 | parite |
| FR-TERM-005 | Unicode 15.1 grapheme cluster segmentasyonu. Emoji, ZWJ dizileri, birleşen işaretler doğru genişlikte. | P0 | fark |
| FR-TERM-006 | Doğu Asya geniş karakter ve belirsiz genişlik (ambiguous width) profil ayarı. | P1 | parite |
| FR-TERM-007 | Scrollback en az 1.000.000 satır. Bellek sınırı aşılınca diske taşma, şeffaf geri okuma. | P1 | fark |
| FR-TERM-008 | Pencere yeniden boyutlandırmada satır yeniden akıtma (reflow), sarılı satırların korunması. | P1 | fark |
| FR-TERM-009 | GPU hızlandırmalı çizim. Glyph atlası, hasar (damage) takibi, sadece değişen hücreyi çiz. | P0 | fark |
| FR-TERM-010 | Ligatür desteği ve font fallback zinciri. Nerd Font ikonları, CJK ve emoji için ayrı font. | P1 | fark |
| FR-TERM-011 | Scrollback'te arama: düz metin ve regex, tüm eşleşmeleri vurgula, sonuçlar arası gezinme. | P0 | parite |
| FR-TERM-012 | OSC 8 hiper bağlantı. Tıklanabilir URL, ayrıca sezgisel URL ve dosya yolu algılama. | P1 | parite |
| FR-TERM-013 | OSC 52 pano okuma/yazma, izin sorusu ile. | P1 | parite |
| FR-TERM-014 | OSC 7 çalışma dizini bildirimi. Yeni sekme aynı dizinde açılır. | P1 | fark |
| FR-TERM-015 | OSC 133 semantik prompt işaretleri. Prompt'lar arası atlama, tek komutun çıktısını seçme ve kopyalama, komut çıkış kodunu sekme başlığında gösterme. | P1 | fark |
| FR-TERM-016 | Sixel grafik protokolü. | P2 | fark |
| FR-TERM-017 | Kitty grafik protokolü ve iTerm2 satır içi görsel protokolü. | P2 | fark |
| FR-TERM-018 | Fare raporlama: X10, VT200, SGR 1006, urxvt 1015; sürükleme ve tekerlek. | P0 | parite |
| FR-TERM-019 | Bracketed paste, focus in/out olayları. | P0 | parite |
| FR-TERM-020 | Seçim modları: karakter, kelime, satır, dikdörtgen blok. Çift/üçlü tık. | P0 | parite |
| FR-TERM-021 | Seçince kopyala ve orta tık yapıştır seçenekleri. | P1 | fark |
| FR-TERM-022 | Yapıştırma koruması. Çok satırlı veya `sudo`/`rm -rf` içeren yapıştırmada onay penceresi, satır önizleme. | P1 | fark |
| FR-TERM-023 | İmleç stili ve yanıp sönme (DECSCUSR), imleç rengi. | P1 | parite |
| FR-TERM-024 | Zil (bell) davranışı: sessiz, görsel, sistem bildirimi. | P1 | parite |
| FR-TERM-025 | Terminal boyutu değişince `SIGWINCH` / SSH `window-change` doğru iletimi. | P0 | parite |
| FR-TERM-026 | Yakınlaştırma, satır aralığı, harf aralığı, minimum kontrast ayarı. | P1 | fark |
| FR-TERM-027 | Tema motoru. iTerm2 `.itermcolors` ve Windows Terminal JSON şemalarını içe aktarma. | P1 | fark |
| FR-TERM-028 | Oturum kaydı asciicast v2 formatında, oynatıcı ve zaman çizelgesinde arama. | P2 | fark |
| FR-TERM-029 | Çıktı akış kısıtlama (flow control) ve aşırı çıktıda otomatik render seyreltme, donmama garantisi. | P0 | fark |
| FR-TERM-030 | ISO 2022 karakter seti geçişleri ve DEC çizgi çizme karakterleri. | P1 | parite |

---

# E2 — Yerel Terminaller (senin ana isteğin)

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-LOCAL-001 | Windows'ta ConPTY tabanlı yerel terminal. Windows 10 1809 ve üstü. | P0 | istek |
| FR-LOCAL-002 | Linux ve macOS'ta `forkpty` tabanlı yerel terminal. | P0 | parite |
| FR-LOCAL-003 | Kabuk otomatik keşfi: Windows PowerShell 5.1, PowerShell 7 (`pwsh`), CMD, Git Bash, MSYS2, Cygwin, Nushell, Developer PowerShell for VS, Anaconda Prompt. | P0 | istek |
| FR-LOCAL-004 | WSL dağıtımlarını kayıt defterinden (`Lxss`) ve `wsl.exe -l -v` çıktısından otomatik listele. Her dağıtım ayrı profil olarak görünsün. | P0 | istek |
| FR-LOCAL-005 | WSL profil ayarları: dağıtım, kullanıcı, başlangıç dizini (`--cd`), varsayılan dağıtım işareti, WSL1/WSL2 rozet. | P0 | istek |
| FR-LOCAL-006 | Profil başına ayar: ikon, renk, başlangıç dizini, ortam değişkenleri, komut satırı argümanları, kod sayfası (`chcp 65001`), sekme başlığı şablonu, font ve tema. | P0 | istek |
| FR-LOCAL-007 | Yükseltilmiş (yönetici) sekme açma. UAC akışı ve sekmede net görsel uyarı işareti. | P1 | istek |
| FR-LOCAL-008 | Varsayılan profil seçimi, yeni sekme açılırken profil seçici açılır menü. | P0 | istek |
| FR-LOCAL-009 | Windows Terminal `settings.json` ve fragment profillerini içe aktarma. | P2 | fark |
| FR-LOCAL-010 | Dosya gezgininden "burada FullTerminal aç" bağlam menüsü kaydı. | P2 | fark |
| FR-LOCAL-011 | Yeni sekme mevcut sekmenin çalışma dizinini devralır (OSC 7 veya süreç sorgusu ile). | P1 | fark |
| FR-LOCAL-012 | PowerShell için shell entegrasyonu: PSReadLine geçmişi, çalışma dizini, prompt işaretleri. Termius'un yapamadığı. | P1 | fark |
| FR-LOCAL-013 | CMD için asgari shell entegrasyonu: `doskey` geçmişi, `cd` takibi. | P2 | fark |
| FR-LOCAL-014 | bash ve zsh için shell entegrasyon scripti (otomatik kurulum teklifi, `rc` dosyasına tek satır). | P1 | parite |
| FR-LOCAL-015 | Yerel süreç izleme. Sekme başlığında çalışan komutun adı, kapatırken "hâlâ çalışıyor" uyarısı. | P1 | fark |
| FR-LOCAL-016 | Docker ve Podman container'ına `exec` ile bağlanma, container listesi envanterde. | P2 | fark |
| FR-LOCAL-017 | Seri port (COM/tty) bağlantısı: baud, data bits, parity, stop bits, flow control. | P2 | parite |
| FR-LOCAL-018 | Yerel terminalde de tetikleyiciler, snippet'ler ve oturum kaydı çalışsın. Uzak oturumdan farkı olmasın. | P1 | fark |
| FR-LOCAL-019 | Windows'ta yol dönüştürme yardımcıları: sürükle-bırak dosyayı WSL yoluna (`/mnt/c/...`) çevir. | P2 | istek |

---

# E3 — SSH

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-SSH-001 | SSH-2 protokolü. Parola, `publickey`, `keyboard-interactive`, `none` kimlik doğrulama. | P0 | parite |
| FR-SSH-002 | Anahtar formatları: OpenSSH (`ed25519`, `rsa`, `ecdsa`), PKCS#8, PuTTY `.ppk` v2/v3. Passphrase korumalı anahtarlar. | P0 | parite |
| FR-SSH-003 | SSH sertifikası ile kimlik doğrulama (SSH CA). | P1 | parite |
| FR-SSH-004 | Dahili SSH agent. Agent forwarding açılabilir, hangi anahtarın forward edildiği görünür. | P1 | parite |
| FR-SSH-005 | İşletim sistemi agent'ı ile köprü: Windows OpenSSH Agent (named pipe), Pageant, `SSH_AUTH_SOCK`. Termius bunu yapmıyor. | P2 | fark |
| FR-SSH-006 | FIDO2 donanım anahtarı: `sk-ssh-ed25519`, `sk-ecdsa`. Dokunma isteği için UI. | P1 | parite |
| FR-SSH-007 | PKCS#11 akıllı kart ve PIV desteği (YubiKey). | P2 | fark |
| FR-SSH-008 | Platform anahtar deposuna bağlı anahtarlar: Windows Hello / TPM, macOS Secure Enclave. | P2 | parite |
| FR-SSH-009 | Post-quantum anahtar değişimi: `mlkem768x25519-sha256` ve `sntrup761x25519-sha512`. | P2 | parite |
| FR-SSH-010 | Jump host zinciri, en az 5 sıçrama. `ProxyJump` semantiği. | P1 | parite |
| FR-SSH-011 | SOCKS4/5 ve HTTP CONNECT proxy üzerinden bağlanma. | P1 | parite |
| FR-SSH-012 | `ProxyCommand` desteği. | P2 | fark |
| FR-SSH-013 | Known hosts yönetimi: ekleme, değişiklik uyarısı, parmak izi görselleştirme, `@cert-authority` ve `@revoked` işaretleri, hashlenmiş host adları. | P0 | parite |
| FR-SSH-014 | Keepalive, `ServerAliveInterval` ve otomatik yeniden bağlanma (üstel geri çekilme, deneme sayısı ayarlanabilir). | P0 | parite |
| FR-SSH-015 | Sıkıştırma, `TCPKeepAlive`, `IPQoS`, IPv6, bağlanma zaman aşımı ayarları. | P1 | parite |
| FR-SSH-016 | Algoritma tercih listesi (KEX, cipher, MAC, host key) profil bazında düzenlenebilir. Eski sunucular için "legacy" profili. | P1 | fark |
| FR-SSH-017 | Bağlantı hata teşhisi. Başarısız el sıkışmada sunucunun desteklediği algoritmaları göster ve düzeltme öner. | P1 | fark |
| FR-SSH-018 | Mosh (UDP, roaming, yerel yankı) desteği. | P2 | parite |
| FR-SSH-019 | Telnet (RFC 854 seçenek pazarlığı). | P2 | parite |
| FR-SSH-020 | AWS SSM Session Manager ile bağlantı, SSH portu açmadan. | P2 | fark |
| FR-SSH-021 | Aynı TCP bağlantısı üzerinde kanal çoğullama. Bir hosta ikinci sekme yeni TCP açmasın (`ControlMaster` benzeri). | P1 | fark |
| FR-SSH-022 | Bağlantı başına bant genişliği, gecikme ve şifreleme paketi göstergesi. | P2 | fark |
| FR-SSH-023 | X11 forwarding. | P3 | fark |

---

# E4 — Dosya Transferi ve SFTP

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-FILE-001 | SFTP v3 istemcisi. Listeleme, indirme, yükleme, silme, yeniden adlandırma, klasör oluşturma. | P0 | parite |
| FR-FILE-002 | Çift panelli dosya yöneticisi. Yerel-uzak, uzak-uzak. | P0 | parite |
| FR-FILE-003 | Sürükle-bırak: paneller arası, işletim sistemi dosya gezgininden içeri, dışarı. | P1 | parite |
| FR-FILE-004 | Transfer kuyruğu: sıralama, duraklat, devam et, iptal, hız sınırı, eşzamanlı transfer sayısı. Termius'ta yok. | P1 | fark |
| FR-FILE-005 | Kesintiden devam etme (resume). Kısmi dosya doğrulama. | P1 | fark |
| FR-FILE-006 | Paralel akışlı transfer, büyük dosyada çoklu SFTP kanalı. | P2 | fark |
| FR-FILE-007 | Uzak dosya izinleri: `chmod` görsel matrisi, `chown`, `chgrp`, sembolik bağlantı oluşturma. Termius'ta yok. | P1 | fark |
| FR-FILE-008 | Uzak dosyayı yerel editörde açma. Değişikliği izle, kaydedince otomatik geri yükle. Editör seçilebilir (VS Code, Notepad++, dahili editör). | P1 | parite |
| FR-FILE-009 | Dahili metin editörü. Sözdizimi renklendirme, arama-değiştir, satır numarası. | P2 | fark |
| FR-FILE-010 | Uzak dizinde arama: ad, boyut, tarih, içerik (`grep` ile). | P2 | fark |
| FR-FILE-011 | Klasör senkronizasyonu: karşılaştır, tek yön veya çift yön eşitle, kuru çalıştırma (dry run). | P2 | fark |
| FR-FILE-012 | `rsync` ve `scp` arka uçlarını alternatif olarak kullanabilme. | P2 | fark |
| FR-FILE-013 | Varsayılan SFTP yolu, host ve grup seviyesinde. | P1 | parite |
| FR-FILE-014 | Yer imleri, son ziyaret edilen dizinler, gizli dosya gösterme anahtarı. | P1 | parite |
| FR-FILE-015 | Transfer bütünlük doğrulaması (sunucuda checksum varsa). | P2 | fark |
| FR-FILE-016 | Terminalden dosya sürükleyince yolu yapıştırma; SFTP panelinden terminale yol gönderme. | P2 | fark |

---

# E5 — Tünelleme ve Ağ

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-TUN-001 | Local port forwarding (`-L`). | P0 | parite |
| FR-TUN-002 | Remote port forwarding (`-R`). | P1 | parite |
| FR-TUN-003 | Dynamic / SOCKS5 proxy (`-D`). | P1 | parite |
| FR-TUN-004 | Unix domain socket forwarding. | P3 | fark |
| FR-TUN-005 | Tüm kuralların tek ekranda listesi, çift tıkla başlat/durdur, durum göstergesi. | P1 | parite |
| FR-TUN-006 | Kurulum sihirbazı. Her parametrenin ne işe yaradığını anlatan adım adım akış. | P1 | parite |
| FR-TUN-007 | Host bağlanınca kuralları otomatik başlatma. Bağlantı kopunca otomatik yeniden kurma. | P1 | fark |
| FR-TUN-008 | Port çakışması tespiti ve boş port önerisi. | P1 | fark |
| FR-TUN-009 | Tünel trafiği sayacı, açık bağlantı sayısı, canlı grafik. | P2 | fark |
| FR-TUN-010 | Kural grupları. "Prod tüneller" tek tıkla hepsi açılsın. | P2 | fark |

---

# E6 — Envanter ve Veri Modeli

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-INV-001 | Vault: en üst kapsayıcı. Yerel vault zorunlu, bulut vault opsiyonel. | P0 | parite |
| FR-INV-002 | Group: iç içe geçebilir, sınırsız derinlik. | P0 | parite |
| FR-INV-003 | Ayar mirası: Vault > Group > Alt grup > Host > Oturum geçersiz kılma. Her alanda "nereden geldi" rozetiyle. | P0 | parite |
| FR-INV-004 | Host: adres, çoklu protokol (her biri kendi port ve kimliğiyle), etiketler, notlar, ikon, renk. | P0 | parite |
| FR-INV-005 | Etiket sistemi ve etikete göre filtreleme, kaydedilmiş akıllı filtreler. | P1 | parite |
| FR-INV-006 | Hızlı geçiş paleti: `Ctrl+K` ile bulanık arama, host, grup, snippet, komut hepsi tek arama kutusunda. | P0 | fark |
| FR-INV-007 | Host durumu göstergesi: erişilebilir mi (ping/TCP probe), son bağlantı zamanı, açık oturum sayısı. | P2 | fark |
| FR-INV-008 | Toplu düzenleme: seçili hostlara kimlik, grup, etiket, ortam değişkeni atama. | P1 | fark |
| FR-INV-009 | Host şablonları. Yeni host eklerken hazır profilden türetme. | P2 | fark |
| FR-INV-010 | Yerel depolama SQLite, hassas alanlar alan bazında şifreli. | P0 | fark |
| FR-INV-011 | Değişiklik geçmişi ve geri alma. Yanlışlıkla silinen host kurtarılabilsin, 30 gün çöp kutusu. | P2 | fark |
| FR-INV-012 | Envanteri JSON/YAML olarak dışa ve içe aktarma. Kilitlenme (vendor lock-in) yok. | P1 | fark |

---

# E7 — Keychain ve Güvenlik

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-SEC-001 | Keychain: parola, SSH anahtarı, sertifika, Identity nesneleri. Bir kayıt birden çok host tarafından referanslanır. | P0 | parite |
| FR-SEC-002 | Ana parola ile kilitlenen vault. Argon2id anahtar türetme, XChaCha20-Poly1305 ile şifreleme. | P0 | parite |
| FR-SEC-003 | Otomatik kilitleme: boşta kalma süresi, uyku, ekran kilidi. | P1 | parite |
| FR-SEC-004 | Biyometrik kilit açma: Windows Hello, Touch ID. | P1 | parite |
| FR-SEC-005 | Anahtar üretici: Ed25519, ECDSA P-256/384, RSA 3072/4096. Passphrase zorunluluğu ayarı. | P1 | parite |
| FR-SEC-006 | Sunucuya public key kurulumu (`ssh-copy-id` eşdeğeri), tek tıkla. | P1 | parite |
| FR-SEC-007 | Parola üreteci ve gücünü gösteren ölçer. | P2 | fark |
| FR-SEC-008 | Bellekte hassas veri: kilitli sayfalar (`mlock`/`VirtualLock`), kullanım sonrası güvenli silme, çekirdek dökümlerinde (core dump) dışlama. | P0 | fark |
| FR-SEC-009 | Hiçbir sır düz metin olarak diske yazılmaz. Geçici dosya gerekiyorsa bellek destekli dizinde ve kapanışta güvenli silinir. | P0 | fark |
| FR-SEC-010 | Denetim izi (audit log). Kim, ne zaman, hangi hosta bağlandı, hangi komutu MCP üzerinden çalıştırdı. Sadece ekleme yapılabilen (append-only) dosya. | P1 | fark |
| FR-SEC-011 | Sır sızıntısı taraması. Terminal çıktısında AWS anahtarı, özel anahtar bloğu, JWT gibi kalıplar görülünce maskele ve uyar. | P2 | fark |
| FR-SEC-012 | Üretim ortamı işaretleme. Kırmızı sekme rengi, komut çalıştırmadan önce onay, yasaklı komut listesi. | P1 | fark |
| FR-SEC-013 | Harici parola yöneticisi entegrasyonu: 1Password CLI, Bitwarden CLI, HashiCorp Vault, Windows Credential Manager. | P2 | fark |
| FR-SEC-014 | Vault yedeği: şifreli tek dosya dışa aktarma ve geri yükleme. | P1 | fark |
| FR-SEC-015 | İki faktörlü doğrulama (TOTP) uygulama kilidi için. | P2 | parite |
| FR-SEC-016 | Sertifika ve anahtar sona erme takibi, uyarı. | P2 | fark |

---

# E8 — Üretkenlik

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-PROD-001 | Sekmeler, bölünmüş paneller, serbest döşeme (yatay/dikey, iç içe), panel yakınlaştırma. | P0 | parite |
| FR-PROD-002 | Workspace: birden çok oturumu tek ortamda toplama. | P1 | parite |
| FR-PROD-003 | Focus mode ve Split view. En az 16 panel. | P1 | parite |
| FR-PROD-004 | Komut yayını (broadcast). Seçili panellere, tüm panellere veya etikete göre gruba. | P1 | parite |
| FR-PROD-005 | Workspace şablonları. Bağlantılar, düzen, sekme adları, çalışma dizinleri, açılış komutları kaydedilir. | P1 | parite |
| FR-PROD-006 | Oturum durum göstergeleri: okunmamış çıktı, girdi bekliyor, hata. | P1 | parite |
| FR-PROD-007 | Snippet: adlandırılmış script, klasörlenebilir, etiketlenebilir. | P0 | parite |
| FR-PROD-008 | **Snippet değişkenleri.** `{{host}}`, `{{user}}`, `{{env.X}}`, `{{prompt:Servis adı}}`, `{{select:a|b|c}}`, `{{secret:kayıt}}`. Çalıştırmadan önce parametre formu. Termius'ta yok. | P1 | fark |
| FR-PROD-009 | Snippet'i çoklu hostta çalıştırma. Sonuçlar tek matris ekranda, çıkış kodu ve çıktı özeti ile. | P1 | parite |
| FR-PROD-010 | Startup snippet, host ve grup seviyesinde. | P1 | parite |
| FR-PROD-011 | Snippet için "önce kuru çalıştır" ve "onay iste" bayrakları. | P2 | fark |
| FR-PROD-012 | **Tetikleyiciler (trigger).** Çıktıda regex eşleşince: satırı vurgula, masaüstü bildirimi, ses, snippet çalıştır, sekmeyi işaretle, oturumu kaydet. Termius'ta yok. | P1 | fark |
| FR-PROD-013 | Otomatik yanıtlayıcı. Belirli prompt görünce kayıtlı değeri gönder (parola, `yes/no`). | P2 | fark |
| FR-PROD-014 | Autocomplete: shell geçmişi, dosya yolu, snippet, dahili komut ve argümanları, host adları, k8s kaynak adları. | P1 | parite |
| FR-PROD-015 | Komut paleti (`Ctrl+Shift+P`). Uygulamadaki her eylem klavyeyle erişilebilir. | P1 | fark |
| FR-PROD-016 | Tam yapılandırılabilir kısayollar, çakışma tespiti, şema dışa aktarma. | P1 | fark |
| FR-PROD-017 | Oturum logları. Otomatik kayıt, arama, yer imi, yorum, saklama süresi politikası. | P1 | parite |
| FR-PROD-018 | Çıktı filtreleme. Canlı `grep` paneli, satırları gizle/göster. | P2 | fark |
| FR-PROD-019 | Yapılandırılmış çıktı algılama. JSON çıktısını katlanabilir ağaç olarak göster, tabloyu hizala. | P2 | fark |
| FR-PROD-020 | Komut geçmişi ekranı. Tüm hostlarda çalıştırılan komutlarda arama, tekrar çalıştırma. | P2 | fark |
| FR-PROD-021 | Kalıcı oturum (persistent session). Uygulama kapansa bile oturum arka planda yaşar, açılınca geri bağlanır. `tmux` gerektirmez. | P2 | fark |
| FR-PROD-022 | Sekme başlığı şablonları: `{host}`, `{user}`, `{cwd}`, `{cmd}`, `{k8s.ctx}`. | P1 | fark |
| FR-PROD-023 | Not defteri paneli. Host bazında serbest not, oturum sırasında açılabilir. | P3 | fark |

---

# E9 — Kubernetes (senin ana isteğin)

Bu bölüm Termius'ta hiç yok. Ürünün en güçlü farklılaşma alanı.

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-K8S-001 | Kubeconfig içe aktarma. Birden çok dosya, `KUBECONFIG` birleştirme kuralları, cluster/context/user ayrıştırma. | P0 | istek |
| FR-K8S-002 | Context'ler envanterde birinci sınıf düğüm. Cluster > Namespace > Workload > Pod ağacı. | P1 | istek |
| FR-K8S-003 | Kimlik doğrulama yöntemleri: client sertifikası, bearer token, `exec` credential plugin (aws-iam-authenticator, gke-gcloud-auth-plugin, az), OIDC yenileme. | P1 | istek |
| FR-K8S-004 | Pod içine terminal. `v4.channel.k8s.io` WebSocket ile exec, container seçimi, TTY yeniden boyutlandırma, stdin. | P1 | istek |
| FR-K8S-005 | Pod log takibi. Follow, `--previous`, çoklu container, çoklu pod tek panelde birleşik akış, renkli kaynak etiketi. | P1 | fark |
| FR-K8S-006 | `kubectl port-forward` eşdeğeri, tünel yöneticisinde diğer kurallarla aynı ekranda. | P1 | istek |
| FR-K8S-007 | Node'a SSH ile bağlanmayı context ile ilişkilendirme. Pod'un çalıştığı node'a tek tıkla SSH. | P2 | fark |
| **Manifest tabanlı ortam içe aktarma** | | | |
| FR-K8S-010 | Bir terminal profiline (yerel veya SSH) bir veya birden çok YAML dosyası iliştirme. Terminal açılırken ortam otomatik kurulur. | P0 | istek |
| FR-K8S-011 | Desteklenen kaynaklar: `Pod`, `Deployment`, `StatefulSet`, `DaemonSet`, `Job`, `CronJob`, `ConfigMap`, `Secret`. Çok belgeli (`---`) dosyalar. | P0 | istek |
| FR-K8S-012 | `spec.containers[].env` ve `envFrom` çıkarımı. Container birden fazlaysa hangisinin alınacağı seçilir. | P0 | istek |
| FR-K8S-013 | `configMapRef` ve `secretRef` referanslarını canlı cluster'dan çözme. Kullanıcı onayı ister, çözülen sırlar maskeli gösterilir, diske yazılmaz. | P1 | istek |
| FR-K8S-014 | `valueFrom.fieldRef` ve `resourceFieldRef` için makul yerel karşılıklar veya boş bırakma seçeneği. | P2 | istek |
| FR-K8S-015 | Ortam kaynağı önceliği ve çakışma çözümü: Manifest < Grup < Host < Profil < Oturum. Çakışan anahtarlar UI'da işaretlenir, hangisinin kazandığı gösterilir. | P0 | istek |
| FR-K8S-016 | Uzak SSH oturumunda taşıma stratejisi. Öncelik `SendEnv` (sunucu `AcceptEnv` izin veriyorsa), aksi halde ilk prompt'tan önce gönderilen `export` ön bloğu, üçüncü seçenek geçici dosyaya yazıp `source` edip silme. Strateji host bazında seçilebilir ve otomatik algılanır. | P0 | istek |
| FR-K8S-017 | `AcceptEnv` uyumsuzluğu tespiti. Değişken sunucuya ulaşmadıysa uyar ve `sshd_config` düzeltmesini göster. | P1 | fark |
| FR-K8S-018 | Terminal açılışında `KUBECONFIG`, `KUBE_CONTEXT`, `KUBE_NAMESPACE` otomatik ayarlama. Geçici birleşik kubeconfig üretilir, oturum kapanınca silinir. | P0 | istek |
| FR-K8S-019 | Helm chart desteği: `helm template` ile render edip ortamı çıkar. Values dosyası seçilebilir. | P2 | istek |
| FR-K8S-020 | Kustomize desteği: `kustomize build` ile render. | P2 | istek |
| FR-K8S-021 | `.env` dosyası ve `docker-compose.yml` `environment` bloğu içe aktarma. Aynı motor. | P1 | fark |
| FR-K8S-022 | Ortam önizleme ekranı. Terminal açılmadan önce enjekte edilecek tüm `KEY=VALUE` listesi, kaynağı ve maskeleme durumu ile. | P0 | istek |
| FR-K8S-023 | YAML dosyasını izleme (watch). Dosya değişince "ortam güncellendi, yeniden yükle" bildirimi. | P2 | fark |
| **Güvenlik ve gündelik kullanım** | | | |
| FR-K8S-030 | Durum çubuğunda aktif context ve namespace. Tek tıkla değiştirme. | P1 | istek |
| FR-K8S-031 | Üretim context'i işaretleme. Kırmızı renk, `delete`/`drain`/`scale 0` gibi komutlarda onay penceresi. | P1 | fark |
| FR-K8S-032 | Context bazında komut yasak listesi. | P2 | fark |
| FR-K8S-033 | Kaynak adları için autocomplete. `kubectl get pod <TAB>` canlı cluster'dan tamamlansın. | P2 | fark |
| FR-K8S-034 | Sır değerleri hiçbir zaman oturum loguna, denetim izine veya AI isteğine düz metin gitmez. | P0 | fark |

---

# E10 — MCP (senin ana isteğin)

İki yön var ve ikisini de istiyoruz.

## 10.1 MCP Sunucusu — terminal, ajanın aracı olur

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-MCP-001 | Uygulama bir MCP sunucusu barındırır. Claude Code, Claude Desktop veya herhangi bir MCP istemcisi bağlanabilir. | P1 | istek |
| FR-MCP-002 | Taşıma: stdio ve Streamable HTTP. HTTP için token tabanlı kimlik doğrulama ve sadece localhost varsayılanı. | P1 | istek |
| FR-MCP-003 | Headless mod. `fullterminal --mcp-stdio` ile GUI açık olmadan çalışır, oturum daemon'una bağlanır. | P1 | istek |
| FR-MCP-004 | Araç seti — envanter: `list_hosts`, `get_host`, `list_groups`, `list_snippets`, `list_sessions`. | P1 | istek |
| FR-MCP-005 | Araç seti — oturum: `open_session`, `send_input`, `read_output`, `close_session`, `resize_session`. | P1 | istek |
| FR-MCP-006 | Araç seti — komut: `run_command` (host, komut, zaman aşımı) tek seferlik, çıkış kodu ve stdout/stderr ayrı döner. | P1 | istek |
| FR-MCP-007 | Araç seti — dosya: `sftp_list`, `sftp_read`, `sftp_write`, `sftp_stat`. Boyut sınırı ve ikili dosya koruması. | P1 | istek |
| FR-MCP-008 | Araç seti — tünel: `port_forward_start`, `port_forward_stop`, `list_forwards`. | P2 | istek |
| FR-MCP-009 | Araç seti — Kubernetes: `k8s_contexts`, `k8s_get`, `k8s_logs`, `k8s_exec`, `k8s_port_forward`. | P2 | istek |
| FR-MCP-010 | Araç seti — log: `search_session_logs`, `get_session_log`. | P2 | istek |
| FR-MCP-011 | MCP kaynakları (resources): host envanteri, oturum logları, snippet'ler salt okunur kaynak olarak sunulur. | P2 | istek |
| FR-MCP-012 | MCP prompt'ları: "olay müdahalesi", "sunucu sağlık kontrolü" gibi hazır iş akışları. | P3 | istek |
| **Politika ve güvenlik. Bu bölüm pazarlığa kapalı.** | | | |
| FR-MCP-020 | Varsayılan salt okunur. Yazma ve komut çalıştırma araçları kapalı gelir, kullanıcı açıkça açar. | P1 | fark |
| FR-MCP-021 | Politika motoru: araç bazında, host bazında, grup bazında, vault bazında izin ver/reddet kuralları. | P1 | fark |
| FR-MCP-022 | Komut izin listesi ve yasak listesi, regex destekli. Yıkıcı komut sezgisel algılama (`rm -rf`, `dd`, `mkfs`, `shutdown`, `DROP TABLE`). | P1 | fark |
| FR-MCP-023 | İnsan onayı akışı. Çalıştırılacak komut, hedef host ve tahmini etki gösterilir. "Bu oturum için hatırla" seçeneği süreli. | P1 | fark |
| FR-MCP-024 | Süreli yetki. Verilen izin belirtilen dakika sonra otomatik düşer. | P2 | fark |
| FR-MCP-025 | Her MCP çağrısı denetim izine yazılır: zaman, istemci, araç, argüman, hedef, sonuç, onaylayan. | P1 | fark |
| FR-MCP-026 | Hız sınırı ve eşzamanlılık sınırı. Kaçak ajan koruması. | P2 | fark |
| FR-MCP-027 | Prompt injection savunması. Sunucu ve komut çıktıları veri olarak işaretlenir, asla talimat olarak yürütülmez. Kontrol dizileri temizlenir. | P1 | fark |
| FR-MCP-028 | Sır redaksiyonu. Vault değerleri, k8s Secret'ları ve algılanan anahtar kalıpları MCP yanıtından çıkarılır. | P1 | fark |
| FR-MCP-029 | Canlı MCP etkinlik paneli. Hangi istemci ne yapıyor, tek tıkla kes. | P2 | fark |

## 10.2 MCP İstemcisi — uygulama, dış sunucuları kullanır

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-MCP-040 | Harici MCP sunucularına bağlanma. stdio ve HTTP. Sunucu kataloğu, ekleme, düzenleme, etkinleştirme. | P2 | istek |
| FR-MCP-041 | Sunucu yapılandırması Claude Desktop / Claude Code `mcp.json` formatıyla uyumlu içe aktarılabilsin. | P2 | istek |
| FR-MCP-042 | Bağlı sunucuların araçları AI panelinden çağrılabilir, her çağrı için onay. | P2 | istek |
| FR-MCP-043 | OAuth akışı gerektiren sunucular için tarayıcı tabanlı yetkilendirme. | P3 | istek |
| FR-MCP-044 | Sunucu sağlık durumu, gecikme, hata sayacı. | P3 | fark |

---

# E11 — AI Asistanı

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-AI-001 | Doğal dilden komut üretimi. Terminalde kısayolla açılır, komut önerilir, açıklaması gösterilir, çalıştırmadan önce onay. | P2 | parite |
| FR-AI-002 | Hata açıklama. Sıfırdan farklı çıkış kodunda "bunu açıkla" eylemi, son komut ve çıktı bağlamıyla. | P2 | fark |
| FR-AI-003 | Uzun log özetleme ve anormallik işaretleme. | P2 | fark |
| FR-AI-004 | Sağlayıcı bağımsız arka uç. Varsayılan Anthropic Claude (`claude-opus-5`, `claude-sonnet-5`, `claude-haiku-4-5-20251001`), ayrıca OpenAI uyumlu uç noktalar ve yerel modeller (Ollama, llama.cpp). | P2 | fark |
| FR-AI-005 | Redaksiyon hattı. Modele giden her şeyden sırlar, anahtarlar ve isteğe bağlı IP/hostname temizlenir. Önizleme gösterilir. | P2 | fark |
| FR-AI-006 | Çevrimdışı mod anahtarı. Kapalıyken hiçbir veri dışarı çıkmaz, AI özellikleri gri görünür. | P2 | fark |
| FR-AI-007 | Vault ve host bazında "AI'ya asla gönderme" bayrağı. | P2 | fark |
| FR-AI-008 | AI paneli kendi MCP araçlarımızı da kullanabilir. Yani asistan gerçekten sunucuya bağlanıp iş yapabilir, politika motoru altında. | P2 | fark |
| FR-AI-009 | Token ve maliyet sayacı, aylık bütçe limiti. | P3 | fark |

---

# E12 — Senkronizasyon ve Ekip

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-SYNC-001 | Uçtan uca şifreli senkron. İstemci tarafında şifreleme, sunucu düz metni asla görmez. | P2 | parite |
| FR-SYNC-002 | Taşıma seçenekleri: kendi barındırdığın sunucu, WebDAV, S3 uyumlu depo, Git deposu, yerel klasör. Satıcıya bağımlılık yok. | P2 | fark |
| FR-SYNC-003 | Çakışma çözümü. Alan bazında son yazan kazanır artı manuel çakışma ekranı. | P2 | fark |
| FR-SYNC-004 | Anahtarları senkron dışında tutma seçeneği. Sadece host ve ayarlar senkronlansın. | P2 | parite |
| FR-SYNC-005 | Cihaz listesi ve cihaz iptali (revoke). | P3 | parite |
| FR-SYNC-006 | Team vault, çoklu vault, üye bazında görüntüleme/düzenleme izni. | P3 | parite |
| FR-SYNC-007 | Gerçek zamanlı ortak oturum (multiplayer). Tek yazar, kontrol devri, salt izleyici. | P3 | parite |
| FR-SYNC-008 | Paylaşılan oturum logları ve yorumlar. | P3 | parite |

---

# E13 — Entegrasyon ve İçe Aktarma

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-IMP-001 | OpenSSH `~/.ssh/config` içe aktarma **ve iki yönlü canlı senkron**. Dosyada değişen host uygulamaya, uygulamada değişen host dosyaya yansır. | P1 | fark |
| FR-IMP-002 | PuTTY ve KiTTY oturumları (kayıt defteri ve `.reg`). | P1 | fark |
| FR-IMP-003 | MobaXterm, WinSCP, SecureCRT, mRemoteNG, Xshell, Royal TS içe aktarma. | P2 | fark |
| FR-IMP-004 | Termius dışa aktarımı içe aktarma. Geçiş kolaylığı bir pazarlama argümanı. | P1 | fark |
| FR-IMP-005 | CSV içe aktarma, kolon eşleme sihirbazı, kullanıcı adı ve parola dahil. | P1 | parite |
| FR-IMP-006 | Bulut envanteri: AWS EC2, Azure VM, GCP, DigitalOcean, Hetzner, Proxmox. Etiketlerden otomatik grup üretme, periyodik yenileme. | P2 | parite |
| FR-IMP-007 | Ansible inventory (INI ve YAML) içe aktarma, grup yapısını koruyarak. | P2 | parite |
| FR-IMP-008 | Terraform state ve `tfstate` çıktısından host çıkarma. | P3 | fark |
| FR-IMP-009 | `known_hosts` ve `authorized_keys` içe aktarma. | P1 | fark |
| FR-IMP-010 | Komut satırı arayüzü: `fullterminal ssh <host>`, `fullterminal open --profile wsl-ubuntu`. `ssh://` URL şeması kaydı. | P2 | fark |

---

# E14 — Uygulama Kabuğu ve UX

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-UX-001 | Tek pencere, çok sekme, sekmeyi ayrı pencereye ayırma ve geri birleştirme. | P0 | parite |
| FR-UX-002 | Kenar çubuğu: vault ağacı, arama, favoriler, son bağlantılar. Gizlenebilir. | P0 | parite |
| FR-UX-003 | Açık ve koyu tema, sistem temasını takip etme. | P0 | parite |
| FR-UX-004 | Yerelleştirme altyapısı. Türkçe ve İngilizce ilk sürümde. Termius'ta hiç yok. | P1 | fark |
| FR-UX-005 | Yüksek DPI, çoklu monitör, farklı ölçekli monitörler arası taşıma. | P0 | fark |
| FR-UX-006 | Erişilebilirlik: ekran okuyucu için terminal içeriği, klavyeyle tam gezinme, kontrast ayarı. | P2 | fark |
| FR-UX-007 | Oturum geri yükleme. Uygulama çöktükten veya kapandıktan sonra sekmeler geri gelir. | P1 | parite |
| FR-UX-008 | Ayarlar ekranı: aranabilir, her ayarın açıklaması, varsayılana dönme, JSON olarak düzenleme seçeneği. | P1 | fark |
| FR-UX-009 | Sistem tepsisi simgesi, arka planda çalışma, hızlı bağlantı menüsü. | P2 | fark |
| FR-UX-010 | Otomatik güncelleme. Kullanıcı başlatır, arka planda indirir, oturumları düşürmez. | P1 | parite |
| FR-UX-011 | Çökme raporlama, opt-in, kişisel veri içermez. | P1 | fark |
| FR-UX-012 | İlk çalıştırma sihirbazı: mevcut istemciden içe aktar, profil keşfi, tema seçimi. | P1 | fark |
| FR-UX-013 | Portable (kurulumsuz) sürüm. Tüm veri uygulama klasöründe. | P2 | fark |

---

# E15 — Eklenti ve Otomasyon

| ID | İster | Önc. | Kaynak |
|---|---|---|---|
| FR-EXT-001 | Eklenti çalıştırma ortamı. Lua (sol2) veya QuickJS. Sanal alan (sandbox) içinde. | P3 | fark |
| FR-EXT-002 | Eklenti API'si: tetikleyici kaydet, komut paleti eylemi ekle, panel ekle, oturum olaylarına abone ol. | P3 | fark |
| FR-EXT-003 | Olay kancaları (hooks): bağlantı öncesi, bağlantı sonrası, kopma, komut çalıştırma öncesi. | P2 | fark |
| FR-EXT-004 | Eklenti dizini ve imzalı paket dağıtımı. | P3 | fark |

---

# Fonksiyonel Olmayan İsterler

| ID | İster | Hedef |
|---|---|---|
| NFR-001 | Dil ve standart | C++23, derleyici uyarıları hata sayılır (`-Werror`) |
| NFR-002 | Soğuk açılış | 400 ms altı (NVMe, orta seviye masaüstü) |
| NFR-003 | Yerel sekme açılışı | 100 ms altı |
| NFR-004 | SSH bağlantı kurulumu | LAN'da 1 s altı |
| NFR-005 | Boşta bellek | 120 MB altı |
| NFR-006 | Oturum başına ek bellek | 15 MB altı |
| NFR-007 | Çıktı işleme | 100 MB/s üstü (`cat büyükdosya` testi) |
| NFR-008 | Girdi gecikmesi | Tuşa basma ile ekranda görünme p99 8 ms altı |
| NFR-009 | Kare hızı | Minimum 60 FPS, monitör destekliyorsa 120 |
| NFR-010 | Kurulu boyut | 80 MB altı |
| NFR-011 | Platform | Windows 10 1809+, Windows 11, Ubuntu 22.04+, macOS 13+ |
| NFR-012 | Mimari | x64 ve ARM64 |
| NFR-013 | Bellek güvenliği | ASan, UBSan, TSan ile CI'da test. Ham `new`/`delete` yok, RAII zorunlu |
| NFR-014 | Fuzzing | VT ayrıştırıcı, SSH paket ayrıştırıcı ve YAML ayrıştırıcı sürekli fuzz edilir |
| NFR-015 | Test kapsamı | Çekirdek modüllerde satır kapsamı %75 üstü |
| NFR-016 | Kararlılık | Çökmesiz oturum oranı %99.9 üstü |
| NFR-017 | Tedarik zinciri | SBOM üretimi, bağımlılıklar sabitlenmiş sürüm, imzalı binary |
| NFR-018 | Yeniden üretilebilir derleme | Aynı kaynak aynı çıktıyı üretir |
| NFR-019 | Gizlilik | Telemetri varsayılan kapalı, opt-in, anonim |
| NFR-020 | Lisans uyumu | Kullanılan her kütüphanenin lisansı ürün modeliyle uyumlu olmalı |

---

# Kilitlenen Kararlar

| # | Konu | Karar |
|---|---|---|
| 1 | UI çatısı | **Yok. Saf Win32.** Qt kullanılmayacak. Gety ve GhostView'in ev tarzı |
| 2 | Çizim | D3D11 + DXGI + DirectComposition, terminal için DirectWrite glyph atlası. Kabuk aynı cihazda Direct2D interop |
| 3 | Bağımlılık | Vendor edilmiş statik kaynak. Paket yöneticisi yok, DLL yok, tek taşınabilir exe |
| 4 | SSH | libssh2 + karma kripto arka ucu (Windows CNG artı libsodium) |
| 5 | Platform | Windows öncelikli. Çekirdek katmanlar platformdan bağımsız yazılır |
| 6 | Sonraki adım | M0 teknik doğrulaması: ConPTY ve glyph atlası prototipleri |

# Kalan Açık Kararlar

| # | Soru | Öneri |
|---|---|---|
| 1 | Kripto kütüphanesi libsodium mu Monocypher mı | Önce libsodium, sonra küçültmeyi değerlendir |
| 2 | Lisans | Açık çekirdek |
| 3 | Senkron | M6'ya kadar dosya tabanlı |
| 4 | Ekip özellikleri | İlk sürümde kapsam dışı |
| 5 | Ticarileştirme | Sonra kararlaştırılır, mimari engel olmasın |
