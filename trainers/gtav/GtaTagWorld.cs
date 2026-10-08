// GTA V side of the world tags: nearby peds/vehicles, projection, MHud tag content, SET_DRAW_ORIGIN reference dots.
using System;
using System.Collections.Generic;
using GTA;
using GTA.Math;
using GTA.Native;

namespace StreamEmber.Trainers
{
    internal sealed class GtaTagWorld : ITagWorld
    {
        private readonly Dictionary<int, string> _vehicleLabels = new Dictionary<int, string>();   // by model hash
        private readonly Dictionary<int, float> _vehicleHeights = new Dictionary<int, float>();    // by model hash
        private readonly OutputArgument _resX = new OutputArgument();
        private readonly OutputArgument _resY = new OutputArgument();

        public void Collect(List<TagCandidate> output, float radius, WorldTags.Filter target)
        {
            Ped player = Game.Player.Character;
            Vector3 me = player.Position;
            Vehicle myVehicle = player.CurrentVehicle;

            if (target != WorldTags.Filter.Vehicles)
            {
                foreach (Ped ped in World.GetNearbyPeds(player, radius))
                {
                    if (ped == null || !ped.Exists()) continue;
                    Vector3 p = ped.Position;
                    output.Add(new TagCandidate
                    {
                        Entity = ped,
                        Handle = ped.Handle,
                        IsPed = true,
                        Distance = p.DistanceTo(me),
                        AX = p.X, AY = p.Y, AZ = p.Z + 1.05f,
                    });
                }
            }
            if (target != WorldTags.Filter.Peds)
            {
                foreach (Vehicle veh in World.GetNearbyVehicles(me, radius))
                {
                    if (veh == null || !veh.Exists() || veh == myVehicle) continue;
                    Vector3 p = veh.Position;
                    output.Add(new TagCandidate
                    {
                        Entity = veh,
                        Handle = veh.Handle,
                        IsPed = false,
                        Distance = p.DistanceTo(me),
                        AX = p.X, AY = p.Y, AZ = p.Z + VehicleTop(veh) + 0.25f,
                    });
                }
            }
        }

        public bool WorldToScreen(float x, float y, float z, out float sx, out float sy)
            => Native.WorldToScreen(new Vector3(x, y, z), out sx, out sy);

        public void GetScreenSize(out int width, out int height)
        {
            Function.Call(Hash.GET_ACTUAL_SCREEN_RESOLUTION, _resX, _resY);
            width = _resX.GetResult<int>();
            height = _resY.GetResult<int>();
        }

        public void DrawReferences(List<TagCandidate> candidates, int max)
        {
            // SET_DRAW_ORIGIN lets the renderer project the world point itself in the frame it draws.
            int drawn = 0;
            foreach (TagCandidate c in candidates)
            {
                if (drawn++ >= max) break;
                Function.Call(Hash.SET_DRAW_ORIGIN, c.AX, c.AY, c.AZ, false);
                Native.DrawRect(0f, 0f, 0.0035f, 0.0062f, 255, 40, 60, 230);
                Function.Call(Hash.CLEAR_DRAW_ORIGIN);
            }
        }

        public string Signature(TagCandidate c, int dist)
        {
            if (c.IsPed)
            {
                var ped = (Ped)c.Entity;
                return "p|" + dist + "|" + PedHealthPercent(ped) + "|" + ped.Armor + "|" + (ped.IsDead ? 1 : 0) + "|" + ped.Model.Hash;
            }
            var veh = (Vehicle)c.Entity;
            return "v|" + dist + "|" + (int)(veh.EngineHealth / 10f) + "|" + veh.Model.Hash;
        }

        public void WriteFields(JsonWriter w, TagCandidate c)
        {
            if (c.IsPed)
            {
                var ped = (Ped)c.Entity;
                int type = Native.PedType(ped);
                bool cop = type == 6 || type == 27 || type == 29;
                bool animal = type == 28;
                w.Prop("name", cop ? "Polis" : animal ? "Hayvan" : ped.Gender == Gender.Female ? "Yaya (K)" : "Yaya (E)")
                    .Prop("icon", animal ? "dog" : "user")
                    .Prop("tone", cop ? "enemy" : "team1")
                    .Prop("health", PedHealthPercent(ped))
                    .Prop("dead", ped.IsDead);
                if (ped.Armor > 0) w.Prop("armor", Math.Min(100, ped.Armor));
            }
            else
            {
                var veh = (Vehicle)c.Entity;
                w.Prop("name", VehicleLabel(veh))
                    .Prop("icon", VehicleIcon(veh))
                    .Prop("tone", "team3")
                    .Prop("health", Clamp((int)(veh.EngineHealth / 10f), 0, 100))
                    .Prop("compact", true);
            }
        }

        /// <summary>Same health scale as MHud's FiveM resource: 100 = dead line, MaxHealth = full.</summary>
        public static int PedHealthPercent(Ped ped)
        {
            int max = ped.MaxHealth;
            if (max <= 100) return Clamp(ped.Health * 100 / Math.Max(1, max), 0, 100);
            return Clamp((ped.Health - 100) * 100 / Math.Max(1, max - 100), 0, 100);
        }

        private string VehicleLabel(Vehicle veh)
        {
            int hash = veh.Model.Hash;
            if (!_vehicleLabels.TryGetValue(hash, out string label))
            {
                label = Native.VehicleDisplayName(veh.Model);
                if (string.IsNullOrEmpty(label) || label == "NULL") label = "Araç";
                _vehicleLabels[hash] = label;
            }
            return label;
        }

        private float VehicleTop(Vehicle veh)
        {
            int hash = veh.Model.Hash;
            if (!_vehicleHeights.TryGetValue(hash, out float top))
            {
                veh.Model.GetDimensions(out Vector3 min, out Vector3 max);
                top = max.Z > 0.1f ? max.Z : 1.4f;
                _vehicleHeights[hash] = top;
            }
            return top;
        }

        private static string VehicleIcon(Vehicle veh)
        {
            Model m = veh.Model;
            if (m.IsHelicopter) return "heli";
            if (m.IsPlane) return "plane";
            if (m.IsBike || m.IsBicycle) return "bike";
            if (m.IsBoat) return "boat";
            return "car";
        }

        private static int Clamp(int v, int min, int max) => v < min ? min : v > max ? max : v;
    }
}
