#include "panel_shader_editor.h"
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
    return IM_COL32((int)(c.x*255), (int)(c.y*255), (int)(c.z*255), (int)(c.w*255));
}

static ImVec2 operator+(ImVec2 a, ImVec2 b) { return { a.x + b.x, a.y + b.y }; }
static ImVec2 operator-(ImVec2 a, ImVec2 b) { return { a.x - b.x, a.y - b.y }; }
static ImVec2 operator*(ImVec2 a, float s)  { return { a.x * s,   a.y * s   }; }

// ===========================================================================
// Construction / file helpers
// ===========================================================================

PanelShaderEditor::PanelShaderEditor(kGuiManager* setGui, Manager* setManager)
    : gui(setGui), manager(setManager)
{
    newGraph();
}

std::string PanelShaderEditor::generateUuid()
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

void PanelShaderEditor::newGraph()
{
    graph       = kShaderGraph{};
    graph.uuid  = generateUuid();
    graph.name  = "NewShader";
    graph.dirty = false;
    filePath.clear();
    lastCompileError.clear();
    compiled    = false;
    selectedNode = -1;
    if (manager) manager->shaderPreview.glslSource.clear();

    // Add a default PBR output node
    kShaderNode out = graph.makeNode(kShaderNodeType::OutputPBR, 500.f, 200.f);
    graph.nodes.push_back(out);
}

void PanelShaderEditor::openFile(const std::string& path)
{
    loadGraph(path);
}

void PanelShaderEditor::loadGraph(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    try
    {
        json j; f >> j;
        graph.fromJson(j);
        filePath = path;
        graph.dirty = false;
        lastCompileError.clear();
        compiled    = false;
        selectedNode = -1;
        if (manager) manager->shaderPreview.glslSource.clear();
    }
    catch (...) {}
}

void PanelShaderEditor::saveGraph()
{
    if (filePath.empty()) { saveGraphAs(); return; }

    // Write .shader JSON
    json j = graph.toJson();
    std::ofstream f(filePath);
    if (!f.is_open()) return;
    f << j.dump(4);
    graph.dirty = false;
}

void SDLCALL PanelShaderEditor::saveShaderCallback(void* userdata,
                                                    const char* const* filelist,
                                                    int /*filter*/)
{
    if (!filelist || !*filelist) return;

    PanelShaderEditor* self = static_cast<PanelShaderEditor*>(userdata);

    std::string path = filelist[0];
    if (path.size() < 7 || path.substr(path.size() - 7) != ".shader")
        path += ".shader";

    self->filePath = path;
    self->saveGraph();
}

void PanelShaderEditor::saveGraphAs()
{
    if (!manager->projectOpened) return;

    fs::path assetsDir = fs::path(manager->projectPath.c_str()) / "Assets" / "Shaders";
    fs::create_directories(assetsDir);

    std::string defaultName = (graph.name.empty() ? "NewShader" : graph.name) + ".shader";

    SDL_DialogFileFilter filters[] = {
        { "Shader files", "shader" },
        { "All files",    "*"      }
    };

    SDL_ShowSaveFileDialog(
        saveShaderCallback,
        this,
        manager->getWindow()->getSdlWindow(),
        filters,
        SDL_arraysize(filters),
        (assetsDir / defaultName).string().c_str()
    );
}

void PanelShaderEditor::compileAndExport()
{
    lastCompileError.clear();
    compiled = false;

    kShaderCompileResult res = kShaderCompiler::compile(graph);
    if (!res.success)
    {
        lastCompileError = res.error;
        return;
    }

    // Determine Library path
    if (!manager->projectOpened) { lastCompileError = "No project open."; return; }

    fs::path libDir = fs::path(manager->projectPath.c_str()) / "Library" / "Shaders";
    fs::create_directories(libDir);

    // Write uuid.glsl
    std::string uuid = graph.uuid;
    fs::path glslPath = libDir / (uuid + ".glsl");
    {
        std::ofstream f(glslPath);
        f << res.glsl;
    }

    // Placeholder stubs for future backends
    {
        std::ofstream f(libDir / (uuid + ".hlsl"));
        f << "// HLSL not yet generated — compile from " << uuid << ".glsl\n";
    }
    {
        std::ofstream f(libDir / (uuid + ".spirv.placeholder"));
        f << "// SPIR-V not yet generated\n";
    }

    compiled = true;
    manager->shaderPreview.glslSource = res.glsl;

    // Auto-save .shader if we have a path
    if (!filePath.empty()) saveGraph();
}

// ===========================================================================
// Coordinate helpers
// ===========================================================================

ImVec2 PanelShaderEditor::canvasToScreen(ImVec2 cp, ImVec2 origin) const
{
    return origin + (cp + canvasOffset) * canvasZoom;
}

ImVec2 PanelShaderEditor::screenToCanvas(ImVec2 sp, ImVec2 origin) const
{
    return (sp - origin) * (1.f / canvasZoom) - canvasOffset;
}

ImVec2 PanelShaderEditor::getPinScreenPos(int nodeId, int pinId, ImVec2 origin) const
{
    const kShaderNode* n = graph.findNode(nodeId);
    if (!n) return { 0, 0 };
    for (const auto& p : n->inputs)
        if (p.id == pinId) return { p.uiX, p.uiY };
    for (const auto& p : n->outputs)
        if (p.id == pinId) return { p.uiX, p.uiY };
    return { 0, 0 };
}

PanelShaderEditor::HitPin PanelShaderEditor::hitTestPins(ImVec2 mouse, ImVec2 origin) const
{
    for (const auto& n : graph.nodes)
    {
        auto check = [&](const kShaderPin& p) -> bool {
            float dx = mouse.x - p.uiX, dy = mouse.y - p.uiY;
            return (dx*dx + dy*dy) <= (PIN_RADIUS * PIN_RADIUS * 2.25f);
        };
        for (const auto& p : n.inputs)
            if (check(p)) return { n.id, p.id, false, p.type };
        for (const auto& p : n.outputs)
            if (check(p)) return { n.id, p.id, true, p.type };
    }
    return {};
}

// ===========================================================================
// Draw helpers
// ===========================================================================

static ImVec4 nodeColor(kShaderNodeType t)
{
    switch (t)
    {
        case kShaderNodeType::ConstFloat:
        case kShaderNodeType::ConstVec2:
        case kShaderNodeType::ConstVec3:
        case kShaderNodeType::ConstVec4:
        case kShaderNodeType::UVCoord:
        case kShaderNodeType::Time:
        case kShaderNodeType::VertexColor:
        case kShaderNodeType::WorldPosition:
        case kShaderNodeType::ViewDirection:
        case kShaderNodeType::VertexNormal:
            return { 0.20f, 0.40f, 0.80f, 1.f };

        case kShaderNodeType::Texture2D:
        case kShaderNodeType::TextureCube:
            return { 0.15f, 0.60f, 0.25f, 1.f };

        case kShaderNodeType::Add:
        case kShaderNodeType::Subtract:
        case kShaderNodeType::Multiply:
        case kShaderNodeType::Divide:
        case kShaderNodeType::Dot:
        case kShaderNodeType::Cross:
        case kShaderNodeType::Normalize:
        case kShaderNodeType::Length:
        case kShaderNodeType::Clamp:
        case kShaderNodeType::Mix:
        case kShaderNodeType::Pow:
        case kShaderNodeType::Abs:
        case kShaderNodeType::Floor:
        case kShaderNodeType::Ceil:
        case kShaderNodeType::Fract:
        case kShaderNodeType::Sqrt:
        case kShaderNodeType::Min:
        case kShaderNodeType::Max:
        case kShaderNodeType::Step:
        case kShaderNodeType::Smoothstep:
        case kShaderNodeType::OneMinus:
            return { 0.80f, 0.45f, 0.05f, 1.f };

        case kShaderNodeType::Split:
        case kShaderNodeType::Combine:
        case kShaderNodeType::Swizzle:
            return { 0.50f, 0.25f, 0.70f, 1.f };

        case kShaderNodeType::MaterialTiling:
        case kShaderNodeType::MaterialAmbient:
        case kShaderNodeType::MaterialDiffuse:
        case kShaderNodeType::MaterialSpecular:
        case kShaderNodeType::MaterialShininess:
        case kShaderNodeType::MaterialMetallic:
        case kShaderNodeType::MaterialRoughness:
            return { 0.55f, 0.20f, 0.55f, 1.f };

        case kShaderNodeType::OutputFlat:
        case kShaderNodeType::OutputPhong:
        case kShaderNodeType::OutputPBR:
            return { 0.75f, 0.10f, 0.10f, 1.f };

        default:
            return { 0.30f, 0.30f, 0.30f, 1.f };
    }
}

static ImU32 pinColor(kPinType t)
{
    switch (t)
    {
        case kPinType::Float:       return IM_COL32(200, 200, 200, 255);
        case kPinType::Vec2:        return IM_COL32( 80, 200, 120, 255);
        case kPinType::Vec3:        return IM_COL32( 80, 160, 255, 255);
        case kPinType::Vec4:        return IM_COL32(200, 100, 255, 255);
        case kPinType::Sampler2D:   return IM_COL32(255, 180,  50, 255);
        case kPinType::SamplerCube: return IM_COL32(255, 120,  50, 255);
        default:                    return IM_COL32(200, 200, 200, 255);
    }
}

static bool nodeHasInlineEditor(kShaderNodeType t)
{
    switch (t)
    {
        case kShaderNodeType::ConstFloat:
        case kShaderNodeType::ConstVec2:
        case kShaderNodeType::ConstVec3:
        case kShaderNodeType::ConstVec4:
        case kShaderNodeType::Texture2D:
        case kShaderNodeType::TextureCube:
        case kShaderNodeType::Swizzle:
            return true;
        default:
            return false;
    }
}

void PanelShaderEditor::drawNode(ImDrawList* dl, kShaderNode& node, ImVec2 origin)
{
    const float zoom     = canvasZoom;
    const float nw       = NODE_WIDTH * zoom;
    const float hdrH     = NODE_HEADER_H * zoom;
    const float rowH     = PIN_ROW_H * zoom;
    const float pinR     = PIN_RADIUS * zoom;
    const float padX     = PIN_PAD_X * zoom;
    const float fontSize = ImGui::GetFontSize();

    bool  hasEditor = nodeHasInlineEditor(node.type);
    float editorH   = hasEditor ? (rowH + 4.f * zoom) : 0.f;

    int numRows = (int)std::max(node.inputs.size(), node.outputs.size());
    // Ensure at least one pin-row height so const nodes (outputs-only) have body space
    if (numRows == 0) numRows = 1;
    float bodyH  = rowH * numRows + 4.f * zoom + editorH;
    float totalH = hdrH + bodyH;

    ImVec2 topLeft = canvasToScreen({ node.posX, node.posY }, origin);
    ImVec2 botRight = topLeft + ImVec2(nw, totalH);

    bool isSelected = (node.id == selectedNode);

    // Shadow
    dl->AddRectFilled({ topLeft.x + 3, topLeft.y + 3 }, { botRight.x + 3, botRight.y + 3 },
                      IM_COL32(0, 0, 0, 80), 6.f * zoom);

    // Body
    dl->AddRectFilled(topLeft, botRight, IM_COL32(45, 45, 45, 230), 6.f * zoom);

    // Header
    ImVec4 hdrCol = nodeColor(node.type);
    dl->AddRectFilled(topLeft, { botRight.x, topLeft.y + hdrH },
                      toImU32(hdrCol), 6.f * zoom);
    // Fix header bottom corners
    dl->AddRectFilled({ topLeft.x, topLeft.y + hdrH - 4.f * zoom },
                      { botRight.x, topLeft.y + hdrH },
                      toImU32(hdrCol), 0.f);

    // Outline
    ImU32 outlineCol = isSelected ? IM_COL32(255, 200, 50, 255) : IM_COL32(100, 100, 100, 180);
    dl->AddRect(topLeft, botRight, outlineCol, 6.f * zoom, 0, isSelected ? 2.f : 1.f);

    // Title
    ImVec2 titlePos = topLeft + ImVec2(6.f * zoom, (hdrH - fontSize) * 0.5f);
    dl->AddText(titlePos, IM_COL32(255, 255, 255, 255), node.name.c_str());

    // Pins
    ImVec2 mouse = ImGui::GetIO().MousePos;

    auto drawPins = [&](std::vector<kShaderPin>& pins, bool isOut)
    {
        for (int i = 0; i < (int)pins.size(); ++i)
        {
            kShaderPin& p = pins[i];
            float rowTop = topLeft.y + hdrH + rowH * i + rowH * 0.5f;
            float pinX   = isOut ? (topLeft.x + nw) : topLeft.x;

            // Store screen pos for hit testing and link drawing
            p.uiX = pinX;
            p.uiY = rowTop;

            ImU32 pc        = pinColor(p.type);
            bool  connected = graph.isPinConnected(node.id, p.id);

            // Evaluate whether this pin is a valid / invalid drop target
            bool isValidTarget   = false;
            bool isInvalidTarget = false;
            if (isDraggingLink && node.id != dragFromNode)
            {
                bool dirOk  = (isOut != dragFromOutput); // must be opposite direction
                bool typeOk = false;
                if (dirOk)
                    typeOk = dragFromOutput ? kPinCompatible(dragPinType, p.type)
                                            : kPinCompatible(p.type, dragPinType);
                if (dirOk && typeOk)  isValidTarget   = true;
                else if (dirOk)       isInvalidTarget = true;
            }

            // Draw pin circle with appropriate style
            if (isValidTarget)
            {
                // Green glow for a legal drop target
                dl->AddCircleFilled({ pinX, rowTop }, pinR * 1.5f, IM_COL32(50, 210, 90, 70));
                dl->AddCircleFilled({ pinX, rowTop }, pinR,        IM_COL32(70, 240, 110, 255));
                dl->AddCircle(      { pinX, rowTop }, pinR * 1.8f, IM_COL32(70, 240, 110, 160), 0, 1.5f);
            }
            else if (isInvalidTarget)
            {
                // Red for direction-correct but type-incompatible
                dl->AddCircleFilled({ pinX, rowTop }, pinR, IM_COL32(210, 55, 55, 220));
                dl->AddCircle(      { pinX, rowTop }, pinR * 1.5f, IM_COL32(210, 55, 55, 150), 0, 1.5f);
            }
            else if (connected)
            {
                dl->AddCircleFilled({ pinX, rowTop }, pinR, pc);
            }
            else
            {
                dl->AddCircleFilled({ pinX, rowTop }, pinR, IM_COL32(40, 40, 40, 255));
                dl->AddCircle(      { pinX, rowTop }, pinR, pc, 0, 1.5f);
            }

            // Label
            float labelX = isOut ? (pinX - padX - ImGui::CalcTextSize(p.name.c_str()).x)
                                 : (pinX + padX);
            dl->AddText({ labelX, rowTop - fontSize * 0.5f }, IM_COL32(220, 220, 220, 255),
                        p.name.c_str());

            // Tooltip: show pin name + type when the cursor is near the pin circle
            float dx = mouse.x - pinX, dy = mouse.y - rowTop;
            if (dx*dx + dy*dy <= pinR * pinR * 4.0f * canvasZoom * canvasZoom)
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(p.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", kPinTypeName(p.type));
                ImGui::EndTooltip();
            }
        }
    };

    drawPins(node.inputs,  false);
    drawPins(node.outputs, true);

    // Inline editors — placed in the dedicated strip below all pin rows
    if (hasEditor && zoom >= 0.75f)
    {
        float pinsH    = rowH * numRows + 4.f * zoom;
        ImVec2 editPos = { topLeft.x + padX, topLeft.y + hdrH + pinsH + 2.f * zoom };
        float editW    = nw - padX * 2.f;

        ImGui::SetCursorScreenPos(editPos);
        ImGui::PushID(node.id);
        ImGui::PushItemWidth(editW);

        switch (node.type)
        {
            case kShaderNodeType::ConstFloat:
                if (ImGui::DragFloat("##v", &node.valueFloat[0], 0.01f))
                    graph.dirty = true;
                break;
            case kShaderNodeType::ConstVec2:
                if (ImGui::DragFloat2("##v", node.valueFloat, 0.01f))
                    graph.dirty = true;
                break;
            case kShaderNodeType::ConstVec3:
                if (ImGui::ColorEdit3("##v", node.valueFloat,
                                     ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_Float))
                    graph.dirty = true;
                break;
            case kShaderNodeType::ConstVec4:
                if (ImGui::ColorEdit4("##v", node.valueFloat,
                                     ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_Float))
                    graph.dirty = true;
                break;
            case kShaderNodeType::Texture2D:
            case kShaderNodeType::TextureCube:
            {
                char buf[128];
                strncpy_s(buf, node.valueStr.c_str(), sizeof(buf));
                ImGui::SetNextItemWidth(editW);
                if (ImGui::InputText("##name", buf, sizeof(buf)))
                {
                    node.valueStr = buf;
                    graph.dirty   = true;
                }
                break;
            }
            case kShaderNodeType::Swizzle:
            {
                char buf[8];
                strncpy_s(buf, node.valueStr.c_str(), sizeof(buf));
                if (ImGui::InputText("##swiz", buf, sizeof(buf)))
                {
                    node.valueStr = buf;
                    graph.dirty   = true;
                }
                break;
            }
            default: break;
        }

        ImGui::PopItemWidth();
        ImGui::PopID();
    }
}

void PanelShaderEditor::drawLinks(ImDrawList* dl, ImVec2 origin)
{
    for (const auto& link : graph.links)
    {
        ImVec2 p0 = getPinScreenPos(link.fromNode, link.fromPin, origin);
        ImVec2 p3 = getPinScreenPos(link.toNode,   link.toPin,   origin);
        if (p0.x == 0 && p0.y == 0) continue;
        if (p3.x == 0 && p3.y == 0) continue;

        float cx = (p3.x - p0.x) * 0.5f;
        ImVec2 p1 = { p0.x + cx, p0.y };
        ImVec2 p2 = { p3.x - cx, p3.y };

        // Colour by the source pin type
        const kShaderNode* src = graph.findNode(link.fromNode);
        kPinType t = kPinType::Float;
        if (src) for (const auto& p : src->outputs)
            if (p.id == link.fromPin) { t = p.type; break; }

        ImU32 col = pinColor(t);
        dl->AddBezierCubic(p0, p1, p2, p3, col, 2.f * canvasZoom);
    }
}

void PanelShaderEditor::drawDragLink(ImDrawList* dl)
{
    if (!isDraggingLink) return;

    ImVec2 p0 = { 0, 0 };
    // Find the pin we started from across all nodes
    for (const auto& n : graph.nodes)
    {
        for (const auto& p : n.outputs)
            if (n.id == dragFromNode && p.id == dragFromPin) { p0 = { p.uiX, p.uiY }; goto found; }
        for (const auto& p : n.inputs)
            if (n.id == dragFromNode && p.id == dragFromPin) { p0 = { p.uiX, p.uiY }; goto found; }
    }
    found:
    ImVec2 p3 = ImGui::GetIO().MousePos;
    float  cx = (p3.x - p0.x) * 0.5f;
    ImVec2 p1 = { p0.x + cx, p0.y };
    ImVec2 p2 = { p3.x - cx, p3.y };
    dl->AddBezierCubic(p0, p1, p2, p3, pinColor(dragPinType), 2.f * canvasZoom);
    dl->AddCircleFilled(p3, PIN_RADIUS * canvasZoom, IM_COL32(255, 255, 255, 180));
}

void PanelShaderEditor::drawNodeContextMenu()
{
    if (ImGui::BeginPopup("##NodeAddMenu"))
    {
        ImVec2 spawnPos = nodeMenuPos;

        struct Entry { const char* label; kShaderNodeType type; };

        auto addEntry = [&](const char* label, kShaderNodeType type)
        {
            if (ImGui::MenuItem(label))
            {
                kShaderNode n = graph.makeNode(type, spawnPos.x, spawnPos.y);
                graph.nodes.push_back(n);
                graph.dirty = true;
            }
        };

        if (ImGui::BeginMenu("Inputs"))
        {
            addEntry("Float",          kShaderNodeType::ConstFloat);
            addEntry("Vec2",           kShaderNodeType::ConstVec2);
            addEntry("Vec3 / Color",   kShaderNodeType::ConstVec3);
            addEntry("Vec4 / Color",   kShaderNodeType::ConstVec4);
            addEntry("UV Coord",       kShaderNodeType::UVCoord);
            addEntry("Vertex Color",   kShaderNodeType::VertexColor);
            addEntry("World Position", kShaderNodeType::WorldPosition);
            addEntry("View Direction", kShaderNodeType::ViewDirection);
            addEntry("Vertex Normal",  kShaderNodeType::VertexNormal);
            addEntry("Time",           kShaderNodeType::Time);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Textures"))
        {
            addEntry("Texture 2D",   kShaderNodeType::Texture2D);
            addEntry("Texture Cube", kShaderNodeType::TextureCube);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Math"))
        {
            addEntry("Add",         kShaderNodeType::Add);
            addEntry("Subtract",    kShaderNodeType::Subtract);
            addEntry("Multiply",    kShaderNodeType::Multiply);
            addEntry("Divide",      kShaderNodeType::Divide);
            addEntry("Min",         kShaderNodeType::Min);
            addEntry("Max",         kShaderNodeType::Max);
            addEntry("Clamp",       kShaderNodeType::Clamp);
            addEntry("Mix",         kShaderNodeType::Mix);
            addEntry("Power",       kShaderNodeType::Pow);
            addEntry("Sqrt",        kShaderNodeType::Sqrt);
            addEntry("Abs",         kShaderNodeType::Abs);
            addEntry("Floor",       kShaderNodeType::Floor);
            addEntry("Ceil",        kShaderNodeType::Ceil);
            addEntry("Fract",       kShaderNodeType::Fract);
            addEntry("One Minus",   kShaderNodeType::OneMinus);
            addEntry("Step",        kShaderNodeType::Step);
            addEntry("Smoothstep",  kShaderNodeType::Smoothstep);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Vector"))
        {
            addEntry("Dot Product",   kShaderNodeType::Dot);
            addEntry("Cross Product", kShaderNodeType::Cross);
            addEntry("Normalize",     kShaderNodeType::Normalize);
            addEntry("Length",        kShaderNodeType::Length);
            addEntry("Split",         kShaderNodeType::Split);
            addEntry("Combine",       kShaderNodeType::Combine);
            addEntry("Swizzle",       kShaderNodeType::Swizzle);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Material"))
        {
            addEntry("Tiling",     kShaderNodeType::MaterialTiling);
            addEntry("Ambient",    kShaderNodeType::MaterialAmbient);
            addEntry("Diffuse",    kShaderNodeType::MaterialDiffuse);
            addEntry("Specular",   kShaderNodeType::MaterialSpecular);
            addEntry("Shininess",  kShaderNodeType::MaterialShininess);
            addEntry("Metallic",   kShaderNodeType::MaterialMetallic);
            addEntry("Roughness",  kShaderNodeType::MaterialRoughness);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Output"))
        {
            addEntry("Output (Flat)",  kShaderNodeType::OutputFlat);
            addEntry("Output (Phong)", kShaderNodeType::OutputPhong);
            addEntry("Output (PBR)",   kShaderNodeType::OutputPBR);
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }
}

// ===========================================================================
// Main draw
// ===========================================================================

void PanelShaderEditor::draw(bool& isOpened)
{
    if (!isOpened) return;

    // Keep manager in sync every frame so the inspector can read it
    manager->shaderPreview.uuid       = graph.uuid;
    manager->shaderPreview.shaderName = graph.name;
    manager->shaderPreview.shaderType = "PBR";
    for (const auto& n : graph.nodes)
    {
        if (n.type == kShaderNodeType::OutputFlat)  { manager->shaderPreview.shaderType = "Flat";  break; }
        if (n.type == kShaderNodeType::OutputPhong) { manager->shaderPreview.shaderType = "Phong"; break; }
        if (n.type == kShaderNodeType::OutputPBR)   { manager->shaderPreview.shaderType = "PBR";   break; }
    }

    ImGui::SetNextWindowSize({ 1000, 700 }, ImGuiCond_FirstUseEver);

    kString title = graph.dirty ? "Shader *" : "Shader";
    title += "###ShaderEditor";

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
    drawNodeContextMenu();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void PanelShaderEditor::drawToolbar()
{
    bool hasProject = manager->projectOpened;

    if (ImGui::Button("New"))
    {
        // TODO: prompt save if dirty
        newGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open") && hasProject)
    {
        // TODO: file dialog
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

    if (ImGui::Button("Compile & Export") && hasProject)
        compileAndExport();

    ImGui::SameLine();
    if (compiled)
    {
        ImGui::TextColored({ 0.3f, 1.f, 0.3f, 1.f }, "OK");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Compiled successfully. Files written to Library/Shaders/%s.*",
                        graph.uuid.c_str());
            ImGui::EndTooltip();
        }
    }
    else if (!lastCompileError.empty())
    {
        ImGui::TextColored({ 1.f, 0.3f, 0.3f, 1.f }, "Error");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(lastCompileError.c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Shader name field
    char nameBuf[128];
    strncpy_s(nameBuf, graph.name.c_str(), sizeof(nameBuf));
    ImGui::SetNextItemWidth(160.f);
    if (ImGui::InputText("##ShaderName", nameBuf, sizeof(nameBuf)))
    {
        graph.name  = nameBuf;
        graph.dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("Shader name");

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

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

void PanelShaderEditor::drawCanvas()
{
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 10 || canvasSize.y < 10) return;

    ImVec2 canvasTL = ImGui::GetCursorScreenPos();

    // Invisible button captures input
    ImGui::InvisibleButton("##canvas", canvasSize,
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

        if (panButton && !isDraggingNode && !isDraggingLink)
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
        // Zoom toward mouse
        ImVec2 mouseCanvas = screenToCanvas(mouse, canvasTL);
        canvasOffset.x += mouseCanvas.x * (1.f / prevZoom - 1.f / canvasZoom);
        canvasOffset.y += mouseCanvas.y * (1.f / prevZoom - 1.f / canvasZoom);
    }

    // --- Draw links ---
    drawLinks(dl, canvasTL);
    drawDragLink(dl);

    // --- Draw nodes ---
    for (auto& node : graph.nodes)
        drawNode(dl, node, canvasTL);

    // --- Interaction ---

    // Detect pin hover and start/finish connection drag
    HitPin hovPin = hitTestPins(mouse, canvasTL);

    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (!io.KeyAlt)
        {
            if (hovPin.nodeId >= 0)
            {
                // Start dragging a link from this pin
                isDraggingLink = true;
                dragFromNode   = hovPin.nodeId;
                dragFromPin    = hovPin.pinId;
                dragFromOutput = hovPin.isOutput;
                dragPinType    = hovPin.type;
            }
            else
            {
                // Check if we hit a node header → start move
                bool hitNode = false;
                for (auto& node : graph.nodes)
                {
                    ImVec2 nTopLeft = canvasToScreen({ node.posX, node.posY }, canvasTL);
                    ImVec2 nBotRight = nTopLeft + ImVec2(NODE_WIDTH * canvasZoom, NODE_HEADER_H * canvasZoom);
                    if (mouse.x >= nTopLeft.x && mouse.x <= nBotRight.x &&
                        mouse.y >= nTopLeft.y && mouse.y <= nBotRight.y)
                    {
                        selectedNode    = node.id;
                        isDraggingNode  = true;
                        dragNodeOffset  = screenToCanvas(mouse, canvasTL) - ImVec2(node.posX, node.posY);
                        hitNode = true;
                        break;
                    }
                }
                if (!hitNode) selectedNode = -1;
            }
        }
    }

    // Move selected node
    if (isDraggingNode && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt)
    {
        kShaderNode* n = graph.findNode(selectedNode);
        if (n)
        {
            ImVec2 cp = screenToCanvas(mouse, canvasTL) - dragNodeOffset;
            n->posX = cp.x;
            n->posY = cp.y;
            graph.dirty = true;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDraggingNode = false;

        if (isDraggingLink)
        {
            isDraggingLink = false;
            if (hovPin.nodeId >= 0 && hovPin.nodeId != dragFromNode)
            {
                // Determine from/to (output→input)
                bool fromIsOutput = dragFromOutput;
                bool toIsOutput   = hovPin.isOutput;

                // dragPinType is the type of the pin we started dragging from.
                // When dragging from an OUTPUT we need: output-type → input-type.
                // When dragging from an INPUT  we need: output-type (hov) → input-type (drag).
                bool typeOk = dragFromOutput
                    ? kPinCompatible(dragPinType, hovPin.type)
                    : kPinCompatible(hovPin.type, dragPinType);

                if (fromIsOutput != toIsOutput && typeOk)
                {
                    int fNode = dragFromOutput ? dragFromNode  : hovPin.nodeId;
                    int fPin  = dragFromOutput ? dragFromPin   : hovPin.pinId;
                    int tNode = dragFromOutput ? hovPin.nodeId : dragFromNode;
                    int tPin  = dragFromOutput ? hovPin.pinId  : dragFromPin;

                    // Remove any existing link to the target input
                    graph.removeLinksByPin(tNode, tPin);

                    kShaderLink lnk;
                    lnk.id       = graph.newId();
                    lnk.fromNode = fNode;
                    lnk.fromPin  = fPin;
                    lnk.toNode   = tNode;
                    lnk.toPin    = tPin;
                    graph.links.push_back(lnk);
                    graph.dirty = true;
                }
            }
        }
    }

    // Right-click on canvas → add node
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !isDraggingLink)
    {
        // Right-click on a node → delete option
        bool hitNode = false;
        for (auto& node : graph.nodes)
        {
            ImVec2 nTL = canvasToScreen({ node.posX, node.posY }, canvasTL);
            float nw = NODE_WIDTH * canvasZoom;
            int nr = (int)std::max(node.inputs.size(), node.outputs.size());
            float nh = (NODE_HEADER_H + PIN_ROW_H * nr + 4.f) * canvasZoom;

            if (mouse.x >= nTL.x && mouse.x <= nTL.x + nw &&
                mouse.y >= nTL.y && mouse.y <= nTL.y + nh)
            {
                selectedNode = node.id;
                ImGui::OpenPopup("##NodeCtxMenu");
                hitNode = true;
                break;
            }
        }
        if (!hitNode)
        {
            nodeMenuPos = screenToCanvas(mouse, canvasTL);
            ImGui::OpenPopup("##NodeAddMenu");
        }
    }

    // Node context menu (right-click on node)
    if (ImGui::BeginPopup("##NodeCtxMenu"))
    {
        if (ImGui::MenuItem("Delete Node") && selectedNode >= 0)
        {
            graph.removeLinksByNode(selectedNode);
            graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(),
                [this](const kShaderNode& n) { return n.id == selectedNode; }),
                graph.nodes.end());
            graph.dirty  = true;
            selectedNode = -1;
        }
        if (ImGui::MenuItem("Disconnect All Links") && selectedNode >= 0)
        {
            graph.removeLinksByNode(selectedNode);
            graph.dirty = true;
        }
        ImGui::EndPopup();
    }

    // Delete key
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedNode >= 0)
    {
        graph.removeLinksByNode(selectedNode);
        graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(),
            [this](const kShaderNode& n) { return n.id == selectedNode; }),
            graph.nodes.end());
        graph.dirty  = true;
        selectedNode = -1;
    }

    dl->PopClipRect();
}
