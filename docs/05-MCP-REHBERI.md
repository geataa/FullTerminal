# FullTerminal MCP Sunucusu ve Entegrasyon Rehberi

FullTerminal, altyapınızdaki sistemleri (yerel kabuklar, WSL2 dağıtımları, Docker konteynerleri, Kubernetes kaynakları ve SSH sunucuları) tek merkezde toplayan ve AI ajanlarına (**Claude Desktop**, **Claude Code**, **Cursor**, **Antigravity**) açan yerel, **düşük token tüketen** bir Model Context Protocol (MCP) sunucusuna sahiptir.

---

## 1. Hızlı Başlangıç & İstemci Yapılandırması

FullTerminal MCP sunucusu `stdio` (standart girdi/çıktı) üzerinden satır başına bir JSON-RPC 2.0 mesajıyla haberleşir. Harici Python, Node.js veya ek çalışma zamanı gerektirmez; `FullTerminal.exe --mcp` (eş anlamlısı: `--mcp-stdio`) komutuyla pencere açmadan çalışır.

> **Önce etkinleştirin.** Sunucu varsayılan olarak **kapalıdır**. FullTerminal'de **Ayarlar > MCP** sayfasında
> "MCP sunucusunu etkinleştir" ve "stdio" kutularını işaretleyin. Kapalıyken sunucu `initialize`'a yine yanıt verir,
> ancak araç listesi boş döner ve her araç çağrısı `isError: true` ile
> "MCP sunucusu ayarlarda kapali (Ayarlar > MCP)" hatası verir.

### Claude Desktop / Cursor / Claude Code Yapılandırması (`mcpServers`)

Aşağıdaki bloğu istemcinizin `mcp.json` veya `claude_desktop_config.json` dosyasına ekleyin (Ayarlar > MCP sayfasındaki "Panoya kopyala" düğmesi aynı bloğu exe'nin gerçek yoluyla üretir):

```json
{
  "mcpServers": {
    "fullterminal": {
      "command": "C:\\Tools\\FullTerminal\\FullTerminal.exe",
      "args": ["--mcp"]
    }
  }
}
```

Sunucu; ayarları, host kasasını, Kubernetes YAML'larını ve döküm dosyalarını her zaman **exe'nin yanındaki** `portable_data` klasöründen okur. İstemcinin hangi çalışma dizininde başlattığı önemli değildir; GUI ile aynı envanter kullanılır.

### Streamable HTTP (2026-10-08)

Ayarlar > MCP içinde sunucuyu ve **Streamable HTTP** seçeneğini açın, **Üret** ile erişim token'ı oluşturun. Varsayılan adres `http://127.0.0.1:8787/mcp`; port değiştirilebilir. **HTTP yapılandırmasını kopyala** düğmesi mevcut adres ve token'ı içeren istemci yapılandırmasını üretir. Token en az 32 görünür ASCII karakteri olmalıdır; ayar dosyasına Windows kullanıcısına bağlı DPAPI korumasıyla yazılır.

```json
{
  "mcpServers": {
    "fullterminal": {
      "url": "http://127.0.0.1:8787/mcp",
      "headers": { "Authorization": "Bearer AYARLARDAN_KOPYALANAN_TOKEN" }
    }
  }
}
```

Bu bir yapılandırma örneğidir; gerçek token için uygulamadaki kopyalama düğmesini kullanın. GUI, HTTP sürecini otomatik başlatır ve tamamen kapanırken durdurur. Headless kullanım: `FullTerminal.exe --mcp-http` (önce ayarlarda etkinleştirilmelidir). Dolu port/başlatma hatası GUI'de bildirilir; headless süreç hata koduyla çıkar.

Sunucu yalnızca IPv4 loopback'e bağlanır. Bearer token, Host ve varsa Origin doğrulanır. POST istekleri JSON yanıtı alır; bildirimler HTTP 202 alır. GET/SSE aboneliği ve DELETE oturum yönetimi yoktur (405); sunucu stateless çalışır. Gövde sınırı 1 MiB, HTTP header sınırı 16 KiB'dir; batch HTTP istekleri kabul edilmez. İstekler sırayla işlenir. Taşıma, [MCP Streamable HTTP 2025-06-18'in JSON yanıt seçeneğini](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports) uygular.

FullTerminal burada **MCP sunucusudur**. Harici MCP sunucularına bağlanan bir MCP istemcisi henüz yoktur.

---

## 2. Güvenlik Ayarları (Ayarlar > MCP)

Ayarlar **her istekte yeniden okunur**: GUI'de bir kutuyu değiştirdiğinizde sunucuyu yeniden başlatmak gerekmez. Araç listesi de ayarlara göre süzülür; istemcinin güncel listeyi görmesi için bağlantıyı yenileyin (Claude Code'da `/mcp`).

| Ayar | Varsayılan | Etkisi |
| :--- | :--- | :--- |
| MCP sunucusunu etkinleştir | Kapalı | Kapalıyken araç listesi boş, her çağrı hata. |
| stdio | Açık | Kapalıyken sunucu etkinleştirilmemiş gibi davranır. |
| Salt okunur mod | **Açık** | `ft_exec`, `ft_fs write`, `ft_vault add_host/remove_host` kapalı ve listeden gizli. Keyfi bir komutun salt okunur olduğu kanıtlanamayacağı için `ft_exec` her zaman yazma sayılır. |
| Her yazma işlemi için insan onayı | **Açık** | `ft_exec`, `ft_fs write` ve kasa değişikliklerinden önce Windows onay kutusu açılır (araç, sistem, komut/yol gösterilir; varsayılan düğme **Hayır**). "Hayır" çağrıyı `isError` ile reddeder. Onay beklenirken sunucu diğer istekleri bekletir. |
| Denetim izi tut | **Açık** | Her `tools/call` için `portable_data\mcp-audit.log` dosyasına bir satır yazılır (ayrıntı aşağıda). |
| ft_exec | Kapalı | `ft_exec` için gerekli (salt okunur mod da kapalı olmalı). |
| ft_fs | Kapalı | `ft_fs` için gerekli; `write` için ayrıca salt okunur mod kapalı olmalı. |
| Kubernetes hedefleri | Kapalı | `k8s:` hedeflerine `ft_exec`/`ft_fs` ile erişim için gerekli. |

`ft_systems` ve `ft_vault list` sunucu etkinken kullanılabilir; kasadaki parolalar bu araçlarla döndürülmez. Panel bölme, panele girdi gönderme, swarm başlatma ve kayıt başlatma/durdurma da yazma izni ve insan onayı politikasına tabidir. Kayıt durumu salt okunur olarak sorgulanabilir. GUI köprüsü ayrıca güncel etkinlik/salt okunur/çalıştırma izinlerini kontrol eder.

### Denetim izi biçimi

Sekmeyle ayrılmış tek satır:

```
2026-09-23T10:15:02Z	ft_exec	system=wsl:Ubuntu-24.04	allowed	exit=0	cmd=df -h
2026-09-23T10:15:40Z	ft_fs:write	system=local	denied	exit=-	path=C:\proj\a.txt bytes=120
```

Alanlar: UTC zaman, araç (ve eylem), sistem, `allowed`/`denied`, çıkış kodu (çalıştırılmadıysa `-`), komut veya yol. Dosya içeriği ayrıca loglanmaz; komut metninin kendisi hassas değer içerebilir. Her alan 500 bayta kısaltılır ve satır sonu/sekme boşluğa çevrilir. Dosya 5 MB'yi aşınca `mcp-audit.log.1` olarak saklanır.

---

## 3. Araç kapsamı ve çıktı bütçesi

İzinler açıkken 11 araç sunulur. Dört altyapı aracına beş panel aracı, swarm ve kayıt kontrolü eklenmiştir. Büyük çıktıların önizlemesi döner, tamamı yerel döküm dosyasına yazılır (bölüm 6). Şema/çıktı token tüketimi istemcinin tokenizer'ına ve isteğe bağlıdır; sabit bir token ya da gecikme garantisi yoktur.

---

## 4. Sistem Kimlikleri (`system`)

`ft_exec` ve `ft_fs` hedefi `system` parametresiyle alır. Geçerli değerler `ft_systems` çıktısındaki **ID** (veya **PATH**) sütunudur. Bilinmeyen bir kimlik hata verir; artık sessizce `ssh <kimlik>` olarak denenmez.

| Kimlik | Nerede, nasıl çalışır |
| :--- | :--- |
| `local` (varsayılan), `powershell` | Windows PowerShell 5.1 (`-NoProfile -NonInteractive -Command`), çıktı UTF-8 |
| `pwsh` | PowerShell 7 (kuruluysa) |
| `cmd` | `cmd.exe /d /s /c` |
| `gitbash`, `msys2` | Profilin `bash.exe -lc` (istemcinin çalışma dizininde) |
| `nu` | `nu.exe -c` |
| `wsl:<dağıtım>` | `wsl.exe -d <dağıtım> --exec sh -c` (varsayılan kabuk komutu ikinci kez yorumlamaz) |
| `docker:<container>` | `docker.exe exec -i <container> sh -c` |
| `k8s:<namespace>/<ad>` (Pod), `k8s:<namespace>/<tür>/<ad>` | `kubectl.exe exec -i -n <ns> <ad> -- sh -c`. İş yüklerinin kimliği `ft_systems`'teki gibi türü içerir (`deploy`, `sts`, `ds`, `rs`, `job`, ör. `k8s:payments/deploy/api`); tür verilmezse YAML'dan bulunur (aynı adda Pod öncelikli). Kubernetes ayarlarında otomatik ortam açıksa uygulamaya eklenen kubeconfig'ler `KUBECONFIG` olarak verilir. |
| SSH host ID'si, etiketi, adresi veya `/ssh/...` yolu | Kasadaki host ayarlarıyla (port, kullanıcı, anahtar, atlama sunucusu) `ssh.exe -o BatchMode=yes -o ConnectTimeout=10 ... --`. BatchMode nedeniyle parola sorulamaz: anahtar veya ssh-agent gerekir. |
| `ssh:kullanici@host` veya `kullanici@host` | Kasada olmayan bir host'a doğrudan ssh |

`-` ile başlayan ya da boşluk/tırnak içeren dağıtım, container, pod ve host adları reddedilir (ssh/docker/kubectl'e seçenek olarak sızmasın diye). Komutlar her katmanda doğru tırnaklanır; `"C:\Program Files"` gibi tırnaklı yollar, sondaki ters bölü ve `$1` gibi kabuk ifadeleri bozulmadan hedefe ulaşır.

---

## 5. Araç Referansı (Tools)

Başarısız her çağrı (`çıkış kodu ≠ 0`, bulunamayan dosya, eksik parametre, reddedilen izin) `isError: true` ve açıklayıcı bir metinle döner.

### 1. `ft_systems`
Sistemdeki çalışma ortamlarını (yerel kabuklar, WSL2 dağıtımları, Docker konteynerleri, Kubernetes kaynakları ve SSH sunucuları) kompakt bir tabloda listeler.
- **Parametreler:**
  - `filter` *(opsiyonel)*: ID, ad veya yolda aranan metin (ör. `docker`, `ubuntu`, `prod`).
  - `type` *(opsiyonel)*: `all`, `local`, `wsl`, `docker`, `ssh`, `k8s`.

### 2. `ft_exec`
Belirtilen hedef sistemde komut veya script çalıştırır. **ft_exec** izni ve salt okunur modun kapalı olması gerekir.
- **Parametreler:**
  - `system` *(varsayılan: `local`)*: Bkz. bölüm 4.
  - `command` *(zorunlu)*: Çalıştırılacak komut (yerelde PowerShell, WSL/Docker/K8s'te `sh`, SSH'ta uzak giriş kabuğu).
  - `timeout_ms` *(varsayılan: `15000`, en çok `600000`)*: Süre dolunca **tüm süreç ağacı** (Job nesnesi) sonlandırılır, çıktıya `[timeout after N ms - process tree killed]` eklenir ve çıkış kodu `124` olur. Uzun süren bir komut sunucuyu artık kilitlemez.
  - `max_lines` *(varsayılan: `50`)*: Context Budget Guard devreye girmeden önceki azami satır sınırı.
- Çıktı en fazla 5 MB okunur. Yerel Windows araçlarının OEM kod sayfasındaki (tr-TR: 857) çıktısı UTF-8'e çevrilir; protokol satırı her zaman geçerli UTF-8'dir.

### 3. `ft_fs`
SFTP kurulu olmayan ortamlarda bile çalışan sanal dosya sistemi köprüsü. **ft_fs** izni gerekir.
- **Parametreler:**
  - `system` *(varsayılan: `local`)*: Hedef sistem kimliği.
  - `action` *(zorunlu)*: `list` (dizin listesi), `read` (metin dosyası oku), `write` (dosya yaz; salt okunur modda kapalı), `info` (dosya bilgisi).
  - `path` *(zorunlu)*: Dosya veya dizin yolu.
  - `content` *(opsiyonel)*: `write` işlemi için içerik.
  - `offset_lines` *(opsiyonel)*: `read` için başlangıç satırı (parçalı okuma).
  - `limit_lines` *(varsayılan: `50`)*: Okunacak/listelenecek azami satır sayısı.
- Yerel hedeflerde (PowerShell, cmd, Git Bash, MSYS2, Nushell) işlemler doğrudan Win32 ile yapılır. `read`; BOM'lu UTF-8, UTF-16 (PowerShell `Out-File`) ve ANSI (1254) dosyaları UTF-8'e çevirir, ikili dosyaları reddeder.
- Uzak hedeflerde (WSL, Docker, K8s, SSH) `sh`, `ls`, `tail`, `head`, `stat`, `cat` kullanılır (BusyBox/Alpine ile uyumlu). Yollar tek tırnakla kaçışlanır; `'` içeren adlar da güvenle çalışır. `write` içeriği komut satırına gömülmez, çocuğun stdin'inden akar: boyut sınırı yoktur ve ikili içerik bozulmaz.

### 4. `ft_vault`
Bağlantı kasasındaki SSH hostlarını ve kimlikleri listeler, ekler veya siler. Parolalar döndürülmez.
- **Parametreler:**
  - `action`: `list`, `add_host`, `remove_host` (ekleme/silme salt okunur modda kapalı ve onay ister).
  - `label`, `address`, `port`, `username`, `id`.
- Not: Değişiklik doğrudan `portable_data\hosts.ini` dosyasına yazılır. FullTerminal GUI o sırada açıksa bellekteki listeyi kaydederken bu değişikliğin üzerine yazabilir; kasa değişikliklerini GUI kapalıyken yapmak en güvenlisidir.

### 5–11. Görsel terminal ve kayıt araçları

| Araç | İşlev |
| --- | --- |
| `ft_pane_list` | Açık sekmeler/paneller, geometri, odak ve ajan durumları |
| `ft_pane_split` | Hedef sekmede yeni panel; profil/sistem ve başlangıç komutu |
| `ft_pane_prompt` | Hedef panele komut veya tuş girdisi |
| `ft_pane_read` | Panel çıktısı ve ajan durumu |
| `ft_pane_wait` | Hedef panel durumunu zaman aşımıyla bekleme |
| `ft_swarm_spawn` | Hazır çok panelli çalışma alanı oluşturma |
| `ft_session_record` | Panel kaydını başlatma/durdurma/durum; `action: start/stop/status` |

Güncel parametreler `tools/list` şemasında bulunur. Bu araçlar aynı exe yolundan çalışan GUI'ye IPC ile bağlanır; gerekirse GUI başlatılır. Köprü yanıtı 4 MiB ile sınırlıdır; yanıt vermeyen GUI için 10 saniyelik mesaj zaman aşımı uygulanır. `ft_terminal` eski uyumluluk yönlendirmesidir; yeni istemciler listelenen panel araçlarını kullanmalıdır.

---

## 6. Context Budget Guard Çalışma Mantığı

Bir komut veya dosya okuma çıktısı satır sınırını (`max_lines`/`limit_lines`, varsayılan **50**) veya karakter bütçesini (`ft_exec` için **2.500**, `ft_fs read` için 3.000, `list` için 4.000) aştığında:
1. Çıktının tamamı exe'nin yanındaki `portable_data\scratch\dump_<zaman>_<kod>.txt` dosyasına kaydedilir. Dosya yazılamazsa model buna göre bilgilendirilir.
2. AI modeline yalnızca en fazla **35 satırlık** ve karakter bütçesini aşmayan bir önizleme döner; tek satırlık dev bir dosya (ör. minify edilmiş JS) da bütçeyi aşamaz, uzun satır kesilip `[line truncated, N bytes total]` notu eklenir.
3. Model token kotasını tüketmez; dilerse `ft_fs` üzerinden `offset_lines` ile ilgilendiği satırları okuyabilir.

---

## 7. Protokol Notları

- stdio protokol sürümü: `2024-11-05`; satır başına JSON, batch destekli. HTTP initialize yanıtı: `2025-06-18`; HTTP sürüm başlığında `2025-03-26` veya `2025-06-18` kabul edilir, HTTP batch desteklenmez.
- `id` taşımayan her mesaj bildirimdir (`notifications/initialized`, `notifications/cancelled` ...) ve yanıtlanmaz.
- Ayrıştırılamayan satır `-32700 Parse error` (id: null), geçersiz istek `-32600`, bilinmeyen metot `-32601`, geçersiz `tools/call` parametreleri `-32602` ile yanıtlanır. Yarım kalmış bir satır asla kısmen çalıştırılmaz.
- Sayı taşmaları, derin iç içe diziler (256 seviye sınırı) veya bir araçta oluşan beklenmedik bir hata oturumu sonlandırmaz; `-32603 Internal error` döner.
