// StreamEmber Overlay — GTA V demo script (ScriptHookVDotNet v3, raw .cs script for the scripts folder).
// Phase 3 test: game -> UI status messages, UI -> game messages, controls disabled while the UI has input.
// Hotkeys come from the backend (overlay.ini): F7 = show/hide overlay, F8 = UI input (menu) mode.
//
// Written in C# 5 on purpose: SHVDN compiles raw scripts with the .NET Framework C# compiler.
// The real integration will live in StreamEmber.Core and use StreamEmber.Overlay.Bridge.dll instead of these imports.
using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using GTA;
using GTA.Native;

public class StreamEmberOverlayDemo : Script
{
    private const string CoreDll = "StreamEmber.Overlay.dll";
    private byte[] _buffer = new byte[64 * 1024];
    private readonly bool _available;
    private int _nextStatusAt;

    public StreamEmberOverlayDemo()
    {
        string exeDir = Path.GetDirectoryName(Process.GetCurrentProcess().MainModule.FileName);
        string core = Path.Combine(Path.Combine(Path.Combine(exeDir, "StreamEmber"), "Overlay"), CoreDll);
        _available = File.Exists(core) && LoadLibraryW(core) != IntPtr.Zero && SEO_GetApiVersion() == 1;
        Interval = 0;
        Tick += OnTick;
    }

    private void OnTick(object sender, EventArgs e)
    {
        if (!_available || SEO_GetState() != 2)
        {
            return;
        }

        // While the UI owns the input, keep the game from reacting to mouse/keyboard
        if (SEO_IsVisible() != 0 && SEO_GetInputMode() == 1)
        {
            Function.Call(Hash.DISABLE_ALL_CONTROL_ACTIONS, 0);
        }

        // UI -> game
        while (true)
        {
            int length = SEO_PollFromUi(_buffer, _buffer.Length);
            if (length < 0)
            {
                _buffer = new byte[-length];  // message larger than the buffer: grow and read it again
                length = SEO_PollFromUi(_buffer, _buffer.Length);
            }
            if (length <= 0)
            {
                break;
            }
            string json = Encoding.UTF8.GetString(_buffer, 0, length);
            if (json.Contains("\"type\":\"close\""))
            {
                SEO_SetInputMode(0);
            }
            GTA.UI.Notification.PostTicker("~o~Overlay~s~: " + json, false);
        }

        // Game -> UI, twice per second
        if (Game.GameTime >= _nextStatusAt)
        {
            _nextStatusAt = Game.GameTime + 500;
            Ped player = Game.Player.Character;
            GTA.Math.Vector3 p = player.Position;
            string status = string.Format(CultureInfo.InvariantCulture,
                "{{\"type\":\"status\",\"health\":{0},\"maxHealth\":{1},\"x\":{2:0.0},\"y\":{3:0.0},\"z\":{4:0.0},\"speed\":{5:0.0},\"wanted\":{6}}}",
                player.Health, player.MaxHealth, p.X, p.Y, p.Z, player.Speed * 3.6f,
                Function.Call<int>(Hash.GET_PLAYER_WANTED_LEVEL, Game.Player));
            SEO_PostToUi(Utf8Z(status));
        }
    }

    private static byte[] Utf8Z(string text)
    {
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(text) + 1];
        Encoding.UTF8.GetBytes(text, 0, text.Length, bytes, 0);
        return bytes;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr LoadLibraryW(string path);

    [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int SEO_GetApiVersion();

    [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int SEO_GetState();

    [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int SEO_IsVisible();

    [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int SEO_GetInputMode();

    [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void SEO_SetInputMode(int mode);

    [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void SEO_PostToUi(byte[] utf8Json);

    [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int SEO_PollFromUi(byte[] buffer, int bufferSize);
}
