// Trainer actions and menu tree.
using System;
using System.Collections.Generic;
using GTA;
using GTA.Math;
using StreamEmber.Overlay;

namespace StreamEmber.TrainerDemo
{
    internal sealed class Trainer
    {
        private readonly WorldTags _tags;
        private readonly HudFeed _hud;
        private readonly MenuController _menus;
        private readonly Random _random = new Random();
        private readonly List<Vehicle> _spawnedVehicles = new List<Vehicle>();
        private readonly List<Ped> _spawnedPeds = new List<Ped>();

        public bool GodMode;
        public float CameraSpinDegreesPerSecond;   // 0 = off
        public bool ShowPerfPanel = true;
        public Menu Root { get; }

        public Trainer(WorldTags tags, HudFeed hud, MenuController menus)
        {
            _tags = tags;
            _hud = hud;
            _menus = menus;
            Root = BuildMenus();
        }

        // ------------------------------------------------------------------ data

        private static readonly (string Label, Vector3 Pos, string Desc)[] Locations =
        {
            ("Legion Meydanı", new Vector3(195.2f, -933.8f, 30.7f), "Kalabalık şehir merkezi: etiket stres testi için iyi."),
            ("Michael'ın evi", new Vector3(-852.4f, 160.0f, 65.6f), "Rockford Hills."),
            ("Franklin'in evi", new Vector3(7.9f, 548.1f, 176.0f), "Vinewood Hills."),
            ("Trevor'ın karavanı", new Vector3(1985.7f, 3812.2f, 32.2f), "Sandy Shores."),
            ("Los Santos Havalimanı", new Vector3(-1336.0f, -3044.0f, 13.9f), "Uçak ve helikopter denemeleri için."),
            ("Maze Bank çatısı", new Vector3(-75.0f, -818.0f, 326.2f), "Şehrin en yüksek noktası: çok uzak etiketler."),
            ("Chiliad Dağı", new Vector3(501.8f, 5604.5f, 797.9f), "Zirve."),
            ("Vinewood tabelası", new Vector3(711.4f, 1198.1f, 348.5f), ""),
            ("Del Perro İskelesi", new Vector3(-1850.1f, -1231.7f, 13.0f), "Kalabalık sahil."),
            ("Fort Zancudo", new Vector3(-2047.4f, 3132.1f, 32.8f), "Dikkat: aranma seviyesi verir."),
            ("Sandy Shores havaalanı", new Vector3(1747.0f, 3273.7f, 41.1f), ""),
        };

        private static readonly (string Label, string Model, string Desc)[] Vehicles =
        {
            ("Adder", "adder", "Süper araba"),
            ("Zentorno", "zentorno", "Süper araba"),
            ("T20", "t20", "Süper araba"),
            ("Sultan RS", "sultanrs", "Spor"),
            ("Elegy RH8", "elegy2", "Spor"),
            ("Kuruma (zırhlı)", "kuruma2", "Zırhlı sedan"),
            ("Polis aracı", "police", "Acil durum"),
            ("Insurgent", "insurgent", "Zırhlı arazi"),
            ("Rhino tankı", "rhino", "Askerî"),
            ("Bati 801", "bati", "Motosiklet"),
            ("Sanchez", "sanchez", "Kros motoru"),
            ("Buzzard", "buzzard2", "Helikopter (silahsız)"),
            ("Duster", "duster", "Tarım uçağı"),
        };

        private static readonly (string Label, string Model)[] PlayerModels =
        {
            ("Michael", "player_zero"), ("Franklin", "player_one"), ("Trevor", "player_two"),
            ("Kaykaycı", "a_m_y_skater_01"), ("Polis", "s_m_y_cop_01"), ("SWAT", "s_m_y_swat_01"),
            ("Plaj (kadın)", "a_f_y_beach_01"), ("Evsiz", "a_m_m_tramp_01"), ("Palyaço", "s_m_y_clown_01"),
            ("Uzaylı", "s_m_m_movalien_01"), ("Impotent Rage", "u_m_y_imporage"),
        };

        private static readonly string[] Themes = { "modern", "neon", "tactical", "minimal", "frontier" };
        private static readonly string[] WeatherLabels = { "Güneşli", "Bulutlu", "Yağmurlu", "Fırtınalı", "Sisli", "Karlı" };
        private static readonly string[] WeatherTypes = { "EXTRASUNNY", "CLOUDS", "RAIN", "THUNDER", "FOGGY", "XMAS" };
        private static readonly string[] Hours = { "06:00", "12:00", "18:00", "00:00" };
        private static readonly string[] RadiusOptions = { "25 m", "50 m", "100 m", "200 m", "400 m" };
        private static readonly float[] RadiusValues = { 25, 50, 100, 200, 400 };
        private static readonly string[] MaxOptions = { "25", "50", "100", "200", "400" };
        private static readonly int[] MaxValues = { 25, 50, 100, 200, 400 };
        private static readonly string[] RateOptions = { "Her kare", "30 Hz", "15 Hz" };
        private static readonly int[] RateValues = { 0, 30, 15 };
        private static readonly string[] TargetOptions = { "Hepsi", "Yalnız yayalar", "Yalnız araçlar" };
        private static readonly string[] PositioningOptions = { "Atlas (kare senkron)", "HTML (MHud/FiveM yolu)" };
        private static readonly string[] DelayOptions = { "0 kare", "1 kare", "2 kare" };
        private static readonly string[] PredictOptions = { "Kapalı", "1 kare" };
        private static readonly string[] DistStepOptions = { "1 m (MHud varsayılanı)", "5 m", "10 m" };
        private static readonly int[] DistStepValues = { 1, 5, 10 };
        private static readonly string[] SpinOptions = { "Kapalı", "45°/sn", "90°/sn", "180°/sn" };
        private static readonly float[] SpinValues = { 0, 45, 90, 180 };

        // ------------------------------------------------------------------ menus

        private Menu BuildMenus()
        {
            var root = new Menu("StreamEmber Trainer", "Overlay + MHud testi");

            var teleport = new Menu("Işınlanma", "Konum seç");
            teleport.Action("Haritadaki işarete", "Haritada işaret koyduğun noktaya ışınlan.", _ => TeleportToWaypoint());
            foreach (var loc in Locations)
            {
                var target = loc.Pos;
                teleport.Action(loc.Label, loc.Desc, _ => Teleport(target, loc.Label));
            }
            root.Sub("Işınlanma", "Hazır konumlar ve harita işareti.", teleport);

            var vehicles = new Menu("Araçlar", "Ver, değiştir, düzenle");
            var spawn = new Menu("Araç ver", "Önünde oluşur ve bindirilirsin");
            foreach (var v in Vehicles)
            {
                string model = v.Model;
                spawn.Action(v.Label, v.Desc, _ => SpawnVehicle(model, replaceCurrent: false));
            }
            vehicles.Sub("Araç ver", "Yeni araç oluştur ve sür.", spawn);
            var names = Array.ConvertAll(Vehicles, v => v.Label);
            vehicles.Choice("Aracı değiştir", "←/→ ile model seç, Enter: bindiğin aracı hızını koruyarak değiştir.", names, 0,
                it => SpawnVehicle(Vehicles[it.Index].Model, replaceCurrent: true));
            vehicles.Action("Tamir et", "Bindiğin aracı tamir et.", _ => WithVehicle(v => { v.Repair(); Ui.Toast("success", "Araç tamir edildi"); }));
            vehicles.Action("Rastgele renk", "", _ => WithVehicle(v => Native.SetColors(v, _random.Next(0, 160), _random.Next(0, 160))));
            vehicles.Action("Tam performans", "Motor, fren, şanzıman, süspansiyon, zırh, turbo.", _ => WithVehicle(v =>
            {
                Native.MaxPerformanceMods(v);
                Ui.Toast("success", "Modifiye edildi", "En üst performans parçaları takıldı.");
            }));
            vehicles.Action("Aracı sil", "", _ => WithVehicle(v => v.Delete()));
            root.Sub("Araçlar", "Araç ver, değiştir, tamir et, modifiye et.", vehicles);

            var models = new Menu("Oyuncu modeli", "Karakter değiştir");
            foreach (var m in PlayerModels)
            {
                string model = m.Model;
                string label = m.Label;
                models.Action(label, model, _ => ChangePlayerModel(model, label));
            }
            root.Sub("Oyuncu modeli", "Farklı bir karaktere dönüş.", models);

            var player = new Menu("Oyuncu", "Durum");
            player.Action("Can ve zırhı doldur", "", _ =>
            {
                Ped p = Game.Player.Character;
                p.Health = p.MaxHealth;
                p.Armor = 100;
                Ui.Toast("success", "Can ve zırh dolu", null, "heart-f");
            });
            player.Toggle("Ölümsüzlük", "Hasar almazsın.", GodMode, it =>
            {
                GodMode = it.Check == true;
                if (!GodMode) Game.Player.Character.IsInvincible = false;
            });
            player.Choice("Aranma seviyesi", "←/→ seç, Enter uygula.", new[] { "0", "1", "2", "3", "4", "5" }, 0,
                it => Native.SetWantedLevel(Game.Player, it.Index));
            player.Action("Silahları ver", "Tabanca, karabina, pompalı, keskin nişancı, RPG.", _ =>
            {
                Ped p = Game.Player.Character;
                foreach (WeaponHash w in new[] { WeaponHash.Pistol, WeaponHash.CarbineRifle, WeaponHash.PumpShotgun, WeaponHash.SniperRifle, WeaponHash.RPG })
                {
                    p.Weapons.Give(w, 500, false, true);
                }
                Ui.Toast("info", "Silahlar verildi", null, "rifle");
            });
            player.Action("+$100.000", "Para HUD'unu dener.", _ => Game.Player.Money += 100000);
            root.Sub("Oyuncu", "Can, ölümsüzlük, aranma, silah, para.", player);

            var world = new Menu("Dünya", "Zaman, hava, kalabalık");
            world.Choice("Saat", "←/→ seç, Enter uygula.", Hours, 1, it => Native.SetClockTime(new[] { 6, 12, 18, 0 }[it.Index]));
            world.Choice("Hava", "←/→ seç, Enter uygula.", WeatherLabels, 0, it => Native.SetWeather(WeatherTypes[it.Index]));
            world.Action("Kalabalık oluştur", "Çevrene 25 yaya ve 15 sürücülü araç ekler (etiket stres testi).", _ => SpawnCrowd(25, 15));
            world.Action("Oluşturulanları temizle", "Trainer'ın oluşturduğu yaya ve araçları siler.", _ => Cleanup());
            root.Sub("Dünya", "Saat, hava, kalabalık.", world);

            var perf = new Menu("Performans testi", "Dünya etiketleri ve gecikme");
            perf.Toggle("Dünya etiketleri", "Çevredeki yaya ve araçların üstünde MHud etiketi.", _tags.Enabled, it =>
            {
                _tags.Enabled = it.Check == true;
                if (!_tags.Enabled) _tags.Clear();
            });
            perf.Choice("Konumlandırma", "Atlas: etiketi oyun kendi karesinde yerleştirir (gecikmesiz). HTML: konum sayfaya " +
                "gider, MHud DOM'u taşır (FiveM yolu, birkaç kare gecikir). Karşılaştırmak için değiştir.", PositioningOptions, 0,
                it => _tags.Positioning = (WorldTags.Mode)it.Index);
            perf.Toggle("Native referans noktaları", "Oyunun dünya koordinatından kendisinin çizdiği kırmızı noktalar (en yakın 30). " +
                "Etiketin alt ucu noktada durmalı; dönerken aradaki kayma gecikmedir.", _tags.NativeReferences,
                it => _tags.NativeReferences = it.Check == true);
            perf.Choice("Senkron gecikmesi (atlas)", "Etiketler noktaların ÖNÜNDE gidiyorsa 1 kare yap. Noktalarla birebir oturan değeri seç.",
                DelayOptions, 0, it => OverlayBridge.SpriteDelay = it.Index);
            perf.Choice("Öngörü (atlas)", "Etiketler noktaların ARKASINDAN geliyorsa aç: ekran hızından 1 kare ileri tahmin.",
                PredictOptions, 0, it => _tags.PredictFrames = it.Index);
            perf.Choice("Mesafe", "Etiket yarıçapı. En büyük maliyet kaldıracı.", RadiusOptions, 2, it => _tags.Radius = RadiusValues[it.Index]);
            perf.Choice("En fazla etiket", "", MaxOptions, 2, it => _tags.MaxCount = MaxValues[it.Index]);
            perf.Choice("Gönderim sıklığı (HTML)", "HTML modunda etiket mesajı her karede mi, daha seyrek mi gitsin.", RateOptions, 0, it => _tags.RateHz = RateValues[it.Index]);
            perf.Choice("Hedef", "", TargetOptions, 0, it => _tags.Target = (WorldTags.Filter)it.Index);
            perf.Choice("Mesafe yazısı adımı", "MHud mesafe yazısı değişince etiketi baştan çizer. 1 m: her harekette çizim " +
                "(pahalı), 5-10 m: çoğu güncelleme yalnız konum (ucuz).", DistStepOptions, 1, it => _tags.DistanceStep = DistStepValues[it.Index]);
            perf.Choice("Kamerayı döndür", "Kamerayı sabit hızla döndürür: senkron testi elle uğraşmadan.", SpinOptions, 0,
                it => CameraSpinDegreesPerSecond = SpinValues[it.Index]);
            perf.Toggle("Performans paneli", "", ShowPerfPanel, it => ShowPerfPanel = it.Check == true);
            perf.Action("Kalabalık oluştur (büyük)", "60 yaya + 30 araç. Dikkat: FPS düşebilir.", _ => SpawnCrowd(60, 30));
            perf.Action("Oluşturulanları temizle", "", _ => Cleanup());
            root.Sub("Performans testi", "Etiket sayısı, mesafe, sıklık, kamera döndürme.", perf);

            var hud = new Menu("HUD", "MHud görünümü");
            hud.Choice("Tema", "←/→ seç, Enter uygula.", Themes, 0, it =>
            {
                _hud.Theme = Themes[it.Index];
                _hud.PushConfig();
            });
            hud.Toggle("GTA HUD parçalarını gizle", "MHud'un yerini aldığı aranma, para, sokak adı vb.", _hud.HideGtaHud,
                it => _hud.HideGtaHud = it.Check == true);
            hud.Action("Bildirim vitrini", "MHud'un bildirim, hediye, duyuru örnekleri.", _ =>
            {
                Ui.Begin("mhud:demo").BeginObject().EndObject();
                Ui.Send();
            });
            root.Sub("HUD", "Tema ve vitrin.", hud);

            root.Action("Kapat", "F5 ile tekrar açılır.", _ => _menus.Close());
            return root;
        }

        // ------------------------------------------------------------------ actions

        private static bool LoadModel(Model model, string name)
        {
            if (!model.IsInCdImage || !model.IsValid)
            {
                Ui.Toast("danger", "Model bulunamadı", name);
                return false;
            }
            if (!model.Request(3000))
            {
                Ui.Toast("danger", "Model yüklenemedi", name);
                return false;
            }
            return true;
        }

        private void Teleport(Vector3 target, string label)
        {
            Ped p = Game.Player.Character;
            Entity e = p.IsInVehicle() ? (Entity)p.CurrentVehicle : p;
            Native.RequestCollisionAt(target);
            e.Position = target;
            Ui.Toast("info", "Işınlandın", label, "pin-f");
        }

        private void TeleportToWaypoint()
        {
            if (!Game.IsWaypointActive)
            {
                Ui.Toast("warn", "Harita işareti yok", "Önce haritada bir nokta işaretle.");
                return;
            }
            Vector3 wp = World.WaypointPosition;
            Ped p = Game.Player.Character;
            Entity e = p.IsInVehicle() ? (Entity)p.CurrentVehicle : p;

            // Collision streams in around the new position; probe the ground for up to ~2.5 s
            float z = 1000f;
            bool found = false;
            for (int i = 0; i < 50 && !found; i++)
            {
                e.Position = new Vector3(wp.X, wp.Y, z);
                Native.RequestCollisionAt(e.Position);
                Script.Wait(50);
                found = Native.GroundZ(wp.X, wp.Y, 1000f, out float ground);
                if (found) z = ground + 1f;
            }
            e.Position = new Vector3(wp.X, wp.Y, found ? z : 200f);
            Ui.Toast(found ? "info" : "warn", "Işınlandın", found ? "Harita işareti" : "Zemin bulunamadı, havadasın.", "pin-f");
        }

        private void SpawnVehicle(string modelName, bool replaceCurrent)
        {
            var model = new Model(modelName);
            if (!LoadModel(model, modelName)) return;

            Ped p = Game.Player.Character;
            Vehicle old = replaceCurrent ? p.CurrentVehicle : null;
            if (replaceCurrent && old == null)
            {
                Ui.Toast("warn", "Araçta değilsin", "Değiştirmek için önce bir araca bin.");
                model.MarkAsNoLongerNeeded();
                return;
            }

            Vector3 pos;
            float heading;
            Vector3 velocity = Vector3.Zero;
            if (old != null)
            {
                pos = old.Position;
                heading = old.Heading;
                velocity = old.Velocity;
                _spawnedVehicles.Remove(old);
                old.Delete();
            }
            else
            {
                pos = p.Position + p.ForwardVector * 5f;
                heading = p.Heading + 90f;
            }

            Vehicle v = World.CreateVehicle(model, pos, heading);
            model.MarkAsNoLongerNeeded();
            if (v == null)
            {
                Ui.Toast("danger", "Araç oluşturulamadı", modelName);
                return;
            }
            if (old == null) v.PlaceOnGround();
            Native.SetPlate(v, "STRMEMBR");
            p.SetIntoVehicle(v, VehicleSeat.Driver);
            if (old != null) v.Velocity = velocity;
            Track(_spawnedVehicles, v, 6);
            Ui.Toast("success", replaceCurrent ? "Araç değiştirildi" : "Araç hazır", Native.VehicleDisplayName(v.Model), "car");
        }

        private void WithVehicle(Action<Vehicle> action)
        {
            Vehicle v = Game.Player.Character.CurrentVehicle;
            if (v == null || !v.Exists())
            {
                Ui.Toast("warn", "Araçta değilsin");
                return;
            }
            action(v);
        }

        private void ChangePlayerModel(string modelName, string label)
        {
            var model = new Model(modelName);
            if (!LoadModel(model, modelName)) return;
            bool ok = Game.Player.ChangeModel(model);
            model.MarkAsNoLongerNeeded();
            if (!ok)
            {
                Ui.Toast("danger", "Model değiştirilemedi", label);
                return;
            }
            Native.SetDefaultComponents(Game.Player.Character);
            Ui.Toast("success", "Yeni karakter", label, "user");
        }

        private void SpawnCrowd(int pedCount, int vehicleCount)
        {
            Ped player = Game.Player.Character;
            Vector3 center = player.Position;
            int peds = 0, vehicles = 0;

            for (int i = 0; i < pedCount; i++)
            {
                Ped ped = World.CreateRandomPed(Around(center, 6f, 25f));
                if (ped == null) continue;
                ped.Task.Wander();
                Track(_spawnedPeds, ped, 400);
                peds++;
            }

            for (int i = 0; i < vehicleCount; i++)
            {
                var model = new Model(Vehicles[_random.Next(0, 8)].Model);  // cars only
                if (!model.Request(1500)) continue;
                Vector3 pos = World.GetNextPositionOnStreet(Around(center, 20f, 60f));
                Vehicle v = World.CreateVehicle(model, pos, (float)_random.NextDouble() * 360f);
                model.MarkAsNoLongerNeeded();
                if (v == null) continue;
                Ped driver = v.CreateRandomPedOnSeat(VehicleSeat.Driver);
                driver?.Task.CruiseWithVehicle(v, 18f, VehicleDrivingFlags.DrivingModeStopForVehicles);
                Track(_spawnedVehicles, v, 400);
                if (driver != null) Track(_spawnedPeds, driver, 400);
                vehicles++;
            }
            Ui.Toast("info", "Kalabalık oluşturuldu", peds + " yaya, " + vehicles + " araç", "users");
        }

        private void Cleanup()
        {
            int n = 0;
            foreach (Ped p in _spawnedPeds) if (p != null && p.Exists()) { p.Delete(); n++; }
            foreach (Vehicle v in _spawnedVehicles)
            {
                if (v == null || !v.Exists() || v == Game.Player.Character.CurrentVehicle) continue;
                v.Delete();
                n++;
            }
            _spawnedPeds.Clear();
            _spawnedVehicles.RemoveAll(v => v == null || !v.Exists());
            Ui.Toast("info", "Temizlendi", n + " varlık silindi.");
        }

        private Vector3 Around(Vector3 center, float minDist, float maxDist)
        {
            double angle = _random.NextDouble() * Math.PI * 2;
            float dist = minDist + (float)_random.NextDouble() * (maxDist - minDist);
            return center + new Vector3((float)Math.Cos(angle) * dist, (float)Math.Sin(angle) * dist, 0f);
        }

        private static void Track<T>(List<T> list, T entity, int max) where T : Entity
        {
            list.Add(entity);
            while (list.Count > max)
            {
                list[0]?.MarkAsNoLongerNeeded();
                list.RemoveAt(0);
            }
        }

        // ------------------------------------------------------------------ per tick

        public void Tick(Ped player, float frameSeconds)
        {
            if (GodMode) player.IsInvincible = true;

            if (CameraSpinDegreesPerSecond > 0)
            {
                float h = Native.GameplayCamRelativeHeading() + CameraSpinDegreesPerSecond * frameSeconds;
                if (h > 180f) h -= 360f;
                Native.SetGameplayCamRelativeHeading(h);
            }
        }

        public void Shutdown()
        {
            Game.Player.Character.IsInvincible = false;
        }
    }
}
