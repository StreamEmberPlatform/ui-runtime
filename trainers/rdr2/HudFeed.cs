// Player HUD feed for RDR2: the same messages and intervals as MHud's RedM resource (client/main.lua):
// vitals with attribute cores, money, compass. No vehicle panel and no street/zone box in RDR2.
using System;
using System.Diagnostics;
using RDR2;

namespace StreamEmber.Trainers
{
    internal sealed class HudFeed
    {
        private const int VitalsMs = 150;
        private const int HeadingMs = 50;

        public bool HideGameHud;
        public string Theme = "frontier";
        public string Accent = "";

        private readonly Stopwatch _clock = Stopwatch.StartNew();
        private long _nextVitals, _nextHeading;
        private int _lastMoney = int.MinValue;
        private bool _gameHudHidden;

        public void PushConfig()
        {
            // game 'redm': MHud hides the armor row and uses the RDR2 compass letters
            Ui.Begin("mhud:config").BeginObject()
                .Prop("theme", Theme).Prop("accent", Accent).Prop("scale", 1)
                .Prop("game", "redm").Prop("compass", true).Prop("location", false).Prop("unit", "kmh")
                .EndObject();
            Ui.Send();
            Ui.Begin("mhud:vehicle").Value(false);
            Ui.Send();
            Ui.Begin("mhud:wanted").BeginObject().Prop("level", 0).EndObject();
            Ui.Send();
            _lastMoney = int.MinValue;
        }

        public void Tick(Player player, Ped ped)
        {
            if (HideGameHud != _gameHudHidden)
            {
                _gameHudHidden = HideGameHud;
                Native.ShowGameHud(!HideGameHud);
            }

            long now = _clock.ElapsedMilliseconds;
            if (now >= _nextVitals)
            {
                _nextVitals = now + VitalsMs;
                SendVitals(player, ped);
            }
            if (now >= _nextHeading)
            {
                _nextHeading = now + HeadingMs;
                float heading = (360f - Native.GameplayCamRotation().Z) % 360f;
                if (heading < 0) heading += 360f;
                Ui.Begin("mhud:heading").Value((int)Math.Round(heading) % 360);
                Ui.Send();
            }
        }

        public void Shutdown()
        {
            if (_gameHudHidden) Native.ShowGameHud(true);
            _gameHudHidden = false;
        }

        private void SendVitals(Player player, Ped ped)
        {
            Ui.Begin("mhud:vitals").BeginObject()
                .Prop("health", RdrTagWorld.HealthPercent(ped))
                .Prop("stamina", Native.StaminaPercent(ped))
                .Name("cores").BeginObject()
                    .Prop("health", Native.Clamp(Native.Core(ped, 0), 0, 100))
                    .Prop("stamina", Native.Clamp(Native.Core(ped, 1), 0, 100))
                    .Prop("deadeye", Native.Clamp(Native.Core(ped, 2), 0, 100))
                .EndObject()
                .EndObject();
            Ui.Send();

            // RDR2 keeps cash in cents; MHud shows whole dollars
            int money = player.Money / 100;
            if (money != _lastMoney)
            {
                _lastMoney = money;
                Ui.Begin("mhud:money").BeginObject().Prop("cash", money).EndObject();
                Ui.Send();
            }
        }
    }
}
