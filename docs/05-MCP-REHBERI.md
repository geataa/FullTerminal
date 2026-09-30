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
      "command": "E:\\0_SkySoft\\FullTerminal\\FullTerminal.exe",
      "args": ["--mcp"]
    }
  }
}
```

Sunucu; ayarları, host kasasını, Kubernetes YAML'larını ve döküm dosyalarını her zaman **exe'nin yanındaki** `portable_data` klasöründen okur. İstemcinin hangi çalışma dizininde başlattığı önemli değildir; GUI ile aynı envanter kullanılır.

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

`ft_systems` ve `ft_vault list` sunucu etkinken her zaman kullanılabilir; kasadaki parolalar hiçbir araçla döndürülmez. HTTP taşıması (Streamable HTTP) henüz uygulanmadı; ilgili ayarlar şimdilik kullanılmıyor.

### Denetim izi biçimi

Sekmeyle ayrılmış tek satır:

```
2026-09-23T10:15:02Z	ft_exec	system=wsl:Ubuntu-24.04	allowed	exit=0	cmd=df -h
2026-09-23T10:15:40Z	ft_fs:write	system=local	denied	exit=-	path=C:\proj\a.txt bytes=120
```

Alanlar: UTC zaman, araç (ve eylem), sistem, `allowed`/`denied`, çıkış kodu (çalıştırılmadıysa `-`), komut veya yol. Dosya içeriği ve sırlar asla yazılmaz; her alan 500 bayta kısaltılır ve alanlardaki satır sonu/sekme boşluğa çevrilir. Dosya 5 MB'yi aşınca `mcp-audit.log.1` olarak saklanır.

---

## 3. Neden FullTerminal MCP? (Token Ekonomisi & SkyMemory Koruması)

| Karşılaştırma | XPipe Resmî MCP Sunucusu | FullTerminal Native MCP |
| :--- | :--- | :--- |
| **Araç Sayısı (Şema Yükü)** | 15 ayrı araç (her prompt'ta ~2500 token şema yükü) | **4 kompakt ortogonal araç** |
| **Bağlam Bütçesi Koruması** | Yok (büyük dosya/log okunduğunda context çöker) | **Context Budget Guard** (büyük çıktı diske yazılır, özet döner) |
| **Çıktı Biçimi** | Şişkin çok satırlı JSON nesneleri | **Kompakt TSV / Tablo çıktısı** |
| **Performans & Boyut** | Ağır Java sanal makinesi (~250 MB RAM) | **Native C++20, tek exe** |

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

> `ft_terminal` (GUI'de sekme açma) henüz uygulanmadı ve araç listesinde yer almaz; çağrılırsa `isError` döner. Komut çalıştırmak için `ft_exec` kullanın.

---

## 6. Context Budget Guard Çalışma Mantığı

Bir komut veya dosya okuma çıktısı satır sınırını (`max_lines`/`limit_lines`, varsayılan **50**) veya karakter bütçesini (`ft_exec` için **2.500**, `ft_fs read` için 3.000, `list` için 4.000) aştığında:
1. Çıktının tamamı exe'nin yanındaki `portable_data\scratch\dump_<zaman>_<kod>.txt` dosyasına kaydedilir. Dosya yazılamazsa model buna göre bilgilendirilir.
2. AI modeline yalnızca en fazla **35 satırlık** ve karakter bütçesini aşmayan bir önizleme döner; tek satırlık dev bir dosya (ör. minify edilmiş JS) da bütçeyi aşamaz, uzun satır kesilip `[line truncated, N bytes total]` notu eklenir.
3. Model token kotasını tüketmez; dilerse `ft_fs` üzerinden `offset_lines` ile ilgilendiği satırları okuyabilir.

---

## 7. Protokol Notları

- Protokol sürümü: `2024-11-05`. Mesajlar satır başına bir JSON nesnesidir; toplu (batch) diziler de desteklenir.
- `id` taşımayan her mesaj bildirimdir (`notifications/initialized`, `notifications/cancelled` ...) ve yanıtlanmaz.
- Ayrıştırılamayan satır `-32700 Parse error` (id: null), geçersiz istek `-32600`, bilinmeyen metot `-32601`, geçersiz `tools/call` parametreleri `-32602` ile yanıtlanır. Yarım kalmış bir satır asla kısmen çalıştırılmaz.
- Sayı taşmaları, derin iç içe diziler (256 seviye sınırı) veya bir araçta oluşan beklenmedik bir hata oturumu sonlandırmaz; `-32603 Internal error` döner.
