#include "panel_animator.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <SDL3/SDL_dialog.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ===========================================================================
// Helpers
// ===========================================================================

static ImU32 toImU32(ImVec4 c)
{
    return IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(c.w * 255));
}

static ImVec2 operator+(ImVec2 a, ImVec2 b) { return { a.x + b.x, a.y + b.y }; }
static ImVec2 operator-(ImVec2 a, ImVec2 b) { return { a.x - b.x, a.y - b.y }; }
static ImVec2 operator*(ImVec2 a, float s)  { return { a.x * s,   a.y * s   }; }

static const char* animVarTypeName(AnimVariableType t)
{
    switch (t)
    {
        case AnimVariableType::Bool:    return "Bool";
        case AnimVariableType::Float:   return "Float";
        case AnimVariableType::Int:     return "Int";
        case AnimVariableType::Trigger: return "Trigger";
    }
    return "Unknown";
}

static AnimVariableType animVarTypeFromName(const std::string& name)
{
    if (name == "Bool")    return AnimVariableType::Bool;
    if (name == "Int")     return AnimVariableType::Int;
    if (name == "Trigger") return AnimVariableType::Trigger;
    return AnimVariableType::Float;
}

static const char* conditionCmpName(AnimCondition::Cmp c)
{
    switch (c)
    {
        case AnimCondition::Greater:      return ">";
        case AnimCondition::Less:         return "<";
        case AnimCondition::Equal:        return "==";
        case AnimCondition::NotEqual:     return "!=";
        case AnimCondition::GreaterEqual: return ">=";
        case AnimCondition::LessEqual:    return "<=";
        case AnimCondition::IsTrue:       return "is true";
        case AnimCondition::IsFalse:      return "is false";
    }
    return "?";
}

// ===========================================================================
// AnimCondition::evaluate
// ===========================================================================

bool AnimCondition::evaluate(const std::unordered_map<std::string, float>& vars) const
{
    auto it = vars.find(variableName);
    float val = (it != vars.end()) ? it->second : 0.0f;

    switch (comparison)
    {
        case Greater:      return val > threshold;
        case Less:         return val < threshold;
        case Equal:        return val == threshold;
        case NotEqual:     return val != threshold;
        case GreaterEqual: return val >= threshold;
        case LessEqual:    return val <= threshold;
        case IsTrue:       return val != 0.0f;
        case IsFalse:      return val == 0.0f;
    }
    return false;
}

// ===========================================================================
// AnimatorGraph implementation
// ===========================================================================

AnimState* AnimatorGraph::findState(int id)
{
    for (auto& s : states)
        if (s.id == id) return &s;
    return nullptr;
}

AnimTransition* AnimatorGraph::findTransition(int id)
{
    for (auto& t : transitions)
        if (t.id == id) return &t;
    return nullptr;
}

void AnimatorGraph::removeState(int stateId)
{
    removeTransitionsForState(stateId);
    states.erase(std::remove_if(states.begin(), states.end(),
        [stateId](const AnimState& s) { return s.id == stateId; }),
        states.end());
}

void AnimatorGraph::removeTransition(int transId)
{
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
        [transId](const AnimTransition& t) { return t.id == transId; }),
        transitions.end());
}

void AnimatorGraph::removeTransitionsForState(int stateId)
{
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
        [stateId](const AnimTransition& t) { return t.fromStateId == stateId || t.toStateId == stateId; }),
        transitions.end());
}

nlohmann::json AnimatorGraph::toJson() const
{
    json j;

    j["uuid"] = uuid;
    j["name"] = name;
    j["nextNodeId"] = nextNodeId;
    j["nextLinkId"] = nextLinkId;

    // Clips
    json clipsArr = json::array();
    for (const auto& [clipUuid, clip] : clips)
    {
        json c;
        c["uuid"] = clip.uuid;
        c["name"] = clip.name;
        clipsArr.push_back(c);
    }
    j["clips"] = clipsArr;

    // Variables
    json varsArr = json::array();
    for (const auto& v : variables)
    {
        json vj;
        vj["name"] = v.name;
        vj["type"] = animVarTypeName(v.type);
        vj["defaultValue"] = v.defaultValue;
        varsArr.push_back(vj);
    }
    j["variables"] = varsArr;

    // States
    json statesArr = json::array();
    for (const auto& s : states)
    {
        json sj;
        sj["id"]        = s.id;
        sj["name"]      = s.name;
        sj["animationUuid"] = s.animationUuid;
        sj["speed"]     = s.speed;
        sj["loop"]      = s.loop;
        sj["isDefault"] = s.isDefault;
        sj["posX"]      = s.posX;
        sj["posY"]      = s.posY;
        statesArr.push_back(sj);
    }
    j["states"] = statesArr;

    // Transitions
    json transArr = json::array();
    for (const auto& t : transitions)
    {
        json tj;
        tj["id"]          = t.id;
        tj["fromStateId"] = t.fromStateId;
        tj["toStateId"]   = t.toStateId;
        tj["hasExitTime"] = t.hasExitTime;
        tj["exitTime"]    = t.exitTime;

        json condsArr = json::array();
        for (const auto& c : t.conditions)
        {
            json cj;
            cj["variableName"] = c.variableName;
            cj["comparison"]   = (int)c.comparison;
            cj["threshold"]    = c.threshold;
            condsArr.push_back(cj);
        }
        tj["conditions"] = condsArr;
        transArr.push_back(tj);
    }
    j["transitions"] = transArr;

    return j;
}

void AnimatorGraph::fromJson(const nlohmann::json& j)
{
    uuid = j.value("uuid", std::string());
    name = j.value("name", std::string("NewAnimator"));
    nextNodeId = j.value("nextNodeId", 1);
    nextLinkId = j.value("nextLinkId", 1);

    clips.clear();
    if (j.contains("clips"))
    {
        for (const auto& c : j["clips"])
        {
            AnimClipRef ref;
            ref.uuid = c.value("uuid", std::string());
            ref.name = c.value("name", std::string());
            clips[ref.uuid] = ref;
        }
    }

    variables.clear();
    if (j.contains("variables"))
    {
        for (const auto& v : j["variables"])
        {
            AnimVariable var;
            var.name = v.value("name", std::string());
            var.type = animVarTypeFromName(v.value("type", std::string("Float")));
            var.defaultValue = v.value("defaultValue", 0.0f);
            variables.push_back(var);
        }
    }

    states.clear();
    if (j.contains("states"))
    {
        for (const auto& s : j["states"])
        {
            AnimState st;
            st.id            = s.value("id", -1);
            st.name          = s.value("name", std::string("State"));
            st.animationUuid = s.value("animationUuid", std::string());
            st.speed         = s.value("speed", 1.0f);
            st.loop          = s.value("loop", true);
            st.isDefault     = s.value("isDefault", false);
            st.posX          = s.value("posX", 100.0f);
            st.posY          = s.value("posY", 100.0f);
            states.push_back(st);
        }
    }

    transitions.clear();
    if (j.contains("transitions"))
    {
        for (const auto& t : j["transitions"])
        {
            AnimTransition tr;
            tr.id          = t.value("id", -1);
            tr.fromStateId = t.value("fromStateId", -1);
            tr.toStateId   = t.value("toStateId", -1);
            tr.hasExitTime = t.value("hasExitTime", false);
            tr.exitTime    = t.value("exitTime", 0.0f);

            if (t.contains("conditions"))
            {
                for (const auto& c : t["conditions"])
                {
                    AnimCondition cond;
                    cond.variableName = c.value("variableName", std::string());
                    cond.comparison   = (AnimCondition::Cmp)c.value("comparison", (int)AnimCondition::Greater);
                    cond.threshold    = c.value("threshold", 0.0f);
                    tr.conditions.push_back(cond);
                }
            }
            transitions.push_back(tr);
        }
    }

    dirty = false;
}

// ===========================================================================
// Construction / file helpers
// ===========================================================================

PanelAnimator::PanelAnimator(kGuiManager* setGui, Manager* setManager)
    : gui(setGui), manager(setManager)
{
    newGraph();
}

std::string PanelAnimator::generateUuid()
{
    using namespace std::chrono;
    auto seed = (uint64_t)duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> dist;
    auto r1 = dist(rng), r2 = dist(rng);
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)r1, (unsigned long long)r2);
    return std::string(buf);
}

void PanelAnimator::newGraph()
{
    graph       = AnimatorGraph{};
    graph.uuid  = generateUuid();
    graph.name  = "NewAnimator";
    graph.dirty = false;
    filePath.clear();
    selectedState   = -1;
    editingVarIndex = -1;
    showClipManager = false;
    showVarEditor   = false;

    // Add a default entry state
    AnimState entry;
    entry.id        = graph.newNodeId();
    entry.name      = "Entry";
    entry.isDefault = true;
    entry.posX      = 300.f;
    entry.posY      = 200.f;
    graph.states.push_back(entry);
}

void PanelAnimator::openFile(const std::string& path)
{
    loadGraph(path);
}

void PanelAnimator::loadGraph(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    try
    {
        json j; f >> j;
        graph.fromJson(j);
        filePath = path;
        graph.dirty = false;
        selectedState   = -1;
        editingVarIndex = -1;
    }
    catch (...) {}
}

void PanelAnimator::saveGraph()
{
    if (filePath.empty()) { saveGraphAs(); return; }

    json j = graph.toJson();
    std::ofstream f(filePath);
    if (!f.is_open()) return;
    f << j.dump(4);
    graph.dirty = false;
}

void SDLCALL PanelAnimator::saveAnimatorCallback(void* userdata,
                                                  const char* const* filelist,
                                                  int /*filter*/)
{
    if (!filelist || !*filelist) return;
    PanelAnimator* self = static_cast<PanelAnimator*>(userdata);

    std::string path = filelist[0];
    if (path.size() < 9 || path.substr(path.size() - 9) != ".animator")
        path += ".animator";

    self->filePath = path;
    self->saveGraph();
}

void PanelAnimator::saveGraphAs()
{
    if (!manager->projectOpened) return;

    fs::path assetsDir = fs::path(manager->projectPath.c_str()) / "Assets" / "Animations";
    fs::create_directories(assetsDir);

    std::string defaultName = (graph.name.empty() ? "NewAnimator" : graph.name) + ".animator";

    SDL_DialogFileFilter filters[] = {
        { "Animator files", "animator" },
        { "All files",      "*"        }
    };

    SDL_ShowSaveFileDialog(
        saveAnimatorCallback,
        this,
        manager->getWindow()->getSdlWindow(),
        filters,
        SDL_arraysize(filters),
        (assetsDir / defaultName).string().c_str()
    );
}

// ===========================================================================
// Coordinate helpers
// ===========================================================================

ImVec2 PanelAnimator::canvasToScreen(ImVec2 cp, ImVec2 origin) const
{
    return origin + (cp + canvasOffset) * canvasZoom;
}

ImVec2 PanelAnimator::screenToCanvas(ImVec2 sp, ImVec2 origin) const
{
    return (sp - origin) * (1.f / canvasZoom) - canvasOffset;
}

ImVec2 PanelAnimator::getInputPinPos(const AnimState& state, ImVec2 origin) const
{
    // Input pin: left side, vertically centered on the node body (below header)
    float zoom = canvasZoom;
    float hdrH = NODE_HEADER_H * zoom;
    float bodyH = (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f) * zoom;
    ImVec2 tl = canvasToScreen({ state.posX, state.posY }, origin);
    return { tl.x, tl.y + hdrH + bodyH * 0.5f };
}

ImVec2 PanelAnimator::getOutputPinPos(const AnimState& state, ImVec2 origin) const
{
    // Output pin: right side, vertically centered on the node body (below header)
    float zoom = canvasZoom;
    float nw   = NODE_WIDTH * zoom;
    float hdrH = NODE_HEADER_H * zoom;
    float bodyH = (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f) * zoom;
    ImVec2 tl = canvasToScreen({ state.posX, state.posY }, origin);
    return { tl.x + nw, tl.y + hdrH + bodyH * 0.5f };
}

int PanelAnimator::hitTestInputPins(ImVec2 mouse, ImVec2 origin) const
{
    for (const auto& s : graph.states)
    {
        // Skip the default entry state for input pin test (it has no input)
        if (s.isDefault) continue;
        ImVec2 p = getInputPinPos(s, origin);
        float dx = mouse.x - p.x, dy = mouse.y - p.y;
        float r = PIN_RADIUS * canvasZoom * 2.5f;
        if (dx * dx + dy * dy <= r * r)
            return s.id;
    }
    return -1;
}

int PanelAnimator::hitTestOutputPins(ImVec2 mouse, ImVec2 origin) const
{
    for (const auto& s : graph.states)
    {
        ImVec2 p = getOutputPinPos(s, origin);
        float dx = mouse.x - p.x, dy = mouse.y - p.y;
        float r = PIN_RADIUS * canvasZoom * 2.5f;
        if (dx * dx + dy * dy <= r * r)
            return s.id;
    }
    return -1;
}

// ===========================================================================
// Draw helpers
// ===========================================================================

void PanelAnimator::drawNode(ImDrawList* dl, AnimState& state, ImVec2 origin)
{
    const float zoom     = canvasZoom;
    const float nw       = NODE_WIDTH * zoom;
    const float hdrH     = NODE_HEADER_H * zoom;
    const float pinR     = PIN_RADIUS * zoom;
    const float fontSize = ImGui::GetFontSize();

    float bodyH = (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f) * zoom;
    float totalH = hdrH + bodyH;

    ImVec2 topLeft = canvasToScreen({ state.posX, state.posY }, origin);
    ImVec2 botRight = topLeft + ImVec2(nw, totalH);

    bool isSelected = (state.id == selectedState);

    // Default state indicator: a slightly different header color
    ImVec4 hdrCol = state.isDefault
        ? ImVec4(0.15f, 0.55f, 0.15f, 1.f) // Green for default/entry
        : ImVec4(0.60f, 0.25f, 0.10f, 1.f); // Orange-red for normal states

    // Shadow
    dl->AddRectFilled({ topLeft.x + 3, topLeft.y + 3 }, { botRight.x + 3, botRight.y + 3 },
                      IM_COL32(0, 0, 0, 80), 6.f * zoom);

    // Body
    dl->AddRectFilled(topLeft, botRight, IM_COL32(45, 45, 45, 230), 6.f * zoom);

    // Header
    dl->AddRectFilled(topLeft, { botRight.x, topLeft.y + hdrH },
                      toImU32(hdrCol), 6.f * zoom);
    // Flatten header bottom corners
    dl->AddRectFilled({ topLeft.x, topLeft.y + hdrH - 4.f * zoom },
                      { botRight.x, topLeft.y + hdrH },
                      toImU32(hdrCol), 0.f);

    // Outline
    ImU32 outlineCol = isSelected ? IM_COL32(255, 200, 50, 255) : IM_COL32(100, 100, 100, 180);
    dl->AddRect(topLeft, botRight, outlineCol, 6.f * zoom, 0, isSelected ? 2.f : 1.f);

    // Title
    ImVec2 titlePos = topLeft + ImVec2(6.f * zoom, (hdrH - fontSize) * 0.5f);
    dl->AddText(titlePos, IM_COL32(255, 255, 255, 255), state.name.c_str());

    // Body content: show animation name if assigned
    float bodyY = topLeft.y + hdrH + 4.f * zoom;
    if (!state.animationUuid.empty())
    {
        auto it = graph.clips.find(state.animationUuid);
        std::string clipName = (it != graph.clips.end()) ? it->second.name : state.animationUuid.substr(0, 8) + "...";
        std::string label = "Anim: " + clipName;
        dl->AddText({ topLeft.x + 6.f * zoom, bodyY }, IM_COL32(200, 200, 200, 255), label.c_str());
        bodyY += fontSize + 2.f * zoom;

        // Show speed & loop
        char speedBuf[32];
        snprintf(speedBuf, sizeof(speedBuf), "Speed: %.2f  %s", state.speed, state.loop ? "[Loop]" : "[Once]");
        dl->AddText({ topLeft.x + 6.f * zoom, bodyY }, IM_COL32(160, 160, 160, 255), speedBuf);
    }
    else
    {
        dl->AddText({ topLeft.x + 6.f * zoom, bodyY }, IM_COL32(140, 140, 140, 255), "No animation");
    }

    // Input pin (left) — hidden on the default entry state
    if (!state.isDefault)
    {
        ImVec2 inPos = getInputPinPos(state, origin);
        dl->AddCircleFilled(inPos, pinR, IM_COL32(100, 180, 255, 255));
        dl->AddCircle(inPos, pinR, IM_COL32(200, 200, 200, 180), 0, 1.5f);
    }

    // Output pin (right)
    {
        ImVec2 outPos = getOutputPinPos(state, origin);
        dl->AddCircleFilled(outPos, pinR, IM_COL32(255, 180, 80, 255));
        dl->AddCircle(outPos, pinR, IM_COL32(200, 200, 200, 180), 0, 1.5f);
    }
}

void PanelAnimator::drawLinks(ImDrawList* dl, ImVec2 origin)
{
    const float zoom     = canvasZoom;
    const float fontSize = ImGui::GetFontSize();

    for (const auto& trans : graph.transitions)
    {
        AnimState* from = graph.findState(trans.fromStateId);
        AnimState* to   = graph.findState(trans.toStateId);
        if (!from || !to) continue;

        ImVec2 p0 = getOutputPinPos(*from, origin);
        ImVec2 p3 = getInputPinPos(*to, origin);

        float cx = (p3.x - p0.x) * 0.5f;
        ImVec2 p1 = { p0.x + cx, p0.y };
        ImVec2 p2 = { p3.x - cx, p3.y };

        // Build condition text for tooltip
        std::string condStr;
        for (size_t i = 0; i < trans.conditions.size(); ++i)
        {
            if (i > 0) condStr += " AND ";
            condStr += trans.conditions[i].variableName + " " +
                       conditionCmpName(trans.conditions[i].comparison);
            if (trans.conditions[i].comparison != AnimCondition::IsTrue &&
                trans.conditions[i].comparison != AnimCondition::IsFalse)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), " %.2f", trans.conditions[i].threshold);
                condStr += buf;
            }
        }
        if (condStr.empty())
            condStr = "(no condition)";

        ImU32 col = IM_COL32(180, 220, 255, 220);
        dl->AddBezierCubic(p0, p1, p2, p3, col, 2.f * zoom);

        // Tooltip on hover (check a point near the center of the bezier)
        ImVec2 mid = { (p0.x + p3.x) * 0.5f, (p0.y + p3.y) * 0.5f };
        ImVec2 mouse = ImGui::GetIO().MousePos;
        float dx = mouse.x - mid.x, dy = mouse.y - mid.y;
        if (dx * dx + dy * dy < 20.f * 20.f)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(condStr.c_str());
            if (trans.hasExitTime)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "Exit time: %.2fs", trans.exitTime);
                ImGui::TextUnformatted(buf);
            }
            ImGui::EndTooltip();
        }

        // Draw condition label near the middle of the link
        if (zoom >= 0.7f && !condStr.empty())
        {
            ImVec2 labelPos = { (p0.x + p3.x) * 0.5f - ImGui::CalcTextSize(condStr.c_str()).x * 0.5f,
                                (p0.y + p3.y) * 0.5f - fontSize * 0.5f - 12.f * zoom };
            dl->AddText(labelPos, IM_COL32(255, 255, 200, 200), condStr.c_str());
        }
    }
}

void PanelAnimator::drawDragLink(ImDrawList* dl)
{
    if (!isDraggingLink) return;

    const float zoom = canvasZoom;

    AnimState* from = graph.findState(dragFromState);
    if (!from) return;

    // Need an origin to compute the output pin screen position.
    // The best we have is the canvas origin from the last drawCanvas call,
    // which we don't store — so estimate it from the ImGui window.
    // The canvas is drawn in an InvisibleButton; we use the window's cursor
    // start position as the origin approximation.
    ImVec2 origin = ImGui::GetCursorScreenPos() - ImGui::GetContentRegionAvail();

    ImVec2 p0 = getOutputPinPos(*from, origin);
    ImVec2 p3 = ImGui::GetIO().MousePos;
    float  cx = (p3.x - p0.x) * 0.5f;
    ImVec2 p1 = { p0.x + cx, p0.y };
    ImVec2 p2 = { p3.x - cx, p3.y };
    dl->AddBezierCubic(p0, p1, p2, p3, IM_COL32(180, 220, 255, 180), 2.f * zoom);
    dl->AddCircleFilled(p3, PIN_RADIUS * zoom, IM_COL32(255, 255, 255, 180));
}

// ===========================================================================
// Context menu (right-click on state)
// ===========================================================================

void PanelAnimator::drawStateContextMenu()
{
    // Right-click on canvas background → add new state
    if (ImGui::BeginPopup("##AnimStateAddMenu"))
    {
        if (ImGui::MenuItem("Add State"))
        {
            AnimState st;
            st.id   = graph.newNodeId();
            st.name = "New State";
            st.posX = contextMenuPos.x;
            st.posY = contextMenuPos.y;
            graph.states.push_back(st);
            graph.dirty = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Add Animation Clip Reference..."))
        {
            promptAddClip();
        }
        ImGui::EndPopup();
    }

    // Right-click on a specific state
    if (ImGui::BeginPopup("##AnimStateCtxMenu"))
    {
        AnimState* state = graph.findState(contextMenuStateId);
        if (state)
        {
            ImGui::TextUnformatted(state->name.c_str());
            ImGui::Separator();

            // Edit state name
            static char nameBuf[128];
            strncpy_s(nameBuf, state->name.c_str(), sizeof(nameBuf));
            ImGui::SetNextItemWidth(150.f);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            {
                state->name = nameBuf;
                graph.dirty = true;
            }

            // Assign animation clip
            if (!graph.clips.empty())
            {
                ImGui::Separator();
                int currentClip = -1;
                std::vector<const char*> clipNames;
                std::vector<std::string> clipUuids;
                clipNames.push_back("(none)");
                clipUuids.push_back("");
                int idx = 0;
                for (const auto& [cid, clip] : graph.clips)
                {
                    if (cid == state->animationUuid)
                        currentClip = idx + 1;
                    clipNames.push_back(clip.name.c_str());
                    clipUuids.push_back(cid);
                    ++idx;
                }
                if (currentClip < 0) currentClip = 0;

                ImGui::SetNextItemWidth(150.f);
                if (ImGui::Combo("Animation", &currentClip, clipNames.data(), (int)clipNames.size()))
                {
                    state->animationUuid = clipUuids[currentClip];
                    graph.dirty = true;
                }
            }

            // Speed
            ImGui::SetNextItemWidth(100.f);
            if (ImGui::DragFloat("Speed", &state->speed, 0.05f, 0.0f, 10.0f))
                graph.dirty = true;

            // Loop
            if (ImGui::Checkbox("Loop", &state->loop))
                graph.dirty = true;

            // Set as default
            if (!state->isDefault)
            {
                if (ImGui::Button("Set as Default"))
                {
                    for (auto& s : graph.states) s.isDefault = false;
                    state->isDefault = true;
                    graph.dirty = true;
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Delete State"))
            {
                graph.removeState(state->id);
                graph.dirty = true;
                selectedState = -1;
                contextMenuStateId = -1;
            }
        }
        ImGui::EndPopup();
    }
}

// ===========================================================================
// Variable editor panel
// ===========================================================================

void PanelAnimator::drawVariableEditor()
{
    if (!showVarEditor) return;

    ImGui::SetNextWindowSize({ 400, 300 }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Animator Variables", &showVarEditor))
    {
        // Add new variable
        if (ImGui::Button("Add Variable"))
        {
            AnimVariable var;
            var.name = "NewVar";
            // Find a unique name
            int counter = 1;
            bool unique = false;
            while (!unique)
            {
                unique = true;
                for (const auto& v : graph.variables)
                {
                    if (v.name == var.name) { unique = false; break; }
                }
                if (!unique) var.name = "NewVar" + std::to_string(counter++);
            }
            graph.variables.push_back(var);
            editingVarIndex = (int)graph.variables.size() - 1;
            graph.dirty = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Close"))
            showVarEditor = false;

        ImGui::Separator();

        if (graph.variables.empty())
        {
            ImGui::TextDisabled("No variables defined. Click 'Add Variable' to create one.");
        }
        else
        {
            ImGui::Columns(4, "VarColumns");
            ImGui::Text("Name"); ImGui::NextColumn();
            ImGui::Text("Type"); ImGui::NextColumn();
            ImGui::Text("Default"); ImGui::NextColumn();
            ImGui::Text(""); ImGui::NextColumn();
            ImGui::Separator();

            int removeIdx = -1;
            for (int i = 0; i < (int)graph.variables.size(); ++i)
            {
                auto& var = graph.variables[i];

                // Name
                char nameBuf[128];
                strncpy_s(nameBuf, var.name.c_str(), sizeof(nameBuf));
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
                {
                    var.name = nameBuf;
                    graph.dirty = true;
                }
                ImGui::NextColumn();

                // Type combo
                const char* types[] = { "Bool", "Float", "Int", "Trigger" };
                int typeIdx = (int)var.type;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##type", &typeIdx, types, IM_ARRAYSIZE(types)))
                {
                    var.type = (AnimVariableType)typeIdx;
                    graph.dirty = true;
                }
                ImGui::NextColumn();

                // Default value
                ImGui::SetNextItemWidth(-1);
                if (var.type == AnimVariableType::Bool)
                {
                    bool bval = (var.defaultValue != 0.0f);
                    if (ImGui::Checkbox("##def", &bval))
                    {
                        var.defaultValue = bval ? 1.0f : 0.0f;
                        graph.dirty = true;
                    }
                }
                else if (var.type == AnimVariableType::Int)
                {
                    int ival = (int)var.defaultValue;
                    if (ImGui::DragInt("##def", &ival, 1.0f))
                    {
                        var.defaultValue = (float)ival;
                        graph.dirty = true;
                    }
                }
                else
                {
                    if (ImGui::DragFloat("##def", &var.defaultValue, 0.1f))
                        graph.dirty = true;
                }
                ImGui::NextColumn();

                // Remove button
                if (ImGui::Button("X"))
                    removeIdx = i;
                ImGui::NextColumn();

                ImGui::PopID();
            }

            if (removeIdx >= 0)
            {
                graph.variables.erase(graph.variables.begin() + removeIdx);
                if (editingVarIndex == removeIdx) editingVarIndex = -1;
                else if (editingVarIndex > removeIdx) editingVarIndex--;
                graph.dirty = true;
            }

            ImGui::Columns(1);
        }
    }
    ImGui::End();
}

// ===========================================================================
// Clip manager
// ===========================================================================

void PanelAnimator::drawClipManager()
{
    if (!showClipManager) return;

    ImGui::SetNextWindowSize({ 400, 300 }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Animation Clip Manager", &showClipManager))
    {
        if (ImGui::Button("Add Clip Reference"))
            promptAddClip();

        ImGui::SameLine();
        if (ImGui::Button("Close"))
            showClipManager = false;

        ImGui::Separator();

        if (graph.clips.empty())
        {
            ImGui::TextDisabled("No animation clips referenced. Add an .animation asset.");
        }
        else
        {
            std::string removeUuid;
            for (auto& [uuid, clip] : graph.clips)
            {
                ImGui::PushID(uuid.c_str());
                ImGui::TextUnformatted(clip.name.c_str());
                ImGui::SameLine(200);
                ImGui::TextDisabled("%s", uuid.c_str());

                // Check if any state uses this clip
                bool inUse = false;
                for (const auto& s : graph.states)
                    if (s.animationUuid == uuid) { inUse = true; break; }

                if (inUse)
                    ImGui::TextColored({0.5f, 1.f, 0.5f, 1.f}, " (in use)");
                else
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove"))
                        removeUuid = uuid;
                }
                ImGui::PopID();
            }

            if (!removeUuid.empty())
            {
                graph.clips.erase(removeUuid);
                // Clear references in states
                for (auto& s : graph.states)
                    if (s.animationUuid == removeUuid) s.animationUuid.clear();
                graph.dirty = true;
            }
        }
    }
    ImGui::End();
}

// ===========================================================================
// Toolbar
// ===========================================================================

void PanelAnimator::drawToolbar()
{
    bool hasProject = manager->projectOpened;

    if (ImGui::Button("New"))
    {
        // TODO: prompt save if dirty
        newGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") && hasProject)
        saveGraph();
    ImGui::SameLine();
    if (ImGui::Button("Save As...") && hasProject)
        saveGraphAs();

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Variables button
    if (ImGui::Button("Variables"))
        showVarEditor = !showVarEditor;

    ImGui::SameLine();
    if (ImGui::Button("Clip Manager"))
        showClipManager = !showClipManager;

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Animator name
    char nameBuf[128];
    strncpy_s(nameBuf, graph.name.c_str(), sizeof(nameBuf));
    ImGui::SetNextItemWidth(160.f);
    if (ImGui::InputText("##AnimName", nameBuf, sizeof(nameBuf)))
    {
        graph.name  = nameBuf;
        graph.dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Animator name");

    // Zoom controls
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.f);
    ImGui::SliderFloat("Zoom", &canvasZoom, 0.25f, 2.f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Reset View"))
    {
        canvasZoom   = 1.f;
        canvasOffset = { 0.f, 0.f };
    }
}

// ===========================================================================
// Canvas
// ===========================================================================

void PanelAnimator::drawCanvas()
{
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 10 || canvasSize.y < 10) return;

    ImVec2 canvasTL = ImGui::GetCursorScreenPos();

    // Invisible button captures input
    ImGui::InvisibleButton("##animCanvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft  |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);

    bool canvasHovered = ImGui::IsItemHovered();
    bool canvasActive  = ImGui::IsItemActive();
    ImVec2 mouse       = ImGui::GetIO().MousePos;
    ImVec2 mouseDelta  = ImGui::GetIO().MouseDelta;
    ImGuiIO& io        = ImGui::GetIO();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasTL, canvasTL + canvasSize, true);

    // --- Background ---
    dl->AddRectFilled(canvasTL, canvasTL + canvasSize, IM_COL32(28, 28, 28, 255));

    // Grid
    {
        float gridStep = 32.f * canvasZoom;
        ImU32 gridColMinor = IM_COL32(50, 50, 50, 255);
        ImU32 gridColMajor = IM_COL32(65, 65, 65, 255);

        float offX = fmodf(canvasOffset.x * canvasZoom, gridStep);
        float offY = fmodf(canvasOffset.y * canvasZoom, gridStep);

        if (offX < 0) offX += gridStep;
        if (offY < 0) offY += gridStep;

        int mx = (int)(canvasSize.x / gridStep) + 2;
        int my = (int)(canvasSize.y / gridStep) + 2;
        for (int i = 0; i <= mx; ++i)
        {
            float x = canvasTL.x + offX + i * gridStep;
            bool major = (i % 4 == 0);
            dl->AddLine({ x, canvasTL.y }, { x, canvasTL.y + canvasSize.y },
                        major ? gridColMajor : gridColMinor);
        }
        for (int i = 0; i <= my; ++i)
        {
            float y = canvasTL.y + offY + i * gridStep;
            bool major = (i % 4 == 0);
            dl->AddLine({ canvasTL.x, y }, { canvasTL.x + canvasSize.x, y },
                        major ? gridColMajor : gridColMinor);
        }
    }

    // --- Pan (middle mouse or alt + left) ---
    if (canvasHovered || isPanning)
    {
        bool panButton = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                         (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyAlt);

        if (panButton && !isDraggingState && !isDraggingLink)
        {
            if (!isPanning)
            {
                isPanning      = true;
                panStartMouse  = mouse;
                panStartOffset = canvasOffset;
            }
            canvasOffset.x = panStartOffset.x + (mouse.x - panStartMouse.x) / canvasZoom;
            canvasOffset.y = panStartOffset.y + (mouse.y - panStartMouse.y) / canvasZoom;
        }
        else { isPanning = false; }
    }

    // Scroll to zoom
    if (canvasHovered && io.MouseWheel != 0.f)
    {
        float prevZoom = canvasZoom;
        canvasZoom = ImClamp(canvasZoom + io.MouseWheel * 0.1f, 0.25f, 2.f);
        ImVec2 mouseCanvas = screenToCanvas(mouse, canvasTL);
        canvasOffset.x += mouseCanvas.x * (1.f / prevZoom - 1.f / canvasZoom);
        canvasOffset.y += mouseCanvas.y * (1.f / prevZoom - 1.f / canvasZoom);
    }

    // --- Draw links ---
    drawLinks(dl, canvasTL);
    drawDragLink(dl);

    // --- Draw nodes ---
    for (auto& state : graph.states)
        drawNode(dl, state, canvasTL);

    // --- Interaction ---

    // Detect pin hover and start/finish connection drag
    int hovOutPin = hitTestOutputPins(mouse, canvasTL);
    int hovInPin  = hitTestInputPins(mouse, canvasTL);

    // Click on pin to start link drag
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
    {
        if (hovOutPin >= 0)
        {
            // Start dragging a link from this output pin
            isDraggingLink = true;
            dragFromState  = hovOutPin;
        }
        else if (hovInPin >= 0)
        {
            // Starting from input pin (reverse direction) — start drag too
            isDraggingLink = true;
            dragFromState  = hovInPin;
        }
        else
        {
            // Check if we hit a state header → start move
            bool hitState = false;
            // Iterate in reverse so top nodes (drawn last) are picked first
            for (int i = (int)graph.states.size() - 1; i >= 0; --i)
            {
                auto& state = graph.states[i];
                ImVec2 nTopLeft = canvasToScreen({ state.posX, state.posY }, canvasTL);
                float nw = NODE_WIDTH * canvasZoom;
                float hdrH = NODE_HEADER_H * canvasZoom;
                float bodyH = (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f) * canvasZoom;

                if (mouse.x >= nTopLeft.x && mouse.x <= nTopLeft.x + nw &&
                    mouse.y >= nTopLeft.y && mouse.y <= nTopLeft.y + hdrH + bodyH)
                {
                    selectedState    = state.id;
                    isDraggingState  = true;
                    dragStateOffset  = screenToCanvas(mouse, canvasTL) - ImVec2(state.posX, state.posY);
                    hitState = true;
                    break;
                }
            }
            if (!hitState) selectedState = -1;
        }
    }

    // Move selected state
    if (isDraggingState && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt)
    {
        AnimState* st = graph.findState(selectedState);
        if (st)
        {
            // Don't move the default entry state
            if (!st->isDefault)
            {
                ImVec2 cp = screenToCanvas(mouse, canvasTL) - dragStateOffset;
                st->posX = cp.x;
                st->posY = cp.y;
                graph.dirty = true;
            }
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDraggingState = false;

        if (isDraggingLink)
        {
            isDraggingLink = false;

            // Complete the link if we released on a valid pin
            int targetOut = hitTestOutputPins(mouse, canvasTL);
            int targetIn  = hitTestInputPins(mouse, canvasTL);

            int fromState = dragFromState;
            int toState   = -1;

            // Determine direction: if we started from an output, connect to an input
            // Check if dragFromState output pin was clicked
            AnimState* dragFrom = graph.findState(dragFromState);
            if (dragFrom)
            {
                ImVec2 outPos = getOutputPinPos(*dragFrom, canvasTL);
                float dOut = (mouse.x - outPos.x) * (mouse.x - outPos.x) + (mouse.y - outPos.y) * (mouse.y - outPos.y);
                float r = PIN_RADIUS * canvasZoom * 3.f;
                bool startedFromOutput = (dOut <= r * r);

                if (startedFromOutput && targetIn >= 0 && targetIn != dragFromState)
                {
                    fromState = dragFromState;
                    toState   = targetIn;
                }
                else if (!startedFromOutput && targetOut >= 0 && targetOut != dragFromState)
                {
                    // Started from input, connecting to an output (reverse direction)
                    fromState = targetOut;
                    toState   = dragFromState;
                }
            }

            if (toState >= 0 && fromState != toState)
            {
                // Check for duplicate transition
                bool exists = false;
                for (const auto& t : graph.transitions)
                {
                    if (t.fromStateId == fromState && t.toStateId == toState)
                    { exists = true; break; }
                }

                if (!exists)
                {
                    // Don't allow transitions TO the default entry state or FROM states that aren't the entry
                    AnimState* toSt = graph.findState(toState);
                    if (toSt && !toSt->isDefault)
                    {
                        AnimTransition trans;
                        trans.id          = graph.newLinkId();
                        trans.fromStateId = fromState;
                        trans.toStateId   = toState;
                        graph.transitions.push_back(trans);
                        graph.dirty = true;
                    }
                }
            }
        }
    }

    // Right-click on canvas → context menu
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !isDraggingLink)
    {
        // Check if we right-clicked on a state
        bool hitState = false;
        for (int i = (int)graph.states.size() - 1; i >= 0; --i)
        {
            auto& state = graph.states[i];
            ImVec2 nTL = canvasToScreen({ state.posX, state.posY }, canvasTL);
            float nw = NODE_WIDTH * canvasZoom;
            float bodyH = (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f) * canvasZoom;
            float totalH = NODE_HEADER_H * canvasZoom + bodyH;

            if (mouse.x >= nTL.x && mouse.x <= nTL.x + nw &&
                mouse.y >= nTL.y && mouse.y <= nTL.y + totalH)
            {
                contextMenuStateId = state.id;
                selectedState      = state.id;
                ImGui::OpenPopup("##AnimStateCtxMenu");
                hitState = true;
                break;
            }
        }
        if (!hitState)
        {
            contextMenuPos = screenToCanvas(mouse, canvasTL);
            ImGui::OpenPopup("##AnimStateAddMenu");
        }
    }

    // Delete key
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedState >= 0)
    {
        graph.removeState(selectedState);
        graph.dirty  = true;
        selectedState = -1;
    }

    dl->PopClipRect();
}

// ===========================================================================
// Prompt add clip — opens an SDL file dialog to select an .animation file
// ===========================================================================

void PanelAnimator::promptAddClip()
{
    if (!manager->projectOpened) return;

    fs::path animDir = fs::path(manager->projectPath.c_str()) / "Assets" / "Animations";
    if (!fs::exists(animDir))
        fs::create_directories(animDir);

    // We'll use a simple approach: scan the Animations folder for .animation files
    // and show them in a selectable list
    std::vector<fs::path> animFiles;
    if (fs::exists(animDir))
    {
        for (const auto& entry : fs::directory_iterator(animDir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".animation")
                animFiles.push_back(entry.path());
        }
    }

    if (animFiles.empty())
    {
        // No .animation files found — prompt the user to create one
        ImGui::OpenPopup("##NoAnimFiles");
        return;
    }

    // For simplicity, we add all found .animation files as clip references
    for (const auto& p : animFiles)
    {
        // Read the .animation file to get its UUID and name
        try
        {
            std::ifstream f(p);
            if (f.is_open())
            {
                json j; f >> j;
                std::string clipUuid = j.value("uuid", std::string());
                std::string clipName = j.value("name", p.stem().string());
                if (!clipUuid.empty() && graph.clips.find(clipUuid) == graph.clips.end())
                {
                    AnimClipRef ref;
                    ref.uuid = clipUuid;
                    ref.name = clipName;
                    graph.clips[clipUuid] = ref;
                    graph.dirty = true;
                }
            }
        }
        catch (...) {}
    }
}

// ===========================================================================
// Main draw
// ===========================================================================

void PanelAnimator::draw(bool& isOpened)
{
    if (!isOpened) return;

    ImGui::SetNextWindowSize({ 1000, 700 }, ImGuiCond_FirstUseEver);

    kString title = graph.dirty ? "Animator *" : "Animator";
    title += "###AnimatorEditor";

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin(title.c_str(), &isOpened, wflags))
    {
        focused = false;
        ImGui::End();
        return;
    }

    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    drawToolbar();
    ImGui::Separator();
    drawCanvas();
    drawStateContextMenu();
    drawVariableEditor();
    drawClipManager();

    ImGui::End();
}
