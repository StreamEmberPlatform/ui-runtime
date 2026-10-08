# StreamEmber Overlay (ui-runtime)

Tek oyunculu oyun modları için **HTML/CSS/JS overlay**'i (FiveM NUI benzeri). Chromium Embedded Framework (CEF)
oyun sürecinde pencere olmadan (offscreen) çalışır; sayfanın görüntüsü her karede oyunun üzerine çizilir; sayfa ile
oyun scriptleri arasında JSON mesajlaşması vardır.

Tek repo, iki ürün: oyundan bağımsız çekirdek + oyuna özel çizim katmanları (backend), **GTA V (Legacy, D3D11)** ve
**RDR2 (DirectX 12)**. İkisi aynı çekirdeği, aynı sayfayı ve aynı C# köprüsünü kullanır; her oyun için ayrı paket çıkar.
Arayüz kiti [MHud](https://github.com/StreamEmberPlatform/mhud)'dan gelir.

StreamEmber Platform'daki yeri:

| Repo | Ürün | Oyun klasöründe |
|---|---|---|
| [gtav-runtime-scripthook](https://github.com/StreamEmberPlatform/gtav-runtime-scripthook) | StreamEmber Runtime (GTA V) | `StreamEmber.Runtime.GTAV.asi`, `StreamEmber\Runtime\StreamEmber.Scripting.GTAV.dll` |
| [rdr2-runtime-scripthook](https://github.com/StreamEmberPlatform/rdr2-runtime-scripthook) | StreamEmber Runtime (RDR2) | `StreamEmber.Runtime.RDR2.asi`, `StreamEmber\Runtime\StreamEmber.Scripting.RDR2.dll` |
| **ui-runtime** (bu repo) | StreamEmber Overlay (GTA V / RDR2) | `StreamEmber.Overlay.<OYUN>.asi`, `StreamEmber\Overlay\`, trainer |
| [mhud](https://github.com/StreamEmberPlatform/mhud) | MHud arayüz kiti | `StreamEmber\Overlay\ui\mhud\` (bu repo paketler) |

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
| `StreamEmber\Overlay\` | `StreamEmber.Overlay.dll` (çekirdek), `StreamEmber.Overlay.Host.exe` (CEF alt süreci), CEF dosyaları, `ui\` (test sayfası, trainer adaptörü, MHud) |
| `StreamEmber\Scripts\` | `StreamEmber.Overlay.Bridge.dll`, `StreamEmber.Trainer.<OYUN>.dll` |
| `StreamEmber\Config\Overlay.ini` | Ayarlar (StartUrl, kısayollar). Güncellemede korunur |
| `StreamEmber\Logs\` | `Overlay.Backend.log`, `Overlay.log` (çekirdek), `Overlay.Cef.log`, RDR2'de `Overlay.Diag.log` |
| `StreamEmber\Cache\Overlay\` | CEF önbelleği |
| `StreamEmber\Manifests\StreamEmber.Overlay.<OYUN>.json` | Paket manifest'i: sürüm, commit, bağımlılıklar, dosyalar ve SHA-256 değerleri |
| `StreamEmber\Licenses\StreamEmber.Overlay.<OYUN>\` | Üçüncü taraf lisansları (CEF, MHud, RDR2'de MinHook) |

Gereken: GTA V **Legacy** + ScriptHookV + StreamEmber Runtime (GTA V); RDR2 **DirectX 12** modunda + ScriptHookRDR2
(`dinput8.dll`) + StreamEmber Runtime (RDR2). Trainer olmadan da overlay çalışır (yalnız sayfa).

Eski düzenden (OverlayRuntime) geçiş: `-Deploy` `StreamEmber\Overlay\overlay.ini`'yi `StreamEmber\Config\Overlay.ini`'ye
taşır, `scripts\` altındaki eski köprü/trainer/`OverlayDemo.3.cs` dosyalarını `.disabled` yapar. Eski
`StreamEmber\Overlay\logs\` ve `cache\` klasörleri elle silinebilir.

## Klasörler

```text
include/se_overlay.h        çekirdeğin C ABI'si (backend'ler + C# köprüsü bunu kullanır)
core/                       StreamEmber.Overlay.dll — CEF başlatma, OnPaint, girdi, mesaj kuyrukları
host/                       StreamEmber.Overlay.Host.exe — CEF alt süreci, JS köprüsü (window.streamember)
backends/common/            backend ortak kodu: Overlay.ini, çekirdeği yükleme, WndProc girdi kancası, D3D11 çizici
backends/gtav-d3d11/        StreamEmber.Overlay.GTAV.asi — ScriptHookV Present callback, oyunun D3D11 cihazı
backends/rdr2-d3d12/        StreamEmber.Overlay.RDR2.asi — kendi DXGI/D3D12 kancaları (MinHook), D3D11On12
bridge/                     StreamEmber.Overlay.Bridge (net48) — scriptler için C# sarmalayıcı
trainers/common/            trainer'ların oyundan bağımsız kısmı: JSON, MHud mesajları, menü, dünya etiketi motoru
trainers/gtav/              StreamEmber.Trainer.GTAV — GTA V trainer + performans testi
trainers/rdr2/              StreamEmber.Trainer.RDR2 — RDR2 trainer + performans testi
ui/index.html               test sayfası
ui/trainer/                 MHud sayfası için adaptör + performans paneli
package/Config/Overlay.ini  oyundaki ayar dosyasının şablonu
cmake/                      sürüm kaynağı (VERSIONINFO) şablonu
vendor/ScriptHookV_SDK/     ScriptHookV SDK alt kümesi (GTA V backend'i için)
tools/StreamEmber.Build.psm1  sürüm, manifest, paket ve kurulum yardımcıları (üç repoda aynı dosya)
build.ps1                   CEF + MHud + derleme + dist\<OYUN>\ + artifacts\*.zip (+ -Deploy)
VERSION, cef.lock, mhud.lock  sürüm tabanı ve sabitlenmiş bağımlılıklar
third_party/cef/  build/  dist/  artifacts/   (git dışı)
```

## Sürümler ve yayın

- Sürüm: `VERSION` dosyası `major.minor`, patch = o dosyanın son değiştiği commit'ten bu yana commit sayısı.
  `main`'e her push yeni bir sürümdür: `v1.0.0`, `v1.0.1`, … Minör/majör artırmak için `VERSION`'ı değiştirip pushla.
- GitHub Actions (`.github/workflows/build.yml`): her push ve PR'da derleme; `main`'de ayrıca etiket ve GitHub Release:
  `StreamEmber.Overlay.GTAV-<sürüm>.zip`, `StreamEmber.Overlay.RDR2-<sürüm>.zip` (+ `.sha256`). Zip'in kökü = oyun klasörü.
- Bağımlılıklar CI'da: CEF `cef.lock`'tan (önbellekli), MHud `mhud.lock`'taki commit'ten, trainer başvuruları
  (`StreamEmber.Scripting.<OYUN>.dll`) runtime repolarının **son release**'inden. Runtime repoları private ise
  ui-runtime'da `PLATFORM_TOKEN` secret'ı gerekir (fine-grained token, iki runtime reposuna "Contents: Read").
  Manifest'te `depends[].builtAgainst` trainer'ın hangi runtime sürümüne karşı derlendiğini yazar.
- MHud'u güncellemek: `mhud.lock`'taki `ref`/`commit`'i değiştirip pushla. CEF'i güncellemek: `.\build.ps1 -UpdateCef`
  (cef.lock yeniden yazılır), test et, pushla.
- Yerel derlemeler `-dev` ekiyle damgalanır (`1.0.5-dev`). Dosya özelliklerinde ürün sürümü = StreamEmber sürümü.
- Dağıtımda `.pdb` ve `.xml` yoktur (CI kontrol eder).

## Derleme

Visual Studio 2022+ ("Desktop development with C++" + "C++ CMake tools"), .NET SDK; cef-builds.spotifycdn.com ve
github.com (MinHook, CMake FetchContent) erişimi. Yerelde kardeş klasörler kullanılır:

```text
StreamEmberPlatform\
  ui-runtime\                 bu repo
  mhud\                       arayüz kiti (ya da -MHudPath / MHUD_PATH)
  gtav-runtime-scripthook\    bin\Release\StreamEmber.Scripting.GTAV.dll (ya da -ScriptingDir)
  rdr2-runtime-scripthook\    bin\Release\StreamEmber.Scripting.RDR2.dll (ya da -ScriptingDir)
```

```powershell
.\build.ps1                                   # iki oyun: derle + dist\GTAV, dist\RDR2 + artifacts\*.zip
.\build.ps1 -Game GTAV -Deploy -GamePath "D:\EpicGames\GTAV"                                  # ya da GTAV_GAME_PATH
.\build.ps1 -Game RDR2 -Deploy -GamePath "...\steamapps\common\Red Dead Redemption 2"         # ya da RDR2_GAME_PATH
.\build.ps1 -Game GTAV -Deploy -ResetConfig   # oyundaki Overlay.ini'yi şablonla değiştir
.\build.ps1 -UpdateCef                        # desteklenen aralıktaki (152-156) en yeni stable CEF'e geç
```

## Test adımları

Loglar: `<oyun>\StreamEmber\Logs\` → `Overlay.Backend.log`, `Overlay.log`, `Overlay.Cef.log` (RDR2: + `Overlay.Diag.log`).

1. **Çizim boru hattı (CEF yok).** `Overlay.ini` → `TestPattern=1`. Oyunda sol üstte turuncu çerçeveli çizgili bir
   panel görünmeli. F7 gizler/gösterir, F8 menü modunda beyaz imleç çizer. Oyun görüntüsü bozulmamalı, çözünürlük
   değişimi ve alt-tab sonrası da çalışmalı.
2. **CEF.** `TestPattern=0`, `StartUrl=` (boş). Sağ üstte "StreamEmber Overlay" paneli; saat ve "Kare hızı" akmalı.
   F8 → menü modu: butonlar fareye tepki vermeli, yazı kutusuna klavyeyle yazılabilmeli.
3. **Trainer.** `StartUrl=mhud/trainer.html`. F5 menüyü açar; HUD (can, para, konum) dolmalı.

Geri bildirim için: hangi adımda ne görüldüğü + log dosyaları.

## Trainer (MHud + performans testi)

`trainers/gtav` → `StreamEmber\Scripts\StreamEmber.Trainer.GTAV.dll`, `trainers/rdr2` → `StreamEmber.Trainer.RDR2.dll`.
Sayfa: `StartUrl=mhud/trainer.html` (şablondaki varsayılan).

Arayüz MHud'un FiveM sayfasıdır (`mhud/integration/mhud/html`, sürümü `mhud.lock`), **değiştirilmeden** kullanılır. `build.ps1`
kiti ve sayfayı `ui\mhud\` altına kopyalar, `trainer.html`'i bu sayfaya yalnız `ui/trainer/trainer.css` ve
`ui/trainer/trainer.js` ekleyerek üretir. `trainer.js` bir adaptördür:
`window.streamember` mesajları → FiveM'deki gibi `window` `message` olayı; `MH.post(ad, veri)` → `streamember.post({ cb, data })`.
C# scripti MHud'un Lua tarafıyla **aynı mesajları** gönderir (`mhud:config`, `mhud:vitals`, `mhud:vehicle`,
`mhud:location`, `mhud:heading`, `mhud:wanted`, `mhud:money`, `mhud:nametags`, `mhud:menu`, `mhud` RPC).

| Tuş | İş |
|---|---|
| F5 | Trainer menüsü (↑ ↓ ← → Enter Backspace ya da numpad 8 2 4 6 5 0). Oyun odağı gerekmez. |
| F7 | Overlay göster/gizle (backend) |
| F8 | Fare+klavye arayüze (backend). Menüye fareyle tıklanabilir. |

Menüler: Işınlanma (11 nokta + harita işareti), Araçlar (ver, hızı koruyarak değiştir, tamir, renk, tam performans,
sil), Oyuncu modeli (11 karakter), Oyuncu (can/zırh, ölümsüzlük, aranma, silah, para), Dünya (saat, hava, kalabalık),
Performans testi, HUD (tema, bildirim vitrini).

**Performans testi:** çevredeki tüm yaya ve araçlara MHud isim etiketi (`mhud:nametags`) her karede gönderilir.

- *Native referans noktaları*: oyun, her etiketin çapasına **aynı karede** kırmızı nokta çizer. Etiketin alt ucu noktada
  durmalı; kamera dönerken aradaki kayma overlay'in gecikmesidir.
- *Kamerayı döndür*: kamerayı 45/90/180°/sn sabit döndürür (elle uğraşmadan senkron testi).
- *Gidiş-dönüş gecikme*: her etiket mesajında `seq` var; sayfa saniyede ~2 örneği hemen onaylar (`ack`), C# ms ve kare
  olarak ölçer. Ayarlar: mesafe 25-400 m, en fazla 25-400 etiket, her kare / 30 Hz / 15 Hz, hedef türü,
  *mesafe yazısı adımı*.
- Panel (sağ orta): oyun FPS, sayfa FPS, etiket sayısı, mesaj/sn, DOM süresi, C# süresi, KB/sn, gecikme.

### Dünyaya bağlı arayüzde kare senkronu: sprite atlası (API 2)

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

**Kalibrasyon** (trainer → Performans testi): *Native referans noktaları*'nı aç (oyun, en yakın 30 varlığın çapasına
`SET_DRAW_ORIGIN` ile kendisi çizer = gerçek konum), *Kamerayı döndür* 90°/sn. Etiketin alt ucu noktada durmalı.
Etiketler noktaların önünde gidiyorsa *Senkron gecikmesi* = 1 kare; arkasından geliyorsa *Öngörü* = 1 kare.
*Konumlandırma*'yı HTML'e alarak eski yolla farkı görebilirsin.

**Bulgu (headless Chromium, 150 etiket):** yalnız konum değişince güncelleme **~1 ms**; mesafe yazısı her mesajda
değişince **~22 ms** (60 Hz'de karşılanamaz). Sebep: MHud `app.js` etiket imzasına (`sig`) mesafeyi koyuyor, imza
değişince `MH.Nametags` etiketin HTML'ini baştan yazıyor. Trainer mesafeyi varsayılan 5 m adımla yuvarlar
(menüden 1 m seçilerek fark ölçülebilir). Kalıcı çözüm MHud'da: mesafe metnini imzadan çıkarıp yalnız o metni güncellemek.

Aynı anda tek bir script `SEO_PollFromUi` okumalı (çekirdekte tek gelen kutusu var).

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
if (OverlayBridge.IsReady) OverlayBridge.Send("{\"type\":\"gift\",\"user\":\"ali\",\"count\":5}");
while (OverlayBridge.TryReceive(out string json)) { /* UI'dan gelen */ }
OverlayBridge.InputMode = OverlayInputMode.Ui;   // menü modu; Tick'te oyun kontrollerini de kapatın
```

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

RDR2 trainer'ı (`trainers/rdr2`) GTA'dakiyle aynı sayfa ve protokolü kullanır; dünya etiketi motoru ortak
(`trainers/common/WorldTags.cs`, oyun tarafı `RdrTagWorld`). Farklar: atlar/arabalar/kayıklar, kasabalar, karakter
modelleri; HUD'da çekirdekler (can/dayanıklılık/dead eye) ve para; native referans olarak oyunun 3B çizdiği küreler
(RDR2'de SET_DRAW_ORIGIN yok). F5 menü, F7/F8 overlay kısayolları. StreamEmber Runtime konsolu F4'tedir (`StreamEmber\Config\Runtime.ini`).
