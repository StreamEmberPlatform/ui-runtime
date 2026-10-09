# Değişiklik günlüğü

Sürümler `VERSION` (major.minor) + commit sayısı (patch) ile otomatik verilir; her `main` push'u bir sürümdür.
Burada yalnız kayda değer değişiklikler tutulur.

## Yayımlanmamış — 2026-10-09
- API 4: mod başına OverlayChannel; ayrı sayfa/mesaj kuyruğu, ortak atlas kotası ve tek menü odağı. Aborted ile kanal temizliği. Kanal yalıtımı için yerel testler.

## 1.0
- Sol üstte küçük "StreamEmber" çalışıyor göstergesi + durum noktası (sarı/yeşil/kırmızı); `ShowBadge=0` kapatır.
- Loglar her açılışta `<ad>.previous.log`'a döndürülür (önceki oturumun logu kaybolmaz).
- Çizim oyunun arka tamponunun alfa kanalına artık yazmaz (yalnız renk).
- Overlay yalnız motor: trainer'lar ayrı repolara (gtav-trainer-scripthook, rdr2-trainer-scripthook), MHud ve
  hazır sayfalar (`ui/`) çıkarıldı. Sayfayı scriptler seçer: `SEO_LoadUrl` / `OverlayBridge.LoadUrl` (API 3); boş
  `StartUrl` = hiçbir şey çizilmez.
- MinHook `vendor/minhook` içinde (derleme dışarıdan yalnız CEF'i indirir).
- OverlayRuntime → **ui-runtime** (StreamEmber Overlay). Her oyun için ayrı paket: `StreamEmber.Overlay.GTAV`,
  `StreamEmber.Overlay.RDR2` (zip + sha256 + manifest), kendi sürüm numaraları.
- Klasör düzeni: ayarlar `StreamEmber\Config\Overlay.ini`, loglar `StreamEmber\Logs\` (`Overlay.Backend.log`,
  `Overlay.log`, `Overlay.Cef.log`, `Overlay.Diag.log`), CEF önbelleği `StreamEmber\Cache\Overlay`, scriptler
  `StreamEmber\Scripts\`.
- Tüm ikili dosyalarda sürüm bilgisi (VERSIONINFO / assembly sürümleri); `.pdb` ve `.xml` dağıtılmaz.
- Üçüncü taraf lisansları pakete eklendi.
- GitHub Actions: derleme, paket kontrolü, `main`'e her push'ta otomatik release.
- RDR2: DXGI tanılama logu (`Overlay.Diag.log`); CEF arka plan servisleri kapalı.
