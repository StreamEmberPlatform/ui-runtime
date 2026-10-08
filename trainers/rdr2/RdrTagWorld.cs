// RDR2 side of the world tags: peds (people, horses, animals) and vehicles (wagons, coaches, boats) around the
// player, projection, MHud tag content, and game-rendered marker spheres as the sync reference.
using System;
using System.Collections.Generic;
using RDR2;
using RDR2.Math;

namespace StreamEmber.Trainers
{
    internal sealed class RdrTagWorld : ITagWorld
    {
        private enum Kind { Human = 0, Horse = 1, Animal = 2 }

        private readonly Dictionary<int, float> _modelTops = new Dictionary<int, float>();      // by model hash
        private readonly Dictionary<int, Kind> _pedKinds = new Dictionary<int, Kind>();          // by model hash
        private readonly Dictionary<int, string> _vehicleLabels = new Dictionary<int, string>(); // by model hash
        private int _screenWidth = 1920, _screenHeight = 1080;
        private int _screenSizeFrame = -1000;

        public void Collect(List<TagCandidate> output, float radius, WorldTags.Filter target)
        {
            Ped player = Game.Player.Character;
            Vector3 me = player.Position;
            float r2 = radius * radius;
            Vehicle myVehicle = player.CurrentVehicle;
            Ped myMount = player.CurrentMount;

            if (target != WorldTags.Filter.Vehicles)
            {
                foreach (Ped ped in World.GetAllPeds())
                {
                    if (ped == null || ped.Handle == player.Handle || !ped.Exists()) continue;
                    if (myMount != null && ped.Handle == myMount.Handle) continue;
                    Vector3 p = ped.Position;
                    float d2 = p.DistanceToSquared(me);
                    if (d2 > r2) continue;
                    output.Add(new TagCandidate
                    {
                        Entity = ped,
                        Handle = ped.Handle,
                        IsPed = true,
                        Distance = (float)Math.Sqrt(d2),
                        AX = p.X, AY = p.Y, AZ = p.Z + ModelTop(ped.Model.Hash, 0.95f) + 0.12f,
                    });
                }
            }
            if (target != WorldTags.Filter.Peds)
            {
                foreach (Vehicle veh in World.GetAllVehicles())
                {
                    if (veh == null || !veh.Exists()) continue;
                    if (myVehicle != null && veh.Handle == myVehicle.Handle) continue;
                    Vector3 p = veh.Position;
                    float d2 = p.DistanceToSquared(me);
                    if (d2 > r2) continue;
                    output.Add(new TagCandidate
                    {
                        Entity = veh,
                        Handle = veh.Handle,
                        IsPed = false,
                        Distance = (float)Math.Sqrt(d2),
                        AX = p.X, AY = p.Y, AZ = p.Z + ModelTop(veh.Model.Hash, 2.0f) + 0.3f,
                    });
                }
            }
        }

        public bool WorldToScreen(float x, float y, float z, out float sx, out float sy)
            => Native.WorldToScreen(x, y, z, out sx, out sy);

        public void GetScreenSize(out int width, out int height)
        {
            // Window lookup is not free: refresh about once a second
            int frame = Game.FrameCount;
            if (frame - _screenSizeFrame > 60 || frame < _screenSizeFrame)
            {
                _screenSizeFrame = frame;
                Native.WindowSize(out _screenWidth, out _screenHeight);
            }
            width = _screenWidth;
            height = _screenHeight;
        }

        public void DrawReferences(List<TagCandidate> candidates, int max)
        {
            int drawn = 0;
            foreach (TagCandidate c in candidates)
            {
                if (drawn++ >= max) break;
                Native.DrawReference(c.AX, c.AY, c.AZ);
            }
        }

        public string Signature(TagCandidate c, int dist)
        {
            var e = (Entity)c.Entity;
            return (c.IsPed ? "p|" : "v|") + dist + "|" + HealthPercent(e) + "|" + (e.IsDead ? 1 : 0) + "|" + e.Model.Hash;
        }

        public void WriteFields(JsonWriter w, TagCandidate c)
        {
            if (c.IsPed)
            {
                var ped = (Ped)c.Entity;
                switch (PedKind(ped))
                {
                    case Kind.Horse:
                        w.Prop("name", "At").Prop("icon", "horse").Prop("tone", "team3");
                        break;
                    case Kind.Animal:
                        w.Prop("name", "Hayvan").Prop("icon", "dog").Prop("tone", "team2");
                        break;
                    default:
                        w.Prop("name", ped.Gender == Gender.Female ? "Kasabalı (K)" : "Kasabalı (E)")
                            .Prop("icon", ped.IsOnMount ? "rider" : "cowboy").Prop("tone", "team1");
                        break;
                }
                w.Prop("health", HealthPercent(ped)).Prop("dead", ped.IsDead);
            }
            else
            {
                var veh = (Vehicle)c.Entity;
                VehicleLabel(veh, out string label, out string icon);
                w.Prop("name", label)
                    .Prop("icon", icon)
                    .Prop("tone", "team3")
                    .Prop("health", HealthPercent(veh))
                    .Prop("compact", true);
            }
        }

        /// <summary>RDR2 health: 0..MaxHealth (same scale as MHud's RedM resource).</summary>
        public static int HealthPercent(Entity e)
        {
            int max = e.MaxHealth;
            return Native.Clamp((int)Math.Round(e.Health * 100.0 / Math.Max(1, max)), 0, 100);
        }

        private float ModelTop(int hash, float fallback)
        {
            if (!_modelTops.TryGetValue(hash, out float top))
            {
                top = Native.ModelTop(hash, fallback);
                _modelTops[hash] = top;
            }
            return top;
        }

        private Kind PedKind(Ped ped)
        {
            int hash = ped.Model.Hash;
            if (!_pedKinds.TryGetValue(hash, out Kind kind))
            {
                kind = Native.IsHorse(ped) ? Kind.Horse : Native.IsHuman(ped) ? Kind.Human : Kind.Animal;
                _pedKinds[hash] = kind;
            }
            return kind;
        }

        private void VehicleLabel(Vehicle veh, out string label, out string icon)
        {
            int hash = veh.Model.Hash;
            if (!_vehicleLabels.TryGetValue(hash, out label))
            {
                string name = Enum.GetName(typeof(VehicleHash), (uint)hash) ?? "";
                string n = name.ToLowerInvariant();
                label = n.Contains("coach") ? "Posta arabası"
                      : n.Contains("wagon") ? "Yük arabası"
                      : n.Contains("buggy") ? "Fayton"
                      : n.Contains("cart") ? "Araba"
                      : n.Contains("canoe") || n.Contains("rowboat") || n.Contains("boat") || n.Contains("keelboat") ? "Kayık"
                      : n.Contains("balloon") ? "Balon"
                      : n.Contains("train") || n.Contains("engine") || n.Contains("loco") ? "Tren"
                      : "Taşıt";
                _vehicleLabels[hash] = label;
            }
            icon = label == "Kayık" ? "boat" : label == "Tren" ? "train" : label == "Balon" ? "flag" : "cart";
        }
    }
}
