// Player HUD feed: the same messages and intervals as MHud's FiveM resource (client/main.lua), read from GTA V SP.
using System;
using System.Diagnostics;
using GTA;
using GTA.Math;

namespace StreamEmber.Trainers
{
    internal sealed class HudFeed
    {
        private const int VitalsMs = 150;
        private const int VehicleMs = 60;
        private const int LocationMs = 500;
        private const int HeadingMs = 50;

        // GTA's own HUD parts that MHud replaces: wanted stars, weapon icon, cash, MP cash, vehicle name,
        // area name, vehicle class, street name, cash change
        private static readonly int[] GtaHudComponents = { 1, 2, 3, 4, 6, 7, 8, 9, 13 };

        public bool HideGtaHud = true;
        public string Theme = "modern";
        public string Accent = "";

        private readonly Stopwatch _clock = Stopwatch.StartNew();
        private long _nextVitals, _nextVehicle, _nextLocation, _nextHeading;
        private bool _vehicleShown;
        private int _lastWanted = -1;
        private int _lastMoney = int.MinValue;

        public void PushConfig()
        {
            Ui.Begin("mhud:config").BeginObject()
                .Prop("theme", Theme).Prop("accent", Accent).Prop("scale", 1)
                .Prop("game", "gtav").Prop("compass", true).Prop("location", true).Prop("unit", "kmh")
                .EndObject();
            Ui.Send();
            _lastWanted = -1;
            _lastMoney = int.MinValue;
            _vehicleShown = true;  // forces a 'false' if not in a vehicle
        }

        public void Tick(Player player, Ped ped)
        {
            if (HideGtaHud)
            {
                foreach (int c in GtaHudComponents) Native.HideHudComponent(c);
            }

            long now = _clock.ElapsedMilliseconds;
            if (now >= _nextVitals)
            {
                _nextVitals = now + VitalsMs;
                SendVitals(player, ped);
            }
            if (now >= _nextVehicle)
            {
                _nextVehicle = now + VehicleMs;
                SendVehicle(ped);
            }
            if (now >= _nextLocation)
            {
                _nextLocation = now + LocationMs;
                SendLocation(ped.Position);
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

        private void SendVitals(Player player, Ped ped)
        {
            int stamina = Clamp((int)Math.Round(100f - Native.SprintStaminaUsed(player)), 0, 100);
            JsonWriter w = Ui.Begin("mhud:vitals").BeginObject()
                .Prop("health", GtaTagWorld.PedHealthPercent(ped))
                .Prop("armor", Clamp(ped.Armor, 0, 100))
                .Prop("stamina", stamina);
            if (Native.IsSwimmingUnderWater(ped))
            {
                w.Prop("oxygen", Clamp((int)Math.Round(Native.UnderwaterTimeRemaining(player) / 10f * 100f), 0, 100));
            }
            w.EndObject();
            Ui.Send();

            int wanted = Native.WantedLevel(player);
            if (wanted != _lastWanted)
            {
                _lastWanted = wanted;
                Ui.Begin("mhud:wanted").BeginObject().Prop("level", wanted).EndObject();
                Ui.Send();
            }

            int money = player.Money;
            if (money != _lastMoney)
            {
                _lastMoney = money;
                Ui.Begin("mhud:money").BeginObject().Prop("cash", money).EndObject();
                Ui.Send();
            }
        }

        private void SendVehicle(Ped ped)
        {
            Vehicle v = ped.CurrentVehicle;
            if (v == null || !v.Exists())
            {
                if (_vehicleShown)
                {
                    _vehicleShown = false;
                    Ui.Begin("mhud:vehicle").Value(false);
                    Ui.Send();
                }
                return;
            }
            _vehicleShown = true;
            Native.VehicleLights(v, out bool lights, out bool high);
            Ui.Begin("mhud:vehicle").BeginObject()
                .Prop("speed", (int)Math.Round(v.Speed * 3.6f))
                .Prop("rpm", v.CurrentRPM, "0.###")
                .Prop("gear", v.CurrentGear)
                .Prop("fuel", Clamp((int)Math.Round(v.FuelLevel / 65f * 100f), 0, 100))
                .Prop("engine", Clamp((int)Math.Round(v.EngineHealth / 10f), 0, 100))
                .Prop("lights", lights || high)
                .Prop("locked", Native.VehicleLocked(v))
                .Prop("name", Native.VehicleDisplayName(v.Model))
                .Prop("plate", Native.Plate(v))
                .EndObject();
            Ui.Send();
        }

        private static void SendLocation(Vector3 p)
        {
            Native.StreetNames(p, out string street, out string cross);
            JsonWriter w = Ui.Begin("mhud:location").BeginObject()
                .Prop("hours", GTA.Native.Function.Call<int>(GTA.Native.Hash.GET_CLOCK_HOURS))
                .Prop("minutes", GTA.Native.Function.Call<int>(GTA.Native.Hash.GET_CLOCK_MINUTES))
                .Prop("street", street ?? "").Prop("zone", Native.ZoneName(p));
            if (!string.IsNullOrEmpty(cross)) w.Prop("cross", cross);
            w.EndObject();
            Ui.Send();
        }

        private static int Clamp(int v, int min, int max) => v < min ? min : v > max ? max : v;
    }
}
