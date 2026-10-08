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

    public static class OverlayBridge
    {
        public const int ApiVersion = 1;
        private const string CoreDll = "StreamEmber.Overlay.dll";

        private static readonly object Gate = new object();
        private static bool _loadAttempted;
        private static bool _loaded;
        private static byte[] _receiveBuffer = new byte[64 * 1024];

        /// <summary>Full path of the core DLL: &lt;game folder&gt;\StreamEmber\Overlay\StreamEmber.Overlay.dll</summary>
        public static string CoreDllPath
        {
            get
            {
                string exe = Process.GetCurrentProcess().MainModule.FileName;
                return Path.Combine(Path.Combine(Path.Combine(Path.GetDirectoryName(exe), "StreamEmber"), "Overlay"), CoreDll);
            }
        }

        public static OverlayState State
        {
            get { return EnsureLoaded() ? (OverlayState)SEO_GetState() : OverlayState.NotInstalled; }
        }

        /// <summary>True when the page is loaded and messages can flow.</summary>
        public static bool IsReady
        {
            get { return State == OverlayState.Ready; }
        }

        public static bool Visible
        {
            get { return EnsureLoaded() && SEO_IsVisible() != 0; }
            set { if (EnsureLoaded()) SEO_SetVisible(value ? 1 : 0); }
        }

        public static OverlayInputMode InputMode
        {
            get { return EnsureLoaded() && SEO_GetInputMode() == 1 ? OverlayInputMode.Ui : OverlayInputMode.Game; }
            set { if (EnsureLoaded()) SEO_SetInputMode((int)value); }
        }

        /// <summary>Sends a JSON text to the page (window.streamember.on listeners). Returns false if not available.</summary>
        public static bool Send(string json)
        {
            if (json == null || !EnsureLoaded())
            {
                return false;
            }
            SEO_PostToUi(ToUtf8Z(json));
            return true;
        }

        /// <summary>Takes the oldest message the page sent with window.streamember.post(...).</summary>
        public static bool TryReceive(out string json)
        {
            json = null;
            if (!EnsureLoaded())
            {
                return false;
            }
            lock (Gate)
            {
                int length = SEO_PollFromUi(_receiveBuffer, _receiveBuffer.Length);
                if (length < 0)
                {
                    _receiveBuffer = new byte[-length];
                    length = SEO_PollFromUi(_receiveBuffer, _receiveBuffer.Length);
                }
                if (length <= 0)
                {
                    return false;
                }
                json = Encoding.UTF8.GetString(_receiveBuffer, 0, length);
                return true;
            }
        }

        /// <summary>Writes a line to StreamEmber\Overlay\logs\overlay.log</summary>
        public static void Log(string message)
        {
            if (message != null && EnsureLoaded())
            {
                SEO_Log(ToUtf8Z(message));
            }
        }

        private static bool EnsureLoaded()
        {
            lock (Gate)
            {
                if (_loadAttempted)
                {
                    return _loaded;
                }
                _loadAttempted = true;
                try
                {
                    // Usually already loaded by the game backend; loading by full path makes the DllImport below
                    // resolve to that module (Windows matches already loaded modules by name).
                    string path = CoreDllPath;
                    _loaded = File.Exists(path) && LoadLibraryW(path) != IntPtr.Zero && SEO_GetApiVersion() == ApiVersion;
                }
                catch (Exception)
                {
                    _loaded = false;
                }
                return _loaded;
            }
        }

        private static byte[] ToUtf8Z(string text)
        {
            byte[] bytes = new byte[Encoding.UTF8.GetByteCount(text) + 1];
            Encoding.UTF8.GetBytes(text, 0, text.Length, bytes, 0);
            return bytes;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string path);

        [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
        private static extern int SEO_GetApiVersion();

        [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
        private static extern int SEO_GetState();

        [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
        private static extern void SEO_SetVisible(int visible);

        [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
        private static extern int SEO_IsVisible();

        [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
        private static extern void SEO_SetInputMode(int mode);

        [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
        private static extern int SEO_GetInputMode();

        [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
        private static extern void SEO_PostToUi(byte[] utf8Json);

        [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
        private static extern int SEO_PollFromUi(byte[] buffer, int bufferSize);

        [DllImport(CoreDll, CallingConvention = CallingConvention.Cdecl)]
        private static extern void SEO_Log(byte[] utf8Message);
    }
}
