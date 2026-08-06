const std = @import("std");
const version = @import("build.zig.zon").version;

const OPENRBRTriples_VERSION = .{
    .openRBRTriples_Major = "0",
    .openRBRTriples_Minor = "4",
    .openRBRTriples_Patch = "0",
    .openRBRTriples_Tweak = "0",
    .openRBRTriples_TweakStr = "-dev",
};

pub fn build(b: *std.Build) void {
    const supported_targets = &.{
        std.Target.Query{ .cpu_arch = .x86, .os_tag = .windows, .abi = .msvc },
    };
    const target = b.standardTargetOptions(.{
        .default_target = supported_targets[0],
        .whitelist = supported_targets,
    });
    const optimize = b.standardOptimizeOption(.{});

    const dll = b.addLibrary(.{
        .name = "openRBRTriples",
        .linkage = .dynamic,
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });
    dll.linkLibC();
    dll.addCSourceFiles(.{ .files = &.{
        "src/API.cpp",
        "src/Dx.cpp",
        "src/FlatTriples.cpp",
        "src/Globals.cpp",
        "src/Menu.cpp",
        "src/RBR.cpp",
        "src/RenderTarget.cpp",
        "src/UI.cpp",
        "src/openRBRTriples.cpp",

        "thirdparty/imgui-1.91.5/imgui.cpp",
        "thirdparty/imgui-1.91.5/imgui_draw.cpp",
        "thirdparty/imgui-1.91.5/imgui_tables.cpp",
        "thirdparty/imgui-1.91.5/imgui_widgets.cpp",
        "thirdparty/imgui-1.91.5/backends/imgui_impl_dx9.cpp",
        "thirdparty/imgui-1.91.5/backends/imgui_impl_win32.cpp",
    }, .flags = &.{
        "-Wno-ignored-attributes",
        "--std=c++23",
    } });

    dll.addLibraryPath(.{ .cwd_relative = "thirdparty/lib" });

    dll.addIncludePath(.{ .cwd_relative = "thirdparty" });
    // dll.addIncludePath(.{ .cwd_relative = "thirdparty/dxvk/include/vulkan/include" });
    // dll.addIncludePath(.{ .cwd_relative = "thirdparty/dxvk/src/d3d9" });
    dll.addIncludePath(.{ .cwd_relative = "thirdparty/glm" });
    dll.addIncludePath(.{ .cwd_relative = "thirdparty/minhook/include" });
    dll.addIncludePath(.{ .cwd_relative = "thirdparty/imgui-1.91.5" });

    const versionHeader = b.addConfigHeader(
        .{
            .style = .{ .cmake = b.path("src/Version.hpp.in") },
            .include_path = "Version.hpp",
        },
        OPENRBRTriples_VERSION,
    );

    const resourceFile = b.addConfigHeader(
        .{
            .style = .{ .cmake = b.path("src/Version.rc.in") },
            .include_path = "Version.rc",
        },
        OPENRBRTriples_VERSION,
    );

    dll.addConfigHeader(versionHeader);
    dll.addWin32ResourceFile(.{ .file = resourceFile.getOutput() });

    dll.linkSystemLibrary("advapi32");
    dll.linkSystemLibrary("d3d9");
    dll.linkSystemLibrary("libminhook.x86");
    dll.linkSystemLibrary("user32");
    dll.linkSystemLibrary("version");

    b.installArtifact(dll);
}
