# Tema ve SFTP mimari incelemesi

Tarih: 2026-10-09. Bu rapor, [08 numaralı düzeltme raporunu](08-GORUNUM-GECISLERI-VE-SECIM-DUZELTMELERI.md) tamamlar. Önceki testlerin geçmesi kullanıcının bildirdiği tüm akışların çalıştığını kanıtlamıyordu.

## Bulgular ve değişiklikler

### Terminal akışı, tema ve imleç

- SSH açılış yolu gelen baytları değiştiriyor, uygulamanın ürettiği bir karşılama ekranını ve renklendirilmiş MOTD metnini terminal modeline ekliyordu. ConPTY bu ek satırları bilmiyordu; yeniden boyutlandırmadaki gerçek ekran yeniden çizimiyle yerel metin/imleç durumu ayrışıyordu. Önceki değişiklik yalnız yeniden oynatmayı kaldırmıştı. Artık girişten itibaren tüm çıktı tek VT akışından, bir kez geçiyor. Bağlantı durumu gözlemcisi ekranı değiştirmiyor. `debug1:` ile başlayan kullanıcı çıktısı da silinmiyor; SSH komutundan `-v` kaldırıldı.
- Tema seçimi ve özel tema Apply işlemi hemen kaydediliyor. Özel temanın ANSI paleti daha önce seçilen şemadan miras alınmıyor. Ayarlarda terminal renklerinin canlı önizlemesi var.
- Blok imleç seçimi `0` gönderiyordu; bu değer aslında uygulamanın imleç stilini izleme anlamına geliyordu. Otomatik/Blok/Alt çizgi/Çubuk artık ayrı seçimler.
- DECSTR (`CSI ! p`) tüm ekranı siliyordu. Soft reset artık metni ve mevcut imleç koordinatını koruyor, ilgili modları ve çizim niteliklerini sıfırlıyor.
- Geçmiş satır sayısı ayarı boş bir fonksiyona gidiyordu. Gerçek tampon limiti ve azaltıldığında mutlak seçim indekslerinin korunması uygulandı.
- Önceki saydamlık, pencere boyutu olaylarını birleştirme ve çift tıklama düzeltmeleri korundu; yeniden doğrulandı. Hakkında ekranında derleme zamanı da gösteriliyor.

### SFTP çalışma modeli

- Kontrolcüden bağımsız yaşayan, ham `this`/dosya sistemi işaretçileri tutan detached işler kaldırıldı. Kontrolcü iş parçacığına sahip; iş yalnız yakalanmış bağlantı ve argümanları kullanıyor. Tamamlama UI iş parçacığında `Poll()` ile uygulanıyor. Kapatma iptal edip işi birleştiriyor; bağlantı nesli eski yanıtları geçersiz kılıyor.
- Klasör adresi, liste gelmeden değişiyordu; meşgulken yenileme isteği kaybolabiliyordu. Adres ve öğeler aynı tamamlama işleminde güncelleniyor. Son bekleyen gezinme çalıştırılıyor; hata halinde önceki adres ve liste korunuyor. Önbellek dönüşü de gerçek yolu güncelliyor.
- Kimlik envanteri bağlantı için kopyalanıyor; arka plandaki iş UI tarafından değiştirilen vektörleri okumuyor. Aktarım, önizleme, sürükle-bırak ve mutasyonlar aynı iş sahibini kullanıyor.
- Klasör silme yanlışlıkla dosya silme komutuna gidiyordu. Gerçek öğe tipi kullanılıyor. Sağ tık Aç klasörde gezinir; sağ tık seçimi imlecin altındaki görünür satırdan alınır, eski seçime uygulanmaz. Gezinme çift tıklamada tek kez tetiklenir. İşlem düğmelerindeki yanlış `enabled` argümanları düzeltildi; başarısız işlemler bildirilir.
- ANSI temizleyici CSI bitişinden sonraki normal harfleri de tüketebiliyordu; düzeltilerek uzun liste testi eklendi. Hata metnine benzeyen dosya adları bağlantı hatası sayılmıyor.
- SFTP `cd/lcd/put/get` komutları eskiden birlikte kuyruğa yazılıyordu. Başarısız `cd` sonrasında aktarım başka dizinde devam edebiliyordu. Artık her komutun sonucu ve terminaldeki yeni SFTP istemi bekleniyor; hatada kalan komutlar gönderilmiyor. ConPTY'nin sondaki boşluğu imleç hareketi olarak göndermesi dikkate alınıyor.
- İndirmede seçilen hedef dosya adı kullanılmıyordu; düzeltildi. Önceden var olan hedef dosya, başarısız aktarımı başarılı göstermiyor. Başka hedefe sessiz aktarım tekrarları kaldırıldı.

## Doğrulama

`ctest --test-dir build/ninja --output-on-failure`: **16/16 başarılı**, son tam koşu yaklaşık 10 saniye. Release/Ninja ve MSVC `build.bat` derleme yolları kontrol edildi.

| Akış | Kanıt |
|---|---|
| Tema seçimi | Ayarlar düğmelerine gerçek mouse-down/up mesajları; 8 hazır şema, %100/%150 DPI; diskte seçilen şema ve GPU zemin renkleri kontrolü |
| Pencere modları | Normal/büyütülmüş/Visor/tam yükseklik/normal; varsayılan ve açıkça boyanmış hücrelerde GPU alfa; imleç görünürlüğü |
| Gerçek terminal | ConPTY üzerinde yerel C++ test süreci ve Windows PowerShell; 140×45, 90×20, 160×50, 80×24; uygulamanın raporladığı boyut, renkli hücreler ve imleç koordinatı |
| SSH giriş akışı | Bölünmüş SGR, MOTD, yeniden çizim ve debug benzeri kullanıcı metni; referans VT ekranıyla hücre/stil/imleç eşitliği |
| SFTP gezinmesi | Geciktirilmiş yanıt, bekleyen son yol, başarısız geçiş, bağlantı kesme, sekme yok edilirken iptal/birleştirme, yerel çift tıklama ve sağ tık satır tespiti |
| Gerçek SFTP | Windows OpenSSH istemcisi + Git'in yerel `sftp-server` süreci (`-D`); 140 dosya, boşluk/köşeli parantez içeren adlar, cd, get/put, rename/delete; başarısız cd ardından aktarımın durması |
| Diğer | MCP/HTTP, kayıt/oynatma, daemon ayır/bağlan, bölünmüş paneller, ajan ve dil testleri |

Testler kullanıcı dosyalarını/hostlarını kullanmaz; gerçek dış SSH sunucusuna bağlanmaz. Pano çağrısı testte yakalanır, kullanıcının panosu değiştirilmez. Gerçek SFTP alt testi Git sunucusu bulunmayan makinelerde açık bir SKIP mesajı verir; bu makinede çalıştı.

## Kalan sınırlar

- SFTP hâlâ `sftp.exe` terminal çıktısından veri çıkarıyor; yapılandırılmış bir SFTP API'si değil. CLI'ye özgü dosya adı/yerelleştirme/symlink hedef tipi sınırlamaları var. Windows OpenSSH'nin güvenilir biçimde adresleyemediği literal `*`/`?` dosya adları çoklu dosya işlemine dönüşmemesi için reddediliyor.
- Dış sunucuda parola, MFA, jump-host, bağlantı kopması ve büyük dosya aktarımı bu yerel protokol testiyle doğrulanmış sayılmaz.
- Daemon yeniden bağlanmada sınırlı ham bayt geçmişi oynatıyor; tam terminal ekranı + ayrıştırıcı durumunun anlık görüntüsünü tutmuyor. Uzun çıktı sonrası yeniden bağlanma, canlı pencere modunu değiştirme testinden ayrı bir mimari çalışma gerektiriyor.
- Tüm monitör/DPI düzenleri ve üçüncü taraf TUI programları test edilmedi. Bu rapor projedeki olası bütün hataların bittiği anlamına gelmez.

## 2026-10-09 SSH açılış düzeltmesi ve sabit exe adı

Kullanıcının yeniden bildirdiği SSH sorununda somut bir eksik bulundu: `ProcessSshOutput` ekranın yalnız son 24 satırını tarıyordu. 40/80 satırlı normal veya büyütülmüş pencerede parola, host-key ve anahtar parolası istemleri üstte kaldığından görünür terminal yerine bağlantı bekleme ekranı gösteriliyordu. Yeni test düzeltmeden önce başarısız oldu; tüm ekran satırları tarandıktan sonra geçti. 24/40/80 satırda parola, host-key, key-passphrase, shell istemi ve bağlantı reddi kapsandı.

Yeniden bağlanmada eski çıktı kuyruğu temizleniyor, kayıtlı hedef parolası yeniden etkinleştiriliyor ve başlatma hatası kullanıcıya gösteriliyor. Gerçek ConPTY test süreciyle 120×80 ekranda ilk giriş ve yeniden giriş doğrulandı; Windows PowerShell testi de 80 satırda başlatıldı. Tam test paketi 16/16 başarılı. Bu doğrulama dış SSH sunucusunda kimlik doğrulama testi değildir.

Kullanıcı tercihi: uygulamanın adı daima `FullTerminal.exe`. MSVC ve CMake yayınlama adımı `tools/Publish-Binary.ps1` kullanır; etkin eski dosya gerekirse `build/binary-backups/<id>/FullTerminal.exe` konumuna taşınır ve kökte aynı adla yeni build yerleştirilir. Kökteki eski alternatif isimli kopyalar kaldırıldı (geri alınabilir build arşivine taşındı). Açık süreçler zorla sonlandırılmaz; yeni kod uygulamadan çıkıp tekrar açıldığında yüklenir.

## 2026-10-09: Erken kapanan loading, renksiz SSH metni ve Hub adları

Önceki SSH düzeltmesinin eksiği: bütün satırları taramak parola sorusunu görünür yaptı ama **her görünür metni Connected sayıyordu**. Bu yüzden parola sorusunda veya MOTD'nin ilk parçasında loading kapanıyordu. Connecting, AwaitingInput ve Connected ayrıldı. Parola/host-key/passphrase/MFA soruları yanıtlanabilir biçimde görünür; Enter sonrası loading geri gelir. Standart kabuk istemi, OSC 133 işareti veya alternatif ekran görülünce hazır durumuna geçilir. Renksiz MOTD ve parça parça gelen `System information:` başlığı bu geçişi tetiklemez. Tanınmayan özel istemlerde Terminali göster düğmesi vardır; hazır durumu uydurulmaz. Bu, protokol seviyesinde bir SSH handshake takipçisi değildir; hazır olma tespiti çözümlenmiş terminal ekranına dayanır.

Ekran görüntüsündeki gibi renksiz çıktı yalnız ANSI paleti değiştirilerek çok renkli hale gelemez. Render katmanına, varsayılan renkli metinde istem/kullanıcı-host/dizin, URL, sayısal değer ve belli karşılama başlıkları için seçili paletten vurgu eklendi. Ayarlar > Görünüm üzerinden kapatılır ve kaydedilir. VT hücreleri, ekran geometrisi, imleç, kopyalanan metin ve kayıt baytları değiştirilmez. Renkli/stilli hücreler korunur; alternatif ekranda vurgulama kapalıdır. Sunucuya komut gönderilmez, `.bashrc`/`.profile` düzenlenmez. Bu işlem kabuk sözdizimi renklendirmesi değildir.

Sekmeler `ssh.exe` yolu veya OSC başlığı yerine Hub/profil adıyla başlar; aynı adın tekrarına boşluk + 1, 2, ... eklenir. Gizli bölünmüş paneller de ad çakışması hesabına dahildir. Açık sekmeler kapatma/sıralama/odak değiştirme nedeniyle yeniden numaralanmaz. Elle verilen adlar ile otomatik adlar ayrı saklanır; sıfırlama otomatik ada döner. `session.json` otomatik adı da taşır; OSC başlığı ajan tespiti için ekran modelinde kalır.

Doğrulama: CMake/Ninja Release derlendi; **17/17 CTest geçti (14.19 sn, SKIP yok)**. Yeni yerel protokol testi gerçek Windows `ssh.exe` + ConPTY/daemon yolunu geçici Paramiko sunucusuna bağlar. Elle yanlış parola ardından doğru parola, kayıtlı parola, yeniden bağlanma, 120×80 giriş, geciktirilmiş MOTD, ANSI yeşil hücre ve 140×40/120×80 uzak PTY ölçüleri doğrulandı. Fixture sadece loopback'te çalışır; kullanıcı SSH config/known_hosts/kimlikleri kullanılmaz. Paramiko yalnız build altında test bağımlılığıdır; uygulama C++20/statik CRT olarak kalır.

GPU testinde iki ayrı oturumun renksiz istemi dört temada çizildi; vurgulama kapatıldığında varsayılan metin rengine dönüldüğü ve hücrelerin değişmediği kontrol edildi. Alternatif ekranın renksiz kaldığı, Hub adları/ekleri/elle değiştirme/sıralama/split adlarının saklanması da doğrulandı. Ekran görüntüsü: `build/ninja/test-runs/window_presentation/ssh-theme-and-names.png` (sentetik çıktı, uygulamanın gerçek renderer'ı). Harici kullanıcı sunucularında MFA/jump-host ve özel prompt kombinasyonları bu testle doğrulanmış sayılmaz.
