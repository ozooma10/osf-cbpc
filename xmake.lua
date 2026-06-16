-- include subprojects
includes("lib/commonlibsf")

-- set project constants
set_project("OSF CBPC")
set_version("0.1.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add requirements
add_requires("nlohmann_json")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- define target
-- target name == repo folder == MO2 mod folder (deploy goes to XSE_SF_MODS_PATH\<target name>)
target("OSF CBPC")
    add_rules("commonlibsf.plugin", {
        name = "OSF CBPC",
        author = "ozooma10",
        description = "OSF CBPC - CBPC-lite spring/damper for Starfield",
        email = "98544147+ozooma10@users.noreply.github.com"
    })

    -- add dependency packages
    add_packages("nlohmann_json")

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- deploy bundled physics profiles alongside the plugin
    after_build(function (target)
        local mods = os.getenv("XSE_SF_MODS_PATH")
        if mods then
            local dst = path.join(mods, target:name(), "SFSE", "Plugins", target:name(), "profiles")
            os.mkdir(dst)
            os.cp("dist/SFSE/Plugins/OSF CBPC/profiles/*.json", dst .. "/")
        end
    end)
