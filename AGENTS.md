# ui-runtime (StreamEmber Overlay) — ajan notları

Önce `README.md` okunur. Bu proje oyundan bağımsız bir çekirdek + oyuna özel backend'lerdir; GTA V'ye özgü kod
yalnız `backends/gtav-*`, RDR2'ye özgü kod yalnız `backends/rdr2-*` içine girer; ortak backend kodu `backends/common`.

- **Overlay yalnız motordur.** Repoya sayfa, trainer, arayüz kiti ya da varsayılan veri eklenmez; sayfayı scriptler
  seçer (`SEO_LoadUrl` / `OverlayBridge.LoadUrl`), genelde CDN'den. Trainer'lar gtav-trainer-scripthook ve
  rdr2-trainer-scripthook repolarındadır.

- C ABI (`include/se_overlay.h`) değişirse `SEO_API_VERSION` artırılır; backend'ler ve `bridge/OverlayBridge.cs`
  (`ApiVersion`) birlikte güncellenir.
- Present callback'inde (GTA) / Present kancasında (RDR2) native çağrılmaz; oyuna dokunan her şey scriptlerin Tick'inde yapılır.
- RDR2 backend'i: DllMain'de kanca kurulmaz (iş parçacığı); D3D12 durumu yalnız Present iş parçacığında değişir;
  SEH korumalı `SafeDrawOverlay` içinde yıkıcılı C++ nesnesi tutulmaz (C2712). MinHook `vendor/minhook` içindedir.
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

## Dağıtım ve adlar

- Çıktı adları `StreamEmber.<Bileşen>[.<OYUN>].<uzantı>` kalıbındadır (OYUN = GTAV | RDR2). Oyun kökünde yalnız
  `.asi`; diğer her şey `StreamEmber\` altında: `Overlay\`, `Scripts\`, `Config\Overlay.ini`, `Logs\Overlay*.log`,
  `Cache\Overlay\`, `Manifests\`, `Licenses\`. Yeni bir dosya/yol eklenirse README'deki tablo, `build.ps1` ve CI'daki
  paket kontrolü birlikte güncellenir.
- Dağıtıma `.pdb`, `.xml`, `.lib`, `.html` girmez (CI kontrol eder).
- Sürüm `VERSION` + git geçmişinden gelir (`tools/StreamEmber.Build.psm1`; tüm StreamEmber repolarında aynı dosya, birlikte değiştirilir).
  Elle sürüm numarası yazılmaz; C# için `Directory.Build.props`, C++ için `cmake/StreamEmberVersion.cmake`.
- Dış bağımlılık yalnız CEF'tir (`cef.lock` ile sabit, commit edilir). Başka bir şey indirilmez; küçük bağımlılıklar
  `vendor/` altına konur.
- C ABI'ye eklenen her işlev köprüde de açılır; köprü exception fırlatmaz, overlay yoksa sessizce bir şey yapmaz.
- Kullanıcı ayar dosyası (`Overlay.ini`) kurulumda korunur; şablonda yeni anahtar eklenirse kodda varsayılanı olmalı.
