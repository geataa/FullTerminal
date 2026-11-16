# Görünüm geçişleri, saydamlık ve çift tıklama

Tarih: 2026-10-08. Önceki ürün incelemesi: [Mevcut durum](06-MEVCUT-DURUM-VE-BUGFIX-HARITASI.md).

## Kök nedenler ve düzeltmeler

- SSH boyutu değişirken eski karşılama ekranı tekrar oynatılıyor, yeniden çizim olarak varsayılan canlı çıktı parçaları atlanıyordu. Bu yol kaldırıldı. Boyutlandırma ve tema uygulama artık VT ayrıştırıcısını, devam eden kaçış dizisini, kalem rengini veya uygulamanın imleç görünürlüğünü sıfırlamıyor.
- Pencere stili/yerleşimi değişirken art arda gelen geçici `WM_SIZE` ölçüleri PTY'ye hemen iletiliyordu. PTY ölçüsü Tick içinde güncellenerek aynı döngüdeki geçici ölçüler birleştiriliyor.
- Uygulamaların açıkça boyadığı hücre arka planları tamamen opaktı. Terminal arka planları Direct2D COPY harmanlama ile seçilen alfa değerinde çiziliyor; üst üste alfa birikmesi önleniyor. Metin, seçim ve imleç opak kalıyor. Normal, büyütülmüş ve Visor modları aynı ayarı kullanıyor.
- DWM bileşim/tema değişiminde ve yeniden boyutlandırmada saydamlık yapılandırması yenileniyor. Microsoft referansı: [DwmExtendFrameIntoClientArea](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmextendframeintoclientarea).
- Yazı/fokus girişleri imlecin yanıp sönme süresini baştan başlatıyor. Sabit imleç stilleri yanıp sönmüyor, geniş karakterin ikinci hücresine denk gelen imleç başlangıç hücresine hizalanıyor. Yazmak, uygulamanın gizlediği imleci zorla açmıyor.
- Terminal metnine sol çift tıklama, boşluğa kadar olan bütünü seçip panoya kopyalıyor. Noktalama işaretleri bütüne dahil; yumuşak satır sarmaları birleştiriliyor, gerçek satır sonu sınır kabul ediliyor. Tek karakter, Unicode/geniş karakter ve scrollback destekleniyor. Boş alana çift tıklamak panoyu değiştirmiyor. Fareyi yakalayan terminal uygulamalarında Shift yerel seçimi etkinleştiriyor.

## Doğrulama

- CMake/Ninja Release ve `build.bat` ile MSVC derlemeleri tamamlandı.
- `ctest --test-dir build/ninja --output-on-failure`: **15/15 başarılı**. DPAPI testleri normal Windows kullanıcı profiliyle çalıştırıldı.
- `window_presentation`: gerçek Win32 pencere normal → büyütülmüş → Visor → tam yükseklik → normal geçişlerinden iki kez geçirildi. GPU dokusundan hem varsayılan hem özel hücre arka planlarının alfa değerleri okundu; normal modda %30, %50, %85 ve %100 opaklık kontrol edildi. İmleç ve palet durumu da denetlendi.
- Aynı test, pencere olayları üzerinden çift tıklama/seçim/kopyalama çağrısını doğruluyor. Kullanıcının gerçek panosunu değiştirmemek için pano yazma işlevi testte yakalanıyor.
- `conpty_daemon`: ayrı daemon ve gerçek ConPTY alt süreci 140×45, 90×20, 160×50 ve 80×24 ölçülerine geçirildi. Alt sürecin bildirdiği ölçü beklenerek renkli hücreler ve imleç konumu doğrulandı.
- `ssh_loading_fleet`: sentetik SSH çıktısı ile yeniden boyutlandırma sırasında devam eden RGB kaçış dizisi, canlı çıktı, renk, gizli imleç ve sabit imleç stili korunumu kontrol edildi. Bu test dış bir SSH sunucusuna bağlanmıyor.

Çoklu monitör/DPI kombinasyonlarının ve tüm harici TUI uygulamalarının kapsamlı testi yapılmadı. Test ekran görüntüsü alfa kanalını opak kaydettiği için saydamlık kanıtı görüntüden değil, doğrudan GPU piksel okumalarından geliyor.
