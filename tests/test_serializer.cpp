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

// ── Default struct values ─────────────────────────────────────────────────────

TEST_CASE("default SceneSettings has expected camera defaults", "[serializer]") {
    SceneSettings s;
    REQUIRE(s.camX        == Approx(0.0f));
    REQUIRE(s.camY        == Approx(0.0f));
    REQUIRE(s.camZ        == Approx(0.0f));
    REQUIRE(s.camZoom     == Approx(45.0f));
    REQUIRE(s.camRotation == Approx(0.0f));
    REQUIRE(s.camPitch    == Approx(0.0f));
    REQUIRE(s.camRoll     == Approx(0.0f));
}

TEST_CASE("default SceneSettings has raytracer disabled", "[serializer]") {
    SceneSettings s;
    REQUIRE(s.raytracerMethod  == 0);
    REQUIRE(s.raytracerIsMain  == false);
    REQUIRE(s.raytracerEnabled == false);
}

TEST_CASE("default SceneSettings has doppler disabled", "[serializer]") {
    SceneSettings s;
    REQUIRE(s.dopplerMode          == false);
    REQUIRE(s.dopplerVelScale      == Approx(0.5f));
    REQUIRE(s.dopplerBrightnessStr == Approx(2.0f));
    REQUIRE(s.dopplerColorStr      == Approx(1.0f));
}

TEST_CASE("default SceneSettings has expected render quality values", "[serializer]") {
    SceneSettings s;
    REQUIRE(s.rtMaxBounces == 1);
    REQUIRE(s.rtMaxSteps   == 256);
    REQUIRE(s.simSpeed     == Approx(1.0f));
    REQUIRE(s.ramBudgetGB  == Approx(1.0f));
}

TEST_CASE("default SceneSettings has expected recording values", "[serializer]") {
    SceneSettings s;
    REQUIRE(s.recordWidth  == 1920);
    REQUIRE(s.recordHeight == 1080);
    REQUIRE(s.recordFps    == 30);
    REQUIRE(s.recordPath   == "output.mp4");
    REQUIRE(s.recStartFrame == -1);
    REQUIRE(s.recStopFrame  == -1);
}

TEST_CASE("default SceneSettings has empty keypoint lists", "[serializer]") {
    SceneSettings s;
    REQUIRE(s.keypoints.empty());
    REQUIRE(s.cameraKeyframes.empty());
}

TEST_CASE("default GridData has expected values", "[serializer]") {
    GridData g;
    REQUIRE(g.count        == 4);
    REQUIRE(g.sizeX        == Approx(10.0f));
    REQUIRE(g.sizeZ        == Approx(10.0f));
    REQUIRE(g.subdivisions == 30);
    REQUIRE(g.ySpacing     == Approx(2.0f));
}

TEST_CASE("default CloudData has expected values", "[serializer]") {
    CloudData c;
    REQUIRE(c.enabled     == false);
    REQUIRE(c.count       == 2000);
    REQUIRE(c.renderMode  == 0);
    REQUIRE(c.temperature == Approx(4500.0f));
    REQUIRE(c.computeMethod == 0);
    REQUIRE(c.scale       == Approx(1.0f));
}

TEST_CASE("default PhysicsObjectData has expected values", "[serializer]") {
    PhysicsObjectData p;
    REQUIRE(p.mass              == Approx(0.0f));
    REQUIRE(p.temperature       == Approx(0.0f));
    REQUIRE(p.schwarzschildRadius == Approx(0.0f));
    REQUIRE(p.shaderType        == 0);
    REQUIRE(p.position.x        == Approx(0.0f));
    REQUIRE(p.position.y        == Approx(0.0f));
    REQUIRE(p.position.z        == Approx(0.0f));
}

// ── Partial JSON loading (missing fields fall back to defaults) ───────────────

static const std::string MINIMAL_PROJECT = R"({
  "physicsObjects": [],
  "clouds": [],
  "grid": {},
  "settings": {}
})";

TEST_CASE("load with empty settings object uses all defaults", "[serializer]") {
    write(TEST_JSON, MINIMAL_PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.camX           == Approx(0.0f));
    REQUIRE(d.settings.raytracerMethod == 0);
    REQUIRE(d.settings.dopplerMode    == false);
    REQUIRE(d.settings.recordPath     == "output.mp4");
    std::remove(TEST_JSON);
}

TEST_CASE("load with empty grid uses grid defaults", "[serializer]") {
    write(TEST_JSON, MINIMAL_PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.grid.count        == 4);
    REQUIRE(d.grid.sizeX        == Approx(10.0f));
    REQUIRE(d.grid.subdivisions == 30);
    std::remove(TEST_JSON);
}

TEST_CASE("load with empty physicsObjects array gives empty objects list", "[serializer]") {
    write(TEST_JSON, MINIMAL_PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects.empty());
    std::remove(TEST_JSON);
}

TEST_CASE("load with empty clouds array gives empty clouds list", "[serializer]") {
    write(TEST_JSON, MINIMAL_PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds.empty());
    std::remove(TEST_JSON);
}

// ── Multiple physics objects ──────────────────────────────────────────────────

static const std::string TWO_OBJECTS = R"({
  "physicsObjects": [
    { "name": "Sun",  "mass": 100.0, "position": [0.0,0.0,0.0], "velocity": [0.0,0.0,0.0],
      "shaderType": 1, "temperature": 5778.0, "schwarzschildRadius": 0.01,
      "color": [1.0,0.9,0.7] },
    { "name": "Planet", "mass": 1.0, "position": [5.0,0.0,0.0], "velocity": [0.0,1.0,0.0],
      "shaderType": 0, "temperature": 0.0, "schwarzschildRadius": 0.0,
      "color": [0.2,0.4,0.8] }
  ],
  "clouds": [],
  "grid": {},
  "settings": {}
})";

TEST_CASE("load two physics objects preserves count", "[serializer]") {
    write(TEST_JSON, TWO_OBJECTS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects.size() == 2);
    std::remove(TEST_JSON);
}

TEST_CASE("load two objects preserves first object name and mass", "[serializer]") {
    write(TEST_JSON, TWO_OBJECTS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[0].name == "Sun");
    REQUIRE(d.objects[0].mass == Approx(100.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load two objects preserves second object position", "[serializer]") {
    write(TEST_JSON, TWO_OBJECTS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[1].position.x == Approx(5.0f));
    REQUIRE(d.objects[1].position.y == Approx(0.0f));
    REQUIRE(d.objects[1].position.z == Approx(0.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load two objects preserves second object velocity", "[serializer]") {
    write(TEST_JSON, TWO_OBJECTS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[1].velocity.x == Approx(0.0f));
    REQUIRE(d.objects[1].velocity.y == Approx(1.0f));
    REQUIRE(d.objects[1].velocity.z == Approx(0.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load two objects preserves shader types", "[serializer]") {
    write(TEST_JSON, TWO_OBJECTS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[0].shaderType == 1);
    REQUIRE(d.objects[1].shaderType == 0);
    std::remove(TEST_JSON);
}

TEST_CASE("load two objects preserves schwarzschild radius", "[serializer]") {
    write(TEST_JSON, TWO_OBJECTS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[0].schwarzschildRadius == Approx(0.01f));
    REQUIRE(d.objects[1].schwarzschildRadius == Approx(0.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load two objects preserves full color", "[serializer]") {
    write(TEST_JSON, TWO_OBJECTS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[1].color.x == Approx(0.2f));
    REQUIRE(d.objects[1].color.y == Approx(0.4f));
    REQUIRE(d.objects[1].color.z == Approx(0.8f));
    std::remove(TEST_JSON);
}

// ── Cloud data loading ────────────────────────────────────────────────────────

static const std::string WITH_CLOUD = R"({
  "physicsObjects": [],
  "clouds": [
    {
      "enabled": true, "count": 5000,
      "sizeX": 4.0, "sizeY": 2.0, "sizeZ": 4.0,
      "formationFile": "",
      "computeMethod": 1, "theta": 0.6,
      "temperature": 8000.0, "renderMode": 1,
      "nebulaScatterScale": 0.7, "particleSizeSpread": 0.1, "scale": 2.0
    }
  ],
  "grid": {},
  "settings": {}
})";

TEST_CASE("load cloud count", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds.size() == 1);
    std::remove(TEST_JSON);
}

TEST_CASE("load cloud enabled flag", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].enabled == true);
    std::remove(TEST_JSON);
}

TEST_CASE("load cloud particle count", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].count == 5000);
    std::remove(TEST_JSON);
}

TEST_CASE("load cloud size dimensions", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].sizeX == Approx(4.0f));
    REQUIRE(d.clouds[0].sizeY == Approx(2.0f));
    REQUIRE(d.clouds[0].sizeZ == Approx(4.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load cloud compute method and theta", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].computeMethod == 1);
    REQUIRE(d.clouds[0].theta == Approx(0.6f));
    std::remove(TEST_JSON);
}

TEST_CASE("load cloud temperature", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].temperature == Approx(8000.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load cloud render mode", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].renderMode == 1);
    std::remove(TEST_JSON);
}

TEST_CASE("load cloud nebula scatter scale", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].nebulaScatterScale == Approx(0.7f));
    std::remove(TEST_JSON);
}

TEST_CASE("load cloud scale", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].scale == Approx(2.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load cloud particle size spread", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].particleSizeSpread == Approx(0.1f));
    std::remove(TEST_JSON);
}

// ── Multiple clouds ───────────────────────────────────────────────────────────

static const std::string TWO_CLOUDS = R"({
  "physicsObjects": [],
  "clouds": [
    { "enabled": true,  "count": 1000, "temperature": 3000.0,
      "renderMode": 0, "sizeX": 1.0, "sizeY": 1.0, "sizeZ": 1.0,
      "formationFile": "", "computeMethod": 0, "theta": 0.5,
      "nebulaScatterScale": 0.4, "particleSizeSpread": 0.0, "scale": 1.0 },
    { "enabled": false, "count": 2000, "temperature": 9000.0,
      "renderMode": 1, "sizeX": 2.0, "sizeY": 2.0, "sizeZ": 2.0,
      "formationFile": "", "computeMethod": 1, "theta": 0.3,
      "nebulaScatterScale": 0.5, "particleSizeSpread": 0.2, "scale": 3.0 }
  ],
  "grid": {},
  "settings": {}
})";

TEST_CASE("load two clouds preserves count", "[serializer]") {
    write(TEST_JSON, TWO_CLOUDS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds.size() == 2);
    std::remove(TEST_JSON);
}

TEST_CASE("load two clouds preserves first cloud fields", "[serializer]") {
    write(TEST_JSON, TWO_CLOUDS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].enabled     == true);
    REQUIRE(d.clouds[0].count       == 1000);
    REQUIRE(d.clouds[0].temperature == Approx(3000.0f));
    REQUIRE(d.clouds[0].renderMode  == 0);
    std::remove(TEST_JSON);
}

TEST_CASE("load two clouds preserves second cloud fields", "[serializer]") {
    write(TEST_JSON, TWO_CLOUDS);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[1].enabled     == false);
    REQUIRE(d.clouds[1].count       == 2000);
    REQUIRE(d.clouds[1].temperature == Approx(9000.0f));
    REQUIRE(d.clouds[1].renderMode  == 1);
    std::remove(TEST_JSON);
}

// ── Physics object velocity ───────────────────────────────────────────────────

TEST_CASE("load preserves physics object velocity", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[0].velocity.x == Approx(0.1f));
    REQUIRE(d.objects[0].velocity.y == Approx(0.2f));
    REQUIRE(d.objects[0].velocity.z == Approx(0.3f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves physics object schwarzschild radius", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[0].schwarzschildRadius == Approx(0.02f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves physics object full color", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[0].color.x == Approx(1.0f));
    REQUIRE(d.objects[0].color.y == Approx(0.9f));
    REQUIRE(d.objects[0].color.z == Approx(0.7f));
    std::remove(TEST_JSON);
}

// ── MilkyWayTemplate ─────────────────────────────────────────────────────────

TEST_CASE("MilkyWayTemplate returns non-empty data", "[serializer]") {
    ProjectData t = ProjectSerializer::MilkyWayTemplate();
    REQUIRE(!t.objects.empty());
}

TEST_CASE("MilkyWayTemplate objects have positive mass", "[serializer]") {
    ProjectData t = ProjectSerializer::MilkyWayTemplate();
    for (const auto& obj : t.objects) {
        REQUIRE(obj.mass >= 0.0f);
    }
}

TEST_CASE("MilkyWayTemplate has at least one cloud", "[serializer]") {
    ProjectData t = ProjectSerializer::MilkyWayTemplate();
    REQUIRE(!t.clouds.empty());
}

// ── Edge cases ────────────────────────────────────────────────────────────────

TEST_CASE("load extra unknown JSON fields does not crash", "[serializer]") {
    std::string json = R"({"physicsObjects":[],"clouds":[],"grid":{},"settings":{},"unknownField":42})";
    write(TEST_JSON, json);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects.empty());
    std::remove(TEST_JSON);
}

TEST_CASE("load empty JSON object does not crash", "[serializer]") {
    write(TEST_JSON, "{}");
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects.empty());
    REQUIRE(d.clouds.empty());
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves sim speed", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.simSpeed == Approx(2.5f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves RAM budget", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.ramBudgetGB == Approx(4.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves nebula detail", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.nebulaDetail == Approx(0.7f));
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves live resolution preset", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.rtLiveResPreset == 4);
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves record resolution preset", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.recordResPreset == 5);
    std::remove(TEST_JSON);
}

TEST_CASE("load preserves physics object temperature", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[0].temperature == Approx(5778.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load camera keyframe position is correct", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.cameraKeyframes[1].pos[0] == Approx(2.0f));
    REQUIRE(d.settings.cameraKeyframes[1].pos[1] == Approx(1.0f));
    REQUIRE(d.settings.cameraKeyframes[1].pos[2] == Approx(3.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load camera keyframe roll is correct", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.cameraKeyframes[1].roll == Approx(5.0f));
    std::remove(TEST_JSON);
}

TEST_CASE("load produces correct number of keypoints", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.keypoints.size() == 2);
    std::remove(TEST_JSON);
}

TEST_CASE("load produces correct number of camera keyframes", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.cameraKeyframes.size() == 2);
    std::remove(TEST_JSON);
}

TEST_CASE("load rt max steps is preserved", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.rtMaxSteps == 512);
    std::remove(TEST_JSON);
}

TEST_CASE("load rt max bounces is preserved", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.settings.rtMaxBounces == 3);
    std::remove(TEST_JSON);
}

TEST_CASE("load physics object name is correct", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects[0].name == "Sol");
    std::remove(TEST_JSON);
}

TEST_CASE("MilkyWayTemplate objects have names", "[serializer]") {
    ProjectData t = ProjectSerializer::MilkyWayTemplate();
    for (const auto& obj : t.objects) {
        REQUIRE(!obj.name.empty());
    }
}

TEST_CASE("default ProjectData has empty objects and clouds", "[serializer]") {
    ProjectData d;
    REQUIRE(d.objects.empty());
    REQUIRE(d.clouds.empty());
}

TEST_CASE("load cloud with formation file empty string", "[serializer]") {
    write(TEST_JSON, WITH_CLOUD);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.clouds[0].formationFile.empty());
    std::remove(TEST_JSON);
}

TEST_CASE("load multiple loads give same result", "[serializer]") {
    write(TEST_JSON, PROJECT);
    ProjectData d1 = ProjectSerializer::Load(TEST_JSON);
    ProjectData d2 = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d1.objects.size() == d2.objects.size());
    REQUIRE(d1.settings.camX == Approx(d2.settings.camX));
    REQUIRE(d1.settings.recordPath == d2.settings.recordPath);
    std::remove(TEST_JSON);
}

TEST_CASE("default GridData sizeX and sizeZ are equal", "[serializer]") {
    GridData g;
    REQUIRE(g.sizeX == Approx(g.sizeZ));
}

TEST_CASE("default CloudData formationFile is empty", "[serializer]") {
    CloudData c;
    REQUIRE(c.formationFile.empty());
}

TEST_CASE("load with only physicsObjects key still returns objects", "[serializer]") {
    std::string json = R"({"physicsObjects":[
        {"name":"A","mass":5.0,"position":[0,0,0],"velocity":[0,0,0],
         "shaderType":0,"temperature":0.0,"schwarzschildRadius":0.0,
         "color":[1,1,1]}]})";
    write(TEST_JSON, json);
    ProjectData d = ProjectSerializer::Load(TEST_JSON);
    REQUIRE(d.objects.size() == 1);
    REQUIRE(d.objects[0].name == "A");
    std::remove(TEST_JSON);
}
