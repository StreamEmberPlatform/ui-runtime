// World name tags over nearby peds and vehicles (MHud 'mhud:nametags') + performance/latency measurement.
//
// Latency test: every tag message carries a sequence number. The page acknowledges the newest one twice per second
// ('ack'); the round trip (game -> CEF -> page -> game) is measured in milliseconds and game frames.
// Native reference dots (DRAW_RECT) are drawn by the game itself at the exact anchor of each tag in the SAME frame;
// when the camera turns, the distance between a tag's bottom edge and its dot shows how far the overlay lags.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using GTA;
using GTA.Math;

namespace StreamEmber.TrainerDemo
{
    internal sealed class WorldTags
    {
        public enum Filter { All = 0, Peds = 1, Vehicles = 2 }

        // Settings (changed from the performance menu)
        public bool Enabled = true;
        public bool NativeReferences;
        public float Radius = 100f;
        public int MaxCount = 100;
        public int RateHz;                 // 0 = every frame
        // MHud's app.js repaints a tag's HTML whenever its signature (incl. distance text) changes; with 1 m steps
        // every moving tag is rebuilt on every message. Rounding the shown distance keeps updates transform-only.
        public int DistanceStep = 5;
        public Filter Target = Filter.All;

        // Results for the performance panel
        public int LastCount;
        public double AvgCollectMs;
        public double RttMs;
        public double RttFrames;
        public int TagMessagesPerSecond;

        private struct Candidate
        {
            public Entity Entity;
            public bool IsPed;
            public float Distance;
            public Vector3 Anchor;
        }

        private struct Sent
        {
            public int Seq;
            public int Frame;
            public long Ticks;
        }

        private readonly List<Candidate> _candidates = new List<Candidate>(512);
        private readonly Dictionary<int, string> _vehicleLabels = new Dictionary<int, string>();   // by model hash
        private readonly Dictionary<int, float> _vehicleHeights = new Dictionary<int, float>();    // by model hash
        private readonly Sent[] _sent = new Sent[64];
        private readonly Stopwatch _clock = Stopwatch.StartNew();
        private readonly Stopwatch _collect = new Stopwatch();
        private int _seq;
        private long _lastSendTicks;
        private bool _lastWasEmpty = true;
        private int _messagesThisSecond;
        private long _secondStartTicks;

        public void Tick(Ped player, int frame)
        {
            if (!Enabled)
            {
                SendEmptyOnce();
                return;
            }

            _collect.Restart();
            Collect(player);
            _collect.Stop();
            AvgCollectMs = AvgCollectMs * 0.9 + _collect.Elapsed.TotalMilliseconds * 0.1;

            if (NativeReferences)
            {
                DrawNativeReferences();
            }

            long now = _clock.ElapsedTicks;
            if (RateHz > 0 && now - _lastSendTicks < Stopwatch.Frequency / RateHz)
            {
                return;
            }
            _lastSendTicks = now;
            SendTags(frame, now);

            _messagesThisSecond++;
            if (now - _secondStartTicks >= Stopwatch.Frequency)
            {
                TagMessagesPerSecond = _messagesThisSecond;
                _messagesThisSecond = 0;
                _secondStartTicks = now;
            }
        }

        /// <summary>Page acknowledged tag message 'seq'.</summary>
        public void OnAck(int seq, int frame)
        {
            Sent s = _sent[seq & 63];
            if (s.Seq != seq) return;  // too old
            double ms = (_clock.ElapsedTicks - s.Ticks) * 1000.0 / Stopwatch.Frequency;
            RttMs = RttMs <= 0 ? ms : RttMs * 0.7 + ms * 0.3;
            RttFrames = RttFrames <= 0 ? frame - s.Frame : RttFrames * 0.7 + (frame - s.Frame) * 0.3;
        }

        public void Clear()
        {
            _candidates.Clear();
            SendEmptyOnce();
        }

        private void Collect(Ped player)
        {
            _candidates.Clear();
            Vector3 me = player.Position;
            Vehicle myVehicle = player.CurrentVehicle;

            if (Target != Filter.Vehicles)
            {
                foreach (Ped ped in World.GetNearbyPeds(player, Radius))
                {
                    if (ped == null || !ped.Exists()) continue;
                    Vector3 p = ped.Position;
                    _candidates.Add(new Candidate
                    {
                        Entity = ped,
                        IsPed = true,
                        Distance = p.DistanceTo(me),
                        Anchor = new Vector3(p.X, p.Y, p.Z + 1.05f),
                    });
                }
            }
            if (Target != Filter.Peds)
            {
                foreach (Vehicle veh in World.GetNearbyVehicles(me, Radius))
                {
                    if (veh == null || !veh.Exists() || veh == myVehicle) continue;
                    Vector3 p = veh.Position;
                    _candidates.Add(new Candidate
                    {
                        Entity = veh,
                        IsPed = false,
                        Distance = p.DistanceTo(me),
                        Anchor = new Vector3(p.X, p.Y, p.Z + VehicleTop(veh) + 0.25f),
                    });
                }
            }

            _candidates.Sort((a, b) => a.Distance.CompareTo(b.Distance));
            if (_candidates.Count > MaxCount)
            {
                _candidates.RemoveRange(MaxCount, _candidates.Count - MaxCount);
            }
        }

        private void DrawNativeReferences()
        {
            foreach (Candidate c in _candidates)
            {
                if (Native.WorldToScreen(c.Anchor, out float x, out float y))
                {
                    Native.DrawRect(x, y, 0.0035f, 0.0062f, 255, 40, 60, 230);
                }
            }
        }

        private void SendTags(int frame, long now)
        {
            int seq = ++_seq;
            _sent[seq & 63] = new Sent { Seq = seq, Frame = frame, Ticks = now };

            int count = 0;
            JsonWriter w = Ui.Begin("mhud:nametags").BeginArray();
            foreach (Candidate c in _candidates)
            {
                if (!Native.WorldToScreen(c.Anchor, out float x, out float y)) continue;
                float k = 1f - Math.Min(1f, c.Distance / Radius);

                w.BeginObject()
                    .Prop("id", c.Entity.Handle)
                    .Prop("x", x).Prop("y", y)
                    .Prop("scale", 0.75f + k * 0.35f, "0.###")
                    .Prop("alpha", 0.45f + k * 0.55f, "0.###")
                    .Prop("dist", RoundDistance(c.Distance));

                if (c.IsPed)
                {
                    var ped = (Ped)c.Entity;
                    int type = Native.PedType(ped);
                    bool cop = type == 6 || type == 27 || type == 29;
                    bool animal = type == 28;
                    w.Prop("name", cop ? "Polis" : animal ? "Hayvan" : ped.Gender == Gender.Female ? "Yaya (K)" : "Yaya (E)")
                        .Prop("icon", animal ? "paw" : "user")
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
                w.EndObject();
                count++;
            }
            w.EndArray();
            Ui.Send(top => top.Prop("seq", seq));

            LastCount = count;
            _lastWasEmpty = count == 0;
        }

        private void SendEmptyOnce()
        {
            LastCount = 0;
            if (_lastWasEmpty) return;
            Ui.Begin("mhud:nametags").BeginArray().EndArray();
            Ui.Send();
            _lastWasEmpty = true;
        }

        private int RoundDistance(float d)
        {
            int step = Math.Max(1, DistanceStep);
            return (int)Math.Round(d / step) * step;
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
