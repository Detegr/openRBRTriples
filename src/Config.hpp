#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>

#include <d3d9.h>

#include "RenderTarget.hpp"
#include "Util.hpp"

#include <vec2.hpp>
#include <vec3.hpp>
#include <vec4.hpp>

#define TOML_HEADER_ONLY 1
#include <toml.hpp>

struct CameraConfig {
    glm::ivec4 extent;
    glm::ivec2 crop;
    double angle_adjustment;
    double fov_adjustment;

    auto operator<=>(const CameraConfig&) const = default;

    constexpr int& x() { return extent.x; }
    constexpr int& y() { return extent.y; }
    constexpr int& w() { return extent[2]; }
    constexpr int& h() { return extent[3]; }
    constexpr const int& x() const { return extent.x; }
    constexpr const int& y() const { return extent.y; }
    constexpr const int& w() const { return extent[2]; }
    constexpr const int& h() const { return extent[3]; }
};

struct Config {
    std::vector<std::optional<CameraConfig>> cameras;
    std::vector<std::reference_wrapper<std::optional<CameraConfig>>> valid_cameras;
    bool aa_center_screen_only = true;
    bool side_monitors_half_hz = true;
    bool side_monitors_half_hz_btb_only = true;

    Config& operator=(const Config& rhs)
    {
        cameras = rhs.cameras;
        valid_cameras = cameras | std::views::filter([](const auto& cam) { return cam.has_value(); }) | std::ranges::to<std::vector<std::reference_wrapper<std::optional<CameraConfig>>>>();
        std::sort(valid_cameras.begin(), valid_cameras.end(), [](auto& a, auto& b) { return a.get()->x() < b.get()->x(); });
        aa_center_screen_only = rhs.aa_center_screen_only;
        side_monitors_half_hz = rhs.side_monitors_half_hz;
        side_monitors_half_hz_btb_only = rhs.side_monitors_half_hz_btb_only;
        return *this;
    }

    bool operator==(const Config& rhs) const
    {
        return cameras == rhs.cameras
            && aa_center_screen_only == rhs.aa_center_screen_only
            && side_monitors_half_hz == rhs.side_monitors_half_hz
            && side_monitors_half_hz_btb_only == rhs.side_monitors_half_hz_btb_only;
    }

    bool write(const std::filesystem::path& path) const
    {
        std::ofstream f(path);
        if (!f.good()) {
            return false;
        }
        auto cams = toml::table {};
        for (const auto& [i, cam_opt] : std::views::enumerate(cameras)) {
            if (cam_opt.has_value()) {
                const auto cam = *cam_opt;
                const auto data = toml::table {
                    { "x", cam.extent[0] },
                    { "y", cam.extent[1] },
                    { "w", cam.extent[2] },
                    { "h", cam.extent[3] },
                    { "cropx", cam.crop.x },
                    { "cropy", cam.crop.y },
                    { "angle", cam.angle_adjustment },
                    { "fov", cam.fov_adjustment },
                };
                if (i == Primary)
                    cams.insert_or_assign("center", data);
                if (i == Left)
                    cams.insert_or_assign("left", data);
                if (i == Right)
                    cams.insert_or_assign("right", data);
            }
        }
        toml::table out {
            { "anti_alias_center_screen_only", aa_center_screen_only },
            { "side_monitors_half_hz", side_monitors_half_hz },
            { "side_monitors_half_hz_btb_only", side_monitors_half_hz_btb_only },
            { "screen", cams },
        };

        f << out;
        f.close();
        return f.good();
    }

    static CameraConfig cameraconfig_from_toml(Config& cfg, const toml::table& tbl)
    {
        auto extent = glm::ivec4 {
            tbl["x"].value_or(0.0),
            tbl["y"].value_or(0.0),
            tbl["w"].value_or(0.0),
            tbl["h"].value_or(0.0),
        };

        auto crop = glm::ivec2 { tbl["cropx"].value_or(0), tbl["cropy"].value_or(0) };

        return CameraConfig {
            extent,
            crop,
            tbl["angle"].value_or(0.0),
            tbl["fov"].value_or(0.0),
        };
    }

    static Config from_toml(const std::filesystem::path& path, glm::ivec4 defaultExtent)
    {
        toml::table parsed;
        auto cfg = Config {};

        if (!std::filesystem::exists(path)) {
            cfg.cameras.emplace_back(CameraConfig {
                defaultExtent,
                { 0, 0 },
                0, 0 });
            if (!cfg.write(path)) {
                MessageBoxA(nullptr, "Could not write openRBRTriples.toml", "Error", MB_OK);
            }
            return cfg;
        } else {
            try {
                parsed = toml::parse_file(path.c_str());
            } catch (const toml::parse_error& e) {
                MessageBoxA(nullptr, std::format("Failed to parse openRBRTriples.toml: {}. Please check the syntax.", e.what()).c_str(), "Parse error", MB_OK);
                return cfg;
            }
        }
        if (parsed.size() == 0) {
            MessageBoxA(nullptr, "openRBRTriples.toml is empty, continuing with default config.", "Parse error", MB_OK);
            return cfg;
        }

        auto cameras = parsed["screen"];
        if (cameras.is_table()) {
            cfg.cameras.resize(3);

            const auto center = cameras["center"];
            if (center) {
                cfg.cameras[Primary] = cameraconfig_from_toml(cfg, *center.as_table());
            } else {
                throw std::runtime_error("openRBRTriples.toml is invalid. No center screen defined.");
            }

            const auto left = cameras["left"];
            if (left) {
                const auto camera_cfg = cameraconfig_from_toml(cfg, *left.as_table());
                cfg.cameras[Left] = camera_cfg;
            } else {
                cfg.cameras[Left] = std::nullopt;
            }

            const auto right = cameras["right"];
            if (right) {
                const auto camera_cfg = cameraconfig_from_toml(cfg, *right.as_table());
                cfg.cameras[Right] = camera_cfg;
            } else {
                cfg.cameras[Right] = std::nullopt;
            }
        } else {
            throw std::runtime_error("Key 'screen' must be a table");
        }

        cfg.aa_center_screen_only = parsed["anti_alias_center_screen_only"].value_or(true);
        cfg.side_monitors_half_hz = parsed["side_monitors_half_hz"].value_or(true);
        cfg.side_monitors_half_hz_btb_only = parsed["side_monitors_half_hz_btb_only"].value_or(true);

        if (cfg.cameras.empty()) {
            cfg.cameras.emplace_back(CameraConfig {
                defaultExtent,
                { 0, 0 },
                0, 0 });
        }

        cfg.valid_cameras = cfg.cameras | std::views::filter([](const auto& cam) { return cam.has_value(); }) | std::ranges::to<std::vector<std::reference_wrapper<std::optional<CameraConfig>>>>();
        std::sort(cfg.valid_cameras.begin(), cfg.valid_cameras.end(), [](auto& a, auto& b) { return a.get()->x() < b.get()->x(); });

        return cfg;
    }

    static Config from_path(const std::filesystem::path& path, glm::ivec4 defaultExtent)
    {
        return from_toml(path / "openRBRTriples.toml", defaultExtent);
    }
};
