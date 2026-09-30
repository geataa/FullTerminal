# FullTerminal — Ürün Vizyonu ve Rakip Analizi

| | |
|---|---|
| Belge | 01 / Ürün Vizyonu |
| Sürüm | 0.1 (taslak) |
| Tarih | 2026-09-09 |
| Sahip | İlter |
| Durum | İnceleme bekliyor |

---

## 1. Tek cümlelik vizyon

Termius'un yaptığı her şeyi yapan, ama Electron değil **native C++** olduğu için
kat kat hızlı açılan; SSH/SFTP'nin yanı sıra **Windows yerel terminallerini
(PowerShell, CMD, WSL)** birinci sınıf vatandaş kabul eden; terminal açılırken
**Kubernetes ortamını otomatik içeri aktaran**; ve tüm yeteneklerini **MCP
(Model Context Protocol)** üzerinden yapay zekâ ajanlarına açan terminal
istemcisi.

## 2. Neden bu ürün

Termius pazarın en olgun ürünü, 2 milyondan fazla mühendis kullanıyor. Üç tane
yapısal açığı var.

**Birinci açık: Electron.** Uygulama ağır, soğuk açılış saniyeler sürüyor, boş
haldeyken yüz megabaytlarca RAM tutuyor. Terminal gün içinde en çok açılıp
kapanan araçlardan biri. Native bir istemci burada ölçülebilir fark yaratır.

**İkinci açık: yerel terminal ikinci sınıf.** Termius'ta Local Terminal var ama
üzerine bir şey inşa edilmemiş. Autocomplete PowerShell ve CMD'de çalışmıyor,
WSL dağıtım yönetimi yok, profil sistemi yok. Windows kullanıcısı bugün Windows
Terminal ile Termius arasında mekik dokuyor. İkisini tek uygulamada birleştirmek
gerçek bir boşluk dolduruyor.

**Üçüncü açık: yapay zekâ yüzeysel.** Termius'un AI'ı tek iş yapıyor, doğal dili
komuta çeviriyor. Terminalin kendisi bir ajan için araç değil. Oysa terminal
istemcisi, bir AI ajanının altyapıya erişebileceği en doğal köprüdür. MCP sunucusu
olarak çalışan bir terminal, Claude Code'un envanterdeki sunuculara güvenli,
politikalı ve denetlenebilir şekilde erişmesini sağlar. Bunu yapan ürün yok.

## 3. Hedef kullanıcı

| Persona | Kim | En büyük acı noktası | Bizim cevabımız |
|---|---|---|---|
| **DevOps Deniz** | 50–500 sunucu, k8s, günde 20 sekme | Bağlantı öncesi ortam hazırlama ritüeli, context/namespace karışıklığı | K8s profili + otomatik env import |
| **Windows Geliştirici Efe** | .NET/C++ geliştirici, WSL kullanıyor | Windows Terminal + Termius + WinSCP üçgeni | Tek uygulamada yerel + uzak + dosya |
| **SRE Selin** | Nöbetçi, olay müdahalesi | Olay anında 12 sunucuda aynı komut, log korelasyonu | Workspace + broadcast + tetikleyici + oturum kaydı |
| **AI-first Kaan** | Claude Code ile çalışıyor | Ajanın sunucuya erişimi yok, kopyala-yapıştır | MCP sunucusu + politika motoru |
| **Homelab Mert** | 5–10 makine, hobi | Ücretsiz plan kısıtları, zorunlu hesap | Hesapsız tam işlevsellik |

## 4. Termius özellik envanteri (parite tabanı)

Aşağıdaki liste termius.com, fiyatlandırma karşılaştırma tablosu ve
docs.termius.com üzerinden 2026-09-09 tarihinde çıkarıldı. Ürünümüzün **en az**
bu seviyeyi yakalaması gerekiyor.

### 4.1 Protokoller ve bağlantı
- SSH, SFTP, Telnet, Mosh, Serial, Local terminal
- Port yönlendirme: Local (-L), Remote (-R), Dynamic/SOCKS (-D)
- Agent forwarding. Termius kendi dahili SSH agent'ını kullanıyor, işletim sisteminkini değil
- Jump host / host zinciri
- SOCKS ve HTTP proxy
- Otomatik yeniden bağlanma, keepalive

### 4.2 Kimlik doğrulama ve anahtar yönetimi
- Keychain: kullanıcı adı, parola, SSH anahtarı, sertifika, Identity nesneleri
- SSH sertifikaları (SSH CA)
- FIDO2 donanım anahtarı
- Biyometrik cihaza bağlı anahtarlar: Windows Hello, Touch ID, Android biometrics
- SSH ID: cihaza bağlı, biyometrik korumalı passkey tabanlı SSH. Özel anahtar hiç paylaşılmıyor
- Anahtar üretimi ve passphrase koruması
- Sunucuya otomatik public key push
- Post-quantum anahtar değişimi: `mlkem768x25519-sha256`, `mlkem512/768/1024-sha256`
- ML-DSA anahtar üretimi ve kimlik doğrulama
- İki faktörlü doğrulama, PIN kilidi

### 4.3 Veri modeli
- **Vault**: en üst seviye kapsayıcı, uçtan uca şifreli. Personal / Team / çoklu vault
- **Group**: iç içe geçebilen host koleksiyonu. Protokol, kimlik, jump host, port ve ortam değişkeni ayarlarını içindeki tüm hostlara miras bırakıyor
- **Host**: IP veya hostname, her biri kendi portu ve kimliğiyle çoklu protokol, etiket, jump host ve proxy ayarı
- **Tag**: arama ve gezinme için
- **Known hosts** vault içinde senkron

### 4.4 Terminal ve üretkenlik
- Workspace: iki veya daha fazla oturumu tek ortamda gruplayan yapı
- Focus mode, tek terminal büyük, diğerleri yan panelde
- Split view, aynı anda 16 terminale kadar
- Command broadcast, tüm aktif terminallere aynı girdi
- Workspace şablonları. Bağlantı, sekme adı, düzen, çalışma dizini ve çalışan komut geri yükleniyor
- Oturum durum göstergeleri: yeşil okunmamış çıktı, sarı girdi bekliyor, kırmızı hata
- Snippet: etiketli shell scripti. Yan panelden, autocomplete'ten veya çoklu hostta çalıştırma
- Startup snippet, her bağlantıda otomatik çalışır
- Snippet çoklu host çalıştırma, seçilen hostlarla otomatik workspace açıyor
- **Snippet'te değişken desteği yok.** Termius'un açığı, biz kapatacağız
- Ortam değişkenleri Host ve Group seviyesinde, OpenSSH `SendEnv` mekanizmasıyla
- Shell integration yerelde sadece bash ve zsh, uzakta OpenSSH 7.2+ ve bash
- Autocomplete kaynakları: snippet, shell geçmişi, dosya yolu, dahili komut ve argümanları, sudo parolası
- Autocomplete PowerShell ve CMD'de çalışmıyor
- AI komut üretimi. Aşağı ok tuşu, doğal dil girilince komut üretiyor, hesap gerektiriyor
- Terminal yan paneli: tema, geçmiş, snippet
- Terminal emülasyon tipi seçimi: xterm, Linux, VT100
- ANSI OSC 52 kopyala-yapıştır
- Grup seviyesi konfigürasyon mirası

### 4.5 SFTP
- Çift panel arayüz, aynı anda iki SFTP paneli
- Paneller arası ve yerel dosya sisteminden sürükle-bırak
- Uzak dosya düzenleme: geçici klasöre indir, değişikliği izle, geri yüklemeyi teklif et
- Transfers bölümü, durum ve ilerleme gösterimi
- Transfer kuyruğu ve kesintiden devam etme dokümante değil, muhtemelen yok
- Uzak dosya izni düzenleme dokümante değil

### 4.6 İşbirliği ve senkronizasyon
- Cihazlar arası senkron: Windows, Linux, macOS, iOS, iPadOS, Android
- Team vault, çoklu vault, granüler erişim (Can edit / Can view)
- Terminal multiplayer: gerçek zamanlı oturum paylaşımı, tek kişi yazar, kontrol devri
- Oturum logları: her host için otomatik kayıt, senkron, ekip içinde paylaşım
- Log arama, bookmark, yorum, saklama politikası

### 4.7 Entegrasyon ve içe aktarma
- AWS, DigitalOcean, Azure envanterini otomatik içe aktarma
- Ansible, Vanta, API bridge
- CSV içe aktarma, kullanıcı adı ve parola dahil
- Başka SSH istemcilerinden içe aktarma

### 4.8 Güvenlik mimarisi
- SRP6a tabanlı kimlik doğrulama. Parola veya hash ağa gitmiyor
- Argon2id, libsodium `crypto_pwhash`, OPSLIMIT/MEMLIMIT_INTERACTIVE, ARGON2ID13
- `crypto_box`: X25519 + XSalsa20 + Poly1305
- `crypto_secretbox`: XSalsa20 + Poly1305
- SRP implementasyonu Botan 3.2.0, taşıma gRPC over TLS
- Personal vault: rastgele anahtar çifti, özel anahtar kullanıcının şifreleme parolasıyla şifreleniyor
- Team vault: ayrı vault anahtarı, her üyenin public key'iyle sarmalanıyor, sahibin özel anahtarıyla MAC üretiliyor
- SOC 2 Type II, SAML SSO eklentisi, onaylanmış alan adları

### 4.9 Ticari model

| Plan | Fiyat | İçerik |
|---|---|---|
| Starter | Ücretsiz | Yerel vault, SSH ve SFTP, autocomplete, port forwarding |
| Pro | 10 USD/ay | Bulut vault, senkron, oturum logları, snippet otomasyonu |
| Team | 20 USD/koltuk/ay | Team vault, gerçek zamanlı işbirliği, paylaşılan loglar |
| Business | 30 USD/koltuk/ay | Çoklu vault, granüler erişim, SAML SSO eklentisi |
| Enterprise | Özel | SOC2 raporu, SLA, öncelikli destek, geçiş desteği |

## 5. Termius'un açık bıraktığı alanlar

Farklılaşma listemiz. Her satırın ister belgesinde karşılığı var.

| # | Açık | Bizim yaklaşımımız |
|---|---|---|
| 1 | Electron, ağır ve yavaş | Native C++23, GPU render, hedef 400 ms soğuk açılış |
| 2 | Yerel terminal ikinci sınıf | Profil sistemi. PowerShell, CMD, WSL, Git Bash birinci sınıf |
| 3 | PowerShell ve CMD'de autocomplete yok | PSReadLine köprüsü ve kendi shell entegrasyonumuz |
| 4 | Snippet'te değişken yok | Şablon motoru, parametre sorma, host değişkenleri |
| 5 | Kubernetes desteği hiç yok | Kubeconfig, pod exec, log, port-forward, manifest env import |
| 6 | Çıktı tetikleyicileri yok | Regex tetikleyici: vurgula, bildir, komut çalıştır, işaretle |
| 7 | Semantik prompt işaretleri yok | OSC 133, prompt'lar arası atlama, komut bazlı çıktı seçimi |
| 8 | Uygulama kapanınca oturum gidiyor | Ayrı daemon süreci, GUI kapansa da oturumlar yaşar |
| 9 | Zorunlu hesap ve satıcıya bağlı bulut | Hesapsız tam işlevsellik, kendi barındırdığın senkron |
| 10 | AI sadece komut üretiyor | MCP sunucusu ve istemcisi, politika motoru, denetim izi |
| 11 | Yerelleştirme yok | Türkçe dahil çoklu dil |
| 12 | `~/.ssh/config` ile canlı senkron yok | İki yönlü canlı senkron |
| 13 | Docker/Podman host tipi yok | Container exec birinci sınıf oturum tipi |
| 14 | AWS SSM Session Manager yok | SSH portu açmadan bağlantı |
| 15 | Oturum kaydı tescilli format | asciicast v2, dışa aktarılabilir |

## 6. Kapsam dışı (ilk sürüm)

- Mobil uygulamalar. Veri modeli senkrona hazır tasarlanır, istemci yazılmaz.
- Ticari ekip yönetim konsolu, faturalandırma, SAML SSO.
- SOC 2 sertifikasyonu.
- Termius multiplayer benzeri gerçek zamanlı ortak oturum. M6'ya bırakıldı.

## 7. Başarı ölçütleri

| Ölçüt | Hedef | Milestone |
|---|---|---|
| Soğuk açılış | 400 ms veya altı | M1 |
| Yerel sekme açılışı | 100 ms veya altı | M0 |
| Boş RAM | 120 MB veya altı | M1 |
| 10 oturumla RAM | 300 MB veya altı | M3 |
| Çıktı işleme hızı | 100 MB/s veya üstü | M0 |
| Girdi gecikmesi p99 | 8 ms veya altı | M1 |
| Termius parite oranı | %85 veya üstü | M5 |
