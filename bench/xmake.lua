-- OSF CBPC — perf harness (xmake build).
--
-- Self-contained project (NOT included by the plugin's root xmake.lua). Build it
-- with MSVC for game-representative numbers (the plugin ships as an MSVC DLL), or
-- with clang-cl / gcc. Two targets let you A/B the Tier-1 codegen flags from
-- docs/PERF.md without editing anything:
--
--     cd bench
--     xmake f -m release -y
--     xmake build bench        -- baseline: /O2, no LTO   (~ today's mode.releasedbg)
--     xmake build bench-opt    -- Tier 1: LTO + /arch:AVX2 + /fp:contract
--     xmake run bench
--     xmake run bench-opt
--
-- The Makefile alongside this file is the portable, already-verified path
-- (g++/clang on Linux/CI); this xmake build mirrors it for the MSVC toolchain.

set_project("osf-cbpc-bench")
set_xmakever("2.8.0")
set_languages("c++23")
set_warnings("allextra")
add_rules("mode.debug", "mode.release")

local root = path.absolute(path.join(os.scriptdir(), ".."))

-- name -> { lto, simd } ; simd turns on AVX2 + FMA + fp contraction
local configs = {
    { name = "bench",     lto = false, simd = false },
    { name = "bench-opt", lto = true,  simd = true  },
}

for _, cfg in ipairs(configs) do
    target(cfg.name, function()
        set_kind("binary")
        set_default(cfg.name == "bench")

        add_files(path.join(os.scriptdir(), "main.cpp"))
        add_files(path.join(os.scriptdir(), "clsf_ops.cpp"))
        add_files(path.join(root, "src/Physics/JiggleSolver.cpp"))

        add_includedirs(root,
            path.join(root, "src"),
            path.join(root, "lib/commonlibsf/include"))

        -- Stand in for the SFSE PCH the CLSF headers expect (see prelude.h).
        add_forceincludes(path.join(os.scriptdir(), "prelude.h"))

        if cfg.lto then
            set_policy("build.optimization.lto", true)
        end
        if cfg.simd then
            -- Safe on every machine that can launch Starfield (it requires AVX2).
            add_cxflags("cl::/arch:AVX2", "cl::/fp:contract")
            add_cxflags("clang::-mavx2", "clang::-mfma", "clang::-ffp-contract=fast")
            add_cxflags("gcc::-mavx2", "gcc::-mfma", "gcc::-ffp-contract=fast")
        end
    end)
end
