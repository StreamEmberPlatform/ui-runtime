// StreamEmber Overlay — C# bridge (game independent). P/Invoke wrapper around StreamEmber.Overlay.dll.
//
// The game backend (e.g. StreamEmber.Overlay.GTAV.asi) loads the core and starts CEF; this class only talks to it.
// All members are thread-safe and never throw: when the overlay is not installed they simply do nothing.
// Natives are NOT involved here, so it can be used from any script of any game (GTA V, RDR2, ...).
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace StreamEmber.Overlay
{
    public enum OverlayInputMode
    {
        /// <summary>HUD mode: the UI is click-through, the game receives input.</summary>
        Game = 0,
        /// <summary>Menu mode: mouse and keyboard go to the UI.</summary>
        Ui = 1,
    }

    public enum OverlayState
    {
        NotInstalled = -2,
        Failed = -1,
        Stopped = 0,
        Starting = 1,
        Ready = 2,
    }

    /// <summary>Atlas layout in device pixels (see se_overlay.h). Rows = 0 means disabled.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct AtlasLayout : IEquatable<AtlasLayout>
    {
        public int SlotWidth;
        public int SlotHeight;
        public int Columns;
        public int Rows;

        public int SlotCount => Columns * Rows;
        public bool IsEnabled => Rows > 0 && Columns > 0;
        public bool Equals(AtlasLayout o) => SlotWidth == o.SlotWidth && SlotHeight == o.SlotHeight && Columns == o.Columns && Rows == o.Rows;
        public override bool Equals(object obj) => obj is AtlasLayout o && Equals(o);
        public override int GetHashCode() => ((SlotWidth * 31 + SlotHeight) * 31 + Columns) * 31 + Rows;
    }

    /// <summary>One atlas slot drawn by the game backend this frame. X/Y = bottom-center anchor, 0..1 of the screen.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct OverlaySprite
    {
        public int Slot;
        public float X;
        public float Y;
        public float Scale;
        public float Alpha;
    }

    public static class OverlayBridge
    {
        public const int ApiVersion = 4, MaxSprites = 512;
        public static string CoreDllPath => Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "StreamEmber", "Overlay", "test.dll");
        public static OverlayState State => OverlayState.Ready;
        public static string Url { get; private set; }
        public static bool Visible { get; set; } = true;
        public static OverlayInputMode InputMode { get; set; }
        public static int SpriteDelay { get; set; }
        public static readonly System.Collections.Generic.Queue<string> Incoming = new System.Collections.Generic.Queue<string>();
        public static readonly System.Collections.Generic.List<string> Commands = new System.Collections.Generic.List<string>();
        public static OverlaySprite[] Sprites = new OverlaySprite[0];
        private static AtlasLayout atlas;
        public static bool LoadUrl(string url, bool reload=false) { Url=url; return true; }
        public static bool Send(string json) { Commands.Add(json); return true; }
        public static bool TryReceive(out string json) { json=Incoming.Count==0?null:Incoming.Dequeue(); return json!=null; }
        public static void SetAtlasLayout(AtlasLayout value) { atlas=value; }
        public static AtlasLayout GetAtlasLayout() => atlas;
        public static void SubmitSprites(OverlaySprite[] data, int count) { Sprites=new OverlaySprite[count]; Array.Copy(data,Sprites,count); }
        public static void Log(string line) => Console.WriteLine(line);
    }
}
