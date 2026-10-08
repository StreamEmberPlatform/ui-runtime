# Değişiklik günlüğü

Sürümler `VERSION` (major.minor) + commit sayısı (patch) ile otomatik verilir; her `main` push'u bir sürümdür.
Burada yalnız kayda değer değişiklikler tutulur.

## 1.0
- OverlayRuntime → **ui-runtime** (StreamEmber Overlay). Her oyun için ayrı paket: `StreamEmber.Overlay.GTAV`,
  `StreamEmber.Overlay.RDR2` (zip + sha256 + manifest), kendi sürüm numaraları.
- Klasör düzeni: ayarlar `StreamEmber\Config\Overlay.ini`, loglar `StreamEmber\Logs\` (`Overlay.Backend.log`,
  `Overlay.log`, `Overlay.Cef.log`, `Overlay.Diag.log`), CEF önbelleği `StreamEmber\Cache\Overlay`, scriptler
  `StreamEmber\Scripts\`.
- Trainer'lar `StreamEmber.Trainer.GTAV` / `StreamEmber.Trainer.RDR2` oldu ve StreamEmber Runtime API'sine
  (`StreamEmber.Scripting.<OYUN>.dll`) karşı derleniyor. İlk köprü test scripti (`OverlayDemo.3.cs`) kaldırıldı.
- Tüm ikili dosyalarda sürüm bilgisi (VERSIONINFO / assembly sürümleri); `.pdb` ve `.xml` dağıtılmaz.
- MHud sürümü `mhud.lock` ile sabitlendi; üçüncü taraf lisansları pakete eklendi.
- GitHub Actions: derleme, paket kontrolü, `main`'e her push'ta otomatik release.
- RDR2: ölüm/yükleme/kararma sırasında trainer duraklar; DXGI tanılama logu; CEF arka plan servisleri kapalı.
