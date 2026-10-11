using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace GodotPlugins
{
    public class PluginLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver _resolver;
        private readonly ICollection<string> _sharedAssemblies;
        private readonly AssemblyLoadContext _mainLoadContext;

        // Assemblies loaded from outside the plugin's folder (editor/mounted_assemblies/paths), by assembly name,
        // and their folders, which are searched for dependencies the plugin's folder doesn't have.
        private readonly Dictionary<string, string> _mountedAssemblies = new();
        private readonly List<string> _mountedAssemblyDirectories = new();

        public string? AssemblyLoadedPath { get; private set; }

        public PluginLoadContext(string pluginPath, ICollection<string> sharedAssemblies,
            AssemblyLoadContext mainLoadContext, bool isCollectible)
            : this(pluginPath, sharedAssemblies, mainLoadContext, isCollectible, Array.Empty<string>())
        {
        }

        public PluginLoadContext(string pluginPath, ICollection<string> sharedAssemblies,
            AssemblyLoadContext mainLoadContext, bool isCollectible, IEnumerable<string> mountedAssemblyPaths)
            : base(isCollectible)
        {
            _resolver = new AssemblyDependencyResolver(pluginPath);
            _sharedAssemblies = sharedAssemblies;
            _mainLoadContext = mainLoadContext;

            foreach (string mountedAssemblyPath in mountedAssemblyPaths)
            {
                // Reads the name from the file's metadata without loading it.
                string? name = AssemblyName.GetAssemblyName(mountedAssemblyPath).Name;
                if (name == null || _mountedAssemblies.ContainsKey(name))
                    continue;
                _mountedAssemblies.Add(name, mountedAssemblyPath);

                string? directory = Path.GetDirectoryName(mountedAssemblyPath);
                if (directory != null && !_mountedAssemblyDirectories.Contains(directory))
                    _mountedAssemblyDirectories.Add(directory);
            }

            if (string.IsNullOrEmpty(AppContext.BaseDirectory))
            {
                // See https://github.com/dotnet/runtime/blob/v6.0.0/src/libraries/System.Private.CoreLib/src/System/AppContext.AnyOS.cs#L17-L35
                // but Assembly.Location is unavailable, because we load assemblies from memory.
                string? baseDirectory = Path.GetDirectoryName(pluginPath);
                if (baseDirectory != null)
                {
                    if (!Path.EndsInDirectorySeparator(baseDirectory))
                        baseDirectory += Path.DirectorySeparatorChar;
                    // This SetData call effectively sets AppContext.BaseDirectory
                    // See https://github.com/dotnet/runtime/blob/v6.0.0/src/libraries/System.Private.CoreLib/src/System/AppContext.cs#L21-L25
                    AppDomain.CurrentDomain.SetData("APP_CONTEXT_BASE_DIRECTORY", baseDirectory);
                }
                else
                {
                    // TODO: How to log from GodotPlugins? (delegate pointer?)
                    Console.Error.WriteLine("Failed to set AppContext.BaseDirectory. Dynamic loading of libraries may fail.");
                }
            }
        }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (assemblyName.Name == null)
                return null;

            if (_sharedAssemblies.Contains(assemblyName.Name))
                return _mainLoadContext.LoadFromAssemblyName(assemblyName);

            // Mounted assemblies win over copies of them in the plugin's folder.
            if (_mountedAssemblies.TryGetValue(assemblyName.Name, out string? mountedAssemblyPath))
                return LoadFromFileInMemory(mountedAssemblyPath);

            string? assemblyPath = _resolver.ResolveAssemblyToPath(assemblyName);
            if (assemblyPath != null)
            {
                AssemblyLoadedPath = assemblyPath;
                return LoadFromFileInMemory(assemblyPath);
            }

            // Dependencies of mounted assemblies usually sit next to them (e.g. in the game's data folder).
            // A self-contained game ships the .NET runtime there too; those must come from the runtime Godot runs on.
            if (_mountedAssemblyDirectories.Count > 0 && PlatformAssemblyNames.Contains(assemblyName.Name))
                return null;

            foreach (string directory in _mountedAssemblyDirectories)
            {
                string candidatePath = Path.Combine(directory, assemblyName.Name + ".dll");
                if (File.Exists(candidatePath))
                    return LoadFromFileInMemory(candidatePath);
            }

            return null;
        }

        // Names of the assemblies of the .NET runtime Godot runs on (System.*, netstandard, ...).
        private static readonly Lazy<HashSet<string>> _platformAssemblyNames = new(() =>
        {
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            if (AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") is string paths)
            {
                foreach (string path in paths.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
                    names.Add(Path.GetFileNameWithoutExtension(path));
            }
            return names;
        });

        private static HashSet<string> PlatformAssemblyNames => _platformAssemblyNames.Value;

        private Assembly LoadFromFileInMemory(string assemblyPath)
        {
            // Load in memory to prevent locking the file
            using var assemblyFile = File.Open(assemblyPath, FileMode.Open, FileAccess.Read, FileShare.Read);
            string pdbPath = Path.ChangeExtension(assemblyPath, ".pdb");

            if (File.Exists(pdbPath))
            {
                using var pdbFile = File.Open(pdbPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                return LoadFromStream(assemblyFile, pdbFile);
            }

            return LoadFromStream(assemblyFile);
        }

        protected override IntPtr LoadUnmanagedDll(string unmanagedDllName)
        {
            string? libraryPath = _resolver.ResolveUnmanagedDllToPath(unmanagedDllName);
            if (libraryPath != null)
                return LoadUnmanagedDllFromPath(libraryPath);

            // Native libraries of mounted assemblies usually sit next to them.
            foreach (string directory in _mountedAssemblyDirectories)
            {
                foreach (string fileName in GetNativeLibraryFileNames(unmanagedDllName))
                {
                    string candidatePath = Path.Combine(directory, fileName);
                    if (File.Exists(candidatePath))
                        return LoadUnmanagedDllFromPath(candidatePath);
                }
            }

            return IntPtr.Zero;
        }

        private static IEnumerable<string> GetNativeLibraryFileNames(string libraryName)
        {
            yield return libraryName;
            if (OperatingSystem.IsWindows())
            {
                yield return libraryName + ".dll";
            }
            else
            {
                string extension = OperatingSystem.IsMacOS() ? ".dylib" : ".so";
                yield return libraryName + extension;
                yield return "lib" + libraryName + extension;
            }
        }
    }
}
