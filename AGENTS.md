# OverlayRuntime — ajan notları

Önce `README.md` okunur. Bu proje oyundan bağımsız bir çekirdek + oyuna özel backend'lerdir; GTA V'ye özgü kod
yalnız `backends/gtav-*` ve `samples/gtav` içine girer.

- C ABI (`include/se_overlay.h`) değişirse `SEO_API_VERSION` artırılır; backend'ler ve `bridge/OverlayBridge.cs`
  (`ApiVersion`) birlikte güncellenir.
- Present callback'inde native çağrılmaz; oyuna dokunan her şey scriptlerin Tick'inde yapılır.
- Dünyaya bağlı (kafa üstü, işaretçi) öğeler HTML ile konumlandırılmaz: atlas + `SEO_SubmitSprites` kullanılır
  (README "kare senkronu"). HTML konumlandırma birkaç kare gecikir.
- Atlas yerleşimi çekirdekte kırpılabilir; sayfaya her zaman `SEO_GetAtlasLayout` ile okunan ETKİN yerleşim gönderilir.
- Çekirdekte CEF nesneleri statik yıkıcıya bırakılmaz (oyun SEO_Shutdown çağırmadan kapanabilir).
- `libcef.dll` gecikmeli yüklenir; çekirdek onu önce tam yoluyla yükler. Bu sıra bozulmamalı.
- Backend, oyunun D3D11 durumunu her karede kaydedip geri yükler; yeni bir durum değiştiren çağrı eklenirse
  `StateBackup`'a da eklenir. Back buffer'a referans kare sonunda bırakılır (ResizeBuffers).
- CEF sürümü `cef.lock` ile sabittir; kod CEF 152-156 başlıklarına karşı kontrol edildi. Aralık dışına çıkılırsa
  `build.ps1` içindeki `$CefMajorMin/Max` ve imzalar gözden geçirilir. Sürümler arasında imzası değişen
  callback'ler (ör. `OnBeforePopup`) override edilmez.
- Kullanıcıya görünen metinler Türkçe; sınıf/değişken adları ve kod yorumları İngilizce.
- SHVDN ham scriptleri (`*.3.cs`) C# 5 ile derlenir: `$""`, `?.`, `nameof`, `out var` kullanılmaz.
