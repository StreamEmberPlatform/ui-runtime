// Thin RDR2 native wrappers (ScriptHookRDRNetAPI's generated RDR2.Native classes), kept in one place.
using System;
using System.Runtime.InteropServices;
using RDR2;
using RDR2.Math;
using RDR2.Native;

namespace StreamEmber.TrainerDemo
{
    internal static class Native
    {
        /// <summary>World position -> normalized screen position (0..1). False when off screen.</summary>
        public static unsafe bool WorldToScreen(float wx, float wy, float wz, out float x, out float y)
        {
            float sx = 0f, sy = 0f;
            bool onScreen = GRAPHICS.GET_SCREEN_COORD_FROM_WORLD_COORD(wx, wy, wz, &sx, &sy);
            x = sx;
            y = sy;
            return onScreen;
        }

        public static void DisableControl(eInputType control) => PAD.DISABLE_CONTROL_ACTION(0, (uint)control, true);

        public static uint Hash(string name) => MISC.GET_HASH_KEY(name);

        /// <summary>Attribute cores (0 = health, 1 = stamina, 2 = dead eye), 0..100.</summary>
        public static int Core(Ped ped, int index) => ATTRIBUTE._GET_ATTRIBUTE_CORE_VALUE(ped.Handle, index);

        public static void SetCore(Ped ped, int index, int value) => ATTRIBUTE._SET_ATTRIBUTE_CORE_VALUE(ped.Handle, index, value);

        /// <summary>Stamina bar (outer ring) 0..100, same formula as MHud's RedM resource.</summary>
        public static int StaminaPercent(Ped ped)
        {
            float st = PED._GET_PED_STAMINA(ped.Handle);
            float max = PED._GET_PED_MAX_STAMINA(ped.Handle);
            return max > 0 ? Clamp((int)Math.Round(st / max * 100f), 0, 100) : 100;
        }

        public static void RestoreStamina(Ped ped) => PED._RESTORE_PED_STAMINA(ped.Handle, 100f);

        public static Vector3 GameplayCamRotation() => CAM.GET_GAMEPLAY_CAM_ROT(2);

        public static float GameplayCamRelativeHeading() => CAM.GET_GAMEPLAY_CAM_RELATIVE_HEADING();

        public static void SetGameplayCamRelativeHeading(float heading) => CAM.SET_GAMEPLAY_CAM_RELATIVE_HEADING(heading, 1f);

        public static int ClockHours() => CLOCK.GET_CLOCK_HOURS();

        public static int ClockMinutes() => CLOCK.GET_CLOCK_MINUTES();

        public static void SetClockTime(int hour) => CLOCK.SET_CLOCK_TIME(hour, 0, 0);

        public static void SetWeather(string weather)
        {
            MISC.CLEAR_OVERRIDE_WEATHER();
            MISC.SET_WEATHER_TYPE(Hash(weather), true, true, false, 0f, false);
        }

        public static unsafe bool GroundZ(float x, float y, float probeZ, out float z)
        {
            float g = 0f;
            bool found = MISC.GET_GROUND_Z_FOR_3D_COORD(x, y, probeZ, &g, false);
            z = g;
            return found;
        }

        public static void RequestCollisionAt(Vector3 p) => STREAMING.REQUEST_COLLISION_AT_COORD(p.X, p.Y, p.Z);

        /// <summary>Model bounding box top (relative to the entity origin); fallback when unknown.</summary>
        public static unsafe float ModelTop(int modelHash, float fallback)
        {
            Vector3 min = Vector3.Zero, max = Vector3.Zero;
            MISC.GET_MODEL_DIMENSIONS((uint)modelHash, &min, &max);
            return max.Z > 0.1f && max.Z < 20f ? max.Z : fallback;
        }

        public static bool IsHorse(Ped ped) => PED._IS_THIS_MODEL_A_HORSE((uint)ped.Model.Hash);

        public static bool IsHuman(Ped ped) => PED.IS_PED_HUMAN(ped.Handle);

        public static void Mount(Ped rider, Ped horse) => PED.SET_PED_ONTO_MOUNT(rider.Handle, horse.Handle, -1, true);

        public static void RandomOutfit(Ped ped)
        {
            PED._SET_RANDOM_OUTFIT_VARIATION(ped.Handle, true);
            PED._UPDATE_PED_VARIATION(ped.Handle, false, true, true, true, false);
        }

        public static void ClearWanted(Player player) => PLAYER.CLEAR_PLAYER_WANTED_LEVEL(player.Handle);

        public static void ShowGameHud(bool show) => HUD.DISPLAY_HUD(show);

        /// <summary>Game-rendered reference: a small sphere marker drawn by the 3D renderer at the world point.</summary>
        public static void DrawReference(float x, float y, float z)
        {
            World.DrawMarker(MarkerType.Sphere, new Vector3(x, y, z), Vector3.Zero, Vector3.Zero,
                new Vector3(0.06f, 0.06f, 0.06f), System.Drawing.Color.FromArgb(230, 255, 40, 60));
        }

        /// <summary>Objective-line text at the bottom of the screen (RDR2's simplest built-in message).</summary>
        public static void Notify(string text) => RDR2.UI.Screen.DisplaySubtitle(text);

        /// <summary>Client size of the game window = back buffer size (the swap chain follows the window).</summary>
        public static void WindowSize(out int width, out int height)
        {
            IntPtr window = System.Diagnostics.Process.GetCurrentProcess().MainWindowHandle;
            if (window != IntPtr.Zero && GetClientRect(window, out Rect r) && r.Right > 0 && r.Bottom > 0)
            {
                width = r.Right;
                height = r.Bottom;
                return;
            }
            width = 1920;
            height = 1080;
        }

        public static int Clamp(int v, int min, int max) => v < min ? min : v > max ? max : v;

        [StructLayout(LayoutKind.Sequential)]
        private struct Rect
        {
            public int Left, Top, Right, Bottom;
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetClientRect(IntPtr window, out Rect rect);
    }
}
