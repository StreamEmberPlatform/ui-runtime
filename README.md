# OverlayRuntime

Tek oyunculu oyun modları için **HTML/CSS/JS overlay**'i (FiveM NUI benzeri). Chromium Embedded Framework (CEF)
oyun sürecinde pencere olmadan (offscreen) çalışır; sayfanın görüntüsü her karede oyunun üzerine çizilir; sayfa ile
oyun scriptleri arasında JSON mesajlaşması vardır.

Oyundan bağımsız bir çekirdek ve oyuna özel çizim katmanlarından (backend) oluşur. Şu an **GTA V (Legacy, D3D11)**
backend'i var; RDR2 backend'i aynı çekirdeği kullanacak. Gösterilecek içerik (HUD kiti) `../MHud`'dan gelecek.

## Mimari

```text
GTA5.exe
 ├─ ScriptHookV ── Present callback ──► StreamEmber.Overlay.GTAV.asi   (backends/gtav-d3d11)
 │                                         │ D3D11: kareyi texture'a yükle, oyunun üstüne çiz
 │                                         │ WndProc: kısayollar, menü modunda fare/klavye → UI
 │                                         ▼ LoadLibrary
 │                                    StreamEmber.Overlay.dll          (core, C ABI: include/se_overlay.h)
 │                                         │ CEF tarayıcı süreci tarafı, OnPaint → BGRA kare
 │                                         │ JSON kuyrukları (oyun ⇄ sayfa)
 ├─ SHVDN scriptleri ── P/Invoke ──► StreamEmber.Overlay.Bridge.dll   (bridge, oyundan bağımsız C#)
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
backends/gtav-d3d11/        StreamEmber.Overlay.GTAV.asi — Present callback, D3D11 çizim, WndProc
bridge/                     StreamEmber.Overlay.Bridge (net48) — scriptler için C# sarmalayıcı
ui/index.html               test sayfası (aşama 2-3)
samples/gtav/OverlayDemo.3.cs  SHVDN test scripti (aşama 3)
overlay.ini                 oyundaki ayar dosyasının şablonu
build.ps1                   CEF indir + derle + dist\ + (-Deploy) oyuna kur
cef.lock                    sabitlenmiş CEF sürümü (ilk derlemede oluşur, commit edilir)
third_party/cef/  build/  dist/   (git dışı)
```

Oyun klasöründeki düzen:

```text
GTA V\StreamEmber.Overlay.GTAV.asi
GTA V\StreamEmber\Overlay\   StreamEmber.Overlay.dll, StreamEmber.Overlay.Host.exe, libcef.dll ve CEF dosyaları,
                             ui\, overlay.ini, logs\, cache\
GTA V\scripts\               StreamEmber.Overlay.Bridge.dll, OverlayDemo.3.cs
```

## Derleme

Ön koşullar: Visual Studio 2022+ ("Desktop development with C++" + "C++ CMake tools"), .NET SDK,
`../GTAVScriptHookRuntime` (ScriptHookV SDK'sı oradan alınır), cef-builds.spotifycdn.com erişimi.

```powershell
.\build.ps1                    # CEF indir (ilk sefer ~150 MB), derle, dist\ hazırla
.\build.ps1 -Deploy            # + oyuna kur (GTAV_GAME_PATH ya da GTAV_SCRIPT_PATH'in üst klasörü)
.\build.ps1 -Deploy -GamePath "D:\EpicGames\GTAV"
.\build.ps1 -UpdateCef         # desteklenen aralıktaki (152-156) en yeni stable CEF'e geç
```

Oyunda gerekenler: ScriptHookV + bizim SHVDN fork'umuz (`../GTAVScriptHookRuntime`), GTA V **Legacy**.

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

## Aşamalar

| # | Konu | Durum |
|---|---|---|
| 0 | SHVDN fork'unu derleyip oyunda doğrulama | bekliyor (`../GTAVScriptHookRuntime`) |
| 1 | D3D11 çizim boru hattı, durum kaydet/geri yükle, test deseni | kod hazır, oyunda test bekliyor |
| 2 | CEF offscreen (CPU/OnPaint yolu), host süreci | kod hazır, oyunda test bekliyor |
| 2b | GPU paylaşımlı texture (`OnAcceleratedPaint`), kopyasız | sonra |
| 3 | JSON köprüsü (JS ⇄ C#), C# köprü kütüphanesi | kod hazır, oyunda test bekliyor |
| 4 | Girdi: menü/HUD modu, imleç, oyun kontrollerini kapatma | temel hali hazır; oyunda ayar gerekecek |
| 5 | MHud entegrasyonu, StreamEmber.Core'a köprü | sonra |
| 6 | Sağlamlık: duraklatma/yükleme ekranında gizleme, kapanış, bellek/FPS ölçümü | sonra |
| 7 | Depot paketi | sonra |

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

## RDR2 notu

RDR2 Vulkan ya da DX12 kullanır. `backends/rdr2-*` aynı çekirdeği yükleyip yalnız çizim katmanını ve oyuna bağlanma
noktasını yeniden yazacak. ScriptHookRDR2'de Present benzeri bir callback olup olmadığı o aşamada kontrol edilecek.
