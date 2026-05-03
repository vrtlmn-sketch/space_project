#include "catch.hpp"
#include "projectSerializer.h"
#include <fstream>
#include <cstdio>

static const char* TEST_JSON  = "/tmp/bs_test_project.json";
static const char* BAD_JSON   = "/tmp/bs_test_badjson.json";
static const char* MISSING    = "/tmp/bs_test_no_such_file_xyz.json";

static void write(const char* path, const std::string& s) {
    std::ofstream f(path); f << s;
}

// A complete project JSON with non-default values in every field so we can
// tell "field was loaded" from "field kept its default".
static const std::string PROJECT = R"({
  "physicsObjects": [
    {
      "name": "Sol",
      "mass": 100.0,
      "position": [1.0, 2.0, 3.0],
      "velocity": [0.1, 0.2, 0.3],
      "shaderType": 1,
      "temperature": 5778.0,
      "schwarzschildRadius": 0.02,
      "color": [1.0, 0.9, 0.7]
    }
  ],
  "grid": { "count": 8, "sizeX": 20.0, "sizeZ": 15.0, "subdivisions": 40, "ySpacing": 3.5 },
  "clouds": [],
  "settings": {
    "camX": 1.5, "camY": -0.5, "camZ": 2.5,
    "camRotation": 45.0, "camPitch": 10.0, "camRoll": -5.0, "camZoom": 60.0,
    "raytracerMethod": 2, "raytracerIsMain": true, "raytracerEnabled": true,
    "dopplerMode": true, "dopplerVelScale": 0.8,
    "dopplerBrightnessStr": 3.0, "dopplerColorStr": 1.5,
    "nebulaDetail": 0.7, "rtMaxBounces": 3, "rtMaxSteps": 512,
    "rtLiveResPreset": 4, "rtLiveWidth": 640, "rtLiveHeight": 360,
    "simSpeed": 2.5, "ramBudgetGB": 4.0,
    "recordResPreset": 5, "recordWidth": 2560, "recordHeight": 1440,
    "recordFps": 60, "recordPath": "my_video.mp4",
    "recStartFrame": 100, "recStopFrame": 500,
    "keypoints": [
      {"frame": 50,  "label": "Start"},
      {"frame": 200, "label": "Perihelion"}
    ],
    "cameraKeyframes": [
      {"frame": 0,   "pos": [0.0, 0.0, 5.0], "rotation": 0.0,  "pitch": 0.0,  "roll": 0.0, "zoom": 45.0},
      {"frame": 100, "pos": [2.0, 1.0, 3.0], "rotation": 30.0, "pitch": 15.0, "roll": 5.0, "zoom": 55.0}
    ]
  }
})";

TEST_CASE("load returns default ProjectData for nonexistent file", "[serializer]") {
    ProjectData d = ProjectSerializer::Load(MISSING);
    REQUIRE(d.objects.empty());
    REQUIRE(d.clouds.empty());
    REQUIRE(d.settings.camX == Approx(0.0f));
    REQUIRE(d.settings.recordPath == "output.mp4");
}

TEST_CASE("load handles malformed JSON without crashing", "[serializer]") {
    write(BAD_JSON, "{ not : valid json !!! ");
    ProjectData d = ProjectSerializer::Load(BAD_JSON);
    REQUIRE(d.objects.empty());
    std::remove(BAD_JSON);
}

TEST_CASE("load preserves camera position and orientation", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.camX        == Approx(1.5f));
    REQUIRE(d.settings.camY        == Approx(-0.5f));
    REQUIRE(d.settings.camZ        == Approx(2.5f));
    REQUIRE(d.settings.camRotation == Approx(45.0f));
    REQUIRE(d.settings.camPitch    == Approx(10.0f));
    REQUIRE(d.settings.camRoll     == Approx(-5.0f));
    REQUIRE(d.settings.camZoom     == Approx(60.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves raytracer mode settings", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.raytracerMethod  == 2);
    REQUIRE(d.settings.raytracerIsMain  == true);
    REQUIRE(d.settings.raytracerEnabled == true);
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves doppler settings", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.dopplerMode          == true);
    REQUIRE(d.settings.dopplerVelScale      == Approx(0.8f));
    REQUIRE(d.settings.dopplerBrightnessStr == Approx(3.0f));
    REQUIRE(d.settings.dopplerColorStr      == Approx(1.5f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves render quality settings", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.nebulaDetail  == Approx(0.7f));
    REQUIRE(d.settings.rtMaxBounces  == 3);
    REQUIRE(d.settings.rtMaxSteps    == 512);
    REQUIRE(d.settings.rtLiveWidth   == 640);
    REQUIRE(d.settings.rtLiveHeight  == 360);
    REQUIRE(d.settings.simSpeed      == Approx(2.5f));
    REQUIRE(d.settings.ramBudgetGB   == Approx(4.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves recording output settings", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.recordWidth   == 2560);
    REQUIRE(d.settings.recordHeight  == 1440);
    REQUIRE(d.settings.recordFps     == 60);
    REQUIRE(d.settings.recordPath    == "my_video.mp4");
    REQUIRE(d.settings.recStartFrame == 100);
    REQUIRE(d.settings.recStopFrame  == 500);
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves timeline keypoints", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.keypoints.size() == 2);
    REQUIRE(d.settings.keypoints[0].frame == 50u);
    REQUIRE(d.settings.keypoints[0].label == "Start");
    REQUIRE(d.settings.keypoints[1].frame == 200u);
    REQUIRE(d.settings.keypoints[1].label == "Perihelion");
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves camera keyframes", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.cameraKeyframes.size() == 2);
    const auto& kf0 = d.settings.cameraKeyframes[0];
    REQUIRE(kf0.frame    == 0u);
    REQUIRE(kf0.pos[0]   == Approx(0.0f));
    REQUIRE(kf0.pos[1]   == Approx(0.0f));
    REQUIRE(kf0.pos[2]   == Approx(5.0f));
    REQUIRE(kf0.zoom     == Approx(45.0f));
    const auto& kf1 = d.settings.cameraKeyframes[1];
    REQUIRE(kf1.frame    == 100u);
    REQUIRE(kf1.rotation == Approx(30.0f));
    REQUIRE(kf1.pitch    == Approx(15.0f));
    REQUIRE(kf1.roll     == Approx(5.0f));
    REQUIRE(kf1.zoom     == Approx(55.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves physics object data", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects.size() == 1);
    REQUIRE(d.objects[0].name        == "Sol");
    REQUIRE(d.objects[0].mass        == Approx(100.0f));
    REQUIRE(d.objects[0].shaderType  == 1);
    REQUIRE(d.objects[0].temperature == Approx(5778.0f));
    REQUIRE(d.objects[0].position.x  == Approx(1.0f));
    REQUIRE(d.objects[0].position.y  == Approx(2.0f));
    REQUIRE(d.objects[0].position.z  == Approx(3.0f));
    REQUIRE(d.objects[0].color.x     == Approx(1.0f));
    REQUIRE(d.objects[0].color.y     == Approx(0.9f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves grid data", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.grid.count        == 8);
    REQUIRE(d.grid.sizeX        == Approx(20.0f));
    REQUIRE(d.grid.sizeZ        == Approx(15.0f));
    REQUIRE(d.grid.subdivisions == 40);
    REQUIRE(d.grid.ySpacing     == Approx(3.5f));
    std::remove(TEST_JSON);
}
