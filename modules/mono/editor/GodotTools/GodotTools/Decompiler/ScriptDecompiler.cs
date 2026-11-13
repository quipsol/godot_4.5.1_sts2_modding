using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using Godot;
using ICSharpCode.Decompiler;
using ICSharpCode.Decompiler.CSharp;
using ICSharpCode.Decompiler.CSharp.Syntax;
using ICSharpCode.Decompiler.CSharp.Transforms;
using ICSharpCode.Decompiler.Metadata;
using ICSharpCode.Decompiler.TypeSystem;
using GodotTools.Internals;

namespace GodotTools.Decompiler
{
    /// <summary>
    /// MoDot: the code of a C# script from a read-only mounted pack, decompiled from the mounted assemblies
    /// (editor/mounted_assemblies/paths and editor_only_paths). A game's pack only holds a blank placeholder for each C# file.
    /// Only used when a script is opened in the script editor (never when it is loaded), and cached.
    /// <para>
    /// The file's types: the class with <c>[ScriptPath("res://...")]</c>, which Godot's source generator records for
    /// classes deriving from a Godot type; otherwise the top-level types named like the file (e.g. Foo and Foo&lt;T&gt;
    /// for Foo.cs), preferring the namespace that matches the file's folders.
    /// </para>
    /// </summary>
    public static class ScriptDecompiler
    {
        /// <summary>Editor setting: also show the members Godot's source generators added to the class.</summary>
        public const string ShowGeneratedMembersSetting = "dotnet/editor/show_generated_members_in_decompiled_scripts";

        private sealed class MountedAssembly
        {
            public required string Path;
            public required DateTime LastWriteTimeUtc;
            public required PEFile Module;
            // Script path (res://...) -> its class, from [ScriptPath].
            public required Dictionary<string, TypeDefinitionHandle> ScriptClasses;
            // Name (without generic arity) -> top-level types of that name.
            public required Dictionary<string, List<(string Namespace, TypeDefinitionHandle Handle)>> TopLevelTypes;
            public CSharpDecompiler? Decompiler;
            public CSharpDecompiler? DecompilerWithGeneratedMembers;
        }

        private static readonly Dictionary<string, MountedAssembly> _assemblies = new(StringComparer.OrdinalIgnoreCase);
        private static readonly Dictionary<(string ScriptPath, bool ShowGenerated, string Assemblies), string> _sources = new();

        public static string GetSource(string scriptPath, IEnumerable<string> assemblyPaths)
        {
            bool showGenerated = (bool)EditorInterface.Singleton.GetEditorSettings().GetSetting(ShowGeneratedMembersSetting);

            var assemblies = new List<MountedAssembly>();
            var problems = new List<string>();
            foreach (string assemblyPath in assemblyPaths.Select(GlobalizeAssemblyPath).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    var assembly = GetAssembly(assemblyPath);
                    if (assembly != null)
                        assemblies.Add(assembly);
                    else
                        problems.Add($"not found: {assemblyPath}");
                }
                catch (Exception e)
                {
                    problems.Add($"can't be read: {assemblyPath} ({e.Message})");
                }
            }

            // Keyed by the assemblies' versions too, so an updated game is decompiled again.
            string assembliesKey = string.Join("|", assemblies.Select(a => a.Path + "@" + a.LastWriteTimeUtc.Ticks));
            var key = (scriptPath, showGenerated, assembliesKey);
            if (_sources.TryGetValue(key, out string? cached))
                return cached;

            var (owner, types, matchedBy) = FindTypes(scriptPath, assemblies);
            if (owner == null)
            {
                string mounted = assemblies.Count == 0
                    ? "No assemblies are mounted (editor/mounted_assemblies/paths and editor_only_paths)."
                    : "Mounted assemblies: " + string.Join(", ", assemblies.Select(a => System.IO.Path.GetFileName(a.Path))) + ".";
                string problemText = problems.Count == 0 ? "" : "\n// Problems: " + string.Join("; ", problems) + ".";
                return "// MoDot: the pack only holds a placeholder for this script, and none of the mounted assemblies has a type\n" +
                       $"// for it ([ScriptPath(\"{scriptPath}\")], or named like the file), so there is no code to show.\n" +
                       "// (A file can also have been left out of the game's build.)\n" +
                       $"// {mounted}{problemText}\n";
            }

            string code;
            using (var progress = new EditorProgress("modot_decompile_script", "Decompiling script...", 1))
            {
                progress.Step($"Decompiling {System.IO.Path.GetFileName(scriptPath)}", 0);
                try
                {
                    code = GetDecompiler(owner, showGenerated).DecompileTypesAsString(types);
                }
                catch (Exception e)
                {
                    return $"// MoDot: decompiling {System.IO.Path.GetFileName(scriptPath)} from {System.IO.Path.GetFileName(owner.Path)} failed:\n" +
                           $"// {e.GetType().Name}: {e.Message}\n";
                }
            }

            var name = owner.Module.Metadata.GetAssemblyDefinition();
            string header =
                $"// Decompiled from {System.IO.Path.GetFileName(owner.Path)} (version {name.Version}) with ICSharpCode.Decompiler. Read-only.\n" +
                $"// {matchedBy}\n" +
                (showGenerated
                    ? "// Including the members Godot's source generators added.\n"
                    : $"// The members Godot's source generators added are hidden (editor setting {ShowGeneratedMembersSetting}).\n") +
                "\n";
            string source = header + code;
            _sources[key] = source;
            return source;
        }

        // The types to show for a script path, the assembly they're in and how they were found.
        private static (MountedAssembly? Owner, List<TypeDefinitionHandle> Types, string MatchedBy) FindTypes(
            string scriptPath, List<MountedAssembly> assemblies)
        {
            foreach (var assembly in assemblies)
            {
                if (assembly.ScriptClasses.TryGetValue(scriptPath, out var handle))
                    return (assembly, new List<TypeDefinitionHandle> { handle }, $"The class with [ScriptPath(\"{scriptPath}\")].");
            }

            // Not a class deriving from a Godot type: the top-level types named like the file. Of those, the ones whose
            // namespace ends with most of the file's folders (res://src/Core/Models/Foo.cs -> *.Core.Models).
            string typeName = System.IO.Path.GetFileNameWithoutExtension(scriptPath);
            string[] folders = scriptPath.Substring("res://".Length).Split('/').SkipLast(1).ToArray();
            var candidates = assemblies
                .SelectMany(a => a.TopLevelTypes.TryGetValue(typeName, out var list)
                    ? list.Select(t => (Assembly: a, t.Namespace, t.Handle, Score: MatchingTrailingFolders(t.Namespace, folders)))
                    : [])
                .ToList();
            if (candidates.Count == 0)
                return (null, new List<TypeDefinitionHandle>(), "");

            int best = candidates.Max(c => c.Score);
            var chosen = candidates.Where(c => c.Score == best).ToList();
            // One file per namespace: if the best match is still spread over several namespaces or assemblies, take the
            // first namespace and say that there are more.
            var first = chosen[0];
            var sameFile = chosen.Where(c => c.Assembly == first.Assembly && c.Namespace == first.Namespace).ToList();
            string ns = first.Namespace.Length == 0 ? "the global namespace" : first.Namespace;
            string matchedBy = $"No class has [ScriptPath(\"{scriptPath}\")]: showing the type(s) named {typeName} in {ns}.";
            if (sameFile.Count < chosen.Count)
            {
                var others = chosen.Except(sameFile).Select(c => c.Namespace.Length == 0 ? "the global namespace" : c.Namespace).Distinct();
                matchedBy += " Also named so in: " + string.Join(", ", others) + ".";
            }
            return (first.Assembly, sameFile.Select(c => c.Handle).ToList(), matchedBy);
        }

        private static int MatchingTrailingFolders(string ns, string[] folders)
        {
            string[] parts = ns.Split('.');
            int count = 0;
            while (count < parts.Length && count < folders.Length &&
                   string.Equals(parts[parts.Length - 1 - count], folders[folders.Length - 1 - count], StringComparison.OrdinalIgnoreCase))
            {
                count++;
            }
            return count;
        }

        private static string GlobalizeAssemblyPath(string path)
        {
            path = path.Trim();
            if (path.StartsWith("res://", StringComparison.Ordinal) || path.StartsWith("user://", StringComparison.Ordinal))
                return System.IO.Path.GetFullPath(ProjectSettings.GlobalizePath(path));
            if (!System.IO.Path.IsPathRooted(path))
                return System.IO.Path.GetFullPath(System.IO.Path.Combine(ProjectSettings.GlobalizePath("res://"), path));
            return System.IO.Path.GetFullPath(path);
        }

        private static MountedAssembly? GetAssembly(string path)
        {
            if (!File.Exists(path))
                return null;

            DateTime lastWrite = File.GetLastWriteTimeUtc(path);
            if (_assemblies.TryGetValue(path, out var existing) && existing.LastWriteTimeUtc == lastWrite)
                return existing;

            // Read into memory, so the game's file isn't kept open (e.g. while the game updates).
            var module = new PEFile(path, new MemoryStream(File.ReadAllBytes(path)), PEStreamOptions.PrefetchEntireImage);
            var assembly = new MountedAssembly
            {
                Path = path,
                LastWriteTimeUtc = lastWrite,
                Module = module,
                ScriptClasses = FindScriptClasses(module.Metadata),
                TopLevelTypes = FindTopLevelTypes(module.Metadata),
            };
            _assemblies[path] = assembly;
            return assembly;
        }

        // The classes with [Godot.ScriptPath("res://...")], read from the metadata without loading any type.
        private static Dictionary<string, TypeDefinitionHandle> FindScriptClasses(MetadataReader metadata)
        {
            var result = new Dictionary<string, TypeDefinitionHandle>(StringComparer.Ordinal);
            foreach (var typeHandle in metadata.TypeDefinitions)
            {
                var type = metadata.GetTypeDefinition(typeHandle);
                foreach (var attributeHandle in type.GetCustomAttributes())
                {
                    var attribute = metadata.GetCustomAttribute(attributeHandle);
                    if (!IsScriptPathAttribute(metadata, attribute.Constructor))
                        continue;

                    var blob = metadata.GetBlobReader(attribute.Value);
                    if (blob.ReadUInt16() != 1) // Custom attribute prolog.
                        continue;
                    string? scriptPath = blob.ReadSerializedString();
                    if (!string.IsNullOrEmpty(scriptPath))
                        result.TryAdd(scriptPath, typeHandle);
                }
            }
            return result;
        }

        private static Dictionary<string, List<(string Namespace, TypeDefinitionHandle Handle)>> FindTopLevelTypes(MetadataReader metadata)
        {
            var result = new Dictionary<string, List<(string, TypeDefinitionHandle)>>(StringComparer.Ordinal);
            foreach (var typeHandle in metadata.TypeDefinitions)
            {
                var type = metadata.GetTypeDefinition(typeHandle);
                if (!type.GetDeclaringType().IsNil)
                    continue;
                string name = metadata.GetString(type.Name);
                if (name.StartsWith('<')) // Compiler-generated, e.g. <Module>.
                    continue;
                int arity = name.IndexOf('`');
                if (arity >= 0)
                    name = name.Substring(0, arity);
                if (!result.TryGetValue(name, out var list))
                    result[name] = list = new List<(string, TypeDefinitionHandle)>();
                list.Add((metadata.GetString(type.Namespace), typeHandle));
            }
            return result;
        }

        private static bool IsScriptPathAttribute(MetadataReader metadata, EntityHandle constructor)
        {
            if (constructor.Kind != HandleKind.MemberReference)
                return false;
            var member = metadata.GetMemberReference((MemberReferenceHandle)constructor);
            if (member.Parent.Kind != HandleKind.TypeReference)
                return false;
            var type = metadata.GetTypeReference((TypeReferenceHandle)member.Parent);
            return metadata.StringComparer.Equals(type.Name, "ScriptPathAttribute") &&
                   metadata.StringComparer.Equals(type.Namespace, "Godot");
        }

        private static CSharpDecompiler GetDecompiler(MountedAssembly assembly, bool showGenerated)
        {
            var existing = showGenerated ? assembly.DecompilerWithGeneratedMembers : assembly.Decompiler;
            if (existing != null)
                return existing;

            // The game's other assemblies (GodotSharp, 0Harmony, ...) are next to it.
            var resolver = new UniversalAssemblyResolver(assembly.Path, false, assembly.Module.DetectTargetFrameworkId());
            resolver.AddSearchDirectory(System.IO.Path.GetDirectoryName(assembly.Path)!);

            var settings = new DecompilerSettings(LanguageVersion.Latest)
            {
                ThrowOnAssemblyResolveErrors = false,
            };
            var decompiler = new CSharpDecompiler(assembly.Module, resolver, settings);
            if (!showGenerated)
            {
                // Before the usings are collected, so that those only the hidden members needed go too.
                var transforms = decompiler.AstTransforms;
                int usings = transforms.IndexOf(transforms.FirstOrDefault(t => t is IntroduceUsingDeclarations)!);
                transforms.Insert(usings >= 0 ? usings : transforms.Count, new HideGodotGeneratedMembers());
            }

            if (showGenerated)
                assembly.DecompilerWithGeneratedMembers = decompiler;
            else
                assembly.Decompiler = decompiler;
            return decompiler;
        }

        /// <summary>
        /// Removes what Godot's source generators add to a script class, so it reads like the code that was written:
        /// members marked <c>[EditorBrowsable(Never)]</c>, the nested <c>MethodName</c>/<c>PropertyName</c>/<c>SignalName</c>
        /// classes and the signals' <c>backing_*</c> delegate fields (the signal events stay).
        /// </summary>
        private sealed class HideGodotGeneratedMembers : IAstTransform
        {
            private static readonly HashSet<string> GeneratedNestedClasses = new() { "MethodName", "PropertyName", "SignalName" };

            public void Run(AstNode rootNode, TransformContext context)
            {
                foreach (var declaration in rootNode.Descendants.OfType<EntityDeclaration>().ToList())
                {
                    if (declaration.GetSymbol() is IEntity entity && IsGenerated(entity))
                        declaration.Remove();
                }
            }

            private static bool IsGenerated(IEntity entity)
            {
                if (entity.GetAttributes().Any(IsEditorBrowsableNever))
                    return true;

                if (entity is ITypeDefinition type && type.DeclaringTypeDefinition != null &&
                    GeneratedNestedClasses.Contains(type.Name) && type.DirectBaseTypes.Any(b => b.Name == type.Name))
                    return true;

                if (entity is IField field && field.Name.StartsWith("backing_", StringComparison.Ordinal) &&
                    field.Type.Kind == TypeKind.Delegate)
                    return true;

                return false;
            }

            private static bool IsEditorBrowsableNever(IAttribute attribute) =>
                attribute.AttributeType.FullName == "System.ComponentModel.EditorBrowsableAttribute" &&
                attribute.FixedArguments.Length == 1 &&
                Convert.ToInt32(attribute.FixedArguments[0].Value) == 1; // EditorBrowsableState.Never
        }
    }
}
