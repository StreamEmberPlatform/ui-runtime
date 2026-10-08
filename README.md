# OverlayRuntime

Tek oyunculu oyun modları için **HTML/CSS/JS overlay**'i (FiveM NUI benzeri). Chromium Embedded Framework (CEF)
oyun sürecinde pencere olmadan (offscreen) çalışır; sayfanın görüntüsü her karede oyunun üzerine çizilir; sayfa ile
oyun scriptleri arasında JSON mesajlaşması vardır.

Oyundan bağımsız bir çekirdek ve oyuna özel çizim katmanlarından (backend) oluşur: **GTA V (Legacy, D3D11)** ve
**RDR2 (DirectX 12)**. İkisi aynı çekirdeği, aynı sayfayı ve aynı C# köprüsünü kullanır. İçerik (HUD kiti) `../MHud`'dan gelir.

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
 ├─ SHVDN / SHRDRDN scriptleri ── P/Invoke ──► StreamEmber.Overlay.Bridge.dll   (bridge, oyundan bağımsız C#)
 │
 └─ alt süreçler: StreamEmber.Overlay.Host.exe (renderer: window.streamember köprüsü, GPU, utility)
```

Kurallar:

- Present callback'inde **native çağrılamaz**. Oyun verisi scriptlerde (Tick) toplanır, köprüyle sayfaya gider.
- Sayfa çökerse yalnız host süreci ölür; çekirdek sayfayı 1 sn sonra yeniden yükler.
- Overlay swapchain'e çizildiği için OBS "Oyun Yakalama" ile **yayına girer**.

## Klasörler

```text
include/se_overlay.h        çekirdeğin C ABI'si (backend'ler + C# köprüsü bunu kullanır)
core/                       StreamEmber.Overlay.dll — CEF başlatma, OnPaint, girdi, mesaj kuyrukları
host/                       StreamEmber.Overlay.Host.exe — CEF alt süreci, JS köprüsü (window.streamember)
backends/common/            backend ortak kodu: overlay.ini, çekirdeği yükleme, WndProc girdi kancası, D3D11 çizici
backends/gtav-d3d11/        StreamEmber.Overlay.GTAV.asi — ScriptHookV Present callback, oyunun D3D11 cihazı
backends/rdr2-d3d12/        StreamEmber.Overlay.RDR2.asi — kendi DXGI/D3D12 kancaları (MinHook), D3D11On12
bridge/                     StreamEmber.Overlay.Bridge (net48) — scriptler için C# sarmalayıcı
ui/index.html               test sayfası (aşama 2-3)
samples/gtav/OverlayDemo.3.cs  ilk köprü test scripti (aşama 3, artık kurulmuyor)
samples/common/              trainer'ların oyundan bağımsız kısmı: JSON, MHud mesajları, menü, dünya etiketi motoru
samples/gtav/TrainerDemo/     GTA V trainer + performans testi (StreamEmber.TrainerDemo.dll)
samples/rdr2/TrainerDemo/     RDR2 trainer + performans testi (StreamEmber.Rdr2TrainerDemo.dll)
ui/trainer/                 MHud sayfası için adaptör + performans paneli
overlay.ini                 oyundaki ayar dosyasının şablonu
build.ps1                   CEF indir + derle + dist\GTAV, dist\RDR2 + (-Deploy [-Game RDR2]) oyuna kur
cef.lock                    sabitlenmiş CEF sürümü (ilk derlemede oluşur, commit edilir)
third_party/cef/  build/  dist/   (git dışı)
```

Oyun klasöründeki düzen:

```text
GTA V\StreamEmber.Overlay.GTAV.asi
GTA V\StreamEmber\Overlay\   StreamEmber.Overlay.dll, StreamEmber.Overlay.Host.exe, libcef.dll ve CEF dosyaları,
                             ui\, overlay.ini, logs\, cache\
GTA V\scripts\               StreamEmber.Overlay.Bridge.dll, StreamEmber.TrainerDemo.dll
```

RDR2'de aynı düzen: `StreamEmber.Overlay.RDR2.asi`, `StreamEmber\Overlay\`, `scripts\` (Bridge + `StreamEmber.Rdr2TrainerDemo.dll`).

## Derleme

Ön koşullar: Visual Studio 2022+ ("Desktop development with C++" + "C++ CMake tools"), .NET SDK,
`../GTAVScriptHookRuntime` (ScriptHookV SDK'sı oradan alınır), `../MHud` (trainer arayüzü),
`../GTAVScriptHookRuntime/bin/Release/ScriptHookVDotNet3.dll` (GTA trainer başvurusu),
`../RDR2ScriptHookRuntime/bin/Release/ScriptHookRDRNetAPI.dll` (RDR2 trainer başvurusu; yoksa RDR2 trainer atlanır),
cef-builds.spotifycdn.com ve github.com (MinHook, CMake FetchContent) erişimi.

```powershell
.\build.ps1                    # CEF indir (ilk sefer ~150 MB), derle, dist\ hazırla
.\build.ps1 -Deploy            # + oyuna kur (GTAV_GAME_PATH ya da GTAV_SCRIPT_PATH'in üst klasörü)
.\build.ps1 -Deploy -GamePath "D:\EpicGames\GTAV"
.\build.ps1 -Deploy -Game RDR2 -GamePath "...\steamapps\common\Red Dead Redemption 2"   # ya da RDR2_GAME_PATH
.\build.ps1 -UpdateCef         # desteklenen aralıktaki (152-156) en yeni stable CEF'e geç
.\build.ps1 -Deploy -ResetConfig   # oyundaki overlay.ini'yi şablonla değiştir (StartUrl=mhud/trainer.html)
```

Oyunda gerekenler: GTA V **Legacy** + ScriptHookV + bizim SHVDN fork'umuz (`../GTAVScriptHookRuntime`);
RDR2 **DirectX 12** modunda + ScriptHookRDR2 (dinput8.dll ASI yükleyicisiyle) + bizim fork'umuz (`../RDR2ScriptHookRuntime`).

## Test adımları

Loglar: `GTA V\StreamEmber\Overlay\logs\` → `gtav-backend.log`, `overlay.log`, `cef.log`.

1. **Aşama 1 — çizim boru hattı (CEF yok).** `overlay.ini` → `TestPattern=1`. Oyunda sol üstte turuncu çerçeveli
   çizgili bir panel görünmeli. F7 gizler/gösterir, F8 menü modunda beyaz imleç çizer. Oyun görüntüsü bozulmamalı,
   çözünürlük değişimi ve alt-tab sonrası da çalışmalı.
2. **Aşama 2 — CEF.** `TestPattern=0`. Sağ üstte "StreamEmber Overlay" paneli; saat ve "Kare hızı" akmalı.
   F8 → menü modu: butonlar fareye tepki vermeli, yazı kutusuna klavyeyle yazılabilmeli.
3. **Aşama 3 — köprü.** `scripts\OverlayDemo.3.cs` yüklüyken panelde can/hız/konum dolmalı ("Köprü: hazır · N mesaj").
   "Oyuna gönder" → oyunda bildirim çıkmalı. "Oyuna dön" → menü modu kapanmalı.

Geri bildirim için: hangi adımda ne görüldüğü + üç log dosyası.

## Trainer demosu (MHud + performans testi)

`samples/gtav/TrainerDemo` → `scripts\StreamEmber.TrainerDemo.dll`. Sayfa: `StartUrl=mhud/trainer.html`.

Arayüz MHud'un FiveM sayfasıdır (`../MHud/integration/mhud/html`), **değiştirilmeden** kullanılır. `build.ps1`
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

Aynı anda tek bir script `SEO_PollFromUi` okumalı (çekirdekte tek gelen kutusu var). Bu yüzden `OverlayDemo.3.cs`
artık kurulmaz; kurulum eski kopyayı `.disabled` yapar.

## Aşamalar

| # | Konu | Durum |
|---|---|---|
| 0 | SHVDN fork'unu derleyip oyunda doğrulama | tamam (`../GTAVScriptHookRuntime`) |
| 1 | D3D11 çizim boru hattı, durum kaydet/geri yükle, test deseni | tamam (GTA V) |
| 2 | CEF offscreen (CPU/OnPaint yolu), host süreci | tamam (GTA V); kare senkronlu atlas oyunda kalibrasyon bekliyor |
| 2b | GPU paylaşımlı texture (`OnAcceleratedPaint`), kopyasız | sonra |
| 3 | JSON köprüsü (JS ⇄ C#), C# köprü kütüphanesi | kod hazır, oyunda test bekliyor |
| 4 | Girdi: menü/HUD modu, imleç, oyun kontrollerini kapatma | temel hali hazır; oyunda ayar gerekecek |
| 5 | MHud entegrasyonu, StreamEmber.Core'a köprü | sonra |
| 6 | Sağlamlık: duraklatma/yükleme ekranında gizleme, kapanış, bellek/FPS ölçümü | sonra |
| 7 | Depot paketi | sonra |
| R1 | RDR2: SHRDRDN fork'u (`../RDR2ScriptHookRuntime`) | kod hazır, oyunda test bekliyor |
| R2 | RDR2: DX12 backend (MinHook + D3D11On12) + RDR2 trainer | kod hazır, oyunda test bekliyor |

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
- CPU yolu: her boyamada tam kare kopyalanır (1080p ≈ 8 MB). Aşama 2b ile GPU yoluna geçilecek.
- Oyundan çıkarken CEF kapatılmıyor (loader lock); süreç kapanınca alt süreçler de kapanır.
- `CefInitialize` GTA5.exe içinde başarısız olursa sebep `cef.log`'dadır (ör. exe manifesti). Bu ilk testte netleşecek.
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
- **Vulkan desteklenmiyor**: 3 dk içinde hiç DXGI Present görülmezse `rdr2-backend.log`'a "Grafik API'sini DirectX 12
  yapın" yazılır. HDR (R16G16B16A16_FLOAT back buffer) açıkken renkler soluk görünür (logda belirtilir).
- Loglar: `RDR2\StreamEmber\Overlay\logs\rdr2-backend.log`, `overlay.log`, `cef.log`.

RDR2 trainer (`samples/rdr2/TrainerDemo`) GTA'dakiyle aynı sayfa ve protokolü kullanır; dünya etiketi motoru ortak
(`samples/common/WorldTags.cs`, oyun tarafı `RdrTagWorld`). Farklar: atlar/arabalar/kayıklar, kasabalar, karakter
modelleri; HUD'da çekirdekler (can/dayanıklılık/dead eye) ve para; native referans olarak oyunun 3B çizdiği küreler
(RDR2'de SET_DRAW_ORIGIN yok). F5 menü, F7/F8 overlay kısayolları. ScriptHookRDRDotNet konsolu F4'tedir
(`../RDR2ScriptHookRuntime/ScriptHookRDRDotNet.ini`), F8 overlay'e ayrıldı.
