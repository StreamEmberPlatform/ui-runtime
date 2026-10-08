# StreamEmber Overlay (ui-runtime)

Tek oyunculu oyun modları için **HTML/CSS/JS overlay motoru** (FiveM NUI benzeri). Chromium Embedded Framework (CEF)
oyun sürecinde pencere olmadan (offscreen) çalışır; sayfanın görüntüsü her karede oyunun üzerine çizilir; sayfa ile
oyun scriptleri arasında JSON mesajlaşması vardır.

Overlay **yalnız motordur**: içinde sayfa, trainer, arayüz kiti ya da varsayılan veri yoktur. Hangi sayfanın
gösterileceğine scriptler karar verir ve sayfayı genelde CDN'den yükler (`OverlayBridge.LoadUrl`). Hiçbir script sayfa
yüklemezse overlay boş (şeffaf) kalır.

Tek repo, iki ürün: oyundan bağımsız çekirdek + oyuna özel çizim katmanları (backend), **GTA V (Legacy, D3D11)** ve
**RDR2 (DirectX 12)**. İkisi aynı çekirdeği ve aynı C# köprüsünü kullanır; her oyun için ayrı paket çıkar.

StreamEmber Platform'daki yeri:

| Repo | Ürün | Oyun klasöründe |
|---|---|---|
| [gtav-runtime-scripthook](https://github.com/StreamEmberPlatform/gtav-runtime-scripthook) | StreamEmber Runtime (GTA V) | `StreamEmber.Runtime.GTAV.asi`, `StreamEmber\Runtime\StreamEmber.Scripting.GTAV.dll` |
| [rdr2-runtime-scripthook](https://github.com/StreamEmberPlatform/rdr2-runtime-scripthook) | StreamEmber Runtime (RDR2) | `StreamEmber.Runtime.RDR2.asi`, `StreamEmber\Runtime\StreamEmber.Scripting.RDR2.dll` |
| **ui-runtime** (bu repo) | StreamEmber Overlay (GTA V / RDR2) | `StreamEmber.Overlay.<OYUN>.asi`, `StreamEmber\Overlay\`, `StreamEmber\Scripts\StreamEmber.Overlay.Bridge.dll` |
| [gtav-trainer-scripthook](https://github.com/StreamEmberPlatform/gtav-trainer-scripthook) | StreamEmber Trainer (GTA V) | `StreamEmber\Scripts\StreamEmber.Trainer.GTAV.dll`; arayüzü GitHub Pages'te |
| [rdr2-trainer-scripthook](https://github.com/StreamEmberPlatform/rdr2-trainer-scripthook) | StreamEmber Trainer (RDR2) | `StreamEmber\Scripts\StreamEmber.Trainer.RDR2.dll`; arayüzü GitHub Pages'te |
| [mhud](https://github.com/StreamEmberPlatform/mhud) | MHud arayüz kiti | — (sayfalar CDN'den yükler: jsDelivr / npm) |

## Mimari

```text
GTA5.exe / RDR2.exe
 ├─ GTA V: ScriptHookV ── Present callback ──► StreamEmber.Overlay.GTAV.asi   (backends/gtav-d3d11)
 ├─ RDR2:  MinHook ── IDXGISwapChain::Present ──► StreamEmber.Overlay.RDR2.asi (backends/rdr2-d3d12, D3D11On12)
 │                                         │ D3D11: kareyi texture'a yükle, oyunun üstüne çiz
 │                                         │ WndProc: kısayollar, menü modunda fare/klavye → UI
 │                                         ▼ LoadLibrary
 │                                    StreamEmber.Overlay.dll          (core, C ABI: include/se_overlay.h)
 │                                         │ CEF tarayıcı süreci tarafı, OnPaint → BGRA kare
 │                                         │ JSON kuyrukları (oyun ⇄ sayfa)
 ├─ StreamEmber Runtime scriptleri ── P/Invoke ──► StreamEmber.Overlay.Bridge.dll   (bridge, oyundan bağımsız C#)
 │
 └─ alt süreçler: StreamEmber.Overlay.Host.exe (renderer: window.streamember köprüsü, GPU, utility)
```

Kurallar:

- Present callback'inde **native çağrılamaz**. Oyun verisi scriptlerde (Tick) toplanır, köprüyle sayfaya gider.
- Sayfa çökerse yalnız host süreci ölür; çekirdek sayfayı 1 sn sonra yeniden yükler.
- Overlay swapchain'e çizildiği için OBS "Oyun Yakalama" ile **yayına girer**.


## Oyun klasöründeki düzen

| Dosya | Görev |
|---|---|
| `StreamEmber.Overlay.<OYUN>.asi` | Backend. ASI yükleyici oyun kökünden yükler |
| `StreamEmber\Overlay\` | `StreamEmber.Overlay.dll` (çekirdek), `StreamEmber.Overlay.Host.exe` (CEF alt süreci), CEF dosyaları |
| `StreamEmber\Scripts\StreamEmber.Overlay.Bridge.dll` | Scriptlerin overlay'e erişimi (C#) |
| `StreamEmber\Config\Overlay.ini` | Ayarlar (kısayollar, kare hızı, geliştirme için `StartUrl`). Güncellemede korunur |
| `StreamEmber\Logs\` | `Overlay.Backend.log`, `Overlay.log` (çekirdek), `Overlay.Cef.log`, RDR2'de `Overlay.Diag.log` |
| `StreamEmber\Cache\Overlay\` | CEF önbelleği (CDN'den gelen sayfa dosyaları burada önbelleklenir) |
| `StreamEmber\Manifests\StreamEmber.Overlay.<OYUN>.json` | Paket manifest'i: sürüm, commit, dosyalar ve SHA-256 değerleri |
| `StreamEmber\Licenses\StreamEmber.Overlay.<OYUN>\` | Üçüncü taraf lisansları (CEF, RDR2'de MinHook) |

Gereken: GTA V **Legacy** + ScriptHookV; RDR2 **DirectX 12** modunda + ScriptHookRDR2 (`dinput8.dll`). Overlay'i kullanan
scriptler için StreamEmber Runtime.

Eski düzenden (OverlayRuntime) geçiş: `-Deploy` `StreamEmber\Overlay\overlay.ini`'yi `StreamEmber\Config\Overlay.ini`'ye
taşır; `scripts\` altındaki eski köprü/trainer dosyalarını ve eski `StreamEmber\Overlay\ui\` klasörünü `.disabled` yapar.
Eski `StreamEmber\Overlay\logs\` ve `cache\` klasörleri elle silinebilir.

## Sayfayı yükleme (API 3)

```csharp
using StreamEmber.Overlay;
// Script başlarken (ör. ilk Tick'te): aynı adres zaten açıksa bir şey yapmaz
OverlayBridge.LoadUrl("https://streamemberplatform.github.io/gtav-trainer-scripthook/");
```

- Tam URL (`https://`, `file://`), `""` = boş sayfa, diğer her şey `<oyun>\StreamEmber`'a göre dosya yolu (yerel geliştirme).
- Sayfa CEF önbelleğinde tutulur; CDN'e her oyunda yeniden gidilmez, değişen dosyalar yeniden indirilir.
- `Overlay.ini` → `StartUrl` yalnız açılışta gösterilen sayfadır (geliştirme için); bir script `LoadUrl` çağırınca onunki geçer.
- Aynı anda tek bir script sayfa sahibi olmalı (çekirdekte tek gelen kutusu var: `SEO_PollFromUi`).

### Çalışıyor göstergesi

Overlay yüklendiğinde ekranın sol üst köşesinde küçük, yarı saydam bir **StreamEmber** yazısı ve yanında bir nokta
çizilir (sayfa olmasa da). Nokta çekirdeğin durumunu gösterir: sarı = CEF başlıyor, yeşil = hazır, kırmızı = başlatılamadı
(sebep `Overlay.Cef.log`'da). Yazı çözünürlükle ölçeklenir, F7 ile overlay gizlenince o da gizlenir.
Kapatmak için `Overlay.ini` → `ShowBadge=0`.

## Sayfa API'si (JS)

```js
if (window.streamember) {
  streamember.post({ type: 'vote', option: 2 });          // → oyun (SEO_PollFromUi / OverlayBridge.TryReceive)
  const off = streamember.on(msg => console.log(msg));    // ← oyun (SEO_PostToUi / OverlayBridge.Send)
}
```

`window.streamember` yalnız ana çerçevede vardır. Sayfa normal bir tarayıcıda da açılabilir (köprü yoksa test edilebilir
olmalı). Mesaj başına üst sınır 1 MiB; oyun okumazsa kuyruk 1024 mesajda en eskiyi atar.

## C# köprüsü

```csharp
using StreamEmber.Overlay;
OverlayBridge.LoadUrl("https://.../index.html");                 // sayfa (CDN)
if (OverlayBridge.IsReady) OverlayBridge.Send("{\"type\":\"gift\",\"user\":\"ali\",\"count\":5}");
while (OverlayBridge.TryReceive(out string json)) { /* UI'dan gelen */ }
OverlayBridge.InputMode = OverlayInputMode.Ui;   // menü modu; Tick'te oyun kontrollerini de kapatın
```

Köprü (`StreamEmber.Overlay.Bridge.dll`) oyundan bağımsızdır, native çağırmaz, hiçbir zaman exception fırlatmaz:
overlay kurulu değilse ya da API sürümü uymuyorsa her çağrı sessizce bir şey yapmaz. Scriptler ona derleme zamanında
başvurur (`Private=false`); oyunda `StreamEmber\Scripts\` altındaki kopya kullanılır.

## Dünyaya bağlı arayüzde kare senkronu: sprite atlası

HTML ile konumlandırılan her şey (MHud/FiveM yolu) tarayıcı boru hattı yüzünden **3-6 kare geç** kalır:
oyun karesi → JSON → CEF görevi → JS/DOM → Chromium raster (kendi 60 Hz saatiyle) → `OnPaint` → sonraki `Present`.
Kamera hızlı dönerken etiketler kafalardan kayar. Bunu tarayıcıyı hızlandırarak çözmek mümkün değil; çözüm
**konumu tarayıcıdan almak**:

- CEF görünümü ekrandan uzundur; ekranın altındaki bölge **atlas**tır (`SEO_SetAtlasLayout`). Sayfa her etiketi bir
  kez sabit bir slota çizer (yalnız içerik: ad, can barı, mesafe; değişince yeniden).
- Oyun her karede hangi slotun ekranın neresine gideceğini **struct dizisi** olarak verir (`SEO_SubmitSprites`,
  JSON/JS yok). Backend aynı karede slotları atlas'tan alıp o konumlara çizer, sonra HUD'u üstüne çizer.
- Sayfa slotu boyadığını bildirmeden (`atlasReady`) oyun o slotu göstermez; yeni etiket birkaç kare sonra belirir,
  ama hareket eden etiket hiç gecikmez.
- Artık yalnız değişen bölgeler kopyalanır ve GPU'ya yüklenir (dirty rect), uzun görünüm ek maliyet getirmez.

API: `SEO_SetAtlasLayout` / `OverlayBridge.SetAtlasLayout`, `SEO_SubmitSprites` / `OverlayBridge.SubmitSprites`,
`SEO_SetSpriteDelay`. Kullanım örneği: StreamEmber Trainer'ın dünya etiketleri.

## Klasörler

```text
include/se_overlay.h        çekirdeğin C ABI'si (backend'ler + C# köprüsü bunu kullanır)
core/                       StreamEmber.Overlay.dll — CEF başlatma, OnPaint, girdi, mesaj kuyrukları, sayfa yükleme
host/                       StreamEmber.Overlay.Host.exe — CEF alt süreci, JS köprüsü (window.streamember)
backends/common/            backend ortak kodu: Overlay.ini, çekirdeği yükleme, WndProc girdi kancası, D3D11 çizici
backends/gtav-d3d11/        StreamEmber.Overlay.GTAV.asi — ScriptHookV Present callback, oyunun D3D11 cihazı
backends/rdr2-d3d12/        StreamEmber.Overlay.RDR2.asi — kendi DXGI/D3D12 kancaları (MinHook), D3D11On12
bridge/                     StreamEmber.Overlay.Bridge (net48) — scriptler için C# sarmalayıcı
package/Config/Overlay.ini  oyundaki ayar dosyasının şablonu
cmake/                      sürüm kaynağı (VERSIONINFO) şablonu
vendor/ScriptHookV_SDK/     ScriptHookV SDK alt kümesi (GTA V backend'i)
vendor/minhook/             MinHook 1.3.4 kaynakları (RDR2 backend'i)
tools/StreamEmber.Build.psm1  sürüm, manifest, paket ve kurulum yardımcıları (tüm StreamEmber repolarında aynı dosya)
build.ps1                   CEF + derleme + dist\<OYUN>\ + artifacts\*.zip (+ -Deploy)
VERSION, cef.lock           sürüm tabanı, sabitlenmiş CEF sürümü
third_party/cef/  build/  dist/  artifacts/   (git dışı)
```

## Sürümler ve yayın

- Sürüm: `VERSION` dosyası `major.minor`, patch = o dosyanın son değiştiği commit'ten bu yana commit sayısı.
  `main`'e her push yeni bir sürümdür: `v1.0.0`, `v1.0.1`, … Minör/majör artırmak için `VERSION`'ı değiştirip pushla.
- GitHub Actions (`.github/workflows/build.yml`): her push ve PR'da derleme; `main`'de ayrıca etiket ve GitHub Release:
  `StreamEmber.Overlay.GTAV-<sürüm>.zip`, `StreamEmber.Overlay.RDR2-<sürüm>.zip` (+ `.sha256`). Zip'in kökü = oyun klasörü.
- Dış bağımlılık: yalnız CEF (dosyaları GitHub'ın 100 MB sınırını aştığı için depoda olamaz). `cef.lock` ile sabit, SHA-1
  doğrulanır, CI'da önbelleklenir. Güncellemek: `.\build.ps1 -UpdateCef`, test et, pushla. ScriptHookV SDK alt kümesi ve
  MinHook `vendor/` altındadır.
- Yerel derlemeler `-dev` ekiyle damgalanır (`1.0.5-dev`). Dosya özelliklerinde ürün sürümü = StreamEmber sürümü.
- Dağıtımda `.pdb`, `.xml` ve sayfa (`.html`) yoktur (CI kontrol eder).

## Derleme

Visual Studio 2022+ ("Desktop development with C++" + "C++ CMake tools"), .NET SDK; cef-builds.spotifycdn.com erişimi.

```powershell
.\build.ps1                                   # iki oyun: derle + dist\GTAV, dist\RDR2 + artifacts\*.zip
.\build.ps1 -Game GTAV -Deploy -GamePath "D:\EpicGames\GTAV"                                  # ya da GTAV_GAME_PATH
.\build.ps1 -Game RDR2 -Deploy -GamePath "...\steamapps\common\Red Dead Redemption 2"         # ya da RDR2_GAME_PATH
.\build.ps1 -Game GTAV -Deploy -ResetConfig   # oyundaki Overlay.ini'yi şablonla değiştir
.\build.ps1 -UpdateCef                        # desteklenen aralıktaki (152-156) en yeni stable CEF'e geç
```

## Test adımları

Loglar: `<oyun>\StreamEmber\Logs\` → `Overlay.Backend.log`, `Overlay.log`, `Overlay.Cef.log` (RDR2: + `Overlay.Diag.log`).
Her açılışta bir önceki oturumun logu `<ad>.previous.log` olarak saklanır (oyun çöktükten/yeniden açıldıktan sonra da
kanıt kaybolmaz).

1. **Çizim boru hattı (CEF yok).** `Overlay.ini` → `TestPattern=1`. Oyunda sol üstte turuncu çerçeveli çizgili bir
   panel görünmeli. F7 gizler/gösterir, F8 menü modunda beyaz imleç çizer. Oyun görüntüsü bozulmamalı, çözünürlük
   değişimi ve alt-tab sonrası da çalışmalı.
2. **CEF.** `TestPattern=0`, `StartUrl=https://example.com/` → sayfa görünmeli; F8 ile fare ve klavye çalışmalı.
   Sonra `StartUrl=` (boş) → sol üstteki StreamEmber yazısı dışında hiçbir şey çizilmemeli.
3. **Script ile.** StreamEmber Trainer kuruluyken F5 → trainer sayfası CDN'den yüklenir (`Overlay.log`: "Loading https://...").

## Bilinen sınırlar / riskler

- `<select>` açılır listeleri (CEF "popup") henüz çizilmiyor; özel dropdown bileşenleri kullanın.
- CPU yolu: her boyamada tam kare kopyalanır (1080p ≈ 8 MB). Sonraki adım: GPU paylaşımlı texture (`OnAcceleratedPaint`).
- Oyundan çıkarken CEF kapatılmıyor (loader lock); süreç kapanınca alt süreçler de kapanır.
- `CefInitialize` başarısız olursa sebep `StreamEmber\Logs\Overlay.Cef.log`'dadır.
- Menü modunda oyunun fareyi pencere ortasına sabitleyip sabitlemediği ve ham girdi engellemenin yeterli olup
  olmadığı oyunda görülecek (`BlockRawInputInUiMode`).
- Menü moduna geçerken basılı tutulan bir tuş, ham girdi engellendiği için oyunda "basılı" kalabilir.
- Back buffer `*_SRGB` formatındaysa overlay soluk görünebilir (oyunda kontrol edilecek).
- Sayfa yüklenmeden gönderilen mesajlar (en fazla 256) bekletilir ve sayfa yüklenince iletilir.
- Yalnız tek oyunculu mod. ScriptHookV online'da çalışmaz.

## RDR2 backend'i (backends/rdr2-d3d12)

ScriptHookRDR2 SDK'sında ScriptHookV'deki gibi bir Present callback yok; backend DXGI/D3D12'ye kendisi bağlanır:

- ASI yüklenince bir iş parçacığında atılacak bir D3D12 cihazı + swap chain oluşturur, vtable'dan adresleri alır ve
  MinHook ile `IDXGISwapChain::Present/Present1`, `ResizeBuffers/ResizeBuffers1`, `ID3D12CommandQueue::ExecuteCommandLists`
  kancalarını kurar.
- Oyunun DIRECT kuyruğu ExecuteCommandLists'ten öğrenilir (Present'i çağıran iş parçacığının son kuyruğu, cihazı
  swap chain'inkiyle aynı olmalı). Bu kuyrukta `D3D11On12` cihazı açılır; back buffer'lar sarılıp ortak D3D11
  çizicisi (`backends/common`) aynen kullanılır, `Flush` işi oyunun Present'inden önce kuyruğa koyar.
- ResizeBuffers'tan önce sarılı back buffer'lar bırakılır. Çizimde yapılandırılmış istisna (SEH) olursa overlay o oturum
  için kapanır, oyun çalışmaya devam eder.
- **Vulkan desteklenmiyor**: 3 dk içinde hiç DXGI Present görülmezse `Overlay.Backend.log`'a "Grafik API'sini DirectX 12
  yapın" yazılır. HDR (R16G16B16A16_FLOAT back buffer) açıkken renkler soluk görünür (logda belirtilir).
- Loglar: `RDR2\StreamEmber\Logs\` → `Overlay.Backend.log`, `Overlay.Diag.log` (DXGI olayları, istisnalar), `Overlay.log`, `Overlay.Cef.log`.
