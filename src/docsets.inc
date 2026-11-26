// The registry of documentation sets winter knows about: the one place to
// add, drop or retarget a docset. It feeds :DocInstall (list, completion,
// `all`), the scope separator and code fence per docset, and which docsets
// :Doc searches first for a buffer's filetype.
//
// DEVDOCS(slug, name, fence, sep, fts)
//     A docset DevDocs ships. `slug` is the DevDocs name without a version
//     (":DocInstall lua" resolves to the newest lua~X). :DocUpdate compares
//     the DevDocs manifest mtime.
//
// MDTREE(slug, name, fence, sep, fts, repo, ref, subdir, strip_ns, from, to)
//     Built from a markdown tree in a GitHub repo at branch `ref`; the
//     docs live in `subdir`. :DocUpdate compares the branch's head commit.
//     Extra aliases per page: `strip_ns` is dropped from the front of the
//     name ("nlohmann::basic_json::push_back" also as "basic_json::push_back"),
//     and the first `from` in a name is swapped for `to` ("json::push_back").
//
// JAVADOC(slug, name, fence, sep, fts, coords)
//     Built from -javadoc.jar files: `coords` is a space-separated list of
//     group:artifact, one jar each, merged into one docset. The jars the
//     build tools already fetched are used first (Maven repository, Gradle
//     cache, /usr/share/javadoc); only when none is found does it download
//     from Maven Central. Each version becomes its own docset, "lwjgl~3.3.6":
//     ":DocInstall lwjgl" takes the newest found, "lwjgl~3.3.6" that one.
//     :DocUpdate compares the version.
//
// LANGONLY(slug, fence, sep, fts)
//     Not offered for install. Only supplies the fence, separator and
//     filetype mapping for a docset you installed under that name by hand.
//
// fts is a space-separated list of buffer filetypes. A leading '!' marks the
// docset as the language's own: those are searched before the rest. Within
// each group the order of the rows below decides, so list a library after the
// language docset it extends.

DEVDOCS("cpp",        "C++",        "cpp",        "::", "!cpp !c++ c")
DEVDOCS("qt",         "Qt",         "cpp",        "::", "cpp c++ !qml !qmljs")
DEVDOCS("c",          "C",          "c",          ".",  "!c cpp c++")
MDTREE("nlohmann_json", "nlohmann/json", "cpp",   "::", "cpp c++",
       "nlohmann/json", "develop", "docs/mkdocs/docs/api",
       "nlohmann::", "basic_json", "json")
MDTREE("sdl3",        "SDL3",       "c",          ".",  "c cpp c++",
       "libsdl-org/sdlwiki", "main", "SDL3", "", "", "")
MDTREE("sdl2",        "SDL2",       "c",          ".",  "c cpp c++",
       "libsdl-org/sdlwiki", "main", "SDL2", "", "", "")
LANGONLY("sdl",                     "c",          ".",  "c cpp c++")
LANGONLY("glfw",                    "c",          ".",  "c cpp c++")
JAVADOC("lwjgl",      "LWJGL",      "java",       ".",  "java kotlin",
        "org.lwjgl:lwjgl org.lwjgl:lwjgl-glfw org.lwjgl:lwjgl-opengl "
        "org.lwjgl:lwjgl-openal org.lwjgl:lwjgl-stb")
DEVDOCS("opengl",     "OpenGL",     "c",          ".",  "c cpp c++ java")
DEVDOCS("vulkan",     "Vulkan",     "c",          ".",  "c cpp c++")
DEVDOCS("eigen3",     "Eigen",      "cpp",        "::", "cpp c++")
LANGONLY("glm",                     "cpp",        "::", "cpp c++")
DEVDOCS("sqlite",     "SQLite",     "c",          ".",  "c cpp c++")
DEVDOCS("gtk",        "GTK",        "c",          ".",  "c cpp c++")

DEVDOCS("lua",        "Lua",        "lua",        ".",  "!lua")
DEVDOCS("openjdk",    "OpenJDK",    "java",       ".",  "!java kotlin")
DEVDOCS("kotlin",     "Kotlin",     "kotlin",     ".",  "!kotlin")
DEVDOCS("rust",       "Rust",       "rust",       "::", "!rust")
DEVDOCS("python",     "Python",     "python",     ".",  "!python")
DEVDOCS("numpy",      "NumPy",      "python",     ".",  "python")
DEVDOCS("pandas",     "pandas",     "python",     ".",  "python")
DEVDOCS("matplotlib", "Matplotlib", "python",     ".",  "python")
DEVDOCS("go",         "Go",         "go",         ".",  "!go")
DEVDOCS("javascript", "JavaScript", "javascript", ".",  "!javascript typescript")
DEVDOCS("dom",        "DOM",        "javascript", ".",  "javascript typescript")
DEVDOCS("node",       "Node.js",    "javascript", ".",  "javascript")
DEVDOCS("typescript", "TypeScript", "typescript", ".",  "!typescript")
DEVDOCS("bash",       "Bash",       "bash",       ".",  "!bash !sh")
DEVDOCS("cmake",      "CMake",      "cmake",      ".",  "!cmake")
