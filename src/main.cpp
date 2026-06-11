#include <cstdlib>
#include <cmath>
#include <cstring>
#include <optional>
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
#include "proceduralGen.h"

// ─── Helper: build scene from ProjectData ────────────────────────────────────

static std::unique_ptr<CloudObject> buildCloudFromData(const CloudData& cd) {
  std::unique_ptr<CloudObject> cloud;
  if (!cd.formationFile.empty()) {
    std::string formPath = "templates/formations/" + cd.formationFile;
    cloud = std::make_unique<CloudObject>(vec3{0, 0, -3}, formPath);
  } else {
    cloud = std::make_unique<CloudObject>(
      vec3{0, 0, -3}, cd.count, randomDistribution,
      vec3{cd.sizeX, cd.sizeY, cd.sizeZ});
  }
  cloud->formationFile      = cd.formationFile;   // keep bare filename, not full path
  cloud->computeMethod      = static_cast<CloudComputeMethod>(cd.computeMethod);
  cloud->barnesHutTheta     = cd.theta;
  cloud->temperature        = cd.temperature;
  cloud->renderMode         = cd.renderMode;
  cloud->nebulaScatterScale = cd.nebulaScatterScale;
  cloud->particleSizeSpread = cd.particleSizeSpread;
  cloud->scale              = cd.scale;
  if (cd.scale != 1.0f)
    cloud->applyVirialScale(cd.scale);
  return cloud;
}

static void buildScene(
  const ProjectData&                        data,
  std::vector<PhysicsObject>&               physicsObjects,
  std::vector<LineObject>&                  lineObjects,
  std::optional<GridObject>&                grid,
  std::vector<std::unique_ptr<CloudObject>>& clouds)
{
  physicsObjects.clear();
  lineObjects.clear();
  clouds.clear();

  for (const auto& pod : data.objects) {
    ObjectShaderType st = ObjectShaderType::Planet;
    if (pod.shaderType == 1)      st = ObjectShaderType::Star;
    else if (pod.shaderType == 2) st = ObjectShaderType::BlackHole;
    physicsObjects.emplace_back(
      vec3{pod.velocity.x, pod.velocity.y, pod.velocity.z},
      vec3{pod.position.x, pod.position.y, pod.position.z},
      pod.mass, pod.name, st, pod.temperature);
    if (pod.schwarzschildRadius > 0.0f)
      physicsObjects.back().schwarzschildRadius = pod.schwarzschildRadius;
    physicsObjects.back().data.color = pod.color;
    if (!pod.texturePath.empty()) {
      physicsObjects.back().texturePath = pod.texturePath;
      physicsObjects.back().renderedObject.loadTexture(pod.texturePath);
    }
  }
  for (auto& obj : physicsObjects)
    lineObjects.emplace_back(vec3{obj.data.position});

  const GridData& g = data.grid;
  grid.emplace(g.cellSize, g.radius, g.showX, g.showY, g.showZ);

  for (const auto& cd : data.clouds) {
    if (cd.enabled)
      clouds.push_back(buildCloudFromData(cd));
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

  std::vector<PhysicsObject>               physicsObjects;
  std::vector<LineObject>                  lineObjects;
  std::optional<GridObject>                grid;
  std::vector<std::unique_ptr<CloudObject>> clouds;
  // Pre-reserve to avoid reallocation (PhysicsObject holds OpenGL handles —
  // reallocation would copy/move them and corrupt GPU state)
  physicsObjects.reserve(256);
  lineObjects.reserve(256);

  GridData currentGrid;

  ProceduralGenWindow procGen;

  PlaneObject background{vec3{0, 0, -3}, 1, 1};
  background.SetShaders("src/shaders/raytracerVertex.glsl",
                        "src/shaders/spaceBackgroundFrag.glsl");

  RenderedObject skybox;
  skybox.GenerateMeshPlane(1, 1);
  skybox.setupShaders("src/shaders/raytracerVertex.glsl",
                      "src/shaders/skyboxFrag.glsl");
  skybox.loadTextureHDR("assets/default_spheremap.hdr");
  renderer.skyboxTexID = skybox.textureHandle();

  // ── Scene callbacks ────────────────────────────────────────────────────────
  SceneCallbacks cb;

  cb.spawnPhysicsObject = [&](const SpawnFormState& form) {
    ObjectShaderType st = ObjectShaderType::Planet;
    if (form.shaderType == 1)      st = ObjectShaderType::Star;
    else if (form.shaderType == 2) st = ObjectShaderType::BlackHole;
    physicsObjects.emplace_back(
      vec3{form.velX, form.velY, form.velZ},
      vec3{form.posX, form.posY, form.posZ},
      form.mass, std::string(form.name), st, form.temperature);
    lineObjects.emplace_back(vec3{form.posX, form.posY, form.posZ});
  };

  cb.applyGrid = [&](const GridFormState& gf) {
    currentGrid.visible  = gf.visible;
    currentGrid.cellSize = gf.cellSize;
    currentGrid.radius   = gf.radius;
    currentGrid.showX   = gf.showX;
    currentGrid.showY   = gf.showY;
    currentGrid.showZ   = gf.showZ;
    if (grid.has_value()) {
      grid->Rebuild(gf.cellSize, gf.radius, gf.showX, gf.showY, gf.showZ);
    } else {
      grid.emplace(gf.cellSize, gf.radius, gf.showX, gf.showY, gf.showZ);
    }
  };

  cb.applyCloud = [&](const CloudFormState& cf) {
    if (!cf.enabled) return;
    CloudData cd{true, cf.count, cf.sizeX, cf.sizeY, cf.sizeZ,
                 cf.formationFile, cf.computeMethod, cf.theta,
                 cf.temperature, cf.renderMode, cf.nebulaScatterScale, cf.particleSizeSpread, cf.scale};
    clouds.push_back(buildCloudFromData(cd));
  };

  cb.deleteCloud = [&](int cloudIdx) {
    if (cloudIdx < 0 || cloudIdx >= (int)clouds.size()) return;
    clouds.erase(clouds.begin() + cloudIdx);
  };

  cb.respawnCloud = [&](int cloudIdx, const CloudFormState& cf) {
    if (cloudIdx < 0 || cloudIdx >= (int)clouds.size()) return;
    CloudData cd{true, cf.count, cf.sizeX, cf.sizeY, cf.sizeZ,
                 cf.formationFile, cf.computeMethod, cf.theta,
                 cf.temperature, cf.renderMode, cf.nebulaScatterScale, cf.particleSizeSpread, cf.scale};
    clouds[cloudIdx] = buildCloudFromData(cd);
  };

  procGen.onGenerate = [&](std::vector<CloudParticle> pts, int cm, float temp) {
    auto cloud = std::make_unique<CloudObject>(vec3{0, 0, -3}, std::move(pts));
    cloud->computeMethod = static_cast<CloudComputeMethod>(cm);
    cloud->temperature   = temp;
    clouds.push_back(std::move(cloud));
  };

  cb.loadSpheremap = [&](const std::string& path) {
    skybox.loadTextureHDR(path);
    renderer.skyboxTexID = skybox.textureHandle();
  };

  cb.deleteObject = [&](int index) {
    if (index < 0 || index >= (int)physicsObjects.size()) return;
    physicsObjects.erase(physicsObjects.begin() + index);
    if (index < (int)lineObjects.size())
      lineObjects.erase(lineObjects.begin() + index);
  };

  auto applySettingsToRenderer = [&](const SceneSettings& s) {
    renderer.cameraTranslate[0] = s.camX;
    renderer.cameraTranslate[1] = s.camY;
    renderer.cameraTranslate[2] = s.camZ;
    renderer.rotation = s.camRotation;
    renderer.pitch    = s.camPitch;
    renderer.roll     = s.camRoll;
    renderer.zoom     = s.camZoom;
    renderer.syncMatrixFromEuler();
    renderer.raytracerMethod  = s.raytracerMethod;
    renderer.raytracerIsMain  = s.raytracerIsMain;
    renderer.raytracerEnabled = s.raytracerEnabled;
    renderer.dopplerMode          = s.dopplerMode;
    renderer.dopplerVelScale      = s.dopplerVelScale;
    renderer.dopplerBrightnessStr = s.dopplerBrightnessStr;
    renderer.dopplerColorStr      = s.dopplerColorStr;
    renderer.spheremapEnabled  = s.spheremapEnabled;
    renderer.spheremapExposure = s.spheremapExposure;
    if (s.spheremapPath != std::string(renderer.spheremapPathBuf)) {
      std::strncpy(renderer.spheremapPathBuf, s.spheremapPath.c_str(),
                   sizeof(renderer.spheremapPathBuf) - 1);
      renderer.spheremapPathBuf[sizeof(renderer.spheremapPathBuf) - 1] = '\0';
      skybox.loadTextureHDR(s.spheremapPath);
      renderer.skyboxTexID = skybox.textureHandle();
    }
    renderer.nebulaDetail  = s.nebulaDetail;
    renderer.simSpeed      = s.simSpeed;
    renderer.ramBudgetGB   = s.ramBudgetGB;
    renderer.recStartFrame = s.recStartFrame;
    renderer.recStopFrame  = s.recStopFrame;
    renderer.keypoints       = s.keypoints;
    renderer.cameraKeyframes = s.cameraKeyframes;
    renderer.SetRtMaxBounces(s.rtMaxBounces);
    renderer.SetRtMaxSteps(s.rtMaxSteps);
    renderer.SetRtLiveRes(s.rtLiveResPreset, s.rtLiveWidth, s.rtLiveHeight);
    renderer.SetRecordRes(s.recordResPreset, s.recordWidth, s.recordHeight);
    renderer.SetRecordFps(s.recordFps);
    renderer.SetRecordPath(s.recordPath);
  };

  cb.saveProject = [&]() {
    std::string path(renderer.savePathBuf);
    if (path.empty()) path = "project.json";
    std::vector<CloudData> cloudDatas;
    for (const auto& c : clouds) {
      cloudDatas.push_back(CloudData{
        true, c->particleCount(),
        3.f, 3.f, 3.f, c->formationFile, static_cast<int>(c->computeMethod),
        c->barnesHutTheta, c->temperature, c->renderMode,
        c->nebulaScatterScale, c->particleSizeSpread, c->scale
      });
    }
    SceneSettings s;
    s.camX        = renderer.cameraTranslate[0];
    s.camY        = renderer.cameraTranslate[1];
    s.camZ        = renderer.cameraTranslate[2];
    s.camRotation = renderer.rotation;
    s.camPitch    = renderer.pitch;
    s.camRoll     = renderer.roll;
    s.camZoom     = renderer.zoom;
    s.raytracerMethod  = renderer.raytracerMethod;
    s.raytracerIsMain  = renderer.raytracerIsMain;
    s.raytracerEnabled = renderer.raytracerEnabled;
    s.dopplerMode          = renderer.dopplerMode;
    s.dopplerVelScale      = renderer.dopplerVelScale;
    s.dopplerBrightnessStr = renderer.dopplerBrightnessStr;
    s.dopplerColorStr      = renderer.dopplerColorStr;
    s.spheremapEnabled  = renderer.spheremapEnabled;
    s.spheremapExposure = renderer.spheremapExposure;
    s.spheremapPath     = std::string(renderer.spheremapPathBuf);
    s.nebulaDetail    = renderer.nebulaDetail;
    s.rtMaxBounces    = renderer.GetRtMaxBounces();
    s.rtMaxSteps      = renderer.GetRtMaxSteps();
    s.rtLiveResPreset = renderer.GetRtLiveResPreset();
    s.rtLiveWidth     = renderer.GetRtLiveWidth();
    s.rtLiveHeight    = renderer.GetRtLiveHeight();
    s.simSpeed        = renderer.simSpeed;
    s.ramBudgetGB     = renderer.ramBudgetGB;
    s.recordResPreset = renderer.GetRecordResPreset();
    s.recordWidth     = renderer.GetRecordWidth();
    s.recordHeight    = renderer.GetRecordHeight();
    s.recordFps       = renderer.GetRecordFps();
    s.recordPath      = renderer.GetRecordPath();
    s.recStartFrame   = renderer.recStartFrame;
    s.recStopFrame    = renderer.recStopFrame;
    s.keypoints       = renderer.keypoints;
    s.cameraKeyframes = renderer.cameraKeyframes;
    ProjectSerializer::Save(path, physicsObjects, currentGrid, cloudDatas, s);
  };

  cb.loadProject = [&](const std::string& path) {
    ProjectData data = ProjectSerializer::Load(path);
    currentGrid = data.grid;
    buildScene(data, physicsObjects, lineObjects, grid, clouds);
    applySettingsToRenderer(data.settings);
    renderer.gridForm.visible  = currentGrid.visible;
    renderer.gridForm.cellSize = currentGrid.cellSize;
    renderer.gridForm.radius   = currentGrid.radius;
    renderer.gridForm.showX   = currentGrid.showX;
    renderer.gridForm.showY   = currentGrid.showY;
    renderer.gridForm.showZ   = currentGrid.showZ;
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
      ProjectData tmpl = ProjectSerializer::MilkyWayTemplate();
      currentGrid = tmpl.grid;
      buildScene(tmpl, physicsObjects, lineObjects, grid, clouds);
    } else if (renderer.startupChoice == SC::Load) {
      ProjectData data = ProjectSerializer::Load(
        std::string(renderer.startupLoadPath));
      currentGrid = data.grid;
      buildScene(data, physicsObjects, lineObjects, grid, clouds);
      applySettingsToRenderer(data.settings);
    } else {
      // SC::Empty → create grid with defaults
      grid.emplace(currentGrid.cellSize, currentGrid.radius,
                   currentGrid.showX, currentGrid.showY, currentGrid.showZ);
    }
    // Sync renderer's gridForm with current grid settings
    renderer.gridForm.visible  = currentGrid.visible;
    renderer.gridForm.cellSize = currentGrid.cellSize;
    renderer.gridForm.radius   = currentGrid.radius;
    renderer.gridForm.showX   = currentGrid.showX;
    renderer.gridForm.showY   = currentGrid.showY;
    renderer.gridForm.showZ   = currentGrid.showZ;
  }

  // ── Main game loop ─────────────────────────────────────────────────────────
  while (true) {
    if (!renderer.BeginFrame()) continue;

    // Set primary view based on which view is "main"
    // raytracerIsMain=false → rasterizer fullscreen (primary), raytracer PiP (secondary)
    // raytracerIsMain=true  → raytracer fullscreen (primary), rasterizer PiP (secondary)
    renderer.rayTracerView = renderer.raytracerIsMain;

    // In editor viewport mode, redirect all primary drawing into the viewport FBO.
    // Must happen before any draw calls so every object ends up in the FBO.
    renderer.BindViewportFBO();

    // Spheremap background — drawn first, depth writes off (rasterized view only)
    renderer.DrawSkybox(skybox);

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

    // Freeze simulation while a recording frame is being assembled across strips
    bool recOverridePause = renderer.IsRecording() && renderer.recFrameActive;
    bool savedPaused = renderer.paused;
    if (recOverridePause) renderer.paused = true;

    // Physics objects + trail lines
    for (int i = 0; i < (int)physicsObjects.size(); i++) {
      physicsObjects[i].Update(physicsObjects, renderer);
      lineObjects[i].Update(renderer);
      // Only grow trails when simulating new frames forward
      if (!renderer.paused && renderer.playingForward) {
        lineObjects[i].AddPoint(physicsObjects[i].data.position);
      }
      // Propagate black hole Schwarzschild radius to the renderer
      if (physicsObjects[i].shaderType == ObjectShaderType::BlackHole) {
        renderer.bhSchwarzschildRadius = physicsObjects[i].schwarzschildRadius;
      }
    }

    // Gather physics data for grid/cloud
    std::vector<PhysicsObjectStructure> physData;
    physData.reserve(physicsObjects.size());
    for (const auto& obj : physicsObjects)
      physData.emplace_back(obj.data);

    if (grid.has_value() && currentGrid.visible) {
      float snap = std::max(0.001f, currentGrid.cellSize);
      float camX = -renderer.cameraTranslate[0];
      float camY = -renderer.cameraTranslate[1];
      float camZ = -renderer.cameraTranslate[2];
      grid->position = {
        std::floor(camX / snap) * snap,
        std::floor(camY / snap) * snap,
        std::floor(camZ / snap) * snap
      };
      grid->Update(renderer, physData);
    }

    for (auto& c : clouds)
      c->Update(renderer, physData);

    if (recOverridePause) renderer.paused = savedPaused;

    // ── Propagate RAM budget to all FrameStores ───────────────────────────
    {
      size_t totalBudget = static_cast<size_t>(renderer.ramBudgetGB * (1024.0 * 1024.0 * 1024.0));
      int storeCount = (int)physicsObjects.size() + (int)clouds.size();
      if (storeCount > 0) {
        size_t perStore = totalBudget / (size_t)storeCount;
        for (auto& obj : physicsObjects)
          obj.setRamBudget(perStore);
        for (auto& c : clouds)
          c->setRamBudget(perStore);
      }
      // Trim trail lines to match the physics buffer frame count
      // (use first physics object's frame count as the reference)
      if (!physicsObjects.empty()) {
        size_t maxTrailPoints = physicsObjects[0].getBufferSize();
        for (auto& line : lineObjects)
          line.TrimLinePoints(maxTrailPoints);
      }
    }

    // ── Camera keyframe interpolation ──────────────────────────────────────
    // When playing, interpolate camera between keyframes
    if (!renderer.paused && !renderer.cameraKeyframes.empty() && !physicsObjects.empty()) {
      unsigned int curFrame = physicsObjects[0].getTimeframe();
      auto& kfs = renderer.cameraKeyframes;
      // Find bracketing keyframes
      const CameraKeyframe* before = nullptr;
      const CameraKeyframe* after  = nullptr;
      for (auto& kf : kfs) {
        if (kf.frame <= curFrame) before = &kf;
        if (kf.frame >= curFrame && !after) after = &kf;
      }
      // Only interpolate if we're within the keyframed range
      if (before && after) {
        if (before->frame == after->frame) {
          // Exactly on a keyframe
          renderer.cameraTranslate[0] = before->pos[0];
          renderer.cameraTranslate[1] = before->pos[1];
          renderer.cameraTranslate[2] = before->pos[2];
          renderer.rotation = before->rotation;
          renderer.pitch    = before->pitch;
          renderer.roll     = before->roll;
          renderer.zoom     = before->zoom;
          renderer.syncMatrixFromEuler();
        } else {
          // Linear interpolation
          float t = (float)(curFrame - before->frame) / (float)(after->frame - before->frame);
          renderer.cameraTranslate[0] = before->pos[0] + t * (after->pos[0] - before->pos[0]);
          renderer.cameraTranslate[1] = before->pos[1] + t * (after->pos[1] - before->pos[1]);
          renderer.cameraTranslate[2] = before->pos[2] + t * (after->pos[2] - before->pos[2]);
          renderer.rotation = before->rotation + t * (after->rotation - before->rotation);
          renderer.pitch    = before->pitch    + t * (after->pitch    - before->pitch);
          renderer.roll     = before->roll     + t * (after->roll     - before->roll);
          renderer.zoom     = before->zoom     + t * (after->zoom     - before->zoom);
          renderer.syncMatrixFromEuler();
        }
      }
    }

    background.Update(renderer);

    // If primary view is raytraced, dispatch compute shader + blit to screen
    if (renderer.rayTracerView && renderer.raytracerEnabled) {
      int rtw = renderer.GetRtLiveWidth();
      int rth = renderer.GetRtLiveHeight();
      if (rtw <= 0 || rth <= 0) {
        rtw = renderer.GetFbWidth();
        rth = renderer.GetFbHeight();
      }
      // Skip live display update while assembling a recording frame — the scene
      // is frozen during strip assembly so the display wouldn't change anyway,
      // and dispatching here on every strip tick caused ~180x slowdown at 720p.
      if (!renderer.recFrameActive) {
        renderer.DispatchRaytracer(rtw, rth);
        renderer.BlitRaytracerToScreen();
      }

      if (renderer.IsRecording())
        renderer.DispatchAndCaptureRecordingFrame();
    }

    // Unbind the editor viewport FBO before the secondary pass and UI
    renderer.UnbindViewportFBO();

    // ── Secondary (PiP) render pass ─────────────────────────────────────────
    // Renders the OTHER view (rasterizer or raytracer) into the PiP FBO.
    // BeginSecondaryPass flips rayTracerView and binds the FBO.
    renderer.BeginSecondaryPass();

    // Re-draw all objects into the FBO (no physics, just rendering)
    renderer.DrawSkybox(skybox);
    for (int i = 0; i < (int)physicsObjects.size(); i++) {
      float objType = 0.0f;
      if (physicsObjects[i].shaderType == ObjectShaderType::Star)      objType = 1.0f;
      else if (physicsObjects[i].shaderType == ObjectShaderType::BlackHole) objType = 3.0f;
      renderer.DrawPhysicsObject(physicsObjects[i].renderedObject,
                                 physicsObjects[i].data.mass,
                                 physicsObjects[i].temperature,
                                 objType,
                                 physicsObjects[i].data.velocity,
                                 physicsObjects[i].data.color);
      lineObjects[i].Update(renderer);
    }
    if (grid.has_value() && currentGrid.visible)
      renderer.Draw(grid->renderedObject);
    for (auto& c : clouds) {
      c->renderedObject.uploadTemperature(c->temperature);
      c->renderedObject.uploadRenderMode(c->renderMode);
      renderer.Draw(c->renderedObject);
    }
    background.Update(renderer);

    // If secondary view is raytraced, dispatch compute + blit into the PiP FBO
    if (renderer.rayTracerView && renderer.raytracerEnabled && !renderer.recFrameActive) {
      int pw = renderer.GetRtLiveWidth();
      int ph = renderer.GetRtLiveHeight();
      if (pw <= 0 || ph <= 0) {
        pw = renderer.GetFbWidth();
        ph = renderer.GetFbHeight();
      }
      renderer.DispatchRaytracer(pw, ph);
      renderer.BlitRaytracerToScreen();
    }

    renderer.EndSecondaryPass();
    // ── End secondary pass ──────────────────────────────────────────────────

    // Draw all UI panels
    renderer.DrawUI(physicsObjects, clouds, cb);

    // Procedural cloud generator (standalone window, opened from Cloud spawn tab)
    if (renderer.showProceduralGen) procGen.open = true;
    procGen.draw();
    renderer.showProceduralGen = procGen.open;

    if (!renderer.UpdateInputs()) {
      std::cout << "Exiting\n";
      return 0;
    }

    // ── Handle camera keyframe capture request ─────────────────────────────
    if (renderer.captureRequested) {
      renderer.captureRequested = false;
      if (!physicsObjects.empty()) {
        unsigned int curFrame = physicsObjects[0].getTimeframe();
        renderer.InsertCameraKeyframe(curFrame);
      }
    }

    // ── Handle camera keyframe clear request ────────────────────────────────
    if (renderer.clearCaptureRequested) {
      renderer.clearCaptureRequested = false;
      if (!physicsObjects.empty()) {
        unsigned int curFrame = physicsObjects[0].getTimeframe();
        renderer.RemoveCameraKeyframe(curFrame);
      }
    }

    // ── Handle recording keyframe requests ─────────────────────────────────
    if (renderer.recStartRequested) {
      renderer.recStartRequested = false;
      if (!physicsObjects.empty()) {
        renderer.recStartFrame = (int)physicsObjects[0].getTimeframe();
      }
    }
    if (renderer.recStopRequested) {
      renderer.recStopRequested = false;
      if (!physicsObjects.empty()) {
        renderer.recStopFrame = (int)physicsObjects[0].getTimeframe();
      }
    }

    // ── Handle marker-based recording (R with both markers set) ──────────
    if (renderer.recMarkerRecordRequested) {
      renderer.recMarkerRecordRequested = false;
      if (!physicsObjects.empty() && renderer.recStartFrame >= 0 && renderer.recStopFrame >= 0) {
        // Jump to start frame
        for (auto& obj : physicsObjects)
          obj.setTimeframeAndRestore((unsigned int)renderer.recStartFrame);
        for (auto& c : clouds)
          c->setTimeframeAndRestore((unsigned int)renderer.recStartFrame);
        // Start recording, unpause, ensure forward playback
        renderer.StartRecording();
        renderer.paused = false;
        renderer.playingForward = true;
      }
    }

    // ── Auto-stop recording when playhead reaches stop marker ──────────────
    if (!renderer.paused && renderer.IsRecording()
        && renderer.recStopFrame >= 0 && !physicsObjects.empty()) {
      unsigned int curFrame = physicsObjects[0].getTimeframe();
      if ((int)curFrame >= renderer.recStopFrame) {
        renderer.StopRecording();
        renderer.paused = true;
      }
    }

    renderer.EndFrame();
  }

  return 0;
}
