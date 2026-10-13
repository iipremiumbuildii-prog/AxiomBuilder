add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

add_requires("levilamina")
add_requires("levibuildscript")
add_requires("preloader")

-- Force any transitive reference of rapidjson to use the stable v1.1.0 release instead of broken archives
add_requireconfs("rapidjson", {version = "v1.1.0"})

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("AxiomBuilder")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
    add_defines("NOMINMAX", "UNICODE")
    add_packages("levilamina", "preloader")
    set_exceptions("none")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
