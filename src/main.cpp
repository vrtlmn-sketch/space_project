#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>

#include "mathStructs.h"
#include "renderedObject.h"
#include "physicsObject.h"
#include "renderer.h"
#include "planeObject.h"
#include "lineObject.h"
#include "cloudObject.h"
#include "gridObject.h"
#include "projectSerializer.h"

// ─── Helper: build scene from ProjectData ────────────────────────────────────

static void buildScene(
  const ProjectData&            data,
  std::vector<PhysicsObject>&   physicsObjects,
  std::vector<LineObject>&      lineObjects,
  std::vector<GridObject>&      grids,
  std::unique_ptr<CloudObject>& cloud)
{
  physicsObjects.clear();
  lineObjects.clear();
  grids.clear();
  cloud.reset();

  for (const auto& pod : data.objects) {
    ObjectShaderType st = (pod.shaderType == 1)
                        ? ObjectShaderType::Star
                        : ObjectShaderType::Planet;
    physicsObjects.emplace_back(
      vec3{pod.velocity.x, pod.velocity.y, pod.velocity.z},
      vec3{pod.position.x, pod.position.y, pod.position.z},
      pod.mass, pod.name, st, pod.temperature);
  }
  for (auto& obj : physicsObjects)
    lineObjects.emplace_back(vec3{obj.data.position});

  const GridData& g = data.grid;
  grids.reserve((size_t)g.count);
  for (int i = -g.count / 2; i < g.count / 2; i++) {
    grids.emplace_back(GridObject{
      vec3{0, (float)i * g.ySpacing, -3.f},
      vec3{g.sizeX, 10.f, g.sizeZ},
      g.subdivisions
    });
  }

  if (data.cloud.enabled) {
    cloud = std::make_unique<CloudObject>(
      vec3{0, 0, -3},
      data.cloud.count,
      sphereDistribution,
      vec3{data.cloud.sizeX, data.cloud.sizeY, data.cloud.sizeZ}
    );
  }
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {

  Renderer renderer;
  renderer.InitWindow("BlackholeSim", 1200, 800);

  // --template flag: skip startup modal and load solar system template directly
  if (argc > 1 && std::string(argv[1]) == "--template") {
    renderer.showStartupModal = false;
    renderer.startupChoice = Renderer::StartupChoice::Template;
  }

  std::vector<PhysicsObject>   physicsObjects;
  std::vector<LineObject>      lineObjects;
  std::vector<GridObject>      grids;
  std::unique_ptr<CloudObject> cloud;
  // Pre-reserve to avoid reallocation (PhysicsObject holds OpenGL handles —
  // reallocation would copy/move them and corrupt GPU state)
  physicsObjects.reserve(256);
  lineObjects.reserve(256);
  grids.reserve(256); // GridObject holds OpenGL handles; reallocation would corrupt them

  GridData  currentGrid  = GridData{4, 10.f, 10.f, 30, 2.f};
  CloudData currentCloud = CloudData{false, 2000, 3.f, 3.f, 3.f};

  PlaneObject background{vec3{0, 0, -3}, 1, 1};
  background.SetShaders("src/shaders/raytracerVertex.glsl",
                        "src/shaders/spaceBackgroundFrag.glsl");

  // ── Scene callbacks ────────────────────────────────────────────────────────
  SceneCallbacks cb;

  cb.spawnPhysicsObject = [&](const SpawnFormState& form) {
    ObjectShaderType st = (form.shaderType == 1)
                        ? ObjectShaderType::Star
                        : ObjectShaderType::Planet;
    physicsObjects.emplace_back(
      vec3{form.velX, form.velY, form.velZ},
      vec3{form.posX, form.posY, form.posZ},
      form.mass, std::string(form.name), st, form.temperature);
    lineObjects.emplace_back(vec3{form.posX, form.posY, form.posZ});
  };

  cb.applyGrid = [&](const GridFormState& gf) {
    currentGrid = GridData{gf.count, gf.sizeX, gf.sizeZ, gf.subdivisions, gf.ySpacing};
    grids.clear();
    for (int i = -gf.count / 2; i < gf.count / 2; i++) {
      grids.emplace_back(GridObject{
        vec3{0, (float)i * gf.ySpacing, -3.f},
        vec3{gf.sizeX, 10.f, gf.sizeZ},
        gf.subdivisions
      });
    }
  };

  cb.applyCloud = [&](const CloudFormState& cf) {
    currentCloud = CloudData{cf.enabled, cf.count,
                             cf.sizeX, cf.sizeY, cf.sizeZ};
    cloud.reset();
    if (cf.enabled) {
      cloud = std::make_unique<CloudObject>(
        vec3{0, 0, -3}, cf.count, sphereDistribution,
        vec3{cf.sizeX, cf.sizeY, cf.sizeZ});
    }
  };

  cb.deleteObject = [&](int index) {
    if (index < 0 || index >= (int)physicsObjects.size()) return;
    physicsObjects.erase(physicsObjects.begin() + index);
    if (index < (int)lineObjects.size())
      lineObjects.erase(lineObjects.begin() + index);
  };

  cb.saveProject = [&]() {
    std::string path(renderer.savePathBuf);
    if (path.empty()) path = "project.json";
    ProjectSerializer::Save(path, physicsObjects, currentGrid, currentCloud);
  };

  cb.loadProject = [&](const std::string& path) {
    ProjectData data = ProjectSerializer::Load(path);
    currentGrid  = data.grid;
    currentCloud = data.cloud;
    buildScene(data, physicsObjects, lineObjects, grids, cloud);
  };

  // ── Startup modal loop ────────────────────────────────────────────────────
  while (renderer.showStartupModal) {
    if (!renderer.BeginFrame()) continue;
    renderer.DrawStartupModal();
    renderer.EndFrame();
    if (!renderer.UpdateInputs()) return 0;
  }

  // Act on startup choice
  {
    using SC = Renderer::StartupChoice;
    if (renderer.startupChoice == SC::Template) {
      ProjectData tmpl = ProjectSerializer::SolarSystemTemplate();
      currentGrid  = tmpl.grid;
      currentCloud = tmpl.cloud;
      buildScene(tmpl, physicsObjects, lineObjects, grids, cloud);
    } else if (renderer.startupChoice == SC::Load) {
      ProjectData data = ProjectSerializer::Load(
        std::string(renderer.startupLoadPath));
      currentGrid  = data.grid;
      currentCloud = data.cloud;
      buildScene(data, physicsObjects, lineObjects, grids, cloud);
    }
    // SC::Empty → start blank
  }

  // ── Main game loop ─────────────────────────────────────────────────────────
  while (true) {
    if (!renderer.BeginFrame()) continue;

    // Ghost-drag: confirm placement on click
    if (renderer.UpdateGhostDrag(renderer.spawnForm)) {
      cb.spawnPhysicsObject(renderer.spawnForm);
    }

    // Collect star positions + blackbody colours for planet lighting
    std::vector<vec3> starPositions;
    std::vector<vec3> starColors;
    for (const auto& obj : physicsObjects) {
      if (obj.shaderType == ObjectShaderType::Star) {
        starPositions.push_back(obj.data.position);
        // Basic blackbody approximation for the light colour
        float t = obj.temperature;
        float r, g, b;
        if (t <= 6600.f) {
          r = 1.0f;
          g = std::max(0.0f, std::min(1.0f, (0.39008157876901960784f * std::log(t/100.f) - 0.63184144378862745098f)));
          b = (t <= 1900.f) ? 0.0f
            : std::max(0.0f, std::min(1.0f, (0.54320678911019607843f * std::log(t/100.f - 10.f) - 1.19625408914f)));
        } else {
          r = std::max(0.0f, std::min(1.0f, (329.698727446f * std::pow(t/100.f - 60.f, -0.1332047592f)) / 255.f));
          g = std::max(0.0f, std::min(1.0f, (288.1221695283f * std::pow(t/100.f - 60.f, -0.0755148492f)) / 255.f));
          b = 1.0f;
        }
        starColors.push_back(vec3{r, g, b});
      }
    }
    // Upload to all planet shaders
    for (auto& obj : physicsObjects) {
      if (obj.shaderType == ObjectShaderType::Planet && !starPositions.empty()) {
        obj.renderedObject.uploadStarLighting(starPositions, starColors);
      }
      if (obj.shaderType == ObjectShaderType::Star) {
        obj.renderedObject.uploadTemperature(obj.temperature);
      }
    }

    // Physics objects + trail lines
    for (int i = 0; i < (int)physicsObjects.size(); i++) {
      physicsObjects[i].Update(physicsObjects, renderer);
      lineObjects[i].Update(renderer);
      lineObjects[i].AddPoint(physicsObjects[i].data.position);
    }

    // Gather physics data for grid/cloud
    std::vector<PhysicsObjectStructure> physData;
    physData.reserve(physicsObjects.size());
    for (const auto& obj : physicsObjects)
      physData.emplace_back(obj.data);

    for (auto& g : grids)
      g.Update(renderer, physData);

    if (cloud)
      cloud->Update(renderer, physData);

    background.Update(renderer);

    // Draw all UI panels
    renderer.DrawUI(physicsObjects, cloud.get(), cb);

    if (!renderer.UpdateInputs()) {
      std::cout << "Exiting\n";
      return 0;
    }
    renderer.EndFrame();
  }

  return 0;
}
