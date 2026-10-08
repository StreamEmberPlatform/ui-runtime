// RDR2 trainer actions and menu tree (same structure as the GTA V demo).
using System;
using System.Collections.Generic;
using RDR2;
using RDR2.Math;
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

        // X/Y of the town centers; Z is found by probing the ground after the jump
        private static readonly (string Label, float X, float Y, string Desc)[] Locations =
        {
            ("Valentine", -281.0f, 793.0f, "Kalabalık kasaba: etiket stres testi için iyi."),
            ("Saint Denis", 2635.0f, -1225.0f, "En büyük şehir: çok sayıda yaya ve araba."),
            ("Rhodes", 1225.0f, -1300.0f, ""),
            ("Blackwater", -813.0f, -1324.0f, ""),
            ("Strawberry", -1791.0f, -386.0f, "Dağ kasabası."),
            ("Annesburg", 2935.0f, 1300.0f, "Maden kasabası."),
            ("Van Horn", 2985.0f, 565.0f, ""),
            ("Emerald Ranch", 1420.0f, 315.0f, ""),
            ("Armadillo", -3685.0f, -2620.0f, "Çöl (New Austin)."),
            ("Tumbleweed", -5512.0f, -2937.0f, "Çöl (New Austin)."),
        };

        private static readonly (string Label, PedHash Model, string Desc)[] Horses =
        {
            ("Arap (beyaz)", PedHash.a_c_horse_arabian_white, "En hızlı atlardan"),
            ("Türkmen (altın)", PedHash.a_c_horse_turkoman_gold, "Dayanıklı savaş atı"),
            ("Missouri Fox Trotter", PedHash.a_c_horse_missourifoxtrotter_amberchampagne, ""),
            ("Safkan (siyah)", PedHash.a_c_horse_thoroughbred_blackchestnut, ""),
        };

        private static readonly (string Label, VehicleHash Model, string Desc)[] Vehicles =
        {
            ("Posta arabası", VehicleHash.StageCoach001X, "Atlı posta arabası"),
            ("Fayton", VehicleHash.Coach2, ""),
            ("Hafif fayton (buggy)", VehicleHash.Buggy01, ""),
            ("Yük arabası", VehicleHash.Cart01, ""),
            ("Ordu erzak arabası", VehicleHash.ArmySupplyWagon, ""),
            ("Kano", VehicleHash.Canoe, "Suda dene"),
            ("Sandal", VehicleHash.RowBoat, "Suda dene"),
            ("Sıcak hava balonu", VehicleHash.HotAirBalloon01, ""),
        };

        private static readonly (string Label, PedHash Model)[] PlayerModels =
        {
            ("Arthur", PedHash.player_zero), ("John", PedHash.player_three), ("Dutch", PedHash.cs_dutch),
            ("Micah", PedHash.cs_micahbell), ("Javier", PedHash.cs_javierescuella), ("Charles", PedHash.cs_charlessmith),
            ("Sadie", PedHash.cs_mrsadler), ("Şerif yardımcısı", PedHash.s_m_m_valdeputy_01),
            ("Kasabalı", PedHash.a_m_m_valtownfolk_01), ("Ayı", PedHash.a_c_bear_01), ("Kurt", PedHash.a_c_wolf),
            ("Geyik", PedHash.a_c_deer_01),
        };

        private static readonly PedHash[] Townsfolk =
        {
            PedHash.a_m_m_valtownfolk_01, PedHash.a_m_m_valtownfolk_02, PedHash.a_f_m_valtownfolk_01,
            PedHash.a_m_m_rhdtownfolk_01, PedHash.a_m_m_rhdtownfolk_02, PedHash.a_f_m_rhdtownfolk_01,
            PedHash.a_m_m_blwtownfolk_01, PedHash.a_f_m_blwtownfolk_01, PedHash.a_m_m_middlesdtownfolk_01,
            PedHash.a_f_m_middlesdtownfolk_01,
        };

        private static readonly (string Label, eWeapon Weapon)[] Weapons =
        {
            ("Cattleman revolver", eWeapon.RevolverCattleman), ("Volcanic tabanca", eWeapon.PistolVolcanic),
            ("Karabina (repeater)", eWeapon.RepeaterCarbine), ("Springfield tüfek", eWeapon.RifleSpringfield),
            ("Pompalı", eWeapon.ShotgunPump), ("Rolling Block", eWeapon.SniperRifleRollingblock),
            ("Yay", eWeapon.Bow), ("Kement", eWeapon.Lasso),
        };

        private static readonly string[] Themes = { "frontier", "modern", "neon", "tactical", "minimal" };
        private static readonly string[] WeatherLabels = { "Güneşli", "Bulutlu", "Yağmurlu", "Fırtınalı", "Sisli", "Karlı" };
        private static readonly string[] WeatherTypes = { "SUNNY", "CLOUDS", "RAIN", "THUNDERSTORM", "FOG", "SNOW" };
        private static readonly string[] Hours = { "06:00", "12:00", "18:00", "00:00" };
        private static readonly string[] RadiusOptions = { "25 m", "50 m", "100 m", "200 m", "400 m" };
        private static readonly float[] RadiusValues = { 25, 50, 100, 200, 400 };
        private static readonly string[] MaxOptions = { "25", "50", "100", "200", "400" };
        private static readonly int[] MaxValues = { 25, 50, 100, 200, 400 };
        private static readonly string[] RateOptions = { "Her kare", "30 Hz", "15 Hz" };
        private static readonly int[] RateValues = { 0, 30, 15 };
        private static readonly string[] TargetOptions = { "Hepsi", "Yalnız insan/hayvan", "Yalnız araba/kayık" };
        private static readonly string[] PositioningOptions = { "Atlas (kare senkron)", "HTML (MHud/RedM yolu)" };
        private static readonly string[] DelayOptions = { "0 kare", "1 kare", "2 kare" };
        private static readonly string[] PredictOptions = { "Kapalı", "1 kare" };
        private static readonly string[] DistStepOptions = { "1 m (MHud varsayılanı)", "5 m", "10 m" };
        private static readonly int[] DistStepValues = { 1, 5, 10 };
        private static readonly string[] SpinOptions = { "Kapalı", "45°/sn", "90°/sn", "180°/sn" };
        private static readonly float[] SpinValues = { 0, 45, 90, 180 };

        // ------------------------------------------------------------------ menus

        private Menu BuildMenus()
        {
            var root = new Menu("StreamEmber Trainer", "RDR2 · Overlay + MHud testi");

            var teleport = new Menu("Işınlanma", "Kasaba seç");
            teleport.Action("Haritadaki işarete", "Haritada işaret koyduğun noktaya ışınlan.", _ => TeleportToWaypoint());
            foreach (var loc in Locations)
            {
                var l = loc;
                teleport.Action(l.Label, l.Desc, _ => TeleportTo(l.X, l.Y, l.Label));
            }
            root.Sub("Işınlanma", "Kasabalar ve harita işareti.", teleport);

            var horses = new Menu("Atlar", "Yanında oluşur ve binersin");
            foreach (var h in Horses)
            {
                var model = h.Model;
                var label = h.Label;
                horses.Action(label, h.Desc, _ => SpawnHorse(model, label));
            }
            root.Sub("Atlar", "At ver ve bin.", horses);

            var vehicles = new Menu("Arabalar ve kayıklar", "Ver, düzenle");
            foreach (var v in Vehicles)
            {
                var model = v.Model;
                var label = v.Label;
                vehicles.Action(label, v.Desc, _ => SpawnVehicle(model, label));
            }
            vehicles.Action("Tamir et", "Bindiğin arabayı tamir et.", _ => WithVehicle(v => { v.Repair(); Ui.Toast("success", "Araba tamir edildi"); }));
            vehicles.Action("Arabayı sil", "", _ => WithVehicle(v => v.Delete()));
            root.Sub("Arabalar ve kayıklar", "Posta arabası, fayton, kano, balon.", vehicles);

            var models = new Menu("Oyuncu modeli", "Karakter değiştir");
            foreach (var m in PlayerModels)
            {
                var model = m.Model;
                var label = m.Label;
                models.Action(label, model.ToString(), _ => ChangePlayerModel(model, label));
            }
            root.Sub("Oyuncu modeli", "Çeteden biri, kasabalı ya da bir hayvan.", models);

            var player = new Menu("Oyuncu", "Durum");
            player.Action("Can, dayanıklılık, dead eye", "Barları ve çekirdekleri doldurur.", _ =>
            {
                Ped p = Game.Player.Character;
                p.Health = p.MaxHealth;
                for (int i = 0; i < 3; i++) Native.SetCore(p, i, 100);
                Native.RestoreStamina(p);
                Ui.Toast("success", "Dolduruldu", "Can, dayanıklılık, dead eye", "heart-f");
            });
            player.Toggle("Ölümsüzlük", "Hasar almazsın.", GodMode, it =>
            {
                GodMode = it.Check == true;
                if (!GodMode) Game.Player.Character.IsInvincible = false;
            });
            player.Action("Ödül ve arananlığı temizle", "", _ =>
            {
                Native.ClearWanted(Game.Player);
                Game.Player.Bounty = 0;
                Ui.Toast("info", "Temiz", "Ödül ve arananlık sıfırlandı.", "sheriff");
            });
            var weapons = new Menu("Silah ver", "Mermisiyle");
            foreach (var w in Weapons)
            {
                var weapon = w.Weapon;
                var label = w.Label;
                weapons.Action(label, "", _ =>
                {
                    Game.Player.Character.Weapons.Give((uint)weapon, 100);
                    Ui.Toast("info", "Silah verildi", label, "revolver");
                });
            }
            weapons.Action("Hepsini ver", "", _ =>
            {
                foreach (var w in Weapons) Game.Player.Character.Weapons.Give((uint)w.Weapon, 100);
                Ui.Toast("info", "Silahlar verildi", null, "rifle");
            });
            player.Sub("Silah ver", "Revolver, tüfek, pompalı, yay, kement.", weapons);
            player.Action("+$100", "Para HUD'unu dener.", _ => Game.Player.Money += 10000);  // cents
            root.Sub("Oyuncu", "Can, ölümsüzlük, ödül, silah, para.", player);

            var world = new Menu("Dünya", "Zaman, hava, kalabalık");
            world.Choice("Saat", "←/→ seç, Enter uygula.", Hours, 1, it => Native.SetClockTime(new[] { 6, 12, 18, 0 }[it.Index]));
            world.Choice("Hava", "←/→ seç, Enter uygula.", WeatherLabels, 0, it => Native.SetWeather(WeatherTypes[it.Index]));
            world.Action("Kalabalık oluştur", "Çevrene 25 kasabalı ve 6 sürücülü araba ekler (etiket stres testi).", _ => SpawnCrowd(25, 6));
            world.Action("Oluşturulanları temizle", "Trainer'ın oluşturduğu kişi ve arabaları siler.", _ => Cleanup());
            root.Sub("Dünya", "Saat, hava, kalabalık.", world);

            var perf = new Menu("Performans testi", "Dünya etiketleri ve gecikme");
            perf.Toggle("Dünya etiketleri", "Çevredeki kişi, at, hayvan ve arabaların üstünde MHud etiketi.", _tags.Enabled, it =>
            {
                _tags.Enabled = it.Check == true;
                if (!_tags.Enabled) _tags.Clear();
            });
            perf.Choice("Konumlandırma", "Atlas: etiketi oyun kendi karesinde yerleştirir (gecikmesiz). HTML: konum sayfaya " +
                "gider, MHud DOM'u taşır (RedM yolu, birkaç kare gecikir). Karşılaştırmak için değiştir.", PositioningOptions, 0,
                it => _tags.Positioning = (WorldTags.Mode)it.Index);
            perf.Toggle("Native referans noktaları", "Oyunun kendi 3B çizdiği kırmızı küreler (en yakın 30). " +
                "Etiketin alt ucu kürede durmalı; dönerken aradaki kayma gecikmedir.", _tags.NativeReferences,
                it => _tags.NativeReferences = it.Check == true);
            perf.Choice("Senkron gecikmesi (atlas)", "Etiketler kürelerin ÖNÜNDE gidiyorsa 1 kare yap. Kürelerle birebir oturan değeri seç.",
                DelayOptions, 0, it => OverlayBridge.SpriteDelay = it.Index);
            perf.Choice("Öngörü (atlas)", "Etiketler kürelerin ARKASINDAN geliyorsa aç: ekran hızından 1 kare ileri tahmin.",
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
            perf.Action("Kalabalık oluştur (büyük)", "60 kasabalı + 12 araba. Dikkat: FPS düşebilir.", _ => SpawnCrowd(60, 12));
            perf.Action("Oluşturulanları temizle", "", _ => Cleanup());
            root.Sub("Performans testi", "Etiket sayısı, mesafe, sıklık, kamera döndürme.", perf);

            var hud = new Menu("HUD", "MHud görünümü");
            hud.Choice("Tema", "←/→ seç, Enter uygula.", Themes, 0, it =>
            {
                _hud.Theme = Themes[it.Index];
                _hud.PushConfig();
            });
            hud.Toggle("RDR2 HUD'unu gizle", "Oyunun kendi HUD'unu (çekirdekler, mini harita) kapatır; yalnız MHud kalır.",
                _hud.HideGameHud, it => _hud.HideGameHud = it.Check == true);
            hud.Action("Bildirim vitrini", "MHud'un bildirim, hediye, duyuru örnekleri.", _ =>
            {
                Ui.Begin("mhud:demo").BeginObject().Prop("game", "redm").EndObject();
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

        /// <summary>The entity that moves with the player: wagon, horse, or the player itself.</summary>
        private static Entity MovingEntity(Ped p)
        {
            if (p.IsInVehicle) return p.CurrentVehicle;
            if (p.IsOnMount && p.CurrentMount != null) return p.CurrentMount;
            return p;
        }

        private void TeleportTo(float x, float y, string label)
        {
            bool found = ProbeAndPlace(x, y);
            Ui.Toast(found ? "info" : "warn", "Işınlandın", found ? label : label + ": zemin bulunamadı, havadasın.", "pin-f");
        }

        private void TeleportToWaypoint()
        {
            if (!World.IsWaypointActive)
            {
                Ui.Toast("warn", "Harita işareti yok", "Önce haritada bir nokta işaretle.");
                return;
            }
            Vector3 wp = World.WaypointPosition;
            bool found = ProbeAndPlace(wp.X, wp.Y);
            Ui.Toast(found ? "info" : "warn", "Işınlandın", found ? "Harita işareti" : "Zemin bulunamadı, havadasın.", "pin-f");
        }

        // Collision streams in around the new position; probe the ground for up to ~2.5 s
        private static bool ProbeAndPlace(float x, float y)
        {
            Entity e = MovingEntity(Game.Player.Character);
            float z = 1000f;
            bool found = false;
            for (int i = 0; i < 50 && !found; i++)
            {
                e.Position = new Vector3(x, y, z);
                Native.RequestCollisionAt(e.Position);
                Script.Wait(50);
                found = Native.GroundZ(x, y, 1000f, out float ground);
                if (found) z = ground + 1f;
            }
            e.Position = new Vector3(x, y, found ? z : 300f);
            return found;
        }

        private void SpawnHorse(PedHash hash, string label)
        {
            Ped p = Game.Player.Character;
            if (p.IsInVehicle)
            {
                Ui.Toast("warn", "Arabadasın", "Ata binmek için önce in.");
                return;
            }
            Vector3 pos = p.Position + p.RightVector * 2f;
            Ped horse = World.CreatePed(hash, pos, p.Heading);
            if (horse == null)
            {
                Ui.Toast("danger", "At oluşturulamadı", label);
                return;
            }
            Native.Mount(p, horse);
            Track(_spawnedPeds, horse, 8);
            Ui.Toast("success", "At hazır", label, "horse");
        }

        private void SpawnVehicle(VehicleHash hash, string label)
        {
            Ped p = Game.Player.Character;
            Vector3 pos = p.Position + p.ForwardVector * 6f;
            Vehicle v = World.CreateVehicle(hash, pos, p.Heading + 90f);
            if (v == null)
            {
                Ui.Toast("danger", "Araba oluşturulamadı", label);
                return;
            }
            v.PlaceOnGround();
            p.SetIntoVehicle(v, eVehicleSeat.Driver);
            Track(_spawnedVehicles, v, 6);
            Ui.Toast("success", "Hazır", label, "cart");
        }

        private void WithVehicle(Action<Vehicle> action)
        {
            Vehicle v = Game.Player.Character.CurrentVehicle;
            if (v == null || !v.Exists())
            {
                Ui.Toast("warn", "Arabada değilsin");
                return;
            }
            action(v);
        }

        private void ChangePlayerModel(PedHash hash, string label)
        {
            var model = new Model(hash);
            if (!LoadModel(model, label)) return;
            bool ok = Game.Player.ChangeModel(model);
            if (!ok)
            {
                Ui.Toast("danger", "Model değiştirilemedi", label);
                return;
            }
            Script.Wait(0);
            Native.RandomOutfit(Game.Player.Character);  // without an outfit the new model is invisible
            Ui.Toast("success", "Yeni karakter", label, "user");
        }

        private void SpawnCrowd(int pedCount, int vehicleCount)
        {
            Ped player = Game.Player.Character;
            Vector3 center = player.Position;
            int peds = 0, vehicles = 0;

            for (int i = 0; i < pedCount; i++)
            {
                Vector3 pos = Around(center, 6f, 25f);
                if (Native.GroundZ(pos.X, pos.Y, center.Z + 20f, out float gz)) pos = new Vector3(pos.X, pos.Y, gz + 0.5f);
                Ped ped = World.CreatePed(Townsfolk[_random.Next(Townsfolk.Length)], pos, (float)_random.NextDouble() * 360f);
                if (ped == null) continue;
                ped.Task.WanderAround();
                Track(_spawnedPeds, ped, 400);
                peds++;
            }

            for (int i = 0; i < vehicleCount; i++)
            {
                Vector3 pos = World.GetNextPositionOnStreet(Around(center, 20f, 60f));
                VehicleHash hash = _random.Next(2) == 0 ? VehicleHash.StageCoach001X : VehicleHash.Cart01;
                Vehicle v = World.CreateVehicle(hash, pos, (float)_random.NextDouble() * 360f);
                if (v == null) continue;
                Ped driver = v.CreatePedOnSeat(eVehicleSeat.Driver, new Model(Townsfolk[_random.Next(Townsfolk.Length)]));
                driver?.Task.DriveWander(v, 6f, eDrivingFlags.DF_StopForCars | eDrivingFlags.DF_StopForPeds | eDrivingFlags.DF_SteerAroundPeds);
                Track(_spawnedVehicles, v, 400);
                if (driver != null) Track(_spawnedPeds, driver, 400);
                vehicles++;
            }
            Ui.Toast("info", "Kalabalık oluşturuldu", peds + " kişi, " + vehicles + " araba", "users");
        }

        private void Cleanup()
        {
            int n = 0;
            Ped me = Game.Player.Character;
            Ped myMount = me.CurrentMount;
            Vehicle myVehicle = me.CurrentVehicle;
            foreach (Ped p in _spawnedPeds)
            {
                if (p == null || !p.Exists() || (myMount != null && p.Handle == myMount.Handle)) continue;
                p.Delete();
                n++;
            }
            foreach (Vehicle v in _spawnedVehicles)
            {
                if (v == null || !v.Exists() || (myVehicle != null && v.Handle == myVehicle.Handle)) continue;
                v.Delete();
                n++;
            }
            _spawnedPeds.RemoveAll(p => p == null || !p.Exists());
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
            _hud.Shutdown();
        }
    }
}
