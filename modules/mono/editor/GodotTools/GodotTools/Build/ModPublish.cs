using System;
using System.IO;
using Godot;

namespace GodotTools.Build
{
    /// <summary>
    /// MoDot: publishing the mod from the editor, with the Publish button in the run bar and before running the project.
    /// A publish is the project's own `dotnet publish` (see <see cref="BuildManager.PublishProjectPortableBlocking"/>):
    /// the mod templates copy the .dll and .json and pack the .pck there. The project run from the editor loads the mod
    /// from where the Debug configuration puts it, so publishing before running uses Debug.
    /// </summary>
    public static class ModPublish
    {
        public const string DebugConfiguration = "Debug";
        public const string ReleaseConfiguration = "ExportRelease";

        /// <summary>Editor setting: publish (Debug) before running the project when its files changed since then.</summary>
        public const string PublishDebugBeforeRunningSetting = "dotnet/build/publish_debug_before_running";

        // When the last Debug publish succeeded, per project (in the editor's project metadata, not in the project).
        private const string MetadataSection = "modot_publish";
        private const string MetadataLastDebugPublish = "last_debug_publish_utc_ticks";

        // Top-level folders without the mod's sources: build output. Hidden folders (.godot, .idea, .git, ...) are skipped too.
        private static readonly string[] IgnoredTopLevelFolders = { "bin", "obj" };

        private static EditorSettings Settings => EditorInterface.Singleton.GetEditorSettings();

        public static bool IsPublishDebugBeforeRunningEnabled
        {
            get => (bool)Settings.GetSetting(PublishDebugBeforeRunningSetting);
            set => Settings.SetSetting(PublishDebugBeforeRunningSetting, value);
        }

        public static bool PublishDebugBlocking()
        {
            if (!BuildManager.PublishProjectPortableBlocking(DebugConfiguration))
                return false;

            // Recorded after the publish, so that files it writes itself (e.g. the manifest's updated dependency
            // versions) don't count as changes.
            Settings.SetProjectMetadata(MetadataSection, MetadataLastDebugPublish, DateTime.UtcNow.Ticks);
            return true;
        }

        public static bool PublishReleaseBlocking() => BuildManager.PublishProjectPortableBlocking(ReleaseConfiguration);

        public static bool ShouldPublishBeforeRunning() =>
            IsPublishDebugBeforeRunningEnabled && HasChangedSinceLastDebugPublish();

        /// <summary>
        /// Whether a file of the project is newer than the last successful Debug publish (or there was none).
        /// Edits made outside the editor (e.g. in an IDE) count too.
        /// </summary>
        public static bool HasChangedSinceLastDebugPublish()
        {
            long ticks = Settings.GetProjectMetadata(MetadataSection, MetadataLastDebugPublish, 0L).AsInt64();
            if (ticks <= 0)
                return true;

            var lastPublish = new DateTime(ticks, DateTimeKind.Utc);
            var projectDir = new DirectoryInfo(ProjectSettings.GlobalizePath("res://"));
            try
            {
                return HasFileNewerThan(projectDir, lastPublish, isTopLevel: true);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                GD.PushWarning($"Couldn't check the project for changes since the last publish ({e.Message}); publishing.");
                return true;
            }
        }

        private static bool HasFileNewerThan(DirectoryInfo dir, DateTime time, bool isTopLevel)
        {
            foreach (var file in dir.EnumerateFiles())
            {
                if (file.LastWriteTimeUtc > time)
                    return true;
            }

            foreach (var subdir in dir.EnumerateDirectories())
            {
                if (subdir.Name.StartsWith('.'))
                    continue;
                if (isTopLevel && Array.IndexOf(IgnoredTopLevelFolders, subdir.Name.ToLowerInvariant()) >= 0)
                    continue;
                // Links (e.g. a junction to a decompiled game) can be huge or loop.
                if (subdir.Attributes.HasFlag(FileAttributes.ReparsePoint))
                    continue;

                if (HasFileNewerThan(subdir, time, isTopLevel: false))
                    return true;
            }

            return false;
        }
    }
}
