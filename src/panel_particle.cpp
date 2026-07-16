#include "panel_particle.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <SDL3/SDL_dialog.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ===========================================================================
// ParticleEmitterDef serialisation
// ===========================================================================

json ParticleEmitterDef::toJson() const
{
    json j;
    j["name"]             = name;
    j["start_time"]       = startTime;
    j["end_time"]         = endTime;
    j["forever"]          = forever;
    j["emission_rate"]    = emissionRate;
    j["max_particles"]    = maxParticles;
    j["lifetime"]         = lifetime;
    j["lifetime_var"]     = lifetimeVar;
    j["speed"]            = speed;
    j["speed_var"]        = speedVar;
    j["gravity_scale"]    = gravityScale;
    j["shape"]            = (int)shape;
    j["shape_size"]       = {{"x", shapeSize.x}, {"y", shapeSize.y}, {"z", shapeSize.z}};
    j["color_start"]      = {{"r", colorStart.r}, {"g", colorStart.g}, {"b", colorStart.b}, {"a", colorStart.a}};
    j["color_end"]        = {{"r", colorEnd.r},   {"g", colorEnd.g},   {"b", colorEnd.b},   {"a", colorEnd.a}};
    j["size_start"]       = sizeStart;
    j["size_end"]         = sizeEnd;
    j["sprite_path"]      = spritePath;
    j["mesh_uuid"]        = meshUuid;
    j["material_uuid"]    = materialUuid;
    j["physics_enabled"]  = physicsEnabled;
    j["physics_mass"]     = physicsMass;
    j["physics_drag"]     = physicsDrag;

    if (!sizeOverLifetime.keys.empty())
    {
        json curve = json::array();
        for (auto& k : sizeOverLifetime.keys)
            curve.push_back({{"t", k.time}, {"v", k.value}});
        j["size_curve"] = curve;
    }
    if (!colorOverLifetime.keys.empty())
    {
        json curve = json::array();
        for (auto& k : colorOverLifetime.keys)
            curve.push_back({{"t", k.time}, {"r", k.value.r}, {"g", k.value.g}, {"b", k.value.b}, {"a", k.value.a}});
        j["color_curve"] = curve;
    }
    if (!speedOverLifetime.keys.empty())
    {
        json curve = json::array();
        for (auto& k : speedOverLifetime.keys)
            curve.push_back({{"t", k.time}, {"v", k.value}});
        j["speed_curve"] = curve;
    }

    return j;
}

void ParticleEmitterDef::fromJson(const json& j)
{
    name            = j.value("name", "Emitter");
    startTime       = j.value("start_time", 0.0f);
    endTime         = j.value("end_time", 5.0f);
    forever         = j.value("forever", false);
    emissionRate    = j.value("emission_rate", 10.0f);
    maxParticles    = j.value("max_particles", 100);
    lifetime        = j.value("lifetime", 2.0f);
    lifetimeVar     = j.value("lifetime_var", 0.0f);
    speed           = j.value("speed", 1.0f);
    speedVar        = j.value("speed_var", 0.2f);
    gravityScale    = j.value("gravity_scale", 1.0f);
    shape           = (ParticleEmitShape)j.value("shape", 2);
    if (j.contains("shape_size"))
    {
        shapeSize.x = j["shape_size"].value("x", 0.5f);
        shapeSize.y = j["shape_size"].value("y", 1.0f);
        shapeSize.z = j["shape_size"].value("z", 0.5f);
    }
    if (j.contains("color_start"))
    {
        colorStart.r = j["color_start"].value("r", 1.0f);
        colorStart.g = j["color_start"].value("g", 1.0f);
        colorStart.b = j["color_start"].value("b", 1.0f);
        colorStart.a = j["color_start"].value("a", 1.0f);
    }
    if (j.contains("color_end"))
    {
        colorEnd.r = j["color_end"].value("r", 1.0f);
        colorEnd.g = j["color_end"].value("g", 1.0f);
        colorEnd.b = j["color_end"].value("b", 1.0f);
        colorEnd.a = j["color_end"].value("a", 0.0f);
    }
    sizeStart  = j.value("size_start", 0.1f);
    sizeEnd    = j.value("size_end", 0.0f);
    spritePath = j.value("sprite_path", "");
    meshUuid   = j.value("mesh_uuid", "");
    materialUuid = j.value("material_uuid", "");
    physicsEnabled = j.value("physics_enabled", false);
    physicsMass    = j.value("physics_mass", 0.1f);
    physicsDrag    = j.value("physics_drag", 0.0f);

    sizeOverLifetime.keys.clear();
    if (j.contains("size_curve") && j["size_curve"].is_array())
        for (auto& k : j["size_curve"])
            sizeOverLifetime.keys.push_back({k.value("t", 0.0f), k.value("v", 1.0f)});

    colorOverLifetime.keys.clear();
    if (j.contains("color_curve") && j["color_curve"].is_array())
        for (auto& k : j["color_curve"])
            colorOverLifetime.keys.push_back({k.value("t", 0.0f),
                kVec4(k.value("r",1.f), k.value("g",1.f), k.value("b",1.f), k.value("a",1.f))});

    speedOverLifetime.keys.clear();
    if (j.contains("speed_curve") && j["speed_curve"].is_array())
        for (auto& k : j["speed_curve"])
            speedOverLifetime.keys.push_back({k.value("t", 0.0f), k.value("v", 1.0f)});
}

// ===========================================================================
// ParticleDoc serialisation
// ===========================================================================

json ParticleDoc::toJson() const
{
    json j;
    j["uuid"]     = uuid;
    j["name"]     = name;
    j["duration"] = duration;
    j["looping"]  = looping;

    json emittersArr = json::array();
    for (auto& e : emitters)
        emittersArr.push_back(e.toJson());
    j["emitters"] = emittersArr;

    return j;
}

void ParticleDoc::fromJson(const json& j)
{
    uuid     = j.value("uuid", "");
    name     = j.value("name", "Particle System");
    duration = j.value("duration", 5.0f);
    looping  = j.value("looping", true);

    emitters.clear();
    if (j.contains("emitters") && j["emitters"].is_array())
    {
        for (auto& ej : j["emitters"])
        {
            ParticleEmitterDef e;
            e.fromJson(ej);
            emitters.push_back(e);
        }
    }
}

// ===========================================================================
// Helpers
// ===========================================================================

static const char* shapeName(ParticleEmitShape s)
{
    switch (s)
    {
        case ParticleEmitShape::Point:  return "Point";
        case ParticleEmitShape::Sphere: return "Sphere";
        case ParticleEmitShape::Cone:   return "Cone";
        case ParticleEmitShape::Box:    return "Box";
    }
    return "Point";
}

std::string PanelParticle::generateUuid()
{
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;

    uint64_t high = dist(gen);
    uint64_t low  = dist(gen);
    high &= 0xFFFFFFFFFFFF0FFFULL;
    high |= 0x0000000000004000ULL;
    low  &= 0x3FFFFFFFFFFFFFFFULL;
    low  |= 0x8000000000000000ULL;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (uint32_t)(high >> 32) << "-";
    ss << std::setw(4) << (uint16_t)(high >> 16) << "-";
    ss << std::setw(4) << (uint16_t)(high)       << "-";
    ss << std::setw(4) << (uint16_t)(low >> 48)  << "-";
    ss << std::setw(12)<< (low & 0x0000FFFFFFFFFFFFULL);
    return ss.str();
}

void SDLCALL PanelParticle::saveParticleCallback(void* userdata, const char* const* filelist, int filter)
{
    if (!filelist || !filelist[0]) return;
    PanelParticle* self = static_cast<PanelParticle*>(userdata);
    std::string path = filelist[0];
    if (path.size() < 9 || path.substr(path.size() - 9) != ".particle")
        path += ".particle";

    self->doc.uuid = self->doc.uuid.empty() ? generateUuid() : self->doc.uuid;
    json j = self->doc.toJson();
    std::ofstream ofs(path);
    ofs << j.dump(2);
    self->filePath = path;
    self->doc.dirty = false;
}

// ===========================================================================
// PanelParticle implementation
// ===========================================================================

PanelParticle::PanelParticle(kGuiManager* setGui, Manager* setManager)
    : gui(setGui), manager(setManager)
{
    newDoc();
}

void PanelParticle::newDoc()
{
    doc = ParticleDoc();
    doc.uuid = generateUuid();
    doc.name = "New Particle System";
    doc.duration = 5.0f;
    doc.looping = true;
    doc.dirty = false;
    filePath.clear();
    selectedEmitter = -1;
    currentTime = 0.0f;
    isPlaying = false;
}

void PanelParticle::loadDoc(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;

    try
    {
        json j = json::parse(ifs);
        doc.fromJson(j);
        filePath = path;
        doc.dirty = false;
        selectedEmitter = doc.emitters.empty() ? -1 : 0;
        currentTime = 0.0f;
        isPlaying = false;
    }
    catch (...) {}
}

void PanelParticle::saveDoc()
{
    if (filePath.empty())
    {
        saveDocAs();
        return;
    }

    doc.uuid = doc.uuid.empty() ? generateUuid() : doc.uuid;
    json j = doc.toJson();
    std::ofstream ofs(filePath);
    ofs << j.dump(2);
    doc.dirty = false;
}

void PanelParticle::saveDocAs()
{
    SDL_DialogFileFilter filters[] = {
        {"Particle System", "particle"},
        {"All Files",       "*"}
    };
    SDL_ShowSaveFileDialog(
        saveParticleCallback,
        this,
        manager->getWindow()->getSdlWindow(),
        filters,
        SDL_arraysize(filters),
        (doc.name + ".particle").c_str()
    );
}

void PanelParticle::openFile(const std::string& path)
{
    loadDoc(path);
}

void PanelParticle::addEmitter()
{
    ParticleEmitterDef e;
    e.name = "Emitter " + std::to_string(doc.emitters.size() + 1);
    e.startTime = doc.emitters.empty() ? 0.0f : doc.duration * 0.3f;
    e.endTime   = doc.duration;
    doc.emitters.push_back(e);
    selectedEmitter = (int)doc.emitters.size() - 1;
    doc.dirty = true;
}

void PanelParticle::removeEmitter(int index)
{
    if (index < 0 || index >= (int)doc.emitters.size()) return;
    doc.emitters.erase(doc.emitters.begin() + index);
    if (selectedEmitter >= (int)doc.emitters.size())
        selectedEmitter = (int)doc.emitters.size() - 1;
    doc.dirty = true;
}

void PanelParticle::duplicateEmitter(int index)
{
    if (index < 0 || index >= (int)doc.emitters.size()) return;
    ParticleEmitterDef e = doc.emitters[index];
    e.name += " (Copy)";
    doc.emitters.insert(doc.emitters.begin() + index + 1, e);
    selectedEmitter = index + 1;
    doc.dirty = true;
}

// ---------------------------------------------------------------------------
// Timeline coordinate helpers
// ---------------------------------------------------------------------------

float PanelParticle::timeToScreenX(float time, float originX, float width) const
{
    float visibleDuration = doc.duration / timelineZoom;
    float t = (time - timelineScroll) / visibleDuration;
    return originX + t * width;
}

float PanelParticle::screenXToTime(float sx, float originX, float width) const
{
    float visibleDuration = doc.duration / timelineZoom;
    float t = (sx - originX) / width;
    return timelineScroll + t * visibleDuration;
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void PanelParticle::drawToolbar()
{
    if (ImGui::Button("New"))          { newDoc(); }
    ImGui::SameLine();
    if (ImGui::Button("Save"))         { saveDoc(); }
    ImGui::SameLine();
    if (ImGui::Button("Save As..."))   { saveDocAs(); }
    ImGui::SameLine();

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if (isPlaying)
    {
        if (ImGui::Button("Stop")) isPlaying = false;
    }
    else
    {
        if (ImGui::Button("Play")) isPlaying = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &loopPlayback);
    ImGui::SameLine();

    ImGui::Text("Time: %.2f / %.2f", currentTime, doc.duration);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("Zoom", &timelineZoom, 0.25f, 4.0f, "%.2f");

    ImGui::Separator();

    char nameBuf[256];
    strncpy(nameBuf, doc.name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = 0;
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
    {
        doc.name = nameBuf;
        doc.dirty = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputFloat("Duration", &doc.duration, 0.5f, 1.0f, "%.1f"))
    {
        if (doc.duration < 1.0f) doc.duration = 1.0f;
        doc.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Looping", &doc.looping)) doc.dirty = true;

    ImGui::Separator();
}

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

void PanelParticle::drawTimeline()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float availW = ImGui::GetContentRegionAvail().x;
    int numRows = doc.emitters.empty() ? 1 : (int)doc.emitters.size();
    float totalH = TIMELINE_HEADER_H + (float)numRows * EMITTER_ROW_H + 40;

    // Background
    ImVec2 tlSize(availW, totalH);
    dl->AddRectFilled(cursor, ImVec2(cursor.x + tlSize.x, cursor.y + tlSize.y), IM_COL32(30,30,30,255));

    // --- Time ruler ---
    ImVec2 rulerOrigin = cursor;
    float visibleDuration = doc.duration / timelineZoom;

    dl->AddRectFilled(rulerOrigin, ImVec2(rulerOrigin.x + availW, rulerOrigin.y + TIMELINE_HEADER_H), IM_COL32(45,45,45,255));

    // Draw tick marks
    float tickInterval = 1.0f;
    if (visibleDuration > 20.0f) tickInterval = 5.0f;
    if (visibleDuration > 60.0f) tickInterval = 10.0f;
    if (visibleDuration < 2.0f)  tickInterval = 0.25f;

    float firstTick = std::ceil(timelineScroll / tickInterval) * tickInterval;
    for (float t = firstTick; t <= timelineScroll + visibleDuration; t += tickInterval)
    {
        float sx = timeToScreenX(t, rulerOrigin.x, availW);
        if (sx < rulerOrigin.x + LABEL_W) continue;
        dl->AddLine(ImVec2(sx, rulerOrigin.y), ImVec2(sx, rulerOrigin.y + TIMELINE_HEADER_H),
                    IM_COL32(100,100,100,150));
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", t);
        dl->AddText(ImVec2(sx + 2, rulerOrigin.y + 4), IM_COL32(200,200,200,255), buf);
    }

    // --- Playback cursor ---
    float cursorX = timeToScreenX(currentTime, rulerOrigin.x, availW);
    if (cursorX >= rulerOrigin.x && cursorX <= rulerOrigin.x + availW)
    {
        dl->AddLine(ImVec2(cursorX, rulerOrigin.y), ImVec2(cursorX, cursor.y + totalH),
                    IM_COL32(255, 80, 80, 255), 2.0f);
    }

    // --- Emitter rows ---
    float rowY = rulerOrigin.y + TIMELINE_HEADER_H;
    ImGui::SetCursorScreenPos(ImVec2(rulerOrigin.x, rowY));

    dl->AddRectFilled(ImVec2(rulerOrigin.x, rowY),
                      ImVec2(rulerOrigin.x + LABEL_W, rowY + totalH - TIMELINE_HEADER_H),
                      IM_COL32(38,38,38,255));

    for (int i = 0; i < (int)doc.emitters.size(); ++i)
    {
        ParticleEmitterDef& e = doc.emitters[i];
        ImVec2 rowOrigin(rulerOrigin.x, rowY + (float)i * EMITTER_ROW_H);

        if (i == selectedEmitter)
            dl->AddRectFilled(rowOrigin, ImVec2(rowOrigin.x + availW, rowOrigin.y + EMITTER_ROW_H), IM_COL32(60,60,80,180));

        // Label
        ImGui::SetCursorScreenPos(ImVec2(rowOrigin.x + 4, rowOrigin.y + 4));
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(LABEL_W - 12);
        char nameBuf[128];
        strncpy(nameBuf, e.name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = 0;
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
        {
            e.name = nameBuf;
            doc.dirty = true;
        }

        // Emitter bar on timeline
        float barX1 = timeToScreenX(e.startTime, rulerOrigin.x, availW);
        float barX2 = timeToScreenX(e.endTime, rulerOrigin.x, availW);

        float clipX1 = barX1;
        float clipX2 = barX2;
        if (clipX1 < rulerOrigin.x + LABEL_W) clipX1 = rulerOrigin.x + LABEL_W;
        if (clipX2 > rulerOrigin.x + availW)  clipX2 = rulerOrigin.x + availW;

        if (clipX2 > clipX1)
        {
            ImU32 barCol = (i == selectedEmitter) ? IM_COL32(100, 180, 255, 200) : IM_COL32(70, 130, 200, 180);
            float barH = EMITTER_ROW_H - 8;
            dl->AddRectFilled(ImVec2(clipX1, rowOrigin.y + 4), ImVec2(clipX2, rowOrigin.y + 4 + barH), barCol);

            if (e.forever)
            {
                dl->AddTriangleFilled(
                    ImVec2(clipX2 - 8, rowOrigin.y + 4),
                    ImVec2(clipX2, rowOrigin.y + 4 + barH * 0.5f),
                    ImVec2(clipX2 - 8, rowOrigin.y + 4 + barH),
                    IM_COL32(255,255,255,200));
            }

            char lbl[64];
            snprintf(lbl, sizeof(lbl), "%s (%.1f-%.1f)", e.name.c_str(), e.startTime, e.endTime);
            dl->AddText(ImVec2(clipX1 + 4, rowOrigin.y + 4), IM_COL32(255,255,255,220), lbl);
        }

        // Click to select emitter
        ImGui::SetCursorScreenPos(rowOrigin);
        ImGui::InvisibleButton("##emitterrow", ImVec2(availW, EMITTER_ROW_H));
        if (ImGui::IsItemClicked())
        {
            selectedEmitter = i;
            float mx = ImGui::GetIO().MousePos.x;
            if (mx > rulerOrigin.x + LABEL_W)
                currentTime = screenXToTime(mx, rulerOrigin.x, availW);
        }

        if (ImGui::BeginPopupContextItem("##emitterctx"))
        {
            if (ImGui::MenuItem("Duplicate")) duplicateEmitter(i);
            if (ImGui::MenuItem("Remove"))    removeEmitter(i);
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    // --- Add emitter button ---
    float addY = rowY + (float)doc.emitters.size() * EMITTER_ROW_H;
    ImGui::SetCursorScreenPos(ImVec2(rulerOrigin.x + 4, addY + 4));
    if (ImGui::Button("+ Add Emitter", ImVec2(140, 24)))
        addEmitter();

    // --- Timeline click/drag for playhead ---
    ImGui::SetCursorScreenPos(rulerOrigin);
    ImGui::InvisibleButton("##timelinebg", ImVec2(availW, totalH));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        float mx = ImGui::GetIO().MousePos.x;
        if (mx > rulerOrigin.x + LABEL_W)
        {
            float t = screenXToTime(mx, rulerOrigin.x, availW);
            if (t < 0.0f) t = 0.0f;
            if (t > doc.duration) t = doc.duration;
            currentTime = t;
        }
    }

    // Scroll wheel zoom/scroll
    if (ImGui::IsItemHovered())
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            if (ImGui::GetIO().KeyCtrl)
            {
                timelineZoom += wheel * 0.25f;
                if (timelineZoom < 0.25f) timelineZoom = 0.25f;
                if (timelineZoom > 4.0f) timelineZoom = 4.0f;
            }
            else
            {
                timelineScroll += wheel * visibleDuration * 0.1f;
                if (timelineScroll < 0.0f) timelineScroll = 0.0f;
            }
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(rulerOrigin.x, addY + 40));
}

// ---------------------------------------------------------------------------
// Emitter detail panel
// ---------------------------------------------------------------------------

void PanelParticle::drawEmitterDetail()
{
    if (selectedEmitter < 0 || selectedEmitter >= (int)doc.emitters.size())
    {
        ImGui::TextDisabled("Select an emitter from the timeline to edit its properties.");
        return;
    }

    ParticleEmitterDef& e = doc.emitters[selectedEmitter];

    if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen))
    {
        char nameBuf[128];
        strncpy(nameBuf, e.name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = 0;
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        {
            e.name = nameBuf;
            doc.dirty = true;
        }

        ImGui::SetNextItemWidth(120);
        if (ImGui::InputFloat("Start Time", &e.startTime, 0.1f, 1.0f, "%.2f")) doc.dirty = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputFloat("End Time", &e.endTime, 0.1f, 1.0f, "%.2f")) doc.dirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Forever", &e.forever)) doc.dirty = true;
    }

    if (ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputFloat("Rate (particles/s)", &e.emissionRate, 1.0f, 10.0f, "%.0f")) doc.dirty = true;
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputInt("Max Particles", &e.maxParticles, 10, 100)) doc.dirty = true;
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputFloat("Lifetime (s)", &e.lifetime, 0.1f, 1.0f, "%.2f")) doc.dirty = true;
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputFloat("Lifetime Var", &e.lifetimeVar, 0.1f, 1.0f, "%.2f")) doc.dirty = true;
    }

    if (ImGui::CollapsingHeader("Velocity"))
    {
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputFloat("Speed", &e.speed, 0.1f, 1.0f, "%.2f")) doc.dirty = true;
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputFloat("Speed Var", &e.speedVar, 0.1f, 1.0f, "%.2f")) doc.dirty = true;
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputFloat("Gravity Scale", &e.gravityScale, 0.1f, 1.0f, "%.2f")) doc.dirty = true;
    }

    if (ImGui::CollapsingHeader("Emission Shape"))
    {
        const char* shapes[] = {"Point", "Sphere", "Cone", "Box"};
        int curShape = (int)e.shape;
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Shape", &curShape, shapes, 4))
        {
            e.shape = (ParticleEmitShape)curShape;
            doc.dirty = true;
        }
        ImGui::SetNextItemWidth(300);
        if (ImGui::SliderFloat3("Shape Size", &e.shapeSize[0], 0.1f, 10.0f)) doc.dirty = true;
    }

    if (ImGui::CollapsingHeader("Visual", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::ColorEdit4("Start Color", &e.colorStart[0], ImGuiColorEditFlags_Float);
        if (ImGui::IsItemEdited()) doc.dirty = true;
        ImGui::ColorEdit4("End Color", &e.colorEnd[0], ImGuiColorEditFlags_Float);
        if (ImGui::IsItemEdited()) doc.dirty = true;

        ImGui::SetNextItemWidth(150);
        if (ImGui::SliderFloat("Start Size", &e.sizeStart, 0.01f, 2.0f, "%.2f")) doc.dirty = true;
        ImGui::SetNextItemWidth(150);
        if (ImGui::SliderFloat("End Size", &e.sizeEnd, 0.0f, 2.0f, "%.2f")) doc.dirty = true;

        char spriteBuf[512];
        strncpy(spriteBuf, e.spritePath.c_str(), sizeof(spriteBuf) - 1);
        spriteBuf[sizeof(spriteBuf) - 1] = 0;
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText("Sprite Path", spriteBuf, sizeof(spriteBuf)))
        {
            e.spritePath = spriteBuf;
            doc.dirty = true;
        }

        char meshBuf[256];
        strncpy(meshBuf, e.meshUuid.c_str(), sizeof(meshBuf) - 1);
        meshBuf[sizeof(meshBuf) - 1] = 0;
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText("Mesh UUID", meshBuf, sizeof(meshBuf)))
        {
            e.meshUuid = meshBuf;
            doc.dirty = true;
        }

        char matBuf[256];
        strncpy(matBuf, e.materialUuid.c_str(), sizeof(matBuf) - 1);
        matBuf[sizeof(matBuf) - 1] = 0;
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText("Material UUID", matBuf, sizeof(matBuf)))
        {
            e.materialUuid = matBuf;
            doc.dirty = true;
        }
    }

    if (ImGui::CollapsingHeader("Physics"))
    {
        if (ImGui::Checkbox("Enable Physics", &e.physicsEnabled)) doc.dirty = true;
        ImGui::BeginDisabled(!e.physicsEnabled);
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputFloat("Mass", &e.physicsMass, 0.01f, 0.1f, "%.2f")) doc.dirty = true;
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputFloat("Drag", &e.physicsDrag, 0.01f, 0.1f, "%.2f")) doc.dirty = true;
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Size over Lifetime"))
    {
        auto& curve = e.sizeOverLifetime.keys;
        int delIdx = -1;
        for (int i = 0; i < (int)curve.size(); ++i)
        {
            ImGui::PushID(1000 + i);
            ImGui::SetNextItemWidth(80);
            bool chg = ImGui::SliderFloat("T", &curve[i].time, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            chg |= ImGui::SliderFloat("Value", &curve[i].value, 0.0f, 5.0f, "%.2f");
            ImGui::SameLine();
            if (ImGui::Button("X")) delIdx = i;
            if (chg) doc.dirty = true;
            ImGui::PopID();
        }
        if (delIdx >= 0) { curve.erase(curve.begin() + delIdx); doc.dirty = true; }
        if (ImGui::Button("+ Add Key")) { curve.push_back({0.5f, 1.0f}); doc.dirty = true; }
    }

    if (ImGui::CollapsingHeader("Speed over Lifetime"))
    {
        auto& curve = e.speedOverLifetime.keys;
        int delIdx = -1;
        for (int i = 0; i < (int)curve.size(); ++i)
        {
            ImGui::PushID(2000 + i);
            ImGui::SetNextItemWidth(80);
            bool chg = ImGui::SliderFloat("T", &curve[i].time, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            chg |= ImGui::SliderFloat("Value", &curve[i].value, 0.0f, 5.0f, "%.2f");
            ImGui::SameLine();
            if (ImGui::Button("X")) delIdx = i;
            if (chg) doc.dirty = true;
            ImGui::PopID();
        }
        if (delIdx >= 0) { curve.erase(curve.begin() + delIdx); doc.dirty = true; }
        if (ImGui::Button("+ Add Key")) { curve.push_back({0.5f, 1.0f}); doc.dirty = true; }
    }
}

// ---------------------------------------------------------------------------
// Main draw
// ---------------------------------------------------------------------------

void PanelParticle::draw(bool& isOpened)
{
    if (!isOpened) return;
    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    std::string title = "Particle Editor";
    if (!doc.name.empty()) title += " - " + doc.name;
    if (doc.dirty) title += " *";

    if (!ImGui::Begin(title.c_str(), &isOpened))
    {
        ImGui::End();
        return;
    }

    drawToolbar();
    drawTimeline();

    ImGui::Separator();
    ImGui::Text("Emitter Properties");
    ImGui::Separator();
    drawEmitterDetail();

    ImGui::End();

    if (isPlaying)
    {
        currentTime += ImGui::GetIO().DeltaTime;
        if (currentTime >= doc.duration)
        {
            if (loopPlayback)
                currentTime = 0.0f;
            else
            {
                currentTime = doc.duration;
                isPlaying = false;
            }
        }
    }
}
