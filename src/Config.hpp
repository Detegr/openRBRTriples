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

#include "Util.hpp"

#include <vec2.hpp>
#include <vec3.hpp>
#include <vec4.hpp>

#define TOML_HEADER_ONLY 1
#include <toml.hpp>

struct CameraConfig {
    glm::ivec4 extent;
    glm::ivec2 crop;
    glm::vec3 translation;
    double angle;
    double angle_adjustment;
    double fov;

    auto operator<=>(const CameraConfig&) const = default;

    constexpr int& x() { return extent.x; }
    constexpr int& y() { return extent.y; }
    constexpr int& w() { return extent[2]; }
    constexpr int& h() { return extent[3]; }
};

struct Config {
    std::vector<CameraConfig> cameras;
    double fov;
    bool aa_center_screen_only = true;
    bool side_monitors_half_hz = true;
    bool side_monitors_half_hz_btb_only = true;

    Config& operator=(const Config& rhs)
    {
        cameras = rhs.cameras;
        fov = rhs.fov;
        aa_center_screen_only = rhs.aa_center_screen_only;
        side_monitors_half_hz = rhs.side_monitors_half_hz;
        side_monitors_half_hz_btb_only = rhs.side_monitors_half_hz_btb_only;
        return *this;
    }

    bool operator==(const Config& rhs) const
    {
        return cameras == rhs.cameras
            && fov == rhs.fov
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
        auto cams = toml::array {};
        for (const auto& [i, cam] : std::views::enumerate(cameras)) {
            cams.push_back(toml::table {
                { "x", cam.extent[0] },
                { "y", cam.extent[1] },
                { "w", cam.extent[2] },
                { "h", cam.extent[3] },
                { "cropx", cam.crop.x },
                { "cropy", cam.crop.y },
                { "translatex", cam.translation.x },
                { "translatey", cam.translation.y },
                { "angle", cam.angle_adjustment },
                { "primary", i == 0 } });
        }
        toml::table out {
            { "anti_alias_center_screen_only", aa_center_screen_only },
            { "side_monitors_half_hz", side_monitors_half_hz },
            { "side_monitors_half_hz_btb_only", side_monitors_half_hz_btb_only },
            { "screen", toml::array { cams } },
        };

        f << out;
        f.close();
        return f.good();
    }

    static Config from_toml(const std::filesystem::path& path, glm::ivec4 defaultExtent)
    {
        toml::table parsed;
        auto cfg = Config {};

        if (!std::filesystem::exists(path)) {
            cfg.cameras.emplace_back(CameraConfig {
                defaultExtent,
                { 0, 0 },
                { 0, 0, 0 },
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

        double aspect = static_cast<double>(defaultExtent[2]) / static_cast<double>(defaultExtent[3]);
        const auto fov = 1.0472; // 60 degrees // parsed["fov"].value_or(1.0) / (4.0 / 3.0);
        auto cameras = parsed["camera"];
        if (!cameras.is_array_of_tables()) {
            cameras = parsed["screen"];
        }
        if (cameras.is_array_of_tables()) {
            cameras.as_array()->for_each([fov, aspect, &cfg](toml::table& tbl) {
                auto extent = glm::ivec4 {
                    tbl["x"].value_or(0.0),
                    tbl["y"].value_or(0.0),
                    tbl["w"].value_or(0.0),
                    tbl["h"].value_or(0.0),
                };

                auto crop = glm::ivec2 { tbl["cropx"].value_or(0), tbl["cropy"].value_or(0) };
                auto translation = glm::vec3 { tbl["translatex"].value_or(0.0), tbl["translatey"].value_or(0.0), tbl["translatez"].value_or(0.0) };

                const auto primary = tbl["primary"].value_or(false);
                auto camCfg = CameraConfig {
                    extent,
                    crop,
                    translation,
                    0.0,
                    tbl["angle"].value_or(0.0),
                    tbl["fov"].value_or(0.0),
                };

                if (primary) {
                    cfg.cameras.insert(cfg.cameras.begin(), camCfg);
                } else {
                    cfg.cameras.emplace_back(camCfg);
                }
            });
        }

        cfg.aa_center_screen_only = parsed["anti_alias_center_screen_only"].value_or(true);
        cfg.side_monitors_half_hz = parsed["side_monitors_half_hz"].value_or(true);
        cfg.side_monitors_half_hz_btb_only = parsed["side_monitors_half_hz_btb_only"].value_or(true);

        if (cfg.cameras.empty()) {
            cfg.cameras.emplace_back(CameraConfig {
                defaultExtent,
                { 0, 0 },
                { 0, 0, 0 },
                0, 0 });
        }

        cfg.fov = fov;

        return cfg;
    }

    static Config from_path(const std::filesystem::path& path, glm::ivec4 defaultExtent)
    {
        return from_toml(path / "openRBRTriples.toml", defaultExtent);
    }
};
