#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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

#include <inicpp.h>

struct CameraConfig {
    glm::ivec4 extent;
    glm::ivec2 crop;
    double angle_adjustment;
    double fov_adjustment;
    double horizon_adjustment;
    double bezel_x = 0.0;
    double bezel_y = 0.0;

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
    // Single source of truth for default triples geometry (mm / degrees).
    static constexpr float default_monitor_width = 597.0f;
    static constexpr float default_eye_distance = 600.0f;
    static constexpr float default_side_angle = 45.0f;

    std::vector<std::optional<CameraConfig>> cameras;
    std::vector<std::reference_wrapper<std::optional<CameraConfig>>> valid_cameras;
    bool aa_center_screen_only = true;
    bool side_monitors_half_hz = true;
    bool side_monitors_half_hz_btb_only = true;
    bool auto_detect_surround = false;
    std::optional<float> horizon_adjustment = std::nullopt;
    float MonitorWidth = default_monitor_width;
    float EyeDistance = default_eye_distance;
    float SideAngle = default_side_angle;

    Config& operator=(const Config& rhs)
    {
        cameras = rhs.cameras;
        valid_cameras = cameras | std::views::filter([](const auto& cam) { return cam.has_value(); }) | std::ranges::to<std::vector<std::reference_wrapper<std::optional<CameraConfig>>>>();
        std::sort(valid_cameras.begin(), valid_cameras.end(), [](auto& a, auto& b) { return a.get()->x() < b.get()->x(); });
        aa_center_screen_only = rhs.aa_center_screen_only;
        side_monitors_half_hz = rhs.side_monitors_half_hz;
        side_monitors_half_hz_btb_only = rhs.side_monitors_half_hz_btb_only;
        auto_detect_surround = rhs.auto_detect_surround;
        horizon_adjustment = rhs.horizon_adjustment;
        MonitorWidth = rhs.MonitorWidth;
        EyeDistance = rhs.EyeDistance;
        SideAngle = rhs.SideAngle;
        return *this;
    }

    bool operator==(const Config& rhs) const
    {
        return cameras == rhs.cameras
            && aa_center_screen_only == rhs.aa_center_screen_only
            && side_monitors_half_hz == rhs.side_monitors_half_hz
            && side_monitors_half_hz_btb_only == rhs.side_monitors_half_hz_btb_only
            && auto_detect_surround == rhs.auto_detect_surround
            && horizon_adjustment == rhs.horizon_adjustment
            && MonitorWidth == rhs.MonitorWidth
            && EyeDistance == rhs.EyeDistance
            && SideAngle == rhs.SideAngle;
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
                    { "horizon", cam.horizon_adjustment },
                    { "bezel_x", cam.bezel_x },
                    { "bezel_y", cam.bezel_y },
                };
                if (i == Primary)
                    cams.insert_or_assign("center", data);
                if (i == Left)
                    cams.insert_or_assign("left", data);
                if (i == Right)
                    cams.insert_or_assign("right", data);
            }
        }
        toml::table triples {
            { "monitor_width", MonitorWidth },
            { "eye_distance", EyeDistance },
            { "side_angle", SideAngle },
        };

        toml::table out {
            { "anti_alias_center_screen_only", aa_center_screen_only },
            { "side_monitors_half_hz", side_monitors_half_hz },
            { "side_monitors_half_hz_btb_only", side_monitors_half_hz_btb_only },
            { "auto_detect_surround", auto_detect_surround },
            { "screen", cams },
            { "triples", triples },
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
            tbl["horizon"].value_or(0.0),
            tbl["bezel_x"].value_or(0.0),
            tbl["bezel_y"].value_or(0.0),
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
        cfg.auto_detect_surround = parsed["auto_detect_surround"].value_or(false);

        auto triples = parsed["triples"];
        if (triples.is_table()) {
            cfg.MonitorWidth = static_cast<float>(triples["monitor_width"].value_or(Config::default_monitor_width));
            cfg.EyeDistance = static_cast<float>(triples["eye_distance"].value_or(Config::default_eye_distance));
            cfg.SideAngle = static_cast<float>(triples["side_angle"].value_or(Config::default_side_angle));
        }

        if (cfg.cameras.empty()) {
            cfg.cameras.emplace_back(CameraConfig {
                defaultExtent,
                { 0, 0 },
                0, 0, 0 });
        }

        cfg.valid_cameras = cfg.cameras | std::views::filter([](const auto& cam) { return cam.has_value(); }) | std::ranges::to<std::vector<std::reference_wrapper<std::optional<CameraConfig>>>>();
        std::sort(cfg.valid_cameras.begin(), cfg.valid_cameras.end(), [](auto& a, auto& b) { return a.get()->x() < b.get()->x(); });

        return cfg;
    }

    // RSF launcher bridge: override per-camera widths from default.ini.
    // The RSF launcher writes CenterXRes/LeftXRes/RightXRes with mode-dependent
    // values (including BezelSize adjustment in Surround mode), keeping them
    // always up-to-date, while openRBRTriples.toml may contain stale widths
    // from a previous mode.
    //
    // This function reads the RSF config, patches the TOML file on disk,
    // then from_toml() parses the corrected file.  The result is that camera
    // widths always match the RSF launcher's current mode and BezelSize.
    //
    // NOTE: This is a bridge for RSF launcher integration.  If this branch
    // is ever separated from the RSF launcher, remove this function and the
    // call to it in from_path() so that camera widths come exclusively from
    // openRBRTriples.toml.
    static void patch_widths_from_rsf(const std::filesystem::path& toml_path)
    {
        char gameDir[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, gameDir);
        std::string iniPath = std::string(gameDir) + "\\rsf_launcher\\Configs\\default.ini";

        auto read_xres = [&](const char* key) -> int {
            char buf[16] = "0";
            GetPrivateProfileStringA("openRBRTriples", key, "0", buf, sizeof(buf), iniPath.c_str());
            return atoi(buf);
        };

        int center_w = read_xres("CenterXRes");
        int left_w   = read_xres("LeftXRes");
        int right_w  = read_xres("RightXRes");

        if (center_w <= 0 && left_w <= 0 && right_w <= 0)
            return;

        if (!std::filesystem::exists(toml_path))
            return;

        try {
            auto tbl = toml::parse_file(toml_path.c_str());
            bool modified = false;

            auto override_w = [&](const char* section, int new_w) {
                if (new_w <= 0) return;
                auto cam = tbl["screen"][section];
                if (!cam.is_table()) return;
                int cur = cam["w"].value_or(0);
                if (cur != new_w) {
                    cam.as_table()->insert_or_assign("w", new_w);
                    modified = true;
                }
            };

            override_w("center", center_w);
            override_w("left", left_w);
            override_w("right", right_w);

            if (modified) {
                std::ofstream f(toml_path);
                if (f.good()) {
                    f << tbl;
                    f.close();
                }
            }
        } catch (...) {
            // Parse failed; leave TOML unchanged.
        }
    }

    static Config from_path(const std::filesystem::path& path, glm::ivec4 defaultExtent)
    {
        auto toml_path = path / "openRBRTriples.toml";
        patch_widths_from_rsf(toml_path);
        return from_toml(toml_path, defaultExtent);
    }

    static std::optional<std::string> to_string(const std::filesystem::path& p)
    {
        return p.generic_string();
    }

    static std::optional<std::filesystem::path> resolve_car_ini_path(uint32_t car_id)
    {
        auto cars_ini_path = "Cars\\cars.ini";
        if (!std::filesystem::exists(cars_ini_path)) {
            dbg("Could not resolve car ini path");
            return std::nullopt;
        }

        try {
            ini::IniFile cars_ini(cars_ini_path);
            auto car_key = std::format("Car0{}", car_id);
            return std::filesystem::path(cars_ini[car_key]["IniFile"].as<std::string>()
                | std::ranges::views::filter([](char c) { return c != '"'; })
                | std::ranges::to<std::string>());
        } catch (...) {
            dbg("Could not resolve car ini path");
            return std::nullopt;
        }
    }

    static std::optional<std::filesystem::path> resolve_personal_car_ini_path(uint32_t car_id)
    {
        auto ini_file_path = resolve_car_ini_path(car_id);
        if (!ini_file_path) {
            return std::nullopt;
        }

        auto personal_filename = ini_file_path.value().filename();
        personal_filename.replace_extension("");
        personal_filename += "_personal";
        personal_filename.replace_extension(".ini");
        ini_file_path.value().replace_filename(personal_filename);

        return ini_file_path;
    }

    static bool insert_or_update_horizon_adjustment(uint32_t car_id, uint32_t camera_id, double horizon_adjustment)
    {
        auto camera_name = camera_id_to_personal_ini_camera_name(camera_id);
        if (!camera_name) {
            return false;
        }

        auto ini_path = resolve_personal_car_ini_path(car_id).and_then(to_string);
        if (!ini_path) {
            return false;
        }

        try {
            ini::IniFile personal_ini(ini_path.value());
            personal_ini[camera_name.value()]["openRBRTriples_horizonAdjustment"] = horizon_adjustment;
            personal_ini.save(ini_path.value());
        } catch (...) {
            dbg("Updating horizon adjustment failed");
            return false;
        }

        return true;
    }

    static float load_horizon_adjustment(uint32_t car_id, uint32_t camera_id)
    {
        auto camera_name = camera_id_to_personal_ini_camera_name(camera_id);
        if (!camera_name) {
            return 0.0f;
        }

        auto ini_path = resolve_personal_car_ini_path(car_id).and_then(to_string);
        if (!ini_path) {
            return 0.0f;
        }

        try {
            ini::IniFile personal_ini(ini_path.value());
            return personal_ini[camera_name.value()]["openRBRTriples_horizonAdjustment"].as<float>();
        } catch (...) {
            return 0.0f;
        }
    }

    static constexpr std::optional<std::string> camera_id_to_personal_ini_camera_name(uint32_t camera_id)
    {
        switch (camera_id) {
            case 0x1: return "Cam_external";
            case 0x3: return "Cam_bonnet";
            case 0x4: return "Cam_bonnet2";
            case 0x5: return "Cam_internal";
            default: return std::nullopt;
        }
    }
};
