# OverlayRuntime — ajan notları

Önce `README.md` okunur. Bu proje oyundan bağımsız bir çekirdek + oyuna özel backend'lerdir; GTA V'ye özgü kod
yalnız `backends/gtav-*` ve `samples/gtav`, RDR2'ye özgü kod yalnız `backends/rdr2-*` ve `samples/rdr2` içine girer.
İki oyunda ortak olan backend kodu `backends/common`, trainer kodu `samples/common` içindedir (WorldTags → ITagWorld).

- C ABI (`include/se_overlay.h`) değişirse `SEO_API_VERSION` artırılır; backend'ler ve `bridge/OverlayBridge.cs`
  (`ApiVersion`) birlikte güncellenir.
- Present callback'inde (GTA) / Present kancasında (RDR2) native çağrılmaz; oyuna dokunan her şey scriptlerin Tick'inde yapılır.
- RDR2 backend'i: DllMain'de kanca kurulmaz (iş parçacığı); D3D12 durumu yalnız Present iş parçacığında değişir;
  SEH korumalı `SafeDrawOverlay` içinde yıkıcılı C++ nesnesi tutulmaz (C2712). MinHook sürümü CMake'te sabittir.
- Dünyaya bağlı (kafa üstü, işaretçi) öğeler HTML ile konumlandırılmaz: atlas + `SEO_SubmitSprites` kullanılır
  (README "kare senkronu"). HTML konumlandırma birkaç kare gecikir.
- Atlas yerleşimi çekirdekte kırpılabilir; sayfaya her zaman `SEO_GetAtlasLayout` ile okunan ETKİN yerleşim gönderilir.
- Çekirdekte CEF nesneleri statik yıkıcıya bırakılmaz (oyun SEO_Shutdown çağırmadan kapanabilir).
- `libcef.dll` gecikmeli yüklenir; çekirdek onu önce tam yoluyla yükler. Bu sıra bozulmamalı.
- GTA backend'i oyunun D3D11 durumunu her karede kaydedip geri yükler (`Draw(rtv, preserveState=true)`); yeni bir
  durum değiştiren çağrı eklenirse `StateBackup`'a da eklenir. Back buffer'a referans kare sonunda bırakılır
  (ResizeBuffers). RDR2'de bağlam bizimdir (D3D11On12), durum saklanmaz; sarılı back buffer'lar ResizeBuffers kancasında bırakılır.
- CEF sürümü `cef.lock` ile sabittir; kod CEF 152-156 başlıklarına karşı kontrol edildi. Aralık dışına çıkılırsa
  `build.ps1` içindeki `$CefMajorMin/Max` ve imzalar gözden geçirilir. Sürümler arasında imzası değişen
  callback'ler (ör. `OnBeforePopup`) override edilmez.
- Kullanıcıya görünen metinler Türkçe; sınıf/değişken adları ve kod yorumları İngilizce.
- SHVDN ham scriptleri (`*.3.cs`) C# 5 ile derlenir: `$""`, `?.`, `nameof`, `out var` kullanılmaz.
