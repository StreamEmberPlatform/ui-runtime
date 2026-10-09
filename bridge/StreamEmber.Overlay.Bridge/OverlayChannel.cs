using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Web.Script.Serialization;

namespace StreamEmber.Overlay
{
    /// <summary>A mod-owned page, inbox, input lease and sprite allocation in one shared CEF view.</summary>
    public sealed class OverlayChannel : IDisposable
    {
        private static readonly object Gate = new object();
        private static readonly Dictionary<string, OverlayChannel> Channels = new Dictionary<string, OverlayChannel>();
        private static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 1048576 };
        private static readonly string Epoch = Guid.NewGuid().ToString("N");
        private static bool _ready;
        private static string _surfaceUrl;
        private static OverlayChannel _focused;
        private static AtlasLayout _atlas;
        private static readonly OverlaySprite[] Combined = new OverlaySprite[512];
        private readonly Queue<string> _inbox = new Queue<string>();
        private readonly Queue<string> _pending = new Queue<string>();
        private readonly OverlaySprite[][] _frames = { new OverlaySprite[0], new OverlaySprite[0], new OverlaySprite[0], new OverlaySprite[0] };
        private long _submission;
        private int _delay, _row;
        private bool _visible = true, _disposed;
        private AtlasLayout _requested, _effective;
        private string _url;

        public OverlayChannel(string id)
        {
            if (string.IsNullOrWhiteSpace(id)) throw new ArgumentException("A mod id is required.", nameof(id));
            lock (Gate)
            {
                if (Channels.ContainsKey(id)) throw new ArgumentException("Overlay mod already registered: " + id);
                if (Channels.Count >= 8) throw new InvalidOperationException("At most eight overlay mods are supported.");
                Id = id; Channels.Add(id, this);
            }
        }
        public string Id { get; }
        public int ApiVersion => OverlayBridge.ApiVersion;
        public int MaxSprites => OverlayBridge.MaxSprites;
        public OverlayState State { get { lock (Gate) { Pump(); return OverlayBridge.State; } } }
        public bool IsReady => State == OverlayState.Ready;
        public string Url { get { lock (Gate) { Pump(); return _url; } } }
        public bool Visible
        {
            get { lock (Gate) return _visible && OverlayBridge.Visible; }
            set { lock (Gate) { _visible = value; Pump(); if (value) OverlayBridge.Visible = true; Command(new { op = "visible", id = Id, visible = value }); if (!value && _focused == this) InputMode = OverlayInputMode.Game; Compose(); } }
        }
        public OverlayInputMode InputMode
        {
            get { lock (Gate) return _focused == this && OverlayBridge.InputMode == OverlayInputMode.Ui ? OverlayInputMode.Ui : OverlayInputMode.Game; }
            set
            {
                lock (Gate)
                {
                    Pump();
                    if (value == OverlayInputMode.Ui)
                    {
                        Focus();
                    }
                    else if (_focused == this)
                    {
                        _focused = null; OverlayBridge.InputMode = OverlayInputMode.Game;
                        Command(new { op = "focus", id = "" });
                    }
                }
            }
        }
        /// <summary>Own the open menu, optionally forwarding keys from the game instead of capturing mouse input.</summary>
        public void Focus(bool captureInput = true)
        {
            lock (Gate)
            {
                if (_disposed) return;
                Pump();
                if (_focused != null && _focused != this) _focused.Enqueue("{\"cb\":\"close\",\"data\":{}}");
                _focused = this; OverlayBridge.InputMode = captureInput ? OverlayInputMode.Ui : OverlayInputMode.Game;
                Command(new { op = "focus", id = Id });
            }
        }
        public bool LoadUrl(string url, bool reloadIfSame = false)
        {
            lock (Gate)
            {
                if (_disposed) return false;
                if (!string.IsNullOrEmpty(url) && !url.Contains("://") && !url.StartsWith("about:", StringComparison.Ordinal))
                    url = new Uri(Path.GetFullPath(Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(OverlayBridge.CoreDllPath)), url))).AbsoluteUri;
                url = string.IsNullOrEmpty(url) ? "about:blank" : url;
                Pump();
                if (_url == url && !reloadIfSame) return false;
                _url = url; _inbox.Clear(); _pending.Clear(); ClearSprites();
                Register(reloadIfSame); return true;
            }
        }
        public bool Send(string json)
        {
            lock (Gate)
            {
                if (json == null || json.Length > 524288 || _disposed) return false;
                Pump();
                if (!_ready) { if (_pending.Count == 256) _pending.Dequeue(); _pending.Enqueue(json); }
                else Command(new { op = "send", id = Id, json });
                return true;
            }
        }
        public bool TryReceive(out string json)
        {
            lock (Gate) { Pump(); json = _inbox.Count == 0 ? null : _inbox.Dequeue(); return json != null; }
        }
        public void SetAtlasLayout(AtlasLayout layout) { lock (Gate) { Pump(); if (_requested.Equals(layout)) return; _requested = layout; Layout(); } }
        public void DisableAtlas() { SetAtlasLayout(default(AtlasLayout)); }
        public AtlasLayout GetAtlasLayout() { lock (Gate) { Pump(); if (!OverlayBridge.GetAtlasLayout().Equals(_atlas)) Layout(); return _effective; } }
        public void SubmitSprites(OverlaySprite[] sprites, int count)
        {
            lock (Gate)
            {
                Pump(); count = sprites == null ? 0 : Math.Max(0, Math.Min(512, Math.Min(count, sprites.Length)));
                int index = (int)(_submission++ % 4);
                if (_frames[index].Length != count) _frames[index] = new OverlaySprite[count];
                if (count > 0) Array.Copy(sprites, _frames[index], count);
                Compose();
            }
        }
        public int SpriteDelay { get => _delay; set => _delay = Math.Max(0, Math.Min(3, value)); }
        public void Log(string message) => OverlayBridge.Log("[" + Id + "] " + message);
        private void ClearSprites() { _submission = 0; }
        private void Enqueue(string json) { if (_inbox.Count == 256) _inbox.Dequeue(); _inbox.Enqueue(json); }
        private void Register(bool reload = false)
        {
            if (_url != null) { Command(new { op = "register", id = Id, url = _url, reload, offset = _row * _atlas.SlotHeight, height = _effective.Rows * _atlas.SlotHeight }); Command(new { op = "visible", id = Id, visible = _visible }); }
        }
        private static void Command(object payload)
        {
            if (!_ready) return;
            var d = Json.Deserialize<Dictionary<string, object>>(Json.Serialize(payload));
            d["surface"] = "command"; d["epoch"] = Epoch;
            OverlayBridge.Send(Json.Serialize(d));
        }
        private void Pump()
        {
            if (_disposed) return;
            if (OverlayBridge.State != OverlayState.Ready) { _ready = false; return; }
            try
            {
                if (_surfaceUrl == null)
                {
                    string dir = Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(OverlayBridge.CoreDllPath)), "Cache", "Overlay");
                    Directory.CreateDirectory(dir);
                    string path = Path.Combine(dir, "StreamEmber.Surface.v4.html");
                    using (var stream = typeof(OverlayChannel).Assembly.GetManifestResourceStream("StreamEmber.Surface.html"))
                    using (var reader = new StreamReader(stream)) File.WriteAllText(path, reader.ReadToEnd(), new UTF8Encoding(false));
                    _surfaceUrl = new Uri(path).AbsoluteUri;
                    // A script-domain reload must reset the old surface and its stale client state.
                    _ready = false; OverlayBridge.LoadUrl(_surfaceUrl, true);
                }
                if (OverlayBridge.Url != _surfaceUrl) { _ready = false; OverlayBridge.LoadUrl(_surfaceUrl); }
                for (int n = 0; n < 128 && OverlayBridge.TryReceive(out string raw); n++)
                {
                    Dictionary<string, object> d;
                    try { d = Json.Deserialize<Dictionary<string, object>>(raw); } catch { continue; }
                    if (d == null || !d.TryGetValue("surface", out object type)) continue;
                    if ((string)type == "ready")
                    {
                        _ready = true; Command(new { op = "reset" });
                        foreach (var c in Channels.Values) { c.Register(); while (c._pending.Count > 0) Command(new { op = "send", id = c.Id, json = c._pending.Dequeue() }); }
                        Layout(); Command(new { op = "focus", id = _focused?.Id ?? "" });
                    }
                    else if ((string)type == "message" && d.TryGetValue("epoch", out object e) && (string)e == Epoch &&
                             d.TryGetValue("id", out object id) && Channels.TryGetValue((string)id, out OverlayChannel c) && d.TryGetValue("json", out object data)) c.Enqueue((string)data);
                }
            }
            catch (Exception error) { OverlayBridge.Log("Overlay channel: " + error.Message); }
        }
        private static void Layout()
        {
            var active = Channels.Values.Where(c => c._requested.IsEnabled).ToArray();
            int width = active.Length == 0 ? 240 : Math.Max(1, active.Max(c => c._requested.SlotWidth));
            int height = active.Length == 0 ? 84 : Math.Max(1, active.Max(c => c._requested.SlotHeight));
            int columns = active.Length == 0 ? 1 : Math.Max(1, active.Min(c => c._requested.Columns));
            int rows = active.Sum(c => (Math.Min(512, c._requested.SlotCount) + columns - 1) / columns);
            OverlayBridge.SetAtlasLayout(new AtlasLayout { SlotWidth = width, SlotHeight = height, Columns = columns, Rows = Math.Min(rows, 512 / columns) });
            _atlas = OverlayBridge.GetAtlasLayout();
            int row = 0, remaining = active.Length;
            foreach (var c in Channels.Values)
            {
                var next = _atlas;
                next.Rows = c._requested.IsEnabled && remaining > 0 ? Math.Min((Math.Min(512, c._requested.SlotCount) + Math.Max(1, _atlas.Columns) - 1) / Math.Max(1, _atlas.Columns), Math.Max(0, (_atlas.Rows - row) / remaining--)) : 0;
                if (c._row != row || !c._effective.Equals(next)) c.ClearSprites();
                c._row = row; c._effective = next; row += next.Rows;
            }
            Command(new { op = "layout", height = _atlas.Rows * _atlas.SlotHeight, layers = Channels.Values.Select(c => new { id = c.Id, offset = c._row * _atlas.SlotHeight, height = c._effective.Rows * _atlas.SlotHeight }).ToArray() });
            Compose();
        }
        private static void Compose()
        {
            int count = 0;
            foreach (var c in Channels.Values)
            {
                if (!c._visible || c._submission == 0) continue;
                var sprites = c._frames[(c._submission - 1 - Math.Min(c._delay, c._submission - 1)) % 4];
                foreach (var source in sprites)
                {
                    if (count == Combined.Length) break;
                    if (source.Slot < 0 || source.Slot >= c._effective.SlotCount) continue;
                    var s = source; s.Slot += c._row * _atlas.Columns; Combined[count++] = s;
                }
            }
            OverlayBridge.SpriteDelay = 0; OverlayBridge.SubmitSprites(Combined, count);
        }
        public void Dispose()
        {
            lock (Gate)
            {
                if (_disposed) return;
                if (_focused == this) { _focused = null; OverlayBridge.InputMode = OverlayInputMode.Game; Command(new { op = "focus", id = "" }); }
                _disposed = true; Channels.Remove(Id); Command(new { op = "remove", id = Id }); Layout();
            }
        }
    }
}
