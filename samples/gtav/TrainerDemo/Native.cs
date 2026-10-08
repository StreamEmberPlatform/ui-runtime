// Thin native wrappers. Natives are used instead of newer SHVDN members so the demo builds against both
// the stable 3.6 API and our nightly fork. Output arguments are reused (no allocation per call).
using GTA;
using GTA.Math;
using GTA.Native;

namespace StreamEmber.TrainerDemo
{
    internal static class Native
    {
        private static readonly OutputArgument OutA = new OutputArgument();
        private static readonly OutputArgument OutB = new OutputArgument();

        /// <summary>World position -> normalized screen position (0..1). False when off screen.</summary>
        public static bool WorldToScreen(Vector3 p, out float x, out float y)
        {
            bool onScreen = Function.Call<bool>(Hash.GET_SCREEN_COORD_FROM_WORLD_COORD, p.X, p.Y, p.Z, OutA, OutB);
            x = OutA.GetResult<float>();
            y = OutB.GetResult<float>();
            return onScreen;
        }

        public static void DrawRect(float x, float y, float w, float h, int r, int g, int b, int a)
            => Function.Call(Hash.DRAW_RECT, x, y, w, h, r, g, b, a, false);

        public static void DisableControl(int control)
            => Function.Call(Hash.DISABLE_CONTROL_ACTION, 0, control, true);

        public static void HideHudComponent(int component)
            => Function.Call(Hash.HIDE_HUD_COMPONENT_THIS_FRAME, component);

        public static string LabelText(string label)
            => Function.Call<string>(Hash.GET_FILENAME_FOR_AUDIO_CONVERSATION, label);

        public static string VehicleDisplayName(Model model)
            => LabelText(Function.Call<string>(Hash.GET_DISPLAY_NAME_FROM_VEHICLE_MODEL, model.Hash));

        public static int PedType(Ped ped) => Function.Call<int>(Hash.GET_PED_TYPE, ped);

        public static float SprintStaminaUsed(Player player)
            => Function.Call<float>(Hash.GET_PLAYER_SPRINT_STAMINA_REMAINING, player);

        public static bool IsSwimmingUnderWater(Ped ped) => Function.Call<bool>(Hash.IS_PED_SWIMMING_UNDER_WATER, ped);

        public static float UnderwaterTimeRemaining(Player player)
            => Function.Call<float>(Hash.GET_PLAYER_UNDERWATER_TIME_REMAINING, player);

        public static int WantedLevel(Player player) => Function.Call<int>(Hash.GET_PLAYER_WANTED_LEVEL, player);

        public static void SetWantedLevel(Player player, int level)
        {
            if (level <= 0)
            {
                Function.Call(Hash.CLEAR_PLAYER_WANTED_LEVEL, player);
                return;
            }
            Function.Call(Hash.SET_PLAYER_WANTED_LEVEL, player, level, false);
            Function.Call(Hash.SET_PLAYER_WANTED_LEVEL_NOW, player, false);
        }

        public static void VehicleLights(Vehicle v, out bool lightsOn, out bool highBeams)
        {
            Function.Call(Hash.GET_VEHICLE_LIGHTS_STATE, v, OutA, OutB);
            lightsOn = OutA.GetResult<bool>();
            highBeams = OutB.GetResult<bool>();
        }

        public static bool VehicleLocked(Vehicle v) => Function.Call<int>(Hash.GET_VEHICLE_DOOR_LOCK_STATUS, v) >= 2;

        public static string Plate(Vehicle v) => Function.Call<string>(Hash.GET_VEHICLE_NUMBER_PLATE_TEXT, v);

        public static void SetPlate(Vehicle v, string text) => Function.Call(Hash.SET_VEHICLE_NUMBER_PLATE_TEXT, v, text);

        public static void StreetNames(Vector3 p, out string street, out string cross)
        {
            Function.Call(Hash.GET_STREET_NAME_AT_COORD, p.X, p.Y, p.Z, OutA, OutB);
            int s1 = OutA.GetResult<int>();
            int s2 = OutB.GetResult<int>();
            street = Function.Call<string>(Hash.GET_STREET_NAME_FROM_HASH_KEY, s1);
            cross = s2 != 0 ? Function.Call<string>(Hash.GET_STREET_NAME_FROM_HASH_KEY, s2) : null;
        }

        public static string ZoneName(Vector3 p)
            => LabelText(Function.Call<string>(Hash.GET_NAME_OF_ZONE, p.X, p.Y, p.Z));

        public static Vector3 GameplayCamRotation() => Function.Call<Vector3>(Hash.GET_GAMEPLAY_CAM_ROT, 2);

        public static float GameplayCamRelativeHeading() => Function.Call<float>(Hash.GET_GAMEPLAY_CAM_RELATIVE_HEADING);

        public static void SetGameplayCamRelativeHeading(float heading)
            => Function.Call(Hash.SET_GAMEPLAY_CAM_RELATIVE_HEADING, heading);

        public static bool GroundZ(float x, float y, float probeZ, out float z)
        {
            bool found = Function.Call<bool>(Hash.GET_GROUND_Z_FOR_3D_COORD, x, y, probeZ, OutA, false, false);
            z = OutA.GetResult<float>();
            return found;
        }

        public static void RequestCollisionAt(Vector3 p) => Function.Call(Hash.REQUEST_COLLISION_AT_COORD, p.X, p.Y, p.Z);

        public static void SetClockTime(int hour) => Function.Call(Hash.SET_CLOCK_TIME, hour, 0, 0);

        public static void SetWeather(string weather) => Function.Call(Hash.SET_WEATHER_TYPE_NOW_PERSIST, weather);

        public static void SetDefaultComponents(Ped ped) => Function.Call(Hash.SET_PED_DEFAULT_COMPONENT_VARIATION, ped);

        public static void MaxPerformanceMods(Vehicle v)
        {
            Function.Call(Hash.SET_VEHICLE_MOD_KIT, v, 0);
            foreach (int modType in new[] { 11, 12, 13, 15, 16 }) // engine, brakes, transmission, suspension, armor
            {
                int count = Function.Call<int>(Hash.GET_NUM_VEHICLE_MODS, v, modType);
                if (count > 0) Function.Call(Hash.SET_VEHICLE_MOD, v, modType, count - 1, false);
            }
            Function.Call(Hash.TOGGLE_VEHICLE_MOD, v, 18, true); // turbo
        }

        /// <summary>GTA feed (ticker) notification through natives, independent of SHVDN's Notification API.</summary>
        public static void Notify(string text)
        {
            Function.Call(Hash.BEGIN_TEXT_COMMAND_THEFEED_POST, "STRING");
            Function.Call(Hash.ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, text);
            Function.Call(Hash.END_TEXT_COMMAND_THEFEED_POST_TICKER, false, true);
        }

        public static void SetColors(Vehicle v, int primary, int secondary)
            => Function.Call(Hash.SET_VEHICLE_COLOURS, v, primary, secondary);
    }
}
