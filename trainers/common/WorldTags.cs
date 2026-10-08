// World name tags over nearby peds and vehicles, in two modes:
//
//  Atlas (default, frame-synchronous): the page renders each tag ONCE into a fixed slot of the overlay's atlas
//    (an area below the visible screen). Every game frame this script submits where each slot goes on screen
//    (OverlayBridge.SubmitSprites) and the game backend draws it in the same frame. Positions never touch
//    JavaScript, so tags stay glued to heads even while the camera turns fast. Content (name, health, distance)
//    only goes through the page when it changes.
//
//  Html (MHud's FiveM way, for comparison): positions are sent to the page as 'mhud:nametags' every frame and
//    MHud moves DOM nodes. The browser pipeline adds several frames of latency (measured with seq/ack).
//
// Native reference dots are drawn by the game itself at the world anchor (projected by the renderer = ground
// truth). A tag's bottom edge should sit on its dot.
//
// Game independent: everything that touches the game (entity pools, projection, tag content) goes through
// ITagWorld (GtaTagWorld in trainers/gtav, RdrTagWorld in trainers/rdr2).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using StreamEmber.Overlay;

namespace StreamEmber.Trainers
{
    /// <summary>One entity that may get a tag this frame. Anchor = world point the tag's bottom edge sits on.</summary>
    internal struct TagCandidate
    {
        public object Entity;   // game entity (GTA.Entity / RDR2.Entity)
        public int Handle;
        public bool IsPed;
        public float Distance;
        public float AX, AY, AZ;
    }

    /// <summary>The game specific side of WorldTags.</summary>
    internal interface ITagWorld
    {
        /// <summary>Adds the entities around the player (unsorted, within radius) to the list.</summary>
        void Collect(List<TagCandidate> output, float radius, WorldTags.Filter target);
        /// <summary>World position -> normalized screen position (0..1). False when off screen.</summary>
        bool WorldToScreen(float x, float y, float z, out float sx, out float sy);
        /// <summary>Back buffer size in pixels.</summary>
        void GetScreenSize(out int width, out int height);
        /// <summary>Draws a small game-rendered marker at each of the first 'max' anchors.</summary>
        void DrawReferences(List<TagCandidate> candidates, int max);
        /// <summary>Changes whenever the visible tag content changes (distance already rounded).</summary>
        string Signature(TagCandidate c, int roundedDistance);
        /// <summary>MHud tag fields (see MH.Nametags in MHud/kit/js/mhud.js) except id/position/dist.</summary>
        void WriteFields(JsonWriter w, TagCandidate c);
    }

    internal sealed class WorldTags
    {
        public enum Filter { All = 0, Peds = 1, Vehicles = 2 }
        public enum Mode { Atlas = 0, Html = 1 }

        // Design size of one atlas slot at 1080p (scaled with the screen height, like MHud's zoom)
        private const int SlotDesignWidth = 240;
        private const int SlotDesignHeight = 84;
        private const int SlotReleaseFrames = 30;
        private const int MaxReferences = 30;  // GTA supports ~32 draw origins per frame

        // Settings (changed from the performance menu)
        public bool Enabled = true;
        public Mode Positioning = Mode.Atlas;
        public bool NativeReferences;
        public float Radius = 100f;
        public int MaxCount = 100;
        public int RateHz;                 // Html mode: 0 = every frame
        public int PredictFrames;          // Atlas mode: screen-space extrapolation (0 or 1 frame)
        // MHud repaints a tag's HTML whenever its content (incl. distance text) changes; rounding keeps that rare.
        public int DistanceStep = 5;
        public Filter Target = Filter.All;

        // Results for the performance panel
        public int LastCount;
        public double AvgCollectMs;
        public double RttMs;
        public double RttFrames;
        public int TagMessagesPerSecond;
        public int ContentUpdatesPerSecond;
        public int AtlasSlots;

        private struct Sent
        {
            public int Seq;
            public int Frame;
            public long Ticks;
        }

        private sealed class Slot
        {
            public int Index;
            public int Handle;            // 0 = free
            public string Signature;
            public int Version;           // increases with every content change (never reset)
            public int FirstVersion;      // first content version of the current entity
            public int ReadyVersion;      // newest version the page reported as painted
            public int LastSeenFrame;
            public float LastX = -1, LastY = -1;
        }

        private readonly ITagWorld _world;
        private readonly List<TagCandidate> _candidates = new List<TagCandidate>(512);
        private readonly Sent[] _sent = new Sent[64];
        private readonly Stopwatch _clock = Stopwatch.StartNew();
        private readonly Stopwatch _collect = new Stopwatch();
        private int _seq;
        private long _lastSendTicks;
        private bool _htmlTagsShown;
        private int _messagesThisSecond;
        private int _contentThisSecond;
        private long _secondStartTicks;

        // Atlas state
        private readonly List<Slot> _slots = new List<Slot>();
        private readonly Dictionary<int, Slot> _slotByHandle = new Dictionary<int, Slot>();
        private readonly Stack<Slot> _freeSlots = new Stack<Slot>();
        private readonly List<Slot> _changed = new List<Slot>();
        private readonly List<int> _cleared = new List<int>();
        private readonly Dictionary<int, TagCandidate> _changedContent = new Dictionary<int, TagCandidate>();
        private OverlaySprite[] _sprites = new OverlaySprite[OverlayBridge.MaxSprites];
        private AtlasLayout _requested;
        private AtlasLayout _pageLayout;
        private int _screenWidth = 1920, _screenHeight = 1080;
        private bool _atlasActive;

        public WorldTags(ITagWorld world)
        {
            _world = world;
        }

        public void Tick(int frame)
        {
            if (!Enabled)
            {
                HideAll();
                return;
            }

            _collect.Restart();
            Collect();
            _collect.Stop();
            AvgCollectMs = AvgCollectMs * 0.9 + _collect.Elapsed.TotalMilliseconds * 0.1;

            if (NativeReferences)
            {
                _world.DrawReferences(_candidates, MaxReferences);
            }

            if (Positioning == Mode.Atlas)
            {
                if (_htmlTagsShown) SendEmptyHtmlTags();
                AtlasTick(frame);
            }
            else
            {
                if (_atlasActive) DisableAtlas();
                HtmlTick(frame);
            }

            long now = _clock.ElapsedTicks;
            if (now - _secondStartTicks >= Stopwatch.Frequency)
            {
                TagMessagesPerSecond = _messagesThisSecond;
                ContentUpdatesPerSecond = _contentThisSecond;
                _messagesThisSecond = 0;
                _contentThisSecond = 0;
                _secondStartTicks = now;
            }
        }

        /// <summary>Page (re)loaded: everything it had is gone.</summary>
        public void OnPageReady()
        {
            _pageLayout = default(AtlasLayout);
            foreach (Slot s in _slots)
            {
                s.Signature = null;
                s.ReadyVersion = 0;
                s.FirstVersion = s.Version + 1;
            }
            _htmlTagsShown = true;
        }

        /// <summary>Html mode: page acknowledged tag message 'seq'.</summary>
        public void OnAck(int seq, int frame)
        {
            Sent s = _sent[seq & 63];
            if (s.Seq != seq) return;  // too old
            double ms = (_clock.ElapsedTicks - s.Ticks) * 1000.0 / Stopwatch.Frequency;
            RttMs = RttMs <= 0 ? ms : RttMs * 0.7 + ms * 0.3;
            RttFrames = RttFrames <= 0 ? frame - s.Frame : RttFrames * 0.7 + (frame - s.Frame) * 0.3;
        }

        /// <summary>Atlas mode: page painted these slot versions. Pairs: slot, version, slot, version, ...</summary>
        public void OnAtlasReady(List<object> pairs)
        {
            if (pairs == null) return;
            for (int i = 0; i + 1 < pairs.Count; i += 2)
            {
                if (!(pairs[i] is double sd) || !(pairs[i + 1] is double vd)) continue;
                int index = (int)sd, version = (int)vd;
                if (index < 0 || index >= _slots.Count) continue;
                Slot slot = _slots[index];
                if (version > slot.ReadyVersion && version <= slot.Version) slot.ReadyVersion = version;
            }
        }

        public void Clear()
        {
            _candidates.Clear();
            HideAll();
        }

        // ------------------------------------------------------------------ collection

        private void Collect()
        {
            _candidates.Clear();
            _world.Collect(_candidates, Radius, Target);
            _candidates.Sort((a, b) => a.Distance.CompareTo(b.Distance));
            if (_candidates.Count > MaxCount)
            {
                _candidates.RemoveRange(MaxCount, _candidates.Count - MaxCount);
            }
        }

        // ------------------------------------------------------------------ atlas mode

        private void AtlasTick(int frame)
        {
            EnsureAtlasLayout();
            AtlasLayout layout = OverlayBridge.GetAtlasLayout();
            if (!layout.IsEnabled)
            {
                OverlayBridge.SubmitSprites(_sprites, 0);
                return;
            }
            if (!layout.Equals(_pageLayout))
            {
                ApplyLayout(layout);
            }
            AtlasSlots = _slots.Count;

            int spriteCount = 0;
            int visible = 0;
            _changed.Clear();
            _changedContent.Clear();
            foreach (TagCandidate c in _candidates)
            {
                if (!_world.WorldToScreen(c.AX, c.AY, c.AZ, out float x, out float y)) continue;
                Slot slot = AcquireSlot(c.Handle);
                if (slot == null) continue;
                slot.LastSeenFrame = frame;
                visible++;

                string signature = ContentSignature(c);
                if (signature != slot.Signature)
                {
                    slot.Signature = signature;
                    slot.Version++;
                    _changed.Add(slot);
                    _changedContent[slot.Index] = c;
                }

                // Screen-space prediction (optional): compensates a constant one-frame lag if calibration shows one
                float px = x, py = y;
                if (PredictFrames > 0 && slot.LastX >= 0)
                {
                    px = x + (x - slot.LastX) * PredictFrames;
                    py = y + (y - slot.LastY) * PredictFrames;
                }
                slot.LastX = x;
                slot.LastY = y;

                if (slot.ReadyVersion < slot.FirstVersion) continue;  // page has not painted this entity yet
                if (spriteCount >= _sprites.Length) continue;
                float k = 1f - Math.Min(1f, c.Distance / Radius);
                _sprites[spriteCount++] = new OverlaySprite
                {
                    Slot = slot.Index,
                    X = px,
                    Y = py,
                    Scale = 0.75f + k * 0.35f,
                    Alpha = 0.45f + k * 0.55f,
                };
            }

            ReleaseUnseenSlots(frame);
            SendContentChanges();

            // Candidates are nearest first; draw the farthest first so near tags end up on top
            Array.Reverse(_sprites, 0, spriteCount);
            OverlayBridge.SubmitSprites(_sprites, spriteCount);
            LastCount = visible;
        }

        private void EnsureAtlasLayout()
        {
            _world.GetScreenSize(out int resX, out int resY);
            int w = Math.Max(640, resX);
            int h = Math.Max(360, resY);
            _screenWidth = w;
            _screenHeight = h;
            int slotW = (int)Math.Round(SlotDesignWidth * h / 1080.0);
            int slotH = (int)Math.Round(SlotDesignHeight * h / 1080.0);
            int columns = Math.Max(1, w / slotW);
            int rows = (Math.Min(MaxCount, OverlayBridge.MaxSprites) + columns - 1) / columns;
            var desired = new AtlasLayout { SlotWidth = slotW, SlotHeight = slotH, Columns = columns, Rows = rows };
            if (!desired.Equals(_requested) || !_atlasActive)
            {
                _requested = desired;
                _atlasActive = true;
                OverlayBridge.SetAtlasLayout(desired);
            }
        }

        private void ApplyLayout(AtlasLayout layout)
        {
            _pageLayout = layout;
            // Slot geometry changed: every slot must be painted again
            _slotByHandle.Clear();
            _freeSlots.Clear();
            int count = layout.SlotCount;
            while (_slots.Count < count) _slots.Add(new Slot { Index = _slots.Count });
            if (_slots.Count > count) _slots.RemoveRange(count, _slots.Count - count);
            for (int i = count - 1; i >= 0; i--)
            {
                Slot s = _slots[i];
                s.Handle = 0;
                s.Signature = null;
                s.ReadyVersion = 0;
                s.FirstVersion = s.Version + 1;
                _freeSlots.Push(s);
            }
            Ui.Begin("trainer:atlas").BeginObject().Prop("reset", true).Name("layout").BeginObject()
                .Prop("slotWidth", layout.SlotWidth).Prop("slotHeight", layout.SlotHeight)
                .Prop("columns", layout.Columns).Prop("rows", layout.Rows)
                .Prop("screenWidth", _screenWidth).Prop("screenHeight", _screenHeight)
                .EndObject().EndObject();
            Ui.Send();
        }

        private Slot AcquireSlot(int handle)
        {
            if (_slotByHandle.TryGetValue(handle, out Slot slot)) return slot;
            if (_freeSlots.Count == 0) return null;
            slot = _freeSlots.Pop();
            slot.Handle = handle;
            slot.Signature = null;
            slot.FirstVersion = slot.Version + 1;
            slot.LastX = slot.LastY = -1;
            _slotByHandle[handle] = slot;
            return slot;
        }

        private void ReleaseUnseenSlots(int frame)
        {
            _cleared.Clear();
            foreach (Slot s in _slots)
            {
                if (s.Handle == 0 || frame - s.LastSeenFrame < SlotReleaseFrames) continue;
                _slotByHandle.Remove(s.Handle);
                s.Handle = 0;
                s.Signature = null;
                _freeSlots.Push(s);
                _cleared.Add(s.Index);
            }
        }

        private void SendContentChanges()
        {
            if (_changed.Count == 0 && _cleared.Count == 0) return;
            JsonWriter w = Ui.Begin("trainer:atlas").BeginObject();
            if (_changed.Count > 0)
            {
                w.Name("set").BeginArray();
                foreach (Slot s in _changed)
                {
                    w.BeginObject().Prop("slot", s.Index).Prop("ver", s.Version).Name("item");
                    WriteTagContent(w, _changedContent[s.Index]);
                    w.EndObject();
                }
                w.EndArray();
            }
            if (_cleared.Count > 0)
            {
                w.Name("clear").BeginArray();
                foreach (int i in _cleared) w.Value(i);
                w.EndArray();
            }
            w.EndObject();
            Ui.Send();
            _contentThisSecond += _changed.Count;
        }

        private void DisableAtlas()
        {
            _atlasActive = false;
            _requested = default(AtlasLayout);
            _pageLayout = default(AtlasLayout);
            OverlayBridge.SubmitSprites(_sprites, 0);
            OverlayBridge.DisableAtlas();
            Ui.Begin("trainer:atlas").BeginObject().Prop("reset", true).EndObject();
            Ui.Send();
            AtlasSlots = 0;
        }

        // ------------------------------------------------------------------ html mode (MHud 'mhud:nametags')

        private void HtmlTick(int frame)
        {
            long now = _clock.ElapsedTicks;
            if (RateHz > 0 && now - _lastSendTicks < Stopwatch.Frequency / RateHz)
            {
                return;
            }
            _lastSendTicks = now;

            int seq = ++_seq;
            _sent[seq & 63] = new Sent { Seq = seq, Frame = frame, Ticks = now };

            int count = 0;
            JsonWriter w = Ui.Begin("mhud:nametags").BeginArray();
            foreach (TagCandidate c in _candidates)
            {
                if (!_world.WorldToScreen(c.AX, c.AY, c.AZ, out float x, out float y)) continue;
                float k = 1f - Math.Min(1f, c.Distance / Radius);
                w.BeginObject()
                    .Prop("id", c.Handle)
                    .Prop("x", x).Prop("y", y)
                    .Prop("scale", 0.75f + k * 0.35f, "0.###")
                    .Prop("alpha", 0.45f + k * 0.55f, "0.###");
                WriteTagFields(w, c);
                w.EndObject();
                count++;
            }
            w.EndArray();
            Ui.Send(top => top.Prop("seq", seq));

            _messagesThisSecond++;
            LastCount = count;
            _htmlTagsShown = count > 0;
        }

        // ------------------------------------------------------------------ shared

        private void HideAll()
        {
            LastCount = 0;
            if (_atlasActive) DisableAtlas();
            if (_htmlTagsShown) SendEmptyHtmlTags();
        }

        private void SendEmptyHtmlTags()
        {
            Ui.Begin("mhud:nametags").BeginArray().EndArray();
            Ui.Send();
            _htmlTagsShown = false;
        }

        private void WriteTagContent(JsonWriter w, TagCandidate c)
        {
            w.BeginObject();
            WriteTagFields(w, c);
            w.EndObject();
        }

        /// <summary>MHud tag fields (see MH.Nametags in MHud/kit/js/mhud.js), without position.</summary>
        private void WriteTagFields(JsonWriter w, TagCandidate c)
        {
            w.Prop("dist", RoundDistance(c.Distance));
            _world.WriteFields(w, c);
        }

        private string ContentSignature(TagCandidate c) => _world.Signature(c, RoundDistance(c.Distance));

        private int RoundDistance(float d)
        {
            int step = Math.Max(1, DistanceStep);
            return (int)Math.Round(d / step) * step;
        }

        private static int Clamp(int v, int min, int max) => v < min ? min : v > max ? max : v;
    }
}
