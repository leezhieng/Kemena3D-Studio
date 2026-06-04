#include "panel_inspector.h"
#include "util.h"
#include "panel_project.h"
#include "commands.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <GL/glew.h>

using namespace kemena;
namespace fs = std::filesystem;

// Forward declarations for file-local helpers used by drawShaderPreview()
static bool beginPropTable(kGuiManager* gui, const char* id);
static void propLabel(kGuiManager* gui, const char* label);

PanelInspector::PanelInspector(kGuiManager *setGuiManager, Manager *setManager)
{
    gui     = setGuiManager;
    manager = setManager;

    kAssetManager *am = manager->getAssetManager();
    if (!am) return;

    auto loadIcon = [&](const char *res) -> ImTextureRef {
        kTexture2D *t = am->loadTexture2DFromResource(res, "icon", kTextureFormat::TEX_FORMAT_RGBA);
        return t ? (ImTextureRef)(intptr_t)t->getTextureID() : nullptr;
    };

    iconObjMesh   = loadIcon("ICON_OBJECT_MESH");
    iconObjLight  = loadIcon("ICON_OBJECT_LIGHT");
    iconObjCamera = loadIcon("ICON_OBJECT_CAMERA");
    iconObjScene  = loadIcon("ICON_OBJECT_SCENE");

    iconFileModel    = loadIcon("ICON_MODEL_FILE");
    iconFileImage    = loadIcon("ICON_IMAGE_FILE");
    iconFileFolder   = loadIcon("ICON_FOLDER_FILE");
    iconFileMaterial = loadIcon("ICON_MATERIAL_FILE");
    iconFilePrefab   = loadIcon("ICON_PREFAB_FILE");
    iconFileAudio    = loadIcon("ICON_AUDIO_FILE");
    iconFileVideo    = loadIcon("ICON_VIDEO_FILE");
    iconFileScript   = loadIcon("ICON_SCRIPT_FILE");
    iconFileText     = loadIcon("ICON_TEXT_FILE");
    iconFileWorld    = loadIcon("ICON_WORLD_FILE");
    iconFileOther    = loadIcon("ICON_OTHER_FILE");
}

PanelInspector::~PanelInspector()
{
    delete previewRenderer;
    for (auto* t : previewDefaultTextures) delete t;
    delete previewShader;
    delete previewMat;
    delete previewCamera;
    delete previewWorld;   // owns previewScene, previewMesh, previewLight

    delete modelViewRenderer;
    delete modelViewCamera;
    delete modelViewWorld;  // owns modelViewScene, modelViewLight (not modelViewMesh — AM-owned)
    for (auto* m : modelViewDefaultMats) delete m;

    delete matViewMat;
    delete matViewRenderer;
    delete matViewCamera;
    delete matViewWorld;  // owns matViewScene, matViewMesh, matViewLight
}

// ---------------------------------------------------------------------------
// Shader preview helpers
// ---------------------------------------------------------------------------

void PanelInspector::initPreviewScene()
{
    if (previewScene) return;

    previewWorld = createWorld(createAssetManager());
    previewScene = previewWorld->createScene("preview");
    previewScene->setFrustumCullingEnabled(false);
    // No shadows in the preview window (see matViewer note).
    previewScene->setShadowsEnabled(false);
    previewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    previewMesh = kMeshGenerator::generateSphere(1.0f, 32, 32);
    previewScene->addMesh(previewMesh);

    previewLight = previewScene->addSunLight(
        kVec3(0.0f, 3.0f, 0.0f),
        kVec3(-0.5f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    previewLight->setPower(1.5f);

    previewCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    previewCamera->setFOV(45.0f);
    previewCamera->setAspectRatio(1.0f);
    previewCamera->setNearClip(0.1f);
    previewCamera->setFarClip(100.0f);
    previewCamera->setLookAt(kVec3(0.0f, 0.0f, 0.0f));
    previewCamera->setPosition(kVec3(0.0f, 0.3f, 3.0f));
}

void PanelInspector::updatePreviewLight()
{
    if (!previewLight || !previewScene) return;
    if (!lightEnabled)
    {
        previewLight->setPower(0.0f);
        previewScene->setAmbientLightColor(kVec3(0.5f, 0.5f, 0.5f));
        return;
    }
    previewLight->setPower(1.5f);
    previewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    // Renderer reads sun light direction from the object's rotation, not setDirection()
    previewLight->setRotation(kVec3(lightPitch, lightYaw, 0.0f));
}

void PanelInspector::loadPreviewParams(const std::string& uuid)
{
    // Reset to defaults
    prevDiffuse[0] = prevDiffuse[1] = prevDiffuse[2] = 1.0f;
    prevSpecular[0] = prevSpecular[1] = prevSpecular[2] = 1.0f;
    prevShininess = 32.0f;
    prevMetallic  = 0.0f;
    prevRoughness = 0.5f;

    if (uuid.empty()) return;
    try
    {
        fs::path p = fs::path(manager->baseDir) / "shader_preview_params.json";
        if (!fs::exists(p)) return;
        std::ifstream f(p);
        if (!f.is_open()) return;
        nlohmann::json j; f >> j;
        if (!j.contains(uuid)) return;
        auto& s = j[uuid];
        if (s.contains("diffuse") && s["diffuse"].is_array() && s["diffuse"].size() >= 3)
        { prevDiffuse[0] = s["diffuse"][0]; prevDiffuse[1] = s["diffuse"][1]; prevDiffuse[2] = s["diffuse"][2]; }
        if (s.contains("specular") && s["specular"].is_array() && s["specular"].size() >= 3)
        { prevSpecular[0] = s["specular"][0]; prevSpecular[1] = s["specular"][1]; prevSpecular[2] = s["specular"][2]; }
        if (s.contains("shininess")) prevShininess = s["shininess"].get<float>();
        if (s.contains("metallic"))  prevMetallic  = s["metallic"].get<float>();
        if (s.contains("roughness")) prevRoughness = s["roughness"].get<float>();
    }
    catch (...) {}
}

void PanelInspector::savePreviewParams(const std::string& uuid)
{
    if (uuid.empty()) return;
    try
    {
        fs::path p = fs::path(manager->baseDir) / "shader_preview_params.json";
        nlohmann::json j;
        if (fs::exists(p))
        {
            std::ifstream f(p);
            if (f.is_open()) { try { f >> j; } catch (...) {} }
        }
        j[uuid]["diffuse"]   = { prevDiffuse[0],  prevDiffuse[1],  prevDiffuse[2]  };
        j[uuid]["specular"]  = { prevSpecular[0], prevSpecular[1], prevSpecular[2] };
        j[uuid]["shininess"] = prevShininess;
        j[uuid]["metallic"]  = prevMetallic;
        j[uuid]["roughness"] = prevRoughness;
        std::ofstream f(p);
        if (f.is_open()) f << j.dump(4);
    }
    catch (...) {}
}

void PanelInspector::applyPreviewParams()
{
    if (!previewMat) return;
    previewMat->setDiffuseColor (kVec3(prevDiffuse[0],  prevDiffuse[1],  prevDiffuse[2]));
    previewMat->setAmbientColor (kVec3(prevDiffuse[0] * 0.08f, prevDiffuse[1] * 0.08f, prevDiffuse[2] * 0.08f));
    previewMat->setSpecularColor(kVec3(prevSpecular[0], prevSpecular[1], prevSpecular[2]));
    previewMat->setShininess(prevShininess);
    previewMat->setMetallic (prevMetallic);
    previewMat->setRoughness(prevRoughness);
}

void PanelInspector::rebuildPreviewShader()
{
    for (auto* t : previewDefaultTextures) delete t;
    previewDefaultTextures.clear();
    delete previewShader;
    delete previewMat;
    previewShader = nullptr;
    previewMat    = nullptr;
    prevValid     = false;

    if (prevGlsl.empty()) return;

    initPreviewScene();
    if (!previewMesh) return;

    previewShader = new kShader();
    previewShader->loadGlslCode(prevGlsl);
    if (previewShader->getShaderProgram() == 0)
    {
        delete previewShader;
        previewShader = nullptr;
        return;
    }

    previewMat = new kMaterial();
    previewMat->setShader(previewShader);
    applyPreviewParams();

    // Bind a 1×1 white placeholder for every sampler2D in the shader so that
    // stale GL texture units from the main scene render don't bleed through.
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    std::regex re(R"(uniform\s+sampler2D\s+(\w+)\s*;)");
    std::sregex_iterator it(prevGlsl.begin(), prevGlsl.end(), re);
    for (; it != std::sregex_iterator(); ++it)
    {
        const std::string samplerName = (*it)[1].str();

        GLuint texId = 0;
        glGenTextures(1, &texId);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        kTexture2D* t = new kTexture2D();
        t->setTextureID(texId);
        t->setTextureName(samplerName);
        t->setType(kTextureType::TEX_TYPE_2D);
        previewDefaultTextures.push_back(t);
        previewMat->addTexture(t);
    }

    previewMesh->setMaterial(previewMat);
    prevValid = true;
}

void PanelInspector::drawShaderPreview()
{
    if (!previewRenderer)
        previewRenderer = new kOffscreenRenderer(256, 256);

    initPreviewScene();

    const auto& ps = manager->shaderPreview;

    // UUID changed: save old params, load new, invalidate shader
    if (ps.uuid != prevUuid)
    {
        if (!prevUuid.empty()) savePreviewParams(prevUuid);
        prevUuid = ps.uuid;
        loadPreviewParams(prevUuid);
        prevGlsl.clear();
        prevValid = false;
    }

    // GLSL changed: rebuild shader/material
    if (!ps.glslSource.empty() && ps.glslSource != prevGlsl)
    {
        prevGlsl = ps.glslSource;
        rebuildPreviewShader();
    }

    // Update light direction / state every frame
    updatePreviewLight();

    // Render offscreen (only when shader is valid)
    if (prevValid && previewScene && previewCamera && previewRenderer)
    {
        applyPreviewParams();
        previewRenderer->render(previewWorld, previewScene, previewCamera);
    }

    // -----------------------------------------------------------------------
    // Header
    // -----------------------------------------------------------------------
    gui->textDisabled("Shader Preview");
    if (!ps.shaderType.empty())
    {
        ImGui::SameLine(0, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.80f, 0.45f, 1.0f));
        ImGui::Text("(%s)", ps.shaderType.c_str());
        ImGui::PopStyleColor();
    }
    gui->spacing();

    // -----------------------------------------------------------------------
    // Preview area
    // -----------------------------------------------------------------------
    float avail = ImGui::GetWindowWidth()
                - ImGui::GetStyle().WindowPadding.x * 2.0f
                - ImGui::GetStyle().ScrollbarSize;
    float sz    = std::min(std::max(avail, 1.0f), 256.0f);
    float ox    = std::max((avail - sz) * 0.5f, 0.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    // Draw rendered image or placeholder via DrawList (doesn't advance cursor)
    if (prevValid && previewRenderer)
    {
        dl->AddImage((ImTextureID)(uintptr_t)previewRenderer->getTexture(),
                     screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz));
    }
    else
    {
        dl->AddRectFilled(screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                          IM_COL32(28, 28, 32, 255));
        dl->AddRect(screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                    IM_COL32(60, 60, 75, 200));
        const char* msg = ps.glslSource.empty()
            ? "Compile shader to preview" : "Shader compile error";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(
            ImVec2(screenPos.x + (sz - ts.x) * 0.5f,
                   screenPos.y + (sz - ts.y) * 0.5f),
            IM_COL32(110, 115, 140, 200), msg);
    }

    // InvisibleButton advances cursor and captures mouse so IsItemActive() works for drag
    ImGui::InvisibleButton("##shaderPreview", ImVec2(sz, sz),
        ImGuiButtonFlags_MouseButtonRight);
    ImVec2 imgMax = ImVec2(screenPos.x + sz, screenPos.y + sz);

    // clip=false bypasses window clipping rect so hover is reliable regardless of scroll position
    bool pvHovered = ImGui::IsMouseHoveringRect(screenPos, imgMax, false);
    if (pvHovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // -----------------------------------------------------------------------
    // Light-bulb toggle button (top-right corner of preview)
    // -----------------------------------------------------------------------
    const float btR   = 11.0f;
    const float btPad =  7.0f;
    ImVec2 btCenter = ImVec2(imgMax.x - btR - btPad, screenPos.y + btR + btPad);
    bool   btHover  = ImGui::IsMouseHoveringRect(
        ImVec2(btCenter.x - btR, btCenter.y - btR),
        ImVec2(btCenter.x + btR, btCenter.y + btR), false);

    // Background shadow + fill
    ImU32 btFg = lightEnabled
        ? (btHover ? IM_COL32(255, 235, 100, 255) : IM_COL32(245, 215, 60,  220))
        : (btHover ? IM_COL32(140, 140, 155, 255) : IM_COL32(80,  80,  95,  200));
    dl->AddCircleFilled(btCenter, btR,        IM_COL32(15, 15, 20, 190));
    dl->AddCircleFilled(btCenter, btR - 1.5f, btFg);

    // Bulb icon: circle (globe) + two horizontal base lines
    float  bx = btCenter.x, by = btCenter.y;
    ImU32  bulbInk = IM_COL32(25, 18, 5, lightEnabled ? 230 : 160);
    dl->AddCircle(ImVec2(bx, by - 1.0f), 4.5f, bulbInk, 10, 1.3f);
    dl->AddLine(ImVec2(bx - 3.0f, by + 5.0f), ImVec2(bx + 3.0f, by + 5.0f), bulbInk, 1.3f);
    dl->AddLine(ImVec2(bx - 2.5f, by + 7.0f), ImVec2(bx + 2.5f, by + 7.0f), bulbInk, 1.3f);

    if (btHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        lightEnabled = !lightEnabled;
    if (btHover)
        ImGui::SetTooltip("%s", lightEnabled ? "Disable preview lighting" : "Enable preview lighting");

    // -----------------------------------------------------------------------
    // Drag to rotate light (RMB held on preview, outside the bulb button)
    // -----------------------------------------------------------------------
    // IsItemActive() is reliable because InvisibleButton captures the mouse press,
    // preventing the window from stealing the drag for scrolling.
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !btHover && !isDraggingLight)
        isDraggingLight = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        isDraggingLight = false;
    if (isDraggingLight)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        lightYaw   += delta.x * 0.015f;
        lightPitch -= delta.y * 0.015f;
        lightPitch  = std::clamp(lightPitch, -89.0f, 89.0f);
    }

    gui->spacing();
    gui->separator();
    gui->spacing();

    // -----------------------------------------------------------------------
    // Preview parameters
    // -----------------------------------------------------------------------
    const std::string& type = ps.shaderType;
    bool changed = false;

    if (gui->collapsingHeader("Preview Parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!beginPropTable(gui, "ShaderPrevTable")) return;

        if (type == "Flat")
        {
            propLabel(gui, "Color");
            if (ImGui::ColorEdit3("##PrevFlatColor", prevDiffuse)) changed = true;
        }
        else
        {
            propLabel(gui, "Albedo");
            if (ImGui::ColorEdit3("##PrevAlbedo", prevDiffuse)) changed = true;
        }

        if (type == "Phong")
        {
            propLabel(gui, "Specular");
            if (ImGui::ColorEdit3("##PrevSpec", prevSpecular)) changed = true;
            propLabel(gui, "Shininess");
            if (ImGui::DragFloat("##PrevShine", &prevShininess, 0.5f, 0.0f, 512.0f)) changed = true;
        }
        else if (type == "PBR" || type.empty())
        {
            propLabel(gui, "Metallic");
            if (ImGui::DragFloat("##PrevMetal", &prevMetallic, 0.01f, 0.0f, 1.0f)) changed = true;
            propLabel(gui, "Roughness");
            if (ImGui::DragFloat("##PrevRough", &prevRoughness, 0.01f, 0.0f, 1.0f)) changed = true;
        }

        gui->tableEnd();
    }

    if (changed)
        savePreviewParams(prevUuid);
}

// ---------------------------------------------------------------------------
// Model viewer
// ---------------------------------------------------------------------------

void PanelInspector::initModelViewScene()
{
    if (modelViewScene) return;

    modelViewWorld = createWorld(createAssetManager());
    modelViewScene = modelViewWorld->createScene("modelViewer");
    modelViewScene->setFrustumCullingEnabled(false);
    // No shadows in the preview window (see matViewer note).
    modelViewScene->setShadowsEnabled(false);
    modelViewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    modelViewLight = modelViewScene->addSunLight(
        kVec3(0.0f, 3.0f, 0.0f),
        kVec3(-0.5f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    modelViewLight->setPower(1.5f);

    modelViewCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    modelViewCamera->setFOV(45.0f);
    modelViewCamera->setAspectRatio(1.0f);
    modelViewCamera->setNearClip(0.1f);
    modelViewCamera->setFarClip(500.0f);
    modelViewCamera->setLookAt(kVec3(0.0f));
    modelViewCamera->setPosition(kVec3(0.0f, 0.3f, 3.0f));
}

void PanelInspector::updateModelViewLight()
{
    if (!modelViewLight || !modelViewScene) return;
    if (!modelViewLightEnabled)
    {
        modelViewLight->setPower(0.0f);
        return;
    }
    modelViewLight->setPower(1.5f);
    modelViewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));
    modelViewLight->setRotation(kVec3(modelViewLightPitch, modelViewLightYaw, 0.0f));
}

void PanelInspector::frameModelViewCamera()
{
    if (!modelViewMesh || !modelViewCamera) return;

    // Compute AABB using the mesh's loaded state (no rotation reset — matches thumbnail)
    kAABB combined;
    std::function<void(kMesh*)> expand = [&](kMesh* m) {
        m->calculateModelMatrix();
        kAABB b = m->getWorldAABB();
        if (b.isValid()) { combined.expandBy(b.min); combined.expandBy(b.max); }
        for (kObject* c : m->getChildren())
            if (c->getType() == kNodeType::NODE_TYPE_MESH)
                expand(static_cast<kMesh*>(c));
    };
    expand(modelViewMesh);

    modelViewCenter = combined.isValid() ? combined.center()      : kVec3(0.0f);
    kVec3 he        = combined.isValid() ? combined.halfExtents() : kVec3(1.0f);
    float radius    = glm::length(he);
    if (radius < 0.001f) radius = 1.0f;

    float fov = 45.0f;
    modelViewCamDist = (radius / glm::tan(glm::radians(fov * 0.5f))) * 1.1f;
    kVec3 dir = glm::normalize(kVec3(0.5f, 0.5f, 1.0f));
    float heAlong = std::abs(dir.x) * he.x + std::abs(dir.y) * he.y + std::abs(dir.z) * he.z;

    modelViewCamera->setFOV(fov);
    modelViewCamera->setNearClip(std::max(0.001f, modelViewCamDist - heAlong - radius * 0.05f));
    modelViewCamera->setFarClip(modelViewCamDist + heAlong + radius * 0.05f);
    // Camera position set per-frame via orbit update in drawModelViewer
}

void PanelInspector::applyDefaultMaterial(kMesh* mesh, kShader* defaultShader)
{
    if (!mesh || !defaultShader) return;
    kMaterial* mat = mesh->getMaterial();
    if (!mat || !mat->getShader() || mat->getShader()->getShaderProgram() == 0)
    {
        kMaterial* def = new kMaterial();
        def->setShader(defaultShader);
        def->setDiffuseColor(kVec3(1.0f));
        def->setAmbientColor(kVec3(1.0f));
        def->setSpecularColor(kVec3(1.0f));
        def->setShininess(32.0f);
        mesh->setMaterial(def, false);  // false = don't override children
        modelViewDefaultMats.push_back(def);
    }
    for (kObject* child : mesh->getChildren())
        if (child->getType() == kNodeType::NODE_TYPE_MESH)
            applyDefaultMaterial(static_cast<kMesh*>(child), defaultShader);
}

void PanelInspector::drawModelViewer(const PanelProject::SelectedProjectAsset& asset)
{
    if (!modelViewRenderer)
        modelViewRenderer = new kOffscreenRenderer(256, 256);

    initModelViewScene();

    // Reload mesh when selection changes
    if (asset.uuid != modelViewUuid)
    {
        modelViewUuid          = asset.uuid;
        modelViewLightEnabled  = false;
        modelViewRotX          =  24.09f;
        modelViewRotY          =  26.57f;
        modelViewLightYaw      =  45.0f;
        modelViewLightPitch    =  60.0f;
        isDraggingMVLight      = false;
        isDraggingMVModel      = false;

        if (modelViewMesh && modelViewScene)
            modelViewScene->removeMesh(modelViewMesh);
        modelViewMesh = nullptr;
        for (auto* m : modelViewDefaultMats) delete m;
        modelViewDefaultMats.clear();

        if (!asset.uuid.empty() && manager->getAssetManager())
        {
            fs::path glbPath = manager->projectPath
                / "Library" / "ImportedAssets" / (asset.uuid + ".glb");
            if (fs::exists(glbPath))
            {
                kAssetManager* am = manager->getAssetManager();
                modelViewMesh = am->loadMesh(glbPath.generic_string());
                if (modelViewMesh)
                {
                    kShader* defShader = am->loadGlslFromResource("SHADER_MESH_PHONG");
                    applyDefaultMaterial(modelViewMesh, defShader);
                    modelViewScene->addMesh(modelViewMesh);
                    frameModelViewCamera();
                }
            }
        }
    }

    // Orbit camera around modelViewCenter — mesh stays fixed at its loaded transform
    if (modelViewCamera)
    {
        float pr = glm::radians(modelViewRotX);
        float yr = glm::radians(modelViewRotY);
        kVec3 camDir(std::cos(pr) * std::sin(yr),
                     std::sin(pr),
                     std::cos(pr) * std::cos(yr));
        modelViewCamera->setPosition(modelViewCenter + camDir * modelViewCamDist);
        modelViewCamera->setLookAt(modelViewCenter);
    }

    updateModelViewLight();

    // Render offscreen
    if (modelViewMesh && modelViewCamera && modelViewRenderer)
    {
        modelViewRenderer->setBackgroundColor(kVec4(42/255.0f, 42/255.0f, 42/255.0f, 1.0f));
        if (modelViewLightEnabled)
            modelViewRenderer->render(modelViewWorld, modelViewScene, modelViewCamera);
        else
            modelViewRenderer->renderMesh(modelViewMesh, modelViewCamera);
    }

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------
    float avail = ImGui::GetWindowWidth()
                - ImGui::GetStyle().WindowPadding.x * 2.0f
                - ImGui::GetStyle().ScrollbarSize;
    float sz    = std::min(std::max(avail, 1.0f), 256.0f);
    float ox    = std::max((avail - sz) * 0.5f, 0.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    if (modelViewMesh && modelViewRenderer)
    {
        dl->AddImage((ImTextureID)(uintptr_t)modelViewRenderer->getTexture(),
                     screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                     ImVec2(0, 1), ImVec2(1, 0));
    }
    else
    {
        dl->AddRectFilled(screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                          IM_COL32(28, 28, 32, 255));
        const char* msg = "No preview available";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(screenPos.x + (sz - ts.x) * 0.5f,
                           screenPos.y + (sz - ts.y) * 0.5f),
                    IM_COL32(110, 115, 140, 200), msg);
    }

    // InvisibleButton captures mouse for both LMB and RMB drag
    ImGui::InvisibleButton("##modelViewer", ImVec2(sz, sz),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImVec2 imgMax = ImVec2(screenPos.x + sz, screenPos.y + sz);

    bool pvHovered = ImGui::IsItemHovered();
    if (pvHovered || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // -----------------------------------------------------------------------
    // Light-bulb toggle (top-right corner)
    // -----------------------------------------------------------------------
    const float btR   = 11.0f;
    const float btPad =  7.0f;
    ImVec2 btCenter = ImVec2(imgMax.x - btR - btPad, screenPos.y + btR + btPad);
    bool   btHover  = ImGui::IsMouseHoveringRect(
        ImVec2(btCenter.x - btR, btCenter.y - btR),
        ImVec2(btCenter.x + btR, btCenter.y + btR), false);

    ImU32 btFg = modelViewLightEnabled
        ? (btHover ? IM_COL32(255, 235, 100, 255) : IM_COL32(245, 215, 60,  220))
        : (btHover ? IM_COL32(140, 140, 155, 255) : IM_COL32(80,  80,  95,  200));
    dl->AddCircleFilled(btCenter, btR,        IM_COL32(15, 15, 20, 190));
    dl->AddCircleFilled(btCenter, btR - 1.5f, btFg);

    float  bx = btCenter.x, by = btCenter.y;
    ImU32  bulbInk = IM_COL32(25, 18, 5, modelViewLightEnabled ? 230 : 160);
    dl->AddCircle(ImVec2(bx, by - 1.0f), 4.5f, bulbInk, 10, 1.3f);
    dl->AddLine(ImVec2(bx - 3.0f, by + 5.0f), ImVec2(bx + 3.0f, by + 5.0f), bulbInk, 1.3f);
    dl->AddLine(ImVec2(bx - 2.5f, by + 7.0f), ImVec2(bx + 2.5f, by + 7.0f), bulbInk, 1.3f);

    if (btHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        modelViewLightEnabled = !modelViewLightEnabled;
    if (btHover)
        ImGui::SetTooltip("%s", modelViewLightEnabled ? "Disable preview lighting" : "Enable preview lighting");

    // -----------------------------------------------------------------------
    // Left-click drag = rotate model
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !btHover && !isDraggingMVModel)
        isDraggingMVModel = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        isDraggingMVModel = false;

    if (isDraggingMVModel)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        modelViewRotY -= delta.x * 0.5f;
        modelViewRotX += delta.y * 0.5f;
        modelViewRotX  = std::clamp(modelViewRotX, -89.0f, 89.0f);
    }

    // -----------------------------------------------------------------------
    // Right-click drag = move light
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !isDraggingMVLight)
        isDraggingMVLight = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        isDraggingMVLight = false;

    if (isDraggingMVLight)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        modelViewLightYaw   += delta.x * 0.015f;
        modelViewLightPitch -= delta.y * 0.015f;
        modelViewLightPitch  = std::clamp(modelViewLightPitch, -89.0f, 89.0f);
    }

    gui->spacing();
    gui->separator();
    gui->spacing();
}

// ---------------------------------------------------------------------------
// Material viewer
// ---------------------------------------------------------------------------

void PanelInspector::initMatViewScene()
{
    if (matViewScene) return;

    matViewWorld = createWorld(createAssetManager());
    matViewScene = matViewWorld->createScene("matViewer");
    matViewScene->setFrustumCullingEnabled(false);
    // No shadows in the preview — the single-mesh thumbnail doesn't benefit
    // from a cast shadow, and skipping the depth pass keeps lit (Phong/PBR)
    // materials rendering correctly in the preview window.
    matViewScene->setShadowsEnabled(false);
    matViewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    matViewLight = matViewScene->addSunLight(
        kVec3(0.0f, 3.0f, 0.0f),
        kVec3(-0.5f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    matViewLight->setPower(1.5f);

    matViewMesh = kMeshGenerator::generateSphere(1.0f, 32, 32);
    matViewScene->addMesh(matViewMesh);

    matViewCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    matViewCamera->setFOV(45.0f);
    matViewCamera->setAspectRatio(1.0f);
    matViewCamera->setNearClip(0.1f);
    matViewCamera->setFarClip(100.0f);
    matViewCamera->setLookAt(kVec3(0.0f));
    matViewCamera->setPosition(kVec3(0.0f, 0.3f, 3.0f));
}

void PanelInspector::updateMatViewLight()
{
    if (!matViewLight || !matViewScene) return;
    if (!matViewLightEnabled)
    {
        matViewLight->setPower(0.0f);
        matViewScene->setAmbientLightColor(kVec3(0.5f, 0.5f, 0.5f));
        return;
    }
    matViewLight->setPower(1.5f);
    matViewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));
    matViewLight->setRotation(kVec3(matViewLightPitch, matViewLightYaw, 0.0f));
}

void PanelInspector::rebuildMatViewMaterial(const nlohmann::json& matJson)
{
    delete matViewMat;       // inspector owns the preview material (shader/textures are shared)
    matViewMat = nullptr;
    if (!matViewMesh) return;
    if (!matJson.is_object()) return;

    matViewMat = manager->buildMaterialFromJson(matJson);
    if (matViewMat)
        matViewMesh->setMaterial(matViewMat);
}

void PanelInspector::drawMaterialViewer(const PanelProject::SelectedProjectAsset& asset)
{
    if (!matViewRenderer)
        matViewRenderer = new kOffscreenRenderer(256, 256);

    initMatViewScene();

    // Rebuild when the inspector's live JSON changes (UUID change or any property edit)
    bool uuidChanged = (matInspUuid != matViewUuid);
    if (uuidChanged)
    {
        matViewLightYaw     =  45.0f;
        matViewLightPitch   =  60.0f;
        matViewLightEnabled = true;
        isDraggingMatLight  = false;
        matViewUuid         = matInspUuid;
    }
    if ((uuidChanged || matInspVersion != matViewVersion) && !matInspUuid.empty())
    {
        matViewVersion = matInspVersion;
        rebuildMatViewMaterial(matInspJson);
    }

    updateMatViewLight();

    if (matViewMat && matViewScene && matViewCamera && matViewRenderer)
    {
        matViewRenderer->setBackgroundColor(kVec4(42/255.0f, 42/255.0f, 42/255.0f, 1.0f));
        matViewRenderer->render(matViewWorld, matViewScene, matViewCamera);
    }

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------
    float avail = ImGui::GetWindowWidth()
                - ImGui::GetStyle().WindowPadding.x * 2.0f
                - ImGui::GetStyle().ScrollbarSize;
    float sz    = std::min(std::max(avail, 1.0f), 256.0f);
    float ox    = std::max((avail - sz) * 0.5f, 0.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    if (matViewMat && matViewRenderer)
    {
        dl->AddImage((ImTextureID)(uintptr_t)matViewRenderer->getTexture(),
                     screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz));
    }
    else
    {
        dl->AddRectFilled(screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                          IM_COL32(28, 28, 32, 255));
        const char* msg = "No preview available";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(screenPos.x + (sz - ts.x) * 0.5f,
                           screenPos.y + (sz - ts.y) * 0.5f),
                    IM_COL32(110, 115, 140, 200), msg);
    }

    ImGui::InvisibleButton("##matViewer", ImVec2(sz, sz),
        ImGuiButtonFlags_MouseButtonRight);
    ImVec2 imgMax = ImVec2(screenPos.x + sz, screenPos.y + sz);

    bool pvHovered = ImGui::IsItemHovered();
    if (pvHovered || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // -----------------------------------------------------------------------
    // Light-bulb toggle (top-right corner)
    // -----------------------------------------------------------------------
    const float btR   = 11.0f;
    const float btPad =  7.0f;
    ImVec2 btCenter = ImVec2(imgMax.x - btR - btPad, screenPos.y + btR + btPad);
    bool   btHover  = ImGui::IsMouseHoveringRect(
        ImVec2(btCenter.x - btR, btCenter.y - btR),
        ImVec2(btCenter.x + btR, btCenter.y + btR), false);

    ImU32 btFg = matViewLightEnabled
        ? (btHover ? IM_COL32(255, 235, 100, 255) : IM_COL32(245, 215, 60,  220))
        : (btHover ? IM_COL32(140, 140, 155, 255) : IM_COL32(80,  80,  95,  200));
    dl->AddCircleFilled(btCenter, btR,        IM_COL32(15, 15, 20, 190));
    dl->AddCircleFilled(btCenter, btR - 1.5f, btFg);

    float  bx = btCenter.x, by = btCenter.y;
    ImU32  bulbInk = IM_COL32(25, 18, 5, matViewLightEnabled ? 230 : 160);
    dl->AddCircle(ImVec2(bx, by - 1.0f), 4.5f, bulbInk, 10, 1.3f);
    dl->AddLine(ImVec2(bx - 3.0f, by + 5.0f), ImVec2(bx + 3.0f, by + 5.0f), bulbInk, 1.3f);
    dl->AddLine(ImVec2(bx - 2.5f, by + 7.0f), ImVec2(bx + 2.5f, by + 7.0f), bulbInk, 1.3f);

    if (btHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        matViewLightEnabled = !matViewLightEnabled;
    if (btHover)
        ImGui::SetTooltip("%s", matViewLightEnabled ? "Disable preview lighting" : "Enable preview lighting");

    // -----------------------------------------------------------------------
    // Right-click drag = move light
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !btHover && !isDraggingMatLight)
        isDraggingMatLight = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        isDraggingMatLight = false;

    if (isDraggingMatLight)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        matViewLightYaw   += delta.x * 0.015f;
        matViewLightPitch -= delta.y * 0.015f;
        matViewLightPitch  = std::clamp(matViewLightPitch, -89.0f, 89.0f);
    }

    gui->spacing();
    gui->separator();
    gui->spacing();
}

ImTextureRef PanelInspector::getFileTypeIcon(const kString &fileType) const
{
    if (fileType == "mesh")     return iconFileModel;
    if (fileType == "image")    return iconFileImage;
    if (fileType == "material") return iconFileMaterial;
    if (fileType == "prefab")   return iconFilePrefab;
    if (fileType == "audio")    return iconFileAudio;
    if (fileType == "video")    return iconFileVideo;
    if (fileType == "script")   return iconFileScript;
    if (fileType == "text")     return iconFileText;
    if (fileType == "world")    return iconFileWorld;
    return iconFileOther;
}

static void drawInlineIcon(ImTextureRef icon, const char *tooltip = nullptr, float size = 16.0f)
{
    if (icon == nullptr) return;
    float pad = (ImGui::GetFrameHeight() - size) * 0.5f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
    ImGui::Image(icon, ImVec2(size, size));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - pad);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
}

// ---------------------------------------------------------------------------
// Table helpers
// ---------------------------------------------------------------------------

static bool beginPropTable(kGuiManager *gui, const char *id)
{
    if (!gui->tableStart(id, 2, ImGuiTableFlags_SizingStretchProp))
        return false;
    gui->tableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    gui->tableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

static void propLabel(kGuiManager *gui, const char *label)
{
    gui->tableNextRow();
    gui->tableSetColumnIndex(0);
    gui->alignTextToFramePadding();
    gui->text(label);
    if (gui->isItemHovered() && gui->calcTextSize(label).x > gui->getColumnWidth())
    {
        gui->beginTooltip();
        gui->textUnformatted(label);
        gui->endTooltip();
    }
    gui->tableSetColumnIndex(1);
    gui->setNextItemWidth(-FLT_MIN);
}

// ---------------------------------------------------------------------------
// Transform section
// ---------------------------------------------------------------------------
static void drawTransformSection(kGuiManager *gui, kObject *obj, Manager *mgr)
{
    if (!gui->collapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!beginPropTable(gui, "TfmTable"))
        return;

    // --- Position ---
    {
        static kVec3 s_posBefore;

        kVec3 pos = obj->getPosition();
        kVec3 posPreEdit = pos;
        float p[3] = {pos.x, pos.y, pos.z};

        propLabel(gui, "Position");
        if (gui->dragFloat3("##Pos", p, 0.1f))
        {
            obj->setPosition(kVec3(p[0], p[1], p[2]));
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }
        if (gui->isItemActivated())
            s_posBefore = posPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = obj->getPosition();
            kVec3 before = s_posBefore;
            kObject *cap = obj;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setPosition(before); },
                [cap, after]()
                { cap->setPosition(after); }));
        }
    }

    // --- Rotation ---
    {
        static kVec3 s_rotBefore;

        kVec3 euler = obj->getRotationEuler();
        kVec3 eulerPreEdit = euler;
        float r[3] = {euler.x, euler.y, euler.z};
        for (int i = 0; i < 3; i++)
            if (r[i] == 0.0f)
                r[i] = 0.0f;

        propLabel(gui, "Rotation");
        if (gui->dragFloat3("##Rot", r, 0.5f))
        {
            obj->setRotation(kQuat(glm::radians(kVec3(r[0], r[1], r[2]))));
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }
        if (gui->isItemActivated())
            s_rotBefore = eulerPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = obj->getRotationEuler();
            kVec3 before = s_rotBefore;
            kObject *cap = obj;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setRotation(kQuat(glm::radians(before))); },
                [cap, after]()
                { cap->setRotation(kQuat(glm::radians(after))); }));
        }
    }

    // --- Scale ---
    {
        static kVec3 s_scaleBefore;

        kVec3 scl = obj->getScale();
        kVec3 sclPreEdit = scl;
        float s[3] = {scl.x, scl.y, scl.z};

        propLabel(gui, "Scale");
        if (gui->dragFloat3("##Scl", s, 0.01f, 0.001f, 10000.0f))
        {
            obj->setScale(kVec3(s[0], s[1], s[2]));
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }
        if (gui->isItemActivated())
            s_scaleBefore = sclPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = obj->getScale();
            kVec3 before = s_scaleBefore;
            kObject *cap = obj;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setScale(before); },
                [cap, after]()
                { cap->setScale(after); }));
        }
    }

    gui->tableEnd();
}

// ---------------------------------------------------------------------------
// Mesh section
// ---------------------------------------------------------------------------
static void drawMeshSection(kGuiManager *gui, kMesh *mesh, Manager *mgr)
{
    // Geometry-less mesh nodes — e.g. the empty root of an imported multi-mesh
    // model, whose geometry lives in its sub-meshes — carry no drawable data, so
    // a File / Material / shadow UI on them is meaningless. Skip the section.
    if (mesh->getVertexCount() <= 0)
        return;

    if (!gui->collapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!beginPropTable(gui, "MeshTable"))
        return;

    propLabel(gui, "File");
    gui->beginDisabled(true);
    char fileBuf[256];
    strncpy_s(fileBuf, sizeof(fileBuf), mesh->getFileName().c_str(), _TRUNCATE);
    ImGui::InputText("##MeshFile", fileBuf, sizeof(fileBuf));
    gui->endDisabled();

    // --- Material picker -----------------------------------------------------
    // Lists project .mat assets; selecting one applies it to this mesh (same
    // runtime effect as dragging a material onto the object in the viewport).
    // Also accepts a material dragged onto the field. The assignment is stored
    // as the material asset's UUID on the object, so it persists across save/load.
    {
        propLabel(gui, "Material");

        std::vector<std::string> matUuids = {""};      // index 0 = "(None)"
        std::vector<std::string> matNames = {"(None)"};
        for (const auto &kv : mgr->fileMap)
        {
            if (kv.second.type == "material")
            {
                matUuids.push_back(kv.first);
                matNames.push_back(fs::path(kv.second.path).stem().string());
            }
        }
        std::vector<const char *> matNamePtrs;
        matNamePtrs.reserve(matNames.size());
        for (auto &n : matNames) matNamePtrs.push_back(n.c_str());

        // Current selection comes from the object's stored material UUID.
        std::string assignedUuid = mesh->getMaterialUuid();
        int current = 0;
        for (size_t i = 0; i < matUuids.size(); ++i)
            if (matUuids[i] == assignedUuid) { current = (int)i; break; }

        auto applyByIndex = [&](int idx) {
            if (idx < 0 || idx >= (int)matUuids.size()) return;
            // Snapshot the whole subtree so undo restores per-part materials of
            // multi-mesh models, not just the root.
            std::vector<MaterialSnapshot> before = mgr->captureMaterialSubtree(mesh);
            bool ok = false;
            if (idx == 0)
            {
                // "(None)" — revert the object to a fresh default material so its
                // appearance visibly changes back (and clear the stored UUID).
                mgr->applyDefaultMaterialToObject(mesh);
                ok = true;
            }
            else
            {
                auto it = mgr->fileMap.find(matUuids[idx]);
                if (it == mgr->fileMap.end()) return;
                fs::path matPath = mgr->projectPath / "Assets" / it->second.path;
                ok = mgr->applyMaterialToObject(mesh, matPath, matUuids[idx]);
            }
            if (ok)
            {
                auto cmd = std::make_unique<MaterialCommand>();
                cmd->manager = mgr;
                cmd->before  = before;
                cmd->after   = mgr->captureMaterialSubtree(mesh);
                mgr->undoRedo.push(std::move(cmd));
                mgr->projectSaved = false;
                mgr->refreshWindowTitle();
            }
        };

        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##MeshMaterial", &current, matNamePtrs.data(), (int)matNamePtrs.size()))
            applyByIndex(current);

        // Accept a material asset dropped directly onto the combo.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload =
                    ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
            {
                std::string dropped((const char *)payload->Data);
                { auto nl = dropped.find('\n'); if (nl != std::string::npos) dropped = dropped.substr(0, nl); }
                auto it = mgr->fileMap.find(dropped);
                if (it != mgr->fileMap.end() && it->second.type == "material")
                {
                    for (size_t i = 0; i < matUuids.size(); ++i)
                        if (matUuids[i] == dropped) { applyByIndex((int)i); break; }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    propLabel(gui, "Cast Shadow");
    bool castShadow = mesh->getCastShadow();
    bool prevCastShadow = castShadow;
    if (gui->checkbox("##CastShadow", &castShadow))
    {
        mesh->setCastShadow(castShadow);
        kMesh *cap = mesh;
        bool after = castShadow;
        bool before = prevCastShadow;
        mgr->undoRedo.push(std::make_unique<PropertyCommand>(
            [cap, before]()
            { cap->setCastShadow(before); },
            [cap, after]()
            { cap->setCastShadow(after); }));
    }

    propLabel(gui, "Receive Shadow");
    bool receiveShadow = mesh->getReceiveShadow();
    bool prevReceiveShadow = receiveShadow;
    if (gui->checkbox("##ReceiveShadow", &receiveShadow))
    {
        mesh->setReceiveShadow(receiveShadow);
        kMesh *cap = mesh;
        bool after = receiveShadow;
        bool before = prevReceiveShadow;
        mgr->undoRedo.push(std::make_unique<PropertyCommand>(
            [cap, before]()
            { cap->setReceiveShadow(before); },
            [cap, after]()
            { cap->setReceiveShadow(after); }));
    }

    gui->tableEnd();
}

// ---------------------------------------------------------------------------
// Camera section
// ---------------------------------------------------------------------------
static void drawCameraSection(kGuiManager *gui, kCamera *cam, Manager *mgr)
{
    if (!gui->collapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!beginPropTable(gui, "CamTable"))
        return;

    // FOV
    {
        static float s_fovBefore = 0.0f;
        float fov = cam->getFOV();
        float fovPreEdit = fov;
        propLabel(gui, "FOV");
        if (gui->dragFloat("##FOV", &fov, 0.5f, 1.0f, 179.0f, "%.1f deg"))
            cam->setFOV(fov);
        if (gui->isItemActivated())
            s_fovBefore = fovPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = cam->getFOV();
            float before = s_fovBefore;
            kCamera *cap = cam;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setFOV(before); },
                [cap, after]()
                { cap->setFOV(after); }));
        }
    }

    // Near Clip
    {
        static float s_nearBefore = 0.0f;
        float nearClip = cam->getNearClip();
        float nearPreEdit = nearClip;
        propLabel(gui, "Near Clip");
        if (gui->dragFloat("##Near", &nearClip, 0.01f, 0.001f, 10000.0f, "%.3f"))
            cam->setNearClip(nearClip);
        if (gui->isItemActivated())
            s_nearBefore = nearPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = cam->getNearClip();
            float before = s_nearBefore;
            kCamera *cap = cam;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setNearClip(before); },
                [cap, after]()
                { cap->setNearClip(after); }));
        }
    }

    // Far Clip
    {
        static float s_farBefore = 0.0f;
        float farClip = cam->getFarClip();
        float farPreEdit = farClip;
        propLabel(gui, "Far Clip");
        if (gui->dragFloat("##Far", &farClip, 1.0f, 0.1f, 100000.0f, "%.1f"))
            cam->setFarClip(farClip);
        if (gui->isItemActivated())
            s_farBefore = farPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = cam->getFarClip();
            float before = s_farBefore;
            kCamera *cap = cam;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setFarClip(before); },
                [cap, after]()
                { cap->setFarClip(after); }));
        }
    }

    // Scene selector
    {
        kWorld *world = mgr->getWorld();
        const auto &scenes = world ? world->getScenes() : std::vector<kScene*>{};

        std::vector<kScene*> gameScenes;
        for (size_t i = 1; i < scenes.size(); ++i)
            gameScenes.push_back(scenes[i]);

        std::vector<std::string> sceneNames;
        sceneNames.push_back("(Auto)");
        for (kScene *s : gameScenes)
            sceneNames.push_back(s->getName().empty() ? "(unnamed)" : s->getName());

        int curIdx = 0;
        for (int i = 0; i < (int)gameScenes.size(); ++i)
            if (gameScenes[i]->getUuid() == cam->getSceneUuid()) { curIdx = i + 1; break; }

        std::vector<const char*> scenePtrs;
        for (auto &s : sceneNames) scenePtrs.push_back(s.c_str());

        propLabel(gui, "Scene");
        if (ImGui::Combo("##CamScene", &curIdx, scenePtrs.data(), (int)scenePtrs.size()))
            cam->setSceneUuid(curIdx == 0 ? "" : gameScenes[curIdx - 1]->getUuid());
    }

    gui->tableEnd();

    // Set / unset this camera as the default for the Game panel
    gui->spacing();
    bool isDefault = (mgr->defaultGameCamera == cam);
    if (isDefault)
    {
        gui->pushStyleColor(ImGuiCol_Button,        kVec4(0.26f, 0.59f, 0.98f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
    }
    if (gui->button(isDefault ? "Default Camera" : "Set as Default", kIvec2(-1, 0)))
        mgr->defaultGameCamera = isDefault ? nullptr : cam;
    if (isDefault)
        gui->popStyleColor(2);
    if (gui->isItemHovered())
        gui->setItemTooltip(isDefault
            ? "This is the active Game panel camera.\nClick to unset."
            : "Use this camera as the default in the Game panel.");
}

// ---------------------------------------------------------------------------
// Light section
// ---------------------------------------------------------------------------
static void drawLightSection(kGuiManager *gui, kLight *light, Manager *mgr)
{
    kLightType lt = light->getLightType();

    const char *header = "Light";
    if (lt == LIGHT_TYPE_SUN)
        header = "Light (Sun)";
    else if (lt == LIGHT_TYPE_POINT)
        header = "Light (Point)";
    else if (lt == LIGHT_TYPE_SPOT)
        header = "Light (Spot)";

    if (!gui->collapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!beginPropTable(gui, "LightTable"))
        return;

    // Power
    {
        static float s_pwrBefore = 0.0f;
        float pwr = light->getPower();
        float pwrPreEdit = pwr;
        propLabel(gui, "Power");
        if (gui->dragFloat("##Power", &pwr, 0.1f, 0.0f, 10000.0f))
            light->setPower(pwr);
        if (gui->isItemActivated())
            s_pwrBefore = pwrPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = light->getPower();
            float before = s_pwrBefore;
            kLight *cap = light;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setPower(before); },
                [cap, after]()
                { cap->setPower(after); }));
        }
    }

    // Diffuse
    {
        static kVec3 s_diffBefore;
        kVec3 diff = light->getDiffuseColor();
        kVec3 diffPreEdit = diff;
        float diffF[3] = {diff.r, diff.g, diff.b};
        propLabel(gui, "Diffuse");
        if (gui->colorEdit3("##Diffuse", diffF))
            light->setDiffuseColor(kVec3(diffF[0], diffF[1], diffF[2]));
        if (gui->isItemActivated())
            s_diffBefore = diffPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = light->getDiffuseColor();
            kVec3 before = s_diffBefore;
            kLight *cap = light;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setDiffuseColor(before); },
                [cap, after]()
                { cap->setDiffuseColor(after); }));
        }
    }

    // Specular
    {
        static kVec3 s_specBefore;
        kVec3 spec = light->getSpecularColor();
        kVec3 specPreEdit = spec;
        float specF[3] = {spec.r, spec.g, spec.b};
        propLabel(gui, "Specular");
        if (gui->colorEdit3("##Specular", specF))
            light->setSpecularColor(kVec3(specF[0], specF[1], specF[2]));
        if (gui->isItemActivated())
            s_specBefore = specPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = light->getSpecularColor();
            kVec3 before = s_specBefore;
            kLight *cap = light;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setSpecularColor(before); },
                [cap, after]()
                { cap->setSpecularColor(after); }));
        }
    }

    if (lt == LIGHT_TYPE_POINT || lt == LIGHT_TYPE_SPOT)
    {
        // Constant
        {
            static float s_constBefore = 0.0f;
            float c = light->getConstant();
            float cPreEdit = c;
            propLabel(gui, "Constant");
            if (gui->dragFloat("##Constant", &c, 0.01f, 0.0f, 10.0f))
                light->setConstant(c);
            if (gui->isItemActivated())
                s_constBefore = cPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = light->getConstant();
                float before = s_constBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setConstant(before); },
                    [cap, after]()
                    { cap->setConstant(after); }));
            }
        }
        // Linear
        {
            static float s_linBefore = 0.0f;
            float l = light->getLinear();
            float lPreEdit = l;
            propLabel(gui, "Linear");
            if (gui->dragFloat("##Linear", &l, 0.01f, 0.0f, 10.0f))
                light->setLinear(l);
            if (gui->isItemActivated())
                s_linBefore = lPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = light->getLinear();
                float before = s_linBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setLinear(before); },
                    [cap, after]()
                    { cap->setLinear(after); }));
            }
        }
        // Quadratic
        {
            static float s_quadBefore = 0.0f;
            float q = light->getQuadratic();
            float qPreEdit = q;
            propLabel(gui, "Quadratic");
            if (gui->dragFloat("##Quadratic", &q, 0.01f, 0.0f, 10.0f))
                light->setQuadratic(q);
            if (gui->isItemActivated())
                s_quadBefore = qPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = light->getQuadratic();
                float before = s_quadBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setQuadratic(before); },
                    [cap, after]()
                    { cap->setQuadratic(after); }));
            }
        }
    }

    if (lt == LIGHT_TYPE_SPOT)
    {
        // Inner cone
        {
            static float s_innerBefore = 0.0f;
            float inner = glm::degrees(glm::acos(light->getCutOff()));
            float innerPreEdit = inner;
            propLabel(gui, "Inner Cone");
            if (gui->dragFloat("##InnerCone", &inner, 0.5f, 0.0f, 89.0f, "%.1f deg"))
                light->setCutOff(glm::cos(glm::radians(inner)));
            if (gui->isItemActivated())
                s_innerBefore = innerPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = glm::degrees(glm::acos(light->getCutOff()));
                float before = s_innerBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setCutOff(glm::cos(glm::radians(before))); },
                    [cap, after]()
                    { cap->setCutOff(glm::cos(glm::radians(after))); }));
            }
        }
        // Outer cone
        {
            static float s_outerBefore = 0.0f;
            float outer = glm::degrees(glm::acos(light->getOuterCutOff()));
            float outerPreEdit = outer;
            propLabel(gui, "Outer Cone");
            if (gui->dragFloat("##OuterCone", &outer, 0.5f, 0.0f, 89.0f, "%.1f deg"))
                light->setOuterCutOff(glm::cos(glm::radians(outer)));
            if (gui->isItemActivated())
                s_outerBefore = outerPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = glm::degrees(glm::acos(light->getOuterCutOff()));
                float before = s_outerBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setOuterCutOff(glm::cos(glm::radians(before))); },
                    [cap, after]()
                    { cap->setOuterCutOff(glm::cos(glm::radians(after))); }));
            }
        }
    }

    gui->tableEnd();
}

// ---------------------------------------------------------------------------
// Helper: collapsing section header with a small "+" button on the right.
// Returns true if the section is open.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Script picker helpers (used by the Scripts component)
//
// The picker lists every script source under Assets/: raw .as files and .logic
// node graphs. Picking a .as attaches that file directly; picking a .logic
// attaches the generated .as path under Library/GeneratedScripts/<logic-uuid>.as.
// The generated .as is produced when the .logic is saved in the Script Editor,
// so picking a .logic that hasn't been saved yet will only succeed once the
// user opens and saves it (buildScripts() warns "source missing" in the meantime).
// ---------------------------------------------------------------------------
namespace {

struct ScriptListEntry
{
    std::string displayName;
    fs::path    asPath;
};

std::vector<ScriptListEntry> collectProjectScripts(Manager *mgr)
{
    std::vector<ScriptListEntry> out;
    if (!mgr || mgr->projectPath.empty()) return out;

    fs::path assetsDir = mgr->projectPath / "Assets";
    if (!fs::exists(assetsDir)) return out;

    fs::path genDir = mgr->projectPath / "Library" / "GeneratedScripts";

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(assetsDir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        const fs::path &p   = it->path();
        std::string     ext = p.extension().string();
        std::string     rel = fs::relative(p, assetsDir, ec).generic_string();

        if (ext == ".as")
        {
            out.push_back({rel, p});
        }
        else if (ext == ".logic")
        {
            try
            {
                std::ifstream f(p);
                if (!f.is_open()) continue;
                nlohmann::json j; f >> j;
                std::string uuid = j.value("uuid", std::string());
                if (uuid.empty()) continue;
                out.push_back({rel, genDir / (uuid + ".as")});
            }
            catch (...) {}
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ScriptListEntry &a, const ScriptListEntry &b)
              { return a.displayName < b.displayName; });
    return out;
}

// Returns the human-friendly name for an attached script's stored fileName.
// Generated scripts (under Library/GeneratedScripts/<uuid>.as) are mapped
// back to the .logic file whose uuid produced them.
std::string scriptDisplayName(Manager *mgr, const std::string &storedPath)
{
    fs::path asPath(storedPath);
    if (!mgr || mgr->projectPath.empty())
        return asPath.filename().string();

    fs::path genDir = mgr->projectPath / "Library" / "GeneratedScripts";
    std::error_code ec;
    if (asPath.parent_path() != genDir)
        return asPath.filename().string();

    // Locate the .logic in Assets/ whose uuid matches the .as stem.
    std::string  uuid      = asPath.stem().string();
    fs::path     assetsDir = mgr->projectPath / "Assets";
    if (!fs::exists(assetsDir))
        return asPath.filename().string();

    for (auto it = fs::recursive_directory_iterator(assetsDir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        if (it->path().extension() != ".logic") continue;
        try
        {
            std::ifstream f(it->path());
            if (!f.is_open()) continue;
            nlohmann::json j; f >> j;
            if (j.value("uuid", std::string()) == uuid)
                return it->path().filename().string();
        }
        catch (...) {}
    }
    return asPath.filename().string();
}

} // namespace

// ---------------------------------------------------------------------------
// Components section (Physics / Scripts / Particles / Audio Sources / Listener)
// ---------------------------------------------------------------------------
static void drawComponentsSection(kGuiManager *gui, kObject *obj, Manager *manager)
{
    gui->spacing();
    gui->separatorText("Components");
    gui->spacing();

    // Each component section renders only when its data is present. To add a
    // component, right-click the hint strip at the bottom — that opens a
    // popup with every available component type (singletons disabled when
    // already present).

    // ── Physics ─────────────────────────────────────────────────────────────
    if (obj->getHasPhysicsDesc())
    {
        if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            kPhysicsObjectDesc &desc = obj->getPhysicsDesc();
            const bool dynamic = (desc.type == kPhysicsObjectType::Dynamic);

            if (beginPropTable(gui, "PhysTable"))
            {
                // Body type — Mesh shape disallows Dynamic / Trigger in Jolt,
                // so we grey those out instead of silently coercing on Play.
                const char *bodyNames[] = { "Dynamic", "Static", "Kinematic", "Trigger" };
                int bodyIdx = (int)desc.type;
                const bool needsStaticBody =
                    (desc.shape.type == kPhysicsShapeType::Mesh ||
                     desc.shape.type == kPhysicsShapeType::Plane);
                propLabel(gui, "Body Type");
                if (ImGui::BeginCombo("##PhysBody", bodyNames[bodyIdx]))
                {
                    for (int i = 0; i < 4; ++i)
                    {
                        bool allowed = !needsStaticBody ||
                                       i == (int)kPhysicsObjectType::Static ||
                                       i == (int)kPhysicsObjectType::Kinematic;
                        bool selected = (i == bodyIdx);
                        if (ImGui::Selectable(bodyNames[i], selected,
                                              allowed ? 0 : ImGuiSelectableFlags_Disabled))
                        {
                            desc.type = (kPhysicsObjectType)i;
                            manager->projectSaved = false;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // Shape
                const char *shapeNames[] = {
                    "Sphere", "Box", "Capsule", "Cylinder", "Convex Hull", "Mesh", "Plane"
                };
                int shapeIdx = (int)desc.shape.type;
                propLabel(gui, "Shape");
                if (ImGui::Combo("##PhysShape", &shapeIdx, shapeNames,
                                 IM_ARRAYSIZE(shapeNames)))
                {
                    desc.shape.type = (kPhysicsShapeType)shapeIdx;
                    // Mesh and Plane can only be Static/Kinematic in Jolt;
                    // auto-coerce so the body-type combo doesn't sit on an
                    // illegal value.
                    const bool needsStatic =
                        (desc.shape.type == kPhysicsShapeType::Mesh ||
                         desc.shape.type == kPhysicsShapeType::Plane);
                    if (needsStatic &&
                        desc.type != kPhysicsObjectType::Static &&
                        desc.type != kPhysicsObjectType::Kinematic)
                        desc.type = kPhysicsObjectType::Static;
                    manager->projectSaved = false;
                }

                // Shape params
                switch (desc.shape.type)
                {
                    case kPhysicsShapeType::Box:
                    {
                        float he[3] = { desc.shape.halfExtents.x, desc.shape.halfExtents.y, desc.shape.halfExtents.z };
                        propLabel(gui, "Half Extents");
                        if (ImGui::DragFloat3("##PhysHE", he, 0.01f, 0.001f, 1000.0f))
                        {
                            desc.shape.halfExtents = kVec3(he[0], he[1], he[2]);
                            manager->projectSaved = false;
                        }
                        break;
                    }
                    case kPhysicsShapeType::Sphere:
                        propLabel(gui, "Radius");
                        if (ImGui::DragFloat("##PhysR", &desc.shape.radius, 0.01f, 0.001f, 1000.0f))
                            manager->projectSaved = false;
                        break;
                    case kPhysicsShapeType::Capsule:
                    case kPhysicsShapeType::Cylinder:
                        propLabel(gui, "Radius");
                        if (ImGui::DragFloat("##PhysR", &desc.shape.radius, 0.01f, 0.001f, 1000.0f))
                            manager->projectSaved = false;
                        propLabel(gui, "Height");
                        if (ImGui::DragFloat("##PhysH", &desc.shape.height, 0.01f, 0.001f, 1000.0f))
                            manager->projectSaved = false;
                        break;
                    case kPhysicsShapeType::ConvexHull:
                    case kPhysicsShapeType::Mesh:
                    {
                        propLabel(gui, "Source");
                        ImGui::TextDisabled(desc.shape.type == kPhysicsShapeType::Mesh
                            ? "Object's mesh (static / kinematic only)"
                            : "Object's mesh (convex hull)");

                        // Per-axis stretch applied via Jolt's ScaledShape.
                        float sc[3] = { desc.shape.customScale.x,
                                        desc.shape.customScale.y,
                                        desc.shape.customScale.z };
                        propLabel(gui, "Size");
                        if (ImGui::DragFloat3("##PhysScale", sc, 0.01f, 0.01f, 1000.0f, "%.3f"))
                        {
                            desc.shape.customScale = kVec3(sc[0], sc[1], sc[2]);
                            manager->projectSaved = false;
                        }
                        break;
                    }
                    case kPhysicsShapeType::Plane:
                    {
                        propLabel(gui, "Source");
                        ImGui::TextDisabled("Object's +Y axis (static / kinematic only)");

                        // Plane is mathematically infinite; halfExtents.x/z
                        // drive the broadphase rectangle so a finite size is
                        // useful for picking and gameplay queries.
                        float he[3] = { desc.shape.halfExtents.x,
                                        desc.shape.halfExtents.y,
                                        desc.shape.halfExtents.z };
                        propLabel(gui, "Half Size");
                        if (ImGui::DragFloat3("##PlaneHE", he, 0.05f, 0.1f, 10000.0f, "%.2f"))
                        {
                            desc.shape.halfExtents = kVec3(he[0], he[1], he[2]);
                            manager->projectSaved = false;
                        }
                        break;
                    }
                }

                // Mass / damping / gravity-factor are Dynamic-only; greyed
                // out otherwise so the user still sees the value.
                gui->beginDisabled(!dynamic);
                propLabel(gui, "Mass (kg)");
                if (ImGui::DragFloat("##PhysMass", &desc.mass, 0.05f, 0.001f, 10000.0f, "%.3f"))
                    manager->projectSaved = false;
                gui->endDisabled();

                propLabel(gui, "Friction");
                if (ImGui::DragFloat("##PhysFr", &desc.friction, 0.01f, 0.0f, 1.0f, "%.2f"))
                    manager->projectSaved = false;

                propLabel(gui, "Restitution");
                if (ImGui::DragFloat("##PhysRest", &desc.restitution, 0.01f, 0.0f, 1.0f, "%.2f"))
                    manager->projectSaved = false;

                gui->beginDisabled(!dynamic);
                propLabel(gui, "Linear Damping");
                if (ImGui::DragFloat("##PhysLD", &desc.linearDamping, 0.01f, 0.0f, 10.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Angular Damping");
                if (ImGui::DragFloat("##PhysAD", &desc.angularDamping, 0.01f, 0.0f, 10.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Gravity Factor");
                if (ImGui::DragFloat("##PhysGF", &desc.gravityFactor, 0.01f, -10.0f, 10.0f, "%.2f"))
                    manager->projectSaved = false;
                gui->endDisabled();

                gui->tableEnd();
            }

            gui->spacing();
            if (ImGui::SmallButton("Remove Physics##RemPhys"))
            {
                obj->setHasPhysicsDesc(false);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── Character Controller ────────────────────────────────────────────────
    if (obj->getHasCharacterDesc())
    {
        if (ImGui::CollapsingHeader("Character Controller", ImGuiTreeNodeFlags_DefaultOpen))
        {
            kCharacterControllerDesc &cd = obj->getCharacterDesc();

            if (beginPropTable(gui, "CharTable"))
            {
                propLabel(gui, "Radius");
                if (ImGui::DragFloat("##CCRad", &cd.radius, 0.01f, 0.01f, 100.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Height");
                if (ImGui::DragFloat("##CCHt", &cd.height, 0.01f, 0.05f, 100.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Mass (kg)");
                if (ImGui::DragFloat("##CCMass", &cd.mass, 0.5f, 0.1f, 10000.0f, "%.2f"))
                    manager->projectSaved = false;

                propLabel(gui, "Friction");
                if (ImGui::DragFloat("##CCFr", &cd.friction, 0.01f, 0.0f, 1.0f, "%.2f"))
                    manager->projectSaved = false;

                propLabel(gui, "Slope Limit");
                if (ImGui::DragFloat("##CCSlope", &cd.slopeLimit, 0.5f, 0.0f, 89.0f, "%.1f"))
                    manager->projectSaved = false;

                propLabel(gui, "Step Height");
                if (ImGui::DragFloat("##CCStep", &cd.stepHeight, 0.01f, 0.0f, 10.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Gravity Factor");
                if (ImGui::DragFloat("##CCGF", &cd.gravityFactor, 0.01f, -10.0f, 10.0f, "%.2f"))
                    manager->projectSaved = false;

                gui->tableEnd();
            }

            gui->spacing();
            if (ImGui::SmallButton("Remove Character##RemChar"))
            {
                obj->setHasCharacterDesc(false);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── Navigation ───────────────────────────────────────────────────────────
    // Bakes a nav mesh from the scene's static meshes (all, or within an area
    // box centred on this object). Baking is an editor action; the result is
    // held by the Manager, not serialized.
    if (obj->getHasNavMeshDesc())
    {
        if (ImGui::CollapsingHeader("Navigation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            kNavMeshDesc &nd = obj->getNavMeshDesc();

            // Geometry source: All vs Area. Radio buttons don't fit the
            // label/widget table layout, so we draw this row inline.
            ImGui::TextUnformatted("Source:");
            ImGui::SameLine();
            if (ImGui::RadioButton("All##NavAll", !nd.useArea))
            {
                nd.useArea = false;
                manager->projectSaved = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Area##NavArea", nd.useArea))
            {
                nd.useArea = true;
                manager->projectSaved = false;
            }

            if (beginPropTable(gui, "NavTable"))
            {
                if (nd.useArea)
                {
                    float sz[3] = { nd.areaSize.x, nd.areaSize.y, nd.areaSize.z };
                    propLabel(gui, "Area Size");
                    if (ImGui::DragFloat3("##NavSz", sz, 0.1f, 0.1f, 100000.0f))
                    {
                        nd.areaSize = kVec3(sz[0], sz[1], sz[2]);
                        manager->projectSaved = false;
                    }
                }

                propLabel(gui, "Agent Radius");
                if (ImGui::DragFloat("##NavAR", &nd.config.agentRadius, 0.01f, 0.01f, 100.0f, "%.2f"))
                    manager->projectSaved = false;
                propLabel(gui, "Agent Height");
                if (ImGui::DragFloat("##NavAH", &nd.config.agentHeight, 0.01f, 0.01f, 100.0f, "%.2f"))
                    manager->projectSaved = false;
                propLabel(gui, "Max Climb");
                if (ImGui::DragFloat("##NavMC", &nd.config.agentMaxClimb, 0.01f, 0.0f, 100.0f, "%.2f"))
                    manager->projectSaved = false;
                propLabel(gui, "Max Slope");
                if (ImGui::DragFloat("##NavMS", &nd.config.agentMaxSlope, 0.5f, 0.0f, 89.0f, "%.1f"))
                    manager->projectSaved = false;
                propLabel(gui, "Cell Size");
                if (ImGui::DragFloat("##NavCS", &nd.config.cellSize, 0.01f, 0.01f, 10.0f, "%.2f"))
                    manager->projectSaved = false;

                gui->tableEnd();
            }

            if (nd.useArea)
                ImGui::TextDisabled("  Box is centred on this object's position.");

            gui->spacing();

            if (ImGui::Button("Bake##Nav", ImVec2(80, 0)))
                manager->bakeNavMesh(obj);
            ImGui::SameLine();
            if (ImGui::Button("Clear##Nav", ImVec2(80, 0)))
                manager->clearNavMesh(obj);
            ImGui::SameLine();
            if (manager->isNavMeshBaked(obj))
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Baked");
            else
                ImGui::TextDisabled("Not baked");

            gui->spacing();
            if (ImGui::SmallButton("Remove Navigation##RemNav"))
            {
                manager->clearNavMesh(obj);
                obj->setHasNavMeshDesc(false);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── Scripts ─────────────────────────────────────────────────────────────
    if (!obj->getScripts().empty())
    {
        if (ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto &scripts = obj->getScripts();
            kString toRemove;
            for (auto &s : scripts)
            {
                ImGui::PushID(s.uuid.c_str());
                if (ImGui::Checkbox("##ScActive", &s.isActive))
                    manager->projectSaved = false;
                ImGui::SameLine();
                ImGui::TextUnformatted(scriptDisplayName(manager, s.fileName).c_str());
                ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f);
                if (ImGui::SmallButton("x##RemSc"))
                    toRemove = s.uuid;
                ImGui::PopID();
            }
            if (!toRemove.empty())
            {
                obj->removeScript(toRemove);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── Particles ───────────────────────────────────────────────────────────
    if (!obj->getParticles().empty())
    {
        if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto &particles = obj->getParticles();
            kString toRemove;
            for (auto &p : particles)
            {
                ImGui::PushID(p.uuid.c_str());
                if (ImGui::Checkbox("##PartActive", &p.isActive))
                    manager->projectSaved = false;
                ImGui::SameLine();
                char nameBuf[64];
                strncpy_s(nameBuf, sizeof(nameBuf), p.name.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
                if (ImGui::InputText("##PartName", nameBuf, sizeof(nameBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    p.name = nameBuf;
                    manager->projectSaved = false;
                }
                ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f);
                if (ImGui::SmallButton("x##RemPart"))
                    toRemove = p.uuid;
                ImGui::PopID();
            }
            if (!toRemove.empty())
            {
                obj->removeParticle(toRemove);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── Audio Sources ────────────────────────────────────────────────────────
    if (!obj->getAudioSources().empty())
    {
        if (ImGui::CollapsingHeader("Audio Sources", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto &sources = obj->getAudioSources();
            kString toRemove;
            for (auto &src : sources)
            {
                ImGui::PushID(src.uuid.c_str());
                if (ImGui::Checkbox("##AudActive", &src.isActive))
                    manager->projectSaved = false;
                ImGui::SameLine();
                fs::path p(src.audioFile);
                ImGui::TextUnformatted(p.filename().string().empty()
                    ? src.name.c_str() : p.filename().string().c_str());
                ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f);
                if (ImGui::SmallButton("x##RemAud"))
                    toRemove = src.uuid;

                // Expanded audio source properties
                ImGui::Indent();
                bool loopV = src.loop;
                if (ImGui::Checkbox("Loop##AudLoop", &loopV))
                { src.loop = loopV; manager->projectSaved = false; }
                ImGui::SameLine();
                bool powV = src.playOnAwake;
                if (ImGui::Checkbox("Play On Awake##AudPOA", &powV))
                { src.playOnAwake = powV; manager->projectSaved = false; }

                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("Volume##AudVol", &src.volume, 0.0f, 1.0f))
                    manager->projectSaved = false;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("Pitch##AudPitch", &src.pitch, 0.1f, 3.0f))
                    manager->projectSaved = false;

                bool spatV = src.spatialize;
                if (ImGui::Checkbox("3D Spatial##AudSpat", &spatV))
                { src.spatialize = spatV; manager->projectSaved = false; }

                if (src.spatialize)
                {
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::DragFloat("Min Dist##AudMin", &src.minDistance, 0.1f, 0.0f, src.maxDistance))
                        manager->projectSaved = false;
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::DragFloat("Max Dist##AudMax", &src.maxDistance, 1.0f, src.minDistance, 10000.0f))
                        manager->projectSaved = false;
                }
                ImGui::Unindent();

                ImGui::PopID();
            }
            if (!toRemove.empty())
            {
                obj->removeAudioSource(toRemove);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── Audio Listener ───────────────────────────────────────────────────────
    if (!obj->getAudioListeners().empty())
    {
        if (ImGui::CollapsingHeader("Audio Listener", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto &l = obj->getAudioListeners()[0];
            ImGui::PushID(l.uuid.c_str());
            if (ImGui::Checkbox("Active##ListActive", &l.isActive))
                manager->projectSaved = false;
            ImGui::SameLine();
            ImGui::TextUnformatted("Audio Listener");
            ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f);
            if (ImGui::SmallButton("x##RemList"))
            {
                obj->removeAudioListener(l.uuid);
                manager->projectSaved = false;
            }
            ImGui::PopID();
        }
        gui->spacing();
    }

    // ── "Add Component" button ──────────────────────────────────────────────
    {
        gui->spacing();
        if (ImGui::Button("Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            if (ImGui::BeginMenu("Physics", !obj->getHasPhysicsDesc()))
            {
                auto addBody = [&](kPhysicsShapeType s, kPhysicsObjectType t) {
                    kPhysicsObjectDesc &d = obj->getPhysicsDesc();
                    d.shape.type = s;
                    d.type       = t;
                    obj->setHasPhysicsDesc(true);
                    manager->projectSaved = false;
                };
                const bool hasMesh = (obj->getType() == NODE_TYPE_MESH);

                if (ImGui::MenuItem("Box Collider"))
                    addBody(kPhysicsShapeType::Box, kPhysicsObjectType::Dynamic);
                if (ImGui::MenuItem("Sphere Collider"))
                    addBody(kPhysicsShapeType::Sphere, kPhysicsObjectType::Dynamic);
                if (ImGui::MenuItem("Capsule Collider"))
                    addBody(kPhysicsShapeType::Capsule, kPhysicsObjectType::Dynamic);
                if (ImGui::MenuItem("Cylinder Collider"))
                    addBody(kPhysicsShapeType::Cylinder, kPhysicsObjectType::Dynamic);
                if (ImGui::MenuItem("Plane Collider"))
                    addBody(kPhysicsShapeType::Plane, kPhysicsObjectType::Static);
                // Mesh-derived shapes need an actual kMesh to read from.
                if (ImGui::MenuItem("Convex Hull Collider", nullptr, false, hasMesh))
                    addBody(kPhysicsShapeType::ConvexHull, kPhysicsObjectType::Dynamic);
                if (ImGui::MenuItem("Mesh Collider", nullptr, false, hasMesh))
                    addBody(kPhysicsShapeType::Mesh, kPhysicsObjectType::Static);
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Character Controller", nullptr, false, !obj->getHasCharacterDesc()))
            {
                obj->setHasCharacterDesc(true);
                manager->projectSaved = false;
            }

            if (ImGui::MenuItem("Navigation", nullptr, false, !obj->getHasNavMeshDesc()))
            {
                obj->setHasNavMeshDesc(true);
                manager->projectSaved = false;
            }

            if (ImGui::BeginMenu("Script"))
            {
                auto entries = collectProjectScripts(manager);
                if (entries.empty())
                {
                    ImGui::TextDisabled("No .as or .logic in Assets/");
                }
                else
                {
                    for (const auto &e : entries)
                    {
                        if (ImGui::MenuItem(e.displayName.c_str()))
                        {
                            kScript s;
                            s.uuid     = generateUuid();
                            s.fileName = e.asPath.string();
                            s.isActive = true;
                            obj->addScript(s);
                            manager->projectSaved = false;
                        }
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Particle System"))
            {
                kParticle p;
                p.uuid = generateUuid();
                obj->addParticle(p);
                manager->projectSaved = false;
            }

            if (ImGui::MenuItem("Audio Source"))
            {
                auto result = pfd::open_file("Select Audio File", "",
                    { "Audio Files", "*.wav *.mp3 *.ogg *.flac", "All Files", "*" }).result();
                if (!result.empty())
                {
                    kAudioSource src;
                    src.uuid      = generateUuid();
                    src.audioFile = result[0];
                    src.name      = fs::path(result[0]).filename().string();
                    obj->addAudioSource(src);
                    manager->projectSaved = false;
                }
            }

            if (ImGui::MenuItem("Audio Listener", nullptr, false, obj->getAudioListeners().empty()))
            {
                kAudioListener l;
                l.uuid = generateUuid();
                obj->addAudioListener(l);
                manager->projectSaved = false;
            }

            ImGui::EndPopup();
        }
    }
}

// ---------------------------------------------------------------------------
// Scene section
// ---------------------------------------------------------------------------
static void drawSceneSection(kGuiManager *gui, kScene *scene, Manager *mgr)
{
    if (!gui->collapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!beginPropTable(gui, "SceneTable"))
        return;

    // Ambient color
    {
        static kVec3 s_ambBefore;
        kVec3 amb = scene->getAmbientLightColor();
        float ambF[3] = {amb.r, amb.g, amb.b};
        propLabel(gui, "Ambient");
        if (gui->colorEdit3("##SceneAmbient", ambF))
            scene->setAmbientLightColor(kVec3(ambF[0], ambF[1], ambF[2]));
        if (gui->isItemActivated())
            s_ambBefore = amb;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = scene->getAmbientLightColor();
            kVec3 before = s_ambBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setAmbientLightColor(before); },
                [cap, after]()
                { cap->setAmbientLightColor(after); }));
        }
    }

    // Shadows toggle
    {
        bool shadows = scene->getShadowsEnabled();
        propLabel(gui, "Shadows");
        if (gui->checkbox("##SceneShadows", &shadows))
        {
            bool before = !shadows;
            bool after  = shadows;
            kScene *cap = scene;
            scene->setShadowsEnabled(shadows);
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]() { cap->setShadowsEnabled(before); },
                [cap, after]()  { cap->setShadowsEnabled(after);  }));
        }
    }

    // Shadow bias controls (only when shadows are enabled).
    if (scene->getShadowsEnabled())
    {
        static float s_shadowBiasBefore = 0.0f;
        float bias = scene->getShadowBias();
        float biasPreEdit = bias;
        propLabel(gui, "Shadow Bias");
        if (gui->dragFloat("##ShadowBias", &bias, 0.0001f, 0.0f, 0.05f, "%.5f"))
            scene->setShadowBias(bias);
        if (gui->isItemActivated())
            s_shadowBiasBefore = biasPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = scene->getShadowBias();
            float before = s_shadowBiasBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]() { cap->setShadowBias(before); },
                [cap, after]()  { cap->setShadowBias(after);  }));
        }

        static float s_shadowNBiasBefore = 0.0f;
        float nbias = scene->getShadowNormalBias();
        float nbiasPreEdit = nbias;
        propLabel(gui, "Shadow N.Bias");
        if (gui->dragFloat("##ShadowNBias", &nbias, 0.0005f, 0.0f, 0.1f, "%.5f"))
            scene->setShadowNormalBias(nbias);
        if (gui->isItemActivated())
            s_shadowNBiasBefore = nbiasPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = scene->getShadowNormalBias();
            float before = s_shadowNBiasBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]() { cap->setShadowNormalBias(before); },
                [cap, after]()  { cap->setShadowNormalBias(after);  }));
        }

        // Shadow map resolution — combo of standard sizes. Bigger = sharper
        // shadow but more VRAM (size² × cascadeCount × depth bpp).
        propLabel(gui, "Shadow Res");
        const char *resItems[] = { "512", "1024", "2048", "4096" };
        const int   resValues[] = { 512, 1024, 2048, 4096 };
        int curRes = scene->getShadowMapResolution();
        int resIdx = 2; // default to 2048
        for (int i = 0; i < 4; ++i) if (resValues[i] == curRes) { resIdx = i; break; }
        if (ImGui::Combo("##ShadowRes", &resIdx, resItems, IM_ARRAYSIZE(resItems)))
        {
            int before = curRes;
            int after  = resValues[resIdx];
            kScene *cap = scene;
            scene->setShadowMapResolution(after);
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]() { cap->setShadowMapResolution(before); },
                [cap, after]()  { cap->setShadowMapResolution(after);  }));
        }

        // Shadow softness — PCF tap spacing in texels. 0 = hard, ~3 = very soft.
        static float s_softnessBefore = 0.0f;
        float soft = scene->getShadowSoftness();
        float softPreEdit = soft;
        propLabel(gui, "Shadow Softness");
        if (gui->dragFloat("##ShadowSoftness", &soft, 0.05f, 0.0f, 5.0f, "%.2f"))
            scene->setShadowSoftness(soft);
        if (gui->isItemActivated())
            s_softnessBefore = softPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = scene->getShadowSoftness();
            float before = s_softnessBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]() { cap->setShadowSoftness(before); },
                [cap, after]()  { cap->setShadowSoftness(after);  }));
        }
    }

    // Skybox ambient toggle
    {
        bool enabled = scene->getSkyboxAmbientEnabled();
        propLabel(gui, "Sky Ambient");
        if (gui->checkbox("##SkyAmbient", &enabled))
        {
            bool before = !enabled;
            bool after = enabled;
            kScene *cap = scene;
            scene->setSkyboxAmbientEnabled(enabled);
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setSkyboxAmbientEnabled(before); },
                [cap, after]()
                { cap->setSkyboxAmbientEnabled(after); }));
        }
    }

    // Skybox ambient strength (only when enabled)
    if (scene->getSkyboxAmbientEnabled())
    {
        static float s_skyStrBefore = 0.0f;
        float str = scene->getSkyboxAmbientStrength();
        float strPreEdit = str;
        propLabel(gui, "Sky Strength");
        if (gui->dragFloat("##SkyStrength", &str, 0.01f, 0.0f, 5.0f))
            scene->setSkyboxAmbientStrength(str);
        if (gui->isItemActivated())
            s_skyStrBefore = strPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = scene->getSkyboxAmbientStrength();
            float before = s_skyStrBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setSkyboxAmbientStrength(before); },
                [cap, after]()
                { cap->setSkyboxAmbientStrength(after); }));
        }
    }

    gui->tableEnd();

    // Skybox controls — for now just a "use the bundled default" button.
    // Future: pick a custom cubemap asset here.
    gui->spacing();
    if (ImGui::Button("Apply Default Skybox"))
        mgr->applyDefaultSkybox(scene);
}

// ---------------------------------------------------------------------------
// Project asset section
// ---------------------------------------------------------------------------

static void drawAssetThumbnail(kGuiManager *gui, const PanelProject::SelectedProjectAsset &asset)
{
    if (asset.thumbnail != nullptr)
    {
        float avail = gui->getContentRegionAvail().x;
        float imgSize = std::min(avail, 128.0f);
        gui->setCursorPosX(gui->getCursorPosX() + (avail - imgSize) * 0.5f);
        ImGui::Image(asset.thumbnail, ImVec2(imgSize, imgSize));
        gui->spacing();
    }
}

static void saveMetaJson(const fs::path &metaPath, const nlohmann::json &j)
{
    std::ofstream f(metaPath);
    if (f.is_open())
        f << j.dump(4);
}

static nlohmann::json loadMetaJson(const fs::path &metaPath)
{
    if (metaPath.empty())
        return nlohmann::json::object();
    try
    {
        if (!fs::exists(metaPath))
            return nlohmann::json::object();
        std::ifstream f(metaPath);
        if (!f.is_open())
            return nlohmann::json::object();
        return nlohmann::json::parse(f);
    }
    catch (...)
    {
        return nlohmann::json::object();
    }
}

// Mesh compression options
static const char *kMeshCompItems[] = {"None", "Low", "Medium", "High"};
// Tangent options
static const char *kTangentItems[] = {"Import", "Generate"};
// Animation compression options
static const char *kAnimCompItems[] = {"Off", "Keyframe Reduction", "Optimal"};

static void loadMeshSettings(const fs::path &metaPath,
                             float &scaleFactor, int &meshCompression, bool &generateCollider,
                             int &tangents, bool &generateLightmapUV, bool &importAnimation,
                             int &animCompression)
{
    auto j = loadMetaJson(metaPath);
    scaleFactor = j.value("scaleFactor", 1.0f);
    meshCompression = j.value("meshCompression", 0);
    generateCollider = j.value("generateCollider", false);
    tangents = j.value("tangents", 0);
    generateLightmapUV = j.value("generateLightmapUV", false);
    importAnimation = j.value("importAnimation", true);
    animCompression = j.value("animCompression", 0);
}

static void drawMeshImportSettings(kGuiManager *gui, const PanelProject::SelectedProjectAsset &asset, Manager *mgr)
{
    static kString lastUuid;
    static float scaleFactor = 1.0f;
    static int meshCompression = 0;
    static bool generateCollider = false;
    static int tangents = 0;
    static bool generateLightmapUV = false;
    static bool importAnimation = true;
    static int animCompression = 0;
    static bool dirty = false;

    if (asset.uuid != lastUuid)
    {
        lastUuid = asset.uuid;
        dirty = false;
        loadMeshSettings(asset.metaPath, scaleFactor, meshCompression, generateCollider, tangents, generateLightmapUV, importAnimation, animCompression);
    }

    if (!gui->collapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!beginPropTable(gui, "MeshImportTable"))
        return;

    propLabel(gui, "Scale Factor");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::DragFloat("##ScaleFactor", &scaleFactor, 0.001f, 0.0001f, 100.0f, "%.4f"))
        dirty = true;

    propLabel(gui, "Mesh Compression");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##MeshComp", &meshCompression, kMeshCompItems, IM_ARRAYSIZE(kMeshCompItems)))
        dirty = true;

    propLabel(gui, "Generate Collider");
    if (ImGui::Checkbox("##GenCollider", &generateCollider))
        dirty = true;

    propLabel(gui, "Tangents");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##Tangents", &tangents, kTangentItems, IM_ARRAYSIZE(kTangentItems)))
        dirty = true;

    propLabel(gui, "Lightmap UVs");
    if (ImGui::Checkbox("##LightmapUV", &generateLightmapUV))
        dirty = true;

    propLabel(gui, "Import Animation");
    if (ImGui::Checkbox("##ImportAnim", &importAnimation))
        dirty = true;

    propLabel(gui, "Anim Compression");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##AnimComp", &animCompression, kAnimCompItems, IM_ARRAYSIZE(kAnimCompItems)))
        dirty = true;

    propLabel(gui, "Material");
    if (ImGui::Button("Extract##Mat", ImVec2(-FLT_MIN, 0)))
    {
        // TODO: extract embedded materials from the mesh asset
    }

    propLabel(gui, "Texture");
    if (ImGui::Button("Extract##Tex", ImVec2(-FLT_MIN, 0)))
    {
        // TODO: extract embedded textures from the mesh asset
    }

    gui->tableEnd();
    gui->spacing();

    bool wasDisabled = !dirty;
    if (wasDisabled)
        gui->beginDisabled(true);
    float btnW = (gui->getContentRegionAvail().x - 4.0f) * 0.5f;
    if (ImGui::Button("Apply##Mesh", ImVec2(btnW, 0)) && !asset.metaPath.empty())
    {
        auto j = loadMetaJson(asset.metaPath);
        j["scaleFactor"] = scaleFactor;
        j["meshCompression"] = meshCompression;
        j["generateCollider"] = generateCollider;
        j["tangents"] = tangents;
        j["generateLightmapUV"] = generateLightmapUV;
        j["importAnimation"] = importAnimation;
        j["animCompression"] = animCompression;
        saveMetaJson(asset.metaPath, j);
        dirty = false;
    }
    gui->sameLine(0, 4.0f);
    if (ImGui::Button("Revert##Mesh", ImVec2(btnW, 0)))
    {
        loadMeshSettings(asset.metaPath, scaleFactor, meshCompression, generateCollider,
                         tangents, generateLightmapUV, importAnimation, animCompression);
        dirty = false;
    }
    if (wasDisabled)
        gui->endDisabled();
}

// Image type options ("Normal Map" = index 3; treated as linear tangent-space data)
static const char *kImgTypeItems[] = {"Texture", "GUI", "Sprite", "Normal Map"};
// Max size options
static const char *kMaxSizeItems[] = {"32", "64", "128", "256", "512", "1024", "2048", "4096"};
// Compression options
static const char *kImgCompItems[] = {"None", "Low Quality", "Normal Quality", "High Quality"};
// Alpha source options
static const char *kAlphaSrcItems[] = {"None", "Input Alpha", "From Grayscale"};
// Wrap mode options
static const char *kWrapModeItems[] = {"Repeat", "Clamp", "Mirror"};
// Filter mode options
static const char *kFilterModeItems[] = {"Point", "Bilinear", "Trilinear"};
// Normal-map gradient filtering (grayscale -> normal)
static const char *kNormalFilterItems[] = {"Sharp", "Smooth"};
// Channel / colour-space options
static const char *kChannelItems[] = {"sRGB", "Linear Color", "Linear Grayscale"};

static constexpr int kImgTypeNormalMap = 3;

// Backing state for the image import-settings panel.
struct ImgImportState
{
    int   imageType      = 0;
    int   maxSizeIndex   = 5;
    int   compression    = 2;
    int   alphaSource    = 0;
    int   channel        = 0;   ///< 0=sRGB, 1=Linear Color, 2=Linear Grayscale.
    bool  generateMipmap = true;
    int   wrapMode       = 0;
    int   filterMode     = 1;
    bool  flipVertical   = false;
    // Normal-map-only
    bool  flipGreen      = false;
    bool  fromGrayscale  = false;
    float bumpiness      = 1.0f;
    int   normalFilter   = 0;
};

static void loadImageSettings(const fs::path &metaPath, ImgImportState &s)
{
    auto j = loadMetaJson(metaPath);
    s.imageType      = j.value("imageType", 0);
    s.maxSizeIndex   = j.value("maxSizeIndex", 5);
    s.compression    = j.value("compression", 2);
    s.alphaSource    = j.value("alphaSource", 0);
    // Migrate the old boolean sRGB flag: true -> sRGB (0), false -> Linear Color (1).
    s.channel        = j.value("channel", j.value("sRGB", true) ? 0 : 1);
    s.generateMipmap = j.value("generateMipmap", true);
    s.wrapMode       = j.value("wrapMode", 0);
    s.filterMode     = j.value("filterMode", 1);
    s.flipVertical   = j.value("flipVertical", false);
    s.flipGreen      = j.value("flipGreen", false);
    s.fromGrayscale  = j.value("fromGrayscale", false);
    s.bumpiness      = j.value("bumpiness", 1.0f);
    s.normalFilter   = j.value("normalFilter", 0);
}

static void drawImageImportSettings(kGuiManager *gui, const PanelProject::SelectedProjectAsset &asset, Manager *mgr)
{
    static kString lastUuid;
    static ImgImportState s;
    static bool dirty = false;

    if (asset.uuid != lastUuid)
    {
        lastUuid = asset.uuid;
        dirty = false;
        loadImageSettings(asset.metaPath, s);
    }

    if (!gui->collapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!beginPropTable(gui, "ImageImportTable"))
        return;

    const bool isNormal = (s.imageType == kImgTypeNormalMap);

    propLabel(gui, "Image Type");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##ImgType", &s.imageType, kImgTypeItems, IM_ARRAYSIZE(kImgTypeItems)))
    {
        dirty = true;
        // Normal maps are linear data — default the Channel to Linear Color when
        // switching to Normal Map (the user can still override it).
        if (s.imageType == kImgTypeNormalMap && s.channel == 0)
            s.channel = 1;
    }

    // Normal-map-specific options.
    if (isNormal)
    {
        propLabel(gui, "Flip Green Channel");
        if (ImGui::Checkbox("##FlipGreen", &s.flipGreen))
            dirty = true;

        propLabel(gui, "Create From Grayscale");
        if (ImGui::Checkbox("##FromGray", &s.fromGrayscale))
            dirty = true;

        propLabel(gui, "Bumpiness");
        gui->setNextItemWidth(-FLT_MIN);
        if (!s.fromGrayscale) gui->beginDisabled(true);
        if (ImGui::DragFloat("##Bumpiness", &s.bumpiness, 0.05f, 0.0f, 20.0f))
            dirty = true;
        if (!s.fromGrayscale) gui->endDisabled();

        propLabel(gui, "Filtering");
        gui->setNextItemWidth(-FLT_MIN);
        // Filtering only affects the grayscale->normal gradient, so it's inert
        // for an already-authored normal map.
        if (!s.fromGrayscale) gui->beginDisabled(true);
        if (ImGui::Combo("##NormalFilter", &s.normalFilter, kNormalFilterItems, IM_ARRAYSIZE(kNormalFilterItems)))
            dirty = true;
        if (!s.fromGrayscale) gui->endDisabled();
    }

    propLabel(gui, "Max Size");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##MaxSize", &s.maxSizeIndex, kMaxSizeItems, IM_ARRAYSIZE(kMaxSizeItems)))
        dirty = true;

    propLabel(gui, "Compression");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##ImgComp", &s.compression, kImgCompItems, IM_ARRAYSIZE(kImgCompItems)))
        dirty = true;

    // Alpha source doesn't apply to normal maps (no alpha channel).
    if (!isNormal)
    {
        propLabel(gui, "Alpha Source");
        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##AlphaSrc", &s.alphaSource, kAlphaSrcItems, IM_ARRAYSIZE(kAlphaSrcItems)))
            dirty = true;
    }

    propLabel(gui, "Channel");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##Channel", &s.channel, kChannelItems, IM_ARRAYSIZE(kChannelItems)))
        dirty = true;

    propLabel(gui, "Generate Mipmap");
    if (ImGui::Checkbox("##GenMipmap", &s.generateMipmap))
        dirty = true;

    propLabel(gui, "Flip Vertically");
    if (ImGui::Checkbox("##FlipVert", &s.flipVertical))
        dirty = true;

    propLabel(gui, "Wrap Mode");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##WrapMode", &s.wrapMode, kWrapModeItems, IM_ARRAYSIZE(kWrapModeItems)))
        dirty = true;

    propLabel(gui, "Filter Mode");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##FilterMode", &s.filterMode, kFilterModeItems, IM_ARRAYSIZE(kFilterModeItems)))
        dirty = true;

    gui->tableEnd();
    gui->spacing();

    bool wasDisabled = !dirty;
    if (wasDisabled)
        gui->beginDisabled(true);
    float btnW = (gui->getContentRegionAvail().x - 4.0f) * 0.5f;
    if (ImGui::Button("Apply##Image", ImVec2(btnW, 0)) && !asset.metaPath.empty())
    {
        auto j = loadMetaJson(asset.metaPath);
        j["imageType"]      = s.imageType;
        j["maxSizeIndex"]   = s.maxSizeIndex;
        j["compression"]    = s.compression;
        j["alphaSource"]    = s.alphaSource;
        j["channel"]        = s.channel;
        j["sRGB"]           = (s.channel == 0); // keep legacy flag in sync
        j["generateMipmap"] = s.generateMipmap;
        j["wrapMode"]       = s.wrapMode;
        j["filterMode"]     = s.filterMode;
        j["flipVertical"]   = s.flipVertical;
        j["flipGreen"]      = s.flipGreen;
        j["fromGrayscale"]  = s.fromGrayscale;
        j["bumpiness"]      = s.bumpiness;
        j["normalFilter"]   = s.normalFilter;
        saveMetaJson(asset.metaPath, j);
        dirty = false;

        // Re-process the source image into Library/ImportedAssets with these
        // settings, then reload it and refresh every material using it.
        if (mgr && !asset.uuid.empty())
            mgr->reimportTexture(asset.uuid);
    }
    gui->sameLine(0, 4.0f);
    if (ImGui::Button("Revert##Image", ImVec2(btnW, 0)))
    {
        loadImageSettings(asset.metaPath, s);
        dirty = false;
    }
    if (wasDisabled)
        gui->endDisabled();
}

// ---------------------------------------------------------------------------
// Material inspector
// ---------------------------------------------------------------------------

static void loadMaterialJson(const fs::path &srcPath, nlohmann::json &out)
{
    if (srcPath.empty() || !fs::exists(srcPath)) { out = nlohmann::json::object(); return; }
    try
    {
        std::ifstream f(srcPath);
        if (f.is_open()) f >> out;
        else out = nlohmann::json::object();
    }
    catch (...) { out = nlohmann::json::object(); }
}

static void saveMaterialJson(const fs::path &srcPath, const nlohmann::json &j)
{
    std::ofstream f(srcPath);
    if (f.is_open()) f << j.dump(4);
}

// Bring a pre-@var .mat (hardcoded top-level keys) into the new params format so
// the dynamic inspector shows its existing values. No-op if already migrated.
static void migrateLegacyMaterialJson(nlohmann::json &m)
{
    if (!m.is_object()) return;
    if (m.contains("params") && m["params"].is_object()) return;
    nlohmann::json p = nlohmann::json::object();
    auto move = [&](const char *legacy, const char *var) {
        if (m.contains(legacy)) p[var] = m[legacy];
    };
    move("diffuse",          "material.diffuse");
    move("ambient",          "material.ambient");
    move("specular",         "material.specular");
    move("shininess",        "material.shininess");
    move("metallic",          "material.metallic");
    move("roughness",         "material.roughness");
    move("glossiness",        "material.glossiness");
    move("uv_tiling",         "material.tiling");
    move("texture_albedo",    "albedoMap");
    move("texture_normal",    "normalMap");
    move("texture_specular",  "specularMap");
    move("texture_glossiness","glossinessMap");
    move("texture_emissive",  "emissiveMap");
    m["params"] = p;
}

void PanelInspector::drawMaterialInspector(const PanelProject::SelectedProjectAsset& asset)
{
    // Reload from file when selection changes
    if (asset.uuid != matInspUuid)
    {
        matInspUuid  = asset.uuid;
        matInspDirty = false;
        fs::path srcPath;
        auto it = manager->fileMap.find(asset.uuid);
        if (it != manager->fileMap.end())
            srcPath = manager->projectPath / "Assets" / it->second.path;
        loadMaterialJson(srcPath, matInspJson);
        migrateLegacyMaterialJson(matInspJson);
        ++matInspVersion;
    }

    if (!gui->collapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!beginPropTable(gui, "MatTable"))
        return;

    auto changed = [&]() { matInspDirty = true; ++matInspVersion; };

    // --- Shader (built-ins + hand-written raw shader assets) ---
    {
        propLabel(gui, "Shader");

        // Options: the three built-ins, then every raw .glsl/.hlsl shader asset.
        std::vector<std::string> labels  = { "Unlit", "Phong", "PBR" };
        std::vector<std::string> uuids   = { "", "", "" }; // built-ins have no asset UUID
        for (const auto &kv : manager->fileMap)
        {
            if (kv.second.type != "shader") continue;
            labels.push_back(fs::path(kv.second.path).stem().string());
            uuids.push_back(kv.first);
        }

        // Current selection: a referenced raw shader (by UUID) wins; else the
        // built-in name.
        std::string curUuid = matInspJson.value("shader_uuid", std::string(""));
        std::string curName = matInspJson.value("shader", std::string("Phong"));
        int selected = 1; // Phong
        if (!curUuid.empty())
        {
            for (size_t i = 0; i < uuids.size(); ++i)
                if (uuids[i] == curUuid) { selected = (int)i; break; }
        }
        else
        {
            for (size_t i = 0; i < 3; ++i)
                if (labels[i] == curName) { selected = (int)i; break; }
        }

        std::vector<const char *> ptrs; ptrs.reserve(labels.size());
        for (auto &l : labels) ptrs.push_back(l.c_str());

        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##MatShader", &selected, ptrs.data(), (int)ptrs.size()))
        {
            if (uuids[selected].empty())
            {
                matInspJson["shader"] = labels[selected]; // built-in
                matInspJson["shader_uuid"] = "";
            }
            else
            {
                matInspJson["shader"] = "Custom";
                matInspJson["shader_uuid"] = uuids[selected]; // raw shader asset
            }
            changed();
        }
    }

    // --- Dynamic parameters, driven by the shader's `// @var` annotations ---
    {
        std::string src = manager->getMaterialShaderSource(matInspJson);
        std::vector<ShaderVar> vars = parseShaderVars(src);

        // Ensure a params object so writes land in the new (@var) format.
        if (!matInspJson.contains("params") || !matInspJson["params"].is_object())
            matInspJson["params"] = nlohmann::json::object();
        nlohmann::json &params = matInspJson["params"];

        // Texture-asset dropdown list — texture-type images only (imageType 0).
        bool hasSampler = false;
        for (const ShaderVar &v : vars)
            if (v.type == "sampler2D" || v.type == "samplerCube") { hasSampler = true; break; }

        std::vector<std::string> texUuids = {""};
        std::vector<std::string> texNames = {"(None)"};
        if (hasSampler)
        {
            // Collect first, then sort by display name (case-insensitive) so the
            // dropdown lists textures alphabetically; "(None)" stays at the top.
            std::vector<std::pair<std::string, std::string>> tex; // {name, uuid}
            for (const auto &kv : manager->fileMap)
            {
                if (kv.second.type != "image") continue;
                int imgType = 0;
                fs::path mp = manager->projectPath / "Library" / "Metadata" / (kv.first + ".json");
                try { std::ifstream mf(mp); if (mf) { nlohmann::json mj; mf >> mj; imgType = mj.value("imageType", 0); } }
                catch (...) {}
                // 0 = Texture, 3 = Normal Map (both used by materials); skip GUI/Sprite.
                if (imgType != 0 && imgType != 3) continue;
                tex.emplace_back(fs::path(kv.second.path).stem().string(), kv.first);
            }
            std::sort(tex.begin(), tex.end(), [](const auto &a, const auto &b) {
                const std::string &x = a.first, &y = b.first;
                return std::lexicographical_compare(
                    x.begin(), x.end(), y.begin(), y.end(),
                    [](unsigned char c1, unsigned char c2) { return std::tolower(c1) < std::tolower(c2); });
            });
            for (auto &t : tex) { texNames.push_back(t.first); texUuids.push_back(t.second); }
        }

        if (vars.empty())
            gui->textDisabled("No @var parameters in shader");

        for (const ShaderVar &v : vars)
        {
            propLabel(gui, v.label.c_str());
            kString id = "##mp_" + v.name;

            if (v.type == "vec3" || v.type == "vec4")
            {
                int n = (v.type == "vec4") ? 4 : 3;
                float c[4] = {1, 1, 1, 1};
                if (params.contains(v.name) && params[v.name].is_array())
                    for (int i = 0; i < n && i < (int)params[v.name].size(); ++i) c[i] = params[v.name][i];
                bool ch = (n == 4) ? ImGui::ColorEdit4(id.c_str(), c) : ImGui::ColorEdit3(id.c_str(), c);
                if (ch) { if (n == 4) params[v.name] = {c[0], c[1], c[2], c[3]}; else params[v.name] = {c[0], c[1], c[2]}; changed(); }
            }
            else if (v.type == "vec2")
            {
                float t[2] = {1, 1};
                if (params.contains(v.name) && params[v.name].is_array())
                    for (int i = 0; i < 2 && i < (int)params[v.name].size(); ++i) t[i] = params[v.name][i];
                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat2(id.c_str(), t, 0.01f)) { params[v.name] = {t[0], t[1]}; changed(); }
            }
            else if (v.type == "float")
            {
                float fdef = (v.name == "material.shininess")  ? 32.0f
                           : (v.name == "material.roughness")  ? 0.5f
                           : (v.name == "material.glossiness") ? 1.0f : 0.0f;
                float f = (params.contains(v.name) && params[v.name].is_number()) ? params[v.name].get<float>() : fdef;
                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat(id.c_str(), &f, 0.01f)) { params[v.name] = f; changed(); }
            }
            else if (v.type == "int")
            {
                int iv = (params.contains(v.name) && params[v.name].is_number()) ? params[v.name].get<int>() : 0;
                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt(id.c_str(), &iv)) { params[v.name] = iv; changed(); }
            }
            else if (v.type == "bool")
            {
                bool b = (params.contains(v.name) && params[v.name].is_boolean()) ? params[v.name].get<bool>() : false;
                if (ImGui::Checkbox(id.c_str(), &b)) { params[v.name] = b; changed(); }
            }
            else if (v.type == "sampler2D" || v.type == "samplerCube")
            {
                std::string cur = (params.contains(v.name) && params[v.name].is_string()) ? params[v.name].get<std::string>() : "";
                int sel = 0;
                for (size_t i = 0; i < texUuids.size(); ++i) if (texUuids[i] == cur) { sel = (int)i; break; }
                std::vector<const char *> ptrs; ptrs.reserve(texNames.size());
                for (auto &nm : texNames) ptrs.push_back(nm.c_str());
                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::Combo(id.c_str(), &sel, ptrs.data(), (int)ptrs.size()))
                { params[v.name] = texUuids[sel]; changed(); }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
                    {
                        std::string dropped((const char *)pl->Data);
                        { auto nl = dropped.find('\n'); if (nl != std::string::npos) dropped = dropped.substr(0, nl); }
                        auto fit = manager->fileMap.find(dropped);
                        if (fit != manager->fileMap.end() && fit->second.type == "image")
                        { params[v.name] = dropped; changed(); }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            else
            {
                gui->textDisabled(("unsupported: " + v.type).c_str());
            }
        }
    }

    // --- Single Sided (material render state, not a shader uniform) ---
    {
        propLabel(gui, "Single Sided");
        bool v = matInspJson.value("single_sided", true);
        if (ImGui::Checkbox("##MatSingle", &v))
        { matInspJson["single_sided"] = v; changed(); }
    }

    gui->tableEnd();
    gui->spacing();

    bool wasDisabled = !matInspDirty;
    if (wasDisabled) gui->beginDisabled(true);
    float btnW = (gui->getContentRegionAvail().x - 4.0f) * 0.5f;
    if (ImGui::Button("Apply##Mat", ImVec2(btnW, 0)))
    {
        auto it = manager->fileMap.find(asset.uuid);
        if (it != manager->fileMap.end())
        {
            fs::path srcPath = manager->projectPath / "Assets" / it->second.path;
            saveMaterialJson(srcPath, matInspJson);
            matInspDirty = false;
            fs::path thumbPath = manager->projectPath / "Library" / "Thumbnails" / (asset.uuid + ".png");
            if (fs::exists(thumbPath)) fs::remove(thumbPath);
            manager->checkAssetChange();
            // Rebuild runtime materials on scene objects that reference this
            // .mat so the edited settings show up immediately in the scene.
            manager->reapplyStoredMaterials();
            if (manager->panelProject)
            {
                manager->panelProject->pendingSelectUuid = asset.uuid;
                manager->panelProject->triggerRefresh();
            }
        }
    }
    gui->sameLine(0, 4.0f);
    if (ImGui::Button("Revert##Mat", ImVec2(btnW, 0)))
    {
        // Reload from file, broadcast new version so preview reverts too
        fs::path srcPath;
        auto it = manager->fileMap.find(asset.uuid);
        if (it != manager->fileMap.end())
            srcPath = manager->projectPath / "Assets" / it->second.path;
        loadMaterialJson(srcPath, matInspJson);
        migrateLegacyMaterialJson(matInspJson);
        matInspDirty = false;
        ++matInspVersion;
    }
    if (wasDisabled) gui->endDisabled();
}

// ---------------------------------------------------------------------------
// Main draw
// ---------------------------------------------------------------------------
void PanelInspector::draw(bool &opened)
{
    if (!opened)
        return;

    // Shader editor active: take over the inspector with preview (always interactive)
    if (manager->shaderPreview.active)
    {
        gui->windowStart("Inspector", &opened);
        drawShaderPreview();
        gui->windowEnd();
        return;
    }

    if (!manager->projectOpened)
        gui->beginDisabled(true);

    gui->windowStart("Inspector", &opened);

    // Project panel selection takes priority when scene selection is empty
    if (manager->panelProject != nullptr && manager->selectedObjects.empty() && manager->selectedScene == nullptr)
    {
        auto asset = manager->panelProject->getProjectSelection();
        if (asset.count > 1)
        {
            gui->spacing();
            kString text = std::to_string(asset.count) + " items selected";
            float tw = gui->calcTextSize(text).x;
            gui->setCursorPosX(gui->getCursorPosX() + (gui->getContentRegionAvail().x - tw) * 0.5f);
            gui->textDisabled(text);
            gui->windowEnd();
            if (!manager->projectOpened)
                gui->endDisabled();
            return;
        }
        if (asset.count == 1)
        {
            if (asset.isFolder)
            {
                drawInlineIcon(iconFileFolder, "Folder");
                gui->sameLine(0, 4.0f);
                gui->textUnformatted(asset.name.c_str());
            }
            else
            {
                drawInlineIcon(getFileTypeIcon(asset.fileType), asset.fileType.c_str());
                gui->sameLine(0, 4.0f);
                gui->textUnformatted(asset.name.c_str());
            }
            gui->spacing();
            gui->separator();
            gui->spacing();
            if (!asset.isFolder && asset.fileType == "mesh")
                drawModelViewer(asset);
            else if (!asset.isFolder && asset.fileType == "material")
                drawMaterialViewer(asset);
            else
                drawAssetThumbnail(gui, asset);
            if (!asset.isFolder)
            {
                if (asset.fileType == "mesh")
                    drawMeshImportSettings(gui, asset, manager);
                else if (asset.fileType == "image")
                    drawImageImportSettings(gui, asset, manager);
                else if (asset.fileType == "material")
                    drawMaterialInspector(asset);
            }
            gui->windowEnd();
            if (!manager->projectOpened)
                gui->endDisabled();
            return;
        }
    }

    if (manager->worldSelected)
    {
        drawInlineIcon(iconObjScene, "World");
        gui->sameLine(0, 4.0f);
        gui->textUnformatted("World");
        gui->spacing();
        gui->separator();
        gui->spacing();

        if (gui->collapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (beginPropTable(gui, "WorldTable"))
            {
                kWorld* world = manager->getWorld();
                propLabel(gui, "Active Camera");

                const auto& cams = world ? world->getCameras() : std::vector<kCamera*>{};
                std::vector<kCamera*> gameCams;
                for (kCamera* c : cams)
                    if (c != manager->editorCamera)
                        gameCams.push_back(c);

                int curIdx = -1;
                for (int i = 0; i < (int)gameCams.size(); ++i)
                    if (gameCams[i] == manager->defaultGameCamera) { curIdx = i; break; }

                std::vector<std::string> camNames;
                camNames.push_back("(None)");
                for (kCamera* c : gameCams)
                    camNames.push_back(c->getName().empty() ? "(unnamed)" : c->getName());

                int comboIdx = (curIdx >= 0) ? curIdx + 1 : 0;
                std::vector<const char*> camNamePtrs;
                for (auto& s : camNames) camNamePtrs.push_back(s.c_str());

                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##ActiveCamera", &comboIdx, camNamePtrs.data(), (int)camNamePtrs.size()))
                    manager->defaultGameCamera = (comboIdx == 0) ? nullptr : gameCams[comboIdx - 1];

                gui->tableEnd();
            }
        }
    }
    else if (manager->selectedScene != nullptr)
    {
        kScene *scene = manager->selectedScene;
        drawInlineIcon(iconObjScene, "Scene");
        gui->sameLine(0, 4.0f);
        {
            char nameBuf[256];
            strncpy_s(nameBuf, sizeof(nameBuf), scene->getName().c_str(), _TRUNCATE);
            gui->setNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##SceneName", nameBuf, sizeof(nameBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                kString before = scene->getName();
                kString after = kString(nameBuf);
                if (before != after)
                {
                    scene->setName(after);
                    kScene *cap = scene;
                    manager->undoRedo.push(std::make_unique<PropertyCommand>(
                        [cap, before]()
                        { cap->setName(before); },
                        [cap, after]()
                        { cap->setName(after); }));
                }
            }
        }
        gui->spacing();
        gui->separator();
        gui->spacing();
        drawSceneSection(gui, scene, manager);
    }
    else
    {

        size_t selCount = manager->selectedObjects.size();

        if (selCount == 0)
        {
            gui->spacing();
            const char *text = "Nothing is selected";
            float tw = gui->calcTextSize(text).x;
            gui->setCursorPosX(gui->getCursorPosX() + (gui->getContentRegionAvail().x - tw) * 0.5f);
            gui->textDisabled(text);
        }
        else if (selCount > 1)
        {
            gui->spacing();
            kString text = std::to_string(selCount) + " objects selected";
            float tw = gui->calcTextSize(text).x;
            gui->setCursorPosX(gui->getCursorPosX() + (gui->getContentRegionAvail().x - tw) * 0.5f);
            gui->textDisabled(text);

            gui->spacing();
            gui->separator();
            gui->spacing();

            // Multi-select Create Prefab: wraps the selection in a new empty
            // object positioned at the last-selected, parents everything under
            // it, and saves the empty as a prefab template + instance.
            if (gui->button("Create Prefab", kIvec2(-1, 0)))
                manager->createPrefabFromSelection();
        }
        else
        {
            kObject *obj = manager->selectedObject;

            if (obj == nullptr)
            {
                gui->textDisabled("(object not found)");
            }
            else
            {
                kNodeType type = obj->getType();

                ImTextureRef typeIcon  = iconObjMesh;
                const char  *typeLabel = "Mesh";
                if (type == NODE_TYPE_LIGHT)       { typeIcon = iconObjLight;  typeLabel = "Light";  }
                else if (type == NODE_TYPE_CAMERA) { typeIcon = iconObjCamera; typeLabel = "Camera"; }

                drawInlineIcon(typeIcon, typeLabel);
                gui->sameLine(0, 4.0f);

                {
                    char nameBuf[256];
                    strncpy_s(nameBuf, sizeof(nameBuf), obj->getName().c_str(), _TRUNCATE);
                    gui->setNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText("##ObjName", nameBuf, sizeof(nameBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        kString before = obj->getName();
                        kString after = kString(nameBuf);
                        if (before != after)
                        {
                            obj->setName(after);
                            kObject *cap = obj;
                            manager->undoRedo.push(std::make_unique<PropertyCommand>(
                                [cap, before]()
                                { cap->setName(before); },
                                [cap, after]()
                                { cap->setName(after); }));
                        }
                    }
                }

                {
                    bool active = obj->getActive();
                    bool prevActive = active;
                    if (gui->checkbox("Active", &active))
                    {
                        obj->setActive(active);
                        kObject *cap = obj;
                        bool after = active;
                        bool before = prevActive;
                        manager->undoRedo.push(std::make_unique<PropertyCommand>(
                            [cap, before]()
                            { cap->setActive(before); },
                            [cap, after]()
                            { cap->setActive(after); }));
                    }

                    gui->sameLine();
                    {
                        bool isStatic = obj->getStatic();
                        bool prevIsStatic = isStatic;
                        if (gui->checkbox("Static", &isStatic))
                        {
                            obj->setStatic(isStatic);
                            manager->getRenderer()->setOctreeDirty();
                            kObject *cap = obj;
                            bool after = isStatic;
                            bool before = prevIsStatic;
                            manager->undoRedo.push(std::make_unique<PropertyCommand>(
                                [cap, before]()
                                { cap->setStatic(before); },
                                [cap, after]()
                                { cap->setStatic(after); }));
                        }
                    }
                }

                gui->spacing();
                gui->separator();
                gui->spacing();

                drawTransformSection(gui, obj, manager);
                gui->spacing();

                if (type == NODE_TYPE_MESH)
                    drawMeshSection(gui, static_cast<kMesh *>(obj), manager);
                else if (type == NODE_TYPE_LIGHT)
                    drawLightSection(gui, static_cast<kLight *>(obj), manager);
                else if (type == NODE_TYPE_CAMERA)
                    drawCameraSection(gui, static_cast<kCamera *>(obj), manager);

                drawComponentsSection(gui, obj, manager);

                // Prefab actions: "Save as Prefab" works for any selection;
                // "Edit Prefab" only appears when the selection is a prefab
                // instance root (i.e. has a non-empty prefab_ref).
                gui->spacing();
                gui->separator();
                gui->spacing();

                if (gui->collapsingHeader("Prefab", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (!obj->getPrefabRef().empty())
                    {
                        // Prefab instance root — show linkage + lifecycle actions.
                        gui->text(kString("Linked: ") + obj->getPrefabRef());

                        if (gui->button("Edit Prefab", kIvec2(-1, 0)))
                        {
                            // Resolve the .prefab file by scanning Assets for
                            // a matching prefab UUID.
                            const kString &refUuid = obj->getPrefabRef();
                            fs::path found;
                            for (const auto &p : fs::recursive_directory_iterator(
                                    manager->projectPath / "Assets"))
                            {
                                if (!p.is_regular_file()) continue;
                                if (p.path().extension() != ".prefab") continue;
                                kPrefab tmp;
                                if (tmp.loadFromFile(p.path().string()) &&
                                    tmp.getUuid() == refUuid)
                                {
                                    found = p.path();
                                    break;
                                }
                            }
                            if (!found.empty())
                                manager->editPrefab(found);
                            else
                                std::cerr << "Edit Prefab: source .prefab not found for "
                                          << refUuid << "\n";
                        }

                        // Apply pushes per-instance edits/additions back into
                        // the .prefab template AND replaces every other live
                        // instance of this prefab with the new template.
                        // This is destructive — gate it behind a modal.
                        if (gui->button("Apply to Prefab", kIvec2(-1, 0)))
                            ImGui::OpenPopup("Apply to Prefab?");

                        if (ImGui::BeginPopupModal("Apply to Prefab?", nullptr,
                            ImGuiWindowFlags_AlwaysAutoResize))
                        {
                            ImGui::TextWrapped(
                                "This rewrites the prefab template with this "
                                "instance's current state, then refreshes every "
                                "instance of this prefab in the world.");
                            ImGui::Spacing();
                            ImGui::TextWrapped(
                                "Per-instance changes on OTHER instances will "
                                "be discarded. This action cannot be undone.");
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();

                            kString prefabUuid = obj->getPrefabRef();
                            if (ImGui::Button("Apply", ImVec2(120, 0)))
                            {
                                manager->applyPrefabInstance(obj);
                                manager->refreshAllPrefabInstances(prefabUuid);
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                                ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                        }

                        // Unpack severs the prefab link, leaving plain scene
                        // objects behind. Undoable via UnpackPrefabCommand.
                        if (gui->button("Unpack Prefab", kIvec2(-1, 0)))
                            manager->unpackPrefabInstance(obj);
                    }
                    else
                    {
                        // Not a prefab instance — offer to create one.
                        if (gui->button("Create Prefab", kIvec2(-1, 0)))
                            manager->createPrefabFromSelection();
                    }
                }
            }
        }

    } // end selectedScene else

    gui->windowEnd();

    if (!manager->projectOpened)
        gui->endDisabled();
}
