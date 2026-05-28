#define IMGUI_DEFINE_MATH_OPERATORS
#include "panel_script_editor.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "portable-file-dialogs.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
namespace
{
    const float NODE_W   = 240.0f;
    const float HEADER_H = 26.0f;
    const float ROW_H    = 22.0f;
    const float PIN_R    = 5.5f;
    const float PIN_HIT  = 9.0f;

    enum class NodeCategory { Event, Flow, Action, Getter, Value, Math };

    NodeCategory categoryOf(kScriptNodeType t)
    {
        switch (t)
        {
            case kScriptNodeType::EventAwake:
            case kScriptNodeType::EventStart:
            case kScriptNodeType::EventUpdate:
            case kScriptNodeType::EventFixedUpdate:
            case kScriptNodeType::EventLateUpdate:
            case kScriptNodeType::EventOnDestroy:  return NodeCategory::Event;
            case kScriptNodeType::Branch:          return NodeCategory::Flow;
            case kScriptNodeType::Print:
            case kScriptNodeType::SetPosition:
            case kScriptNodeType::SetRotation:
            case kScriptNodeType::SetScale:
            case kScriptNodeType::Translate:
            case kScriptNodeType::Rotate:
            case kScriptNodeType::SetActive:
            case kScriptNodeType::SetVariable:     return NodeCategory::Action;
            case kScriptNodeType::GetSelf:
            case kScriptNodeType::GetPosition:
            case kScriptNodeType::GetRotation:
            case kScriptNodeType::GetScale:
            case kScriptNodeType::GetForward:
            case kScriptNodeType::GetRight:
            case kScriptNodeType::GetUp:
            case kScriptNodeType::GetDeltaTime:
            case kScriptNodeType::GetVariable:     return NodeCategory::Getter;
            case kScriptNodeType::LiteralFloat:
            case kScriptNodeType::LiteralBool:
            case kScriptNodeType::LiteralString:
            case kScriptNodeType::LiteralVec3:     return NodeCategory::Value;
            default:                               return NodeCategory::Math;
        }
    }

    ImU32 headerColor(kScriptNodeType t)
    {
        switch (categoryOf(t))
        {
            case NodeCategory::Event:  return IM_COL32(150, 62, 62, 255);
            case NodeCategory::Flow:   return IM_COL32(96, 96, 104, 255);
            case NodeCategory::Action: return IM_COL32(54, 92, 142, 255);
            case NodeCategory::Getter: return IM_COL32(58, 122, 80, 255);
            case NodeCategory::Value:  return IM_COL32(120, 96, 52, 255);
            default:                   return IM_COL32(86, 74, 120, 255);
        }
    }

    ImU32 pinColor(kScriptPinType t)
    {
        switch (t)
        {
            case kScriptPinType::Exec:   return IM_COL32(235, 235, 235, 255);
            case kScriptPinType::Float:  return IM_COL32(126, 206, 126, 255);
            case kScriptPinType::Bool:   return IM_COL32(206, 96, 96, 255);
            case kScriptPinType::String: return IM_COL32(206, 156, 96, 255);
            case kScriptPinType::Vec3:   return IM_COL32(150, 150, 232, 255);
            case kScriptPinType::Object: return IM_COL32(224, 200, 120, 255);
            default:                     return IM_COL32(200, 200, 200, 255);
        }
    }

    int payloadRows(kScriptNodeType t)
    {
        switch (t)
        {
            case kScriptNodeType::LiteralFloat:
            case kScriptNodeType::LiteralBool:
            case kScriptNodeType::LiteralString:
            case kScriptNodeType::GetVariable:
            case kScriptNodeType::SetVariable: return 1;
            case kScriptNodeType::LiteralVec3: return 3;
            default:                           return 0;
        }
    }

    // Finds a pin by id within a node; sets isOutput when found.
    kScriptGraphPin *getPin(kScriptGraphNode *n, int pinId, bool *isOutput)
    {
        for (auto &p : n->inputs)
            if (p.id == pinId) { if (isOutput) *isOutput = false; return &p; }
        for (auto &p : n->outputs)
            if (p.id == pinId) { if (isOutput) *isOutput = true; return &p; }
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Construction / file operations
// ---------------------------------------------------------------------------

PanelScriptEditor::PanelScriptEditor(kGuiManager *setGui, Manager *setManager)
    : gui(setGui), manager(setManager)
{
    newGraph();
}

void PanelScriptEditor::newGraph()
{
    graph = kScriptGraph{};
    graph.uuid = generateUuid();
    graph.name = "NewScriptGraph";
    filePath.clear();
    canvasOffset = ImVec2(0.0f, 0.0f);
    selectedNode = 0;
    statusLine.clear();

    // Seed with an On Update event so the canvas is not empty.
    graph.nodes.push_back(graph.makeNode(kScriptNodeType::EventUpdate, 120.0f, 120.0f));
}

void PanelScriptEditor::openFile(const std::string &path)
{
    loadGraph(path);
}

bool PanelScriptEditor::loadGraph(const std::string &path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        statusLine = "Failed to open: " + path;
        return false;
    }
    try
    {
        json j;
        in >> j;
        graph.fromJson(j);
    }
    catch (const std::exception &e)
    {
        statusLine = std::string("Parse error: ") + e.what();
        return false;
    }
    filePath     = path;
    canvasOffset = ImVec2(0.0f, 0.0f);
    selectedNode = 0;
    statusLine   = "Loaded " + fs::path(path).filename().string();
    return true;
}

bool PanelScriptEditor::saveGraphAs()
{
    std::string defaultDir =
        manager ? (manager->projectPath / "Assets").string() : std::string();

    std::string result = pfd::save_file(
        "Save Logic Graph", defaultDir,
        { "Kemena Logic (*.logic)", "*.logic" }).result();

    if (result.empty())
        return false;

    if (result.size() < 6 || result.substr(result.size() - 6) != ".logic")
        result += ".logic";

    filePath   = result;
    graph.name = fs::path(result).stem().string();
    if (graph.uuid.empty())
        graph.uuid = generateUuid();
    return saveGraph();
}

bool PanelScriptEditor::saveGraph()
{
    if (filePath.empty())
        return saveGraphAs();

    std::ofstream out(filePath);
    if (!out.is_open())
    {
        statusLine = "Cannot write: " + filePath;
        return false;
    }
    out << graph.toJson().dump(2);
    out.close();

    graph.dirty = false;
    regenerateScript();
    return true;
}

void PanelScriptEditor::regenerateScript()
{
    if (!manager || graph.uuid.empty())
    {
        statusLine = "Cannot generate script: missing project or graph UUID";
        return;
    }

    kScriptGraphResult res = kScriptGraphCompiler::compile(graph);
    if (!res.success)
    {
        statusLine = "Compile error: " + res.error;
        return;
    }

    // Generated AngelScript lives outside Assets/: a temp build artifact
    // keyed by the .logic UUID so multiple .logic files can't collide.
    fs::path tempDir = manager->projectPath / "Library" / "GeneratedScripts";
    std::error_code ec;
    fs::create_directories(tempDir, ec);

    fs::path asPath = tempDir / (graph.uuid + ".as");

    std::ofstream out(asPath);
    if (!out.is_open())
    {
        statusLine = "Cannot write generated script: " + asPath.string();
        return;
    }
    out << res.code;
    out.close();

    // Refresh bytecode for any object already using this generated script.
    manager->buildScripts();

    statusLine = "Saved + compiled -> " + asPath.filename().string();
}

// ---------------------------------------------------------------------------
// Coordinate mapping
// ---------------------------------------------------------------------------

ImVec2 PanelScriptEditor::canvasToScreen(ImVec2 cp, ImVec2 origin) const
{
    return ImVec2(origin.x + cp.x + canvasOffset.x,
                  origin.y + cp.y + canvasOffset.y);
}

ImVec2 PanelScriptEditor::screenToCanvas(ImVec2 sp, ImVec2 origin) const
{
    return ImVec2(sp.x - origin.x - canvasOffset.x,
                  sp.y - origin.y - canvasOffset.y);
}

// ---------------------------------------------------------------------------
// Pin geometry — single source of truth, shared by node + link drawing
// ---------------------------------------------------------------------------

namespace
{
    // Returns the screen position of a pin given the node origin.
    ImVec2 pinScreenPos(const kScriptGraphNode &n, int pinId, ImVec2 nodeScreen)
    {
        for (size_t i = 0; i < n.inputs.size(); ++i)
            if (n.inputs[i].id == pinId)
                return ImVec2(nodeScreen.x,
                              nodeScreen.y + HEADER_H + i * ROW_H + ROW_H * 0.5f);
        for (size_t i = 0; i < n.outputs.size(); ++i)
            if (n.outputs[i].id == pinId)
                return ImVec2(nodeScreen.x + NODE_W,
                              nodeScreen.y + HEADER_H + i * ROW_H + ROW_H * 0.5f);
        return nodeScreen;
    }

    void drawWire(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 col, float thick)
    {
        float dist = std::min(std::max(std::fabs(b.x - a.x) * 0.5f, 28.0f), 180.0f);
        dl->AddBezierCubic(a, ImVec2(a.x + dist, a.y),
                           ImVec2(b.x - dist, b.y), b, col, thick);
    }
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void PanelScriptEditor::drawToolbar()
{
    if (ImGui::Button("New"))
        newGraph();
    ImGui::SameLine();
    if (ImGui::Button("Open"))
    {
        std::string dir =
            manager ? (manager->projectPath / "Assets").string() : std::string();
        auto sel = pfd::open_file("Open Logic Graph", dir,
                                  { "Kemena Logic (*.logic)", "*.logic" }).result();
        if (!sel.empty())
            loadGraph(sel[0]);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
        saveGraph();
    ImGui::SameLine();
    if (ImGui::Button("Compile"))
    {
        if (filePath.empty())
            saveGraphAs();
        else
            regenerateScript();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    std::string title = filePath.empty() ? std::string("untitled")
                                         : fs::path(filePath).filename().string();
    if (graph.dirty)
        title += " *";
    ImGui::TextUnformatted(title.c_str());

    if (!statusLine.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("   %s", statusLine.c_str());
    }
}

// ---------------------------------------------------------------------------
// Variables panel
// ---------------------------------------------------------------------------

void PanelScriptEditor::drawVariablesPanel()
{
    ImGui::BeginChild("##scriptvars", ImVec2(180.0f, 0.0f), true);
    ImGui::TextUnformatted("Variables");
    ImGui::Separator();

    if (ImGui::Button("+ Add", ImVec2(-1.0f, 0.0f)))
    {
        kScriptGraphVar v;
        v.name = "var" + std::to_string(graph.variables.size() + 1);
        graph.variables.push_back(v);
        graph.dirty = true;
    }

    int removeIndex = -1;
    for (size_t i = 0; i < graph.variables.size(); ++i)
    {
        kScriptGraphVar &v = graph.variables[i];
        ImGui::PushID((int)i);

        char nameBuf[64];
        std::strncpy(nameBuf, v.name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
        {
            v.name = nameBuf;
            graph.dirty = true;
        }

        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::DragFloat("##def", &v.defValue, 0.05f))
            graph.dirty = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            removeIndex = (int)i;

        ImGui::Separator();
        ImGui::PopID();
    }
    if (removeIndex >= 0)
    {
        graph.variables.erase(graph.variables.begin() + removeIndex);
        graph.dirty = true;
    }

    ImGui::TextDisabled("Variables become float\nglobals in the script.");
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Node drawing
// ---------------------------------------------------------------------------

void PanelScriptEditor::drawNode(ImDrawList *dl, kScriptGraphNode &node, ImVec2 origin)
{
    int rows = (int)std::max(node.inputs.size(), node.outputs.size());
    if (rows < 1) rows = 1;
    float plRows = (float)payloadRows(node.type);
    float height = HEADER_H + rows * ROW_H + plRows * ROW_H + 8.0f;

    ImVec2 nodeScreen = canvasToScreen(ImVec2(node.posX, node.posY), origin);
    ImVec2 nodeMax    = ImVec2(nodeScreen.x + NODE_W, nodeScreen.y + height);
    bool   selected   = (node.id == selectedNode);

    // Body + header + border.
    dl->AddRectFilled(nodeScreen, nodeMax, IM_COL32(40, 42, 48, 245), 5.0f);
    dl->AddRectFilled(nodeScreen, ImVec2(nodeMax.x, nodeScreen.y + HEADER_H),
                      headerColor(node.type), 5.0f, ImDrawFlags_RoundCornersTop);
    dl->AddRect(nodeScreen, nodeMax,
                selected ? IM_COL32(255, 170, 60, 255) : IM_COL32(20, 20, 24, 255),
                5.0f, 0, selected ? 2.5f : 1.2f);
    dl->AddText(ImVec2(nodeScreen.x + 10.0f, nodeScreen.y + 5.0f),
                IM_COL32(245, 245, 245, 255), node.name.c_str());

    ImGui::PushID(node.id);

    // The whole node body is the select / move handle. Allow overlap so the pin
    // and value widgets submitted afterwards still receive input on top of it.
    ImGui::SetCursorScreenPos(nodeScreen);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##node", ImVec2(NODE_W, height));
    if (ImGui::IsItemActivated())
    {
        selectedNode = node.id;
        movingNode   = node.id;
    }
    if (ImGui::IsItemActive() && movingNode == node.id)
    {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        node.posX += d.x;
        node.posY += d.y;
    }
    if (ImGui::IsItemDeactivated() && movingNode == node.id)
        movingNode = 0;

    // Input pins (+ inline default editors for unconnected data pins).
    for (size_t i = 0; i < node.inputs.size(); ++i)
    {
        kScriptGraphPin &p = node.inputs[i];
        ImVec2 pp = pinScreenPos(node, p.id, nodeScreen);
        bool connected = graph.incomingLink(node.id, p.id) != nullptr;

        dl->AddCircleFilled(pp, PIN_R, pinColor(p.type));
        if (!connected)
            dl->AddCircleFilled(pp, PIN_R - 2.0f, IM_COL32(40, 42, 48, 255));
        if (!p.name.empty())
            dl->AddText(ImVec2(pp.x + 10.0f, pp.y - 7.0f),
                        IM_COL32(210, 210, 210, 255), p.name.c_str());

        if (!connected && p.type != kScriptPinType::Exec && p.type != kScriptPinType::Vec3)
        {
            ImGui::PushID((int)p.id);
            ImGui::SetCursorScreenPos(ImVec2(nodeScreen.x + 92.0f, pp.y - 9.0f));
            if (p.type == kScriptPinType::Float)
            {
                ImGui::SetNextItemWidth(64.0f);
                if (ImGui::DragFloat("##d", &p.defFloat, 0.05f))
                    graph.dirty = true;
            }
            else if (p.type == kScriptPinType::Bool)
            {
                if (ImGui::Checkbox("##d", &p.defBool))
                    graph.dirty = true;
            }
            else if (p.type == kScriptPinType::String)
            {
                char buf[128];
                std::strncpy(buf, p.defStr.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                ImGui::SetNextItemWidth(78.0f);
                if (ImGui::InputText("##d", buf, sizeof(buf)))
                {
                    p.defStr = buf;
                    graph.dirty = true;
                }
            }
            ImGui::PopID();
        }
    }

    // Output pins.
    for (size_t i = 0; i < node.outputs.size(); ++i)
    {
        kScriptGraphPin &p = node.outputs[i];
        ImVec2 pp = pinScreenPos(node, p.id, nodeScreen);
        bool connected = graph.outgoingLink(node.id, p.id) != nullptr;

        dl->AddCircleFilled(pp, PIN_R, pinColor(p.type));
        if (!connected)
            dl->AddCircleFilled(pp, PIN_R - 2.0f, IM_COL32(40, 42, 48, 255));
        if (!p.name.empty())
        {
            ImVec2 ts = ImGui::CalcTextSize(p.name.c_str());
            dl->AddText(ImVec2(pp.x - 10.0f - ts.x, pp.y - 7.0f),
                        IM_COL32(210, 210, 210, 255), p.name.c_str());
        }
    }

    // Payload widgets below the pin rows.
    float payloadY = nodeScreen.y + HEADER_H + rows * ROW_H + 3.0f;
    ImGui::SetCursorScreenPos(ImVec2(nodeScreen.x + 12.0f, payloadY));
    ImGui::PushItemWidth(NODE_W - 24.0f);

    switch (node.type)
    {
        case kScriptNodeType::LiteralFloat:
            if (ImGui::DragFloat("##lf", &node.valueFloat[0], 0.05f))
                graph.dirty = true;
            break;
        case kScriptNodeType::LiteralBool:
            if (ImGui::Checkbox("Value##lb", &node.valueBool))
                graph.dirty = true;
            break;
        case kScriptNodeType::LiteralString:
        {
            char buf[256];
            std::strncpy(buf, node.valueStr.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("##ls", buf, sizeof(buf)))
            {
                node.valueStr = buf;
                graph.dirty = true;
            }
            break;
        }
        case kScriptNodeType::LiteralVec3:
            if (ImGui::DragFloat("X##lv", &node.valueFloat[0], 0.05f)) graph.dirty = true;
            ImGui::SetCursorScreenPos(ImVec2(nodeScreen.x + 12.0f, payloadY + ROW_H));
            if (ImGui::DragFloat("Y##lv", &node.valueFloat[1], 0.05f)) graph.dirty = true;
            ImGui::SetCursorScreenPos(ImVec2(nodeScreen.x + 12.0f, payloadY + ROW_H * 2));
            if (ImGui::DragFloat("Z##lv", &node.valueFloat[2], 0.05f)) graph.dirty = true;
            break;
        case kScriptNodeType::GetVariable:
        case kScriptNodeType::SetVariable:
        {
            const char *cur = node.valueStr.empty() ? "(select var)" : node.valueStr.c_str();
            if (ImGui::BeginCombo("##var", cur))
            {
                for (const auto &v : graph.variables)
                {
                    bool sel = (v.name == node.valueStr);
                    if (ImGui::Selectable(v.name.c_str(), sel))
                    {
                        node.valueStr = v.name;
                        graph.dirty = true;
                    }
                }
                if (graph.variables.empty())
                    ImGui::TextDisabled("No variables defined");
                ImGui::EndCombo();
            }
            break;
        }
        default:
            break;
    }

    ImGui::PopItemWidth();
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// Link drawing
// ---------------------------------------------------------------------------

void PanelScriptEditor::drawLinks(ImDrawList *dl)
{
    for (const auto &l : graph.links)
    {
        kScriptGraphNode *from = graph.findNode(l.fromNode);
        kScriptGraphNode *to   = graph.findNode(l.toNode);
        if (!from || !to)
            continue;

        ImVec2 a = pinScreenPos(*from, l.fromPin,
                                canvasToScreen(ImVec2(from->posX, from->posY), canvasOrigin));
        ImVec2 b = pinScreenPos(*to, l.toPin,
                                canvasToScreen(ImVec2(to->posX, to->posY), canvasOrigin));

        bool isOut = false;
        kScriptGraphPin *fp = getPin(from, l.fromPin, &isOut);
        ImU32 col = fp ? pinColor(fp->type) : IM_COL32(200, 200, 200, 255);
        drawWire(dl, a, b, col, 2.6f);
    }
}

// ---------------------------------------------------------------------------
// Add-node context menu
// ---------------------------------------------------------------------------

void PanelScriptEditor::drawAddNodeMenu(ImVec2 spawn)
{
    struct Entry { const char *label; kScriptNodeType type; };
    auto submenu = [&](const char *cat, const Entry *items, int count) {
        if (ImGui::BeginMenu(cat))
        {
            for (int i = 0; i < count; ++i)
                if (ImGui::MenuItem(items[i].label))
                {
                    kScriptGraphNode n = graph.makeNode(items[i].type, spawn.x, spawn.y);
                    graph.nodes.push_back(n);
                    selectedNode = n.id;
                    graph.dirty = true;
                }
            ImGui::EndMenu();
        }
    };

    static const Entry events[] = {
        {"On Awake", kScriptNodeType::EventAwake},
        {"On Start", kScriptNodeType::EventStart},
        {"On Update", kScriptNodeType::EventUpdate},
        {"On Fixed Update", kScriptNodeType::EventFixedUpdate},
        {"On Late Update", kScriptNodeType::EventLateUpdate},
        {"On Destroy", kScriptNodeType::EventOnDestroy},
    };
    static const Entry flow[] = {
        {"Branch", kScriptNodeType::Branch},
    };
    static const Entry actions[] = {
        {"Print", kScriptNodeType::Print},
        {"Set Position", kScriptNodeType::SetPosition},
        {"Set Rotation", kScriptNodeType::SetRotation},
        {"Set Scale", kScriptNodeType::SetScale},
        {"Translate", kScriptNodeType::Translate},
        {"Rotate", kScriptNodeType::Rotate},
        {"Set Active", kScriptNodeType::SetActive},
        {"Set Variable", kScriptNodeType::SetVariable},
    };
    static const Entry getters[] = {
        {"Get Self", kScriptNodeType::GetSelf},
        {"Get Position", kScriptNodeType::GetPosition},
        {"Get Rotation", kScriptNodeType::GetRotation},
        {"Get Scale", kScriptNodeType::GetScale},
        {"Get Forward", kScriptNodeType::GetForward},
        {"Get Right", kScriptNodeType::GetRight},
        {"Get Up", kScriptNodeType::GetUp},
        {"Get Delta Time", kScriptNodeType::GetDeltaTime},
        {"Get Variable", kScriptNodeType::GetVariable},
    };
    static const Entry values[] = {
        {"Float", kScriptNodeType::LiteralFloat},
        {"Bool", kScriptNodeType::LiteralBool},
        {"String", kScriptNodeType::LiteralString},
        {"Vector3", kScriptNodeType::LiteralVec3},
    };
    static const Entry math[] = {
        {"Add", kScriptNodeType::Add},
        {"Subtract", kScriptNodeType::Subtract},
        {"Multiply", kScriptNodeType::Multiply},
        {"Divide", kScriptNodeType::Divide},
        {"Make Vector3", kScriptNodeType::MakeVec3},
        {"Break Vector3", kScriptNodeType::BreakVec3},
        {"Scale Vector3", kScriptNodeType::ScaleVec3},
        {"Greater", kScriptNodeType::Greater},
        {"Less", kScriptNodeType::Less},
        {"Equal", kScriptNodeType::Equal},
        {"And", kScriptNodeType::And},
        {"Or", kScriptNodeType::Or},
        {"Not", kScriptNodeType::Not},
    };

    submenu("Events",   events,  IM_ARRAYSIZE(events));
    submenu("Flow",     flow,    IM_ARRAYSIZE(flow));
    submenu("Actions",  actions, IM_ARRAYSIZE(actions));
    submenu("Get",      getters, IM_ARRAYSIZE(getters));
    submenu("Values",   values,  IM_ARRAYSIZE(values));
    submenu("Math/Logic", math,  IM_ARRAYSIZE(math));

    if (selectedNode != 0)
    {
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Selected Node"))
        {
            graph.removeNode(selectedNode);
            selectedNode = 0;
            graph.dirty = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void PanelScriptEditor::tryConnect(int nodeA, int pinA, int nodeB, int pinB)
{
    if (nodeA == nodeB)
        return;

    kScriptGraphNode *na = graph.findNode(nodeA);
    kScriptGraphNode *nb = graph.findNode(nodeB);
    if (!na || !nb)
        return;

    bool aOut = false, bOut = false;
    kScriptGraphPin *pa = getPin(na, pinA, &aOut);
    kScriptGraphPin *pb = getPin(nb, pinB, &bOut);
    if (!pa || !pb || aOut == bOut)        // need exactly one output + one input
        return;
    if (pa->type != pb->type)              // strict type match (incl. Exec)
        return;

    int outN, outP, inN, inP;
    kScriptGraphPin *outPin;
    if (aOut) { outN = nodeA; outP = pinA; outPin = pa; inN = nodeB; inP = pinB; }
    else      { outN = nodeB; outP = pinB; outPin = pb; inN = nodeA; inP = pinA; }

    // An input pin holds a single wire; an exec output also fans out to one.
    graph.removeLinksByPin(inN, inP);
    if (outPin->type == kScriptPinType::Exec)
        graph.removeLinksByPin(outN, outP);

    kScriptGraphLink l;
    l.id       = graph.newId();
    l.fromNode = outN; l.fromPin = outP;
    l.toNode   = inN;  l.toPin   = inP;
    graph.links.push_back(l);
    graph.dirty = true;
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

void PanelScriptEditor::drawCanvas()
{
    ImGui::BeginChild("##scriptcanvas", ImVec2(0.0f, 0.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    canvasOrigin    = ImGui::GetCursorScreenPos();
    ImVec2 size     = ImGui::GetContentRegionAvail();
    if (size.x < 1.0f) size.x = 1.0f;
    if (size.y < 1.0f) size.y = 1.0f;
    ImDrawList *dl  = ImGui::GetWindowDrawList();

    // Background + grid.
    dl->AddRectFilled(canvasOrigin,
                      ImVec2(canvasOrigin.x + size.x, canvasOrigin.y + size.y),
                      IM_COL32(28, 29, 33, 255));
    const float grid = 24.0f;
    for (float x = std::fmod(canvasOffset.x, grid); x < size.x; x += grid)
        dl->AddLine(ImVec2(canvasOrigin.x + x, canvasOrigin.y),
                    ImVec2(canvasOrigin.x + x, canvasOrigin.y + size.y),
                    IM_COL32(40, 41, 46, 255));
    for (float y = std::fmod(canvasOffset.y, grid); y < size.y; y += grid)
        dl->AddLine(ImVec2(canvasOrigin.x, canvasOrigin.y + y),
                    ImVec2(canvasOrigin.x + size.x, canvasOrigin.y + y),
                    IM_COL32(40, 41, 46, 255));

    // Canvas-level button: captures panning + the empty-space context menu.
    // Allow overlap so the node header/pin widgets submitted afterwards take
    // input priority over this background button (otherwise the canvas, being
    // submitted first, swallows every click and nodes can't be selected/moved).
    ImGui::SetCursorScreenPos(canvasOrigin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##canvasbtn", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool canvasHovered = ImGui::IsItemHovered();
    bool canvasActive  = ImGui::IsItemActive();

    // Links beneath nodes.
    drawLinks(dl);

    // Nodes (also submits their ImGui widgets, drawn on top of the canvas button).
    for (auto &n : graph.nodes)
        drawNode(dl, n, canvasOrigin);

    // ---- Pin hit-testing ---------------------------------------------------
    ImVec2 mouse = ImGui::GetIO().MousePos;
    int hovNode = 0, hovPin = 0;
    for (auto &n : graph.nodes)
    {
        ImVec2 ns = canvasToScreen(ImVec2(n.posX, n.posY), canvasOrigin);
        auto scan = [&](kScriptGraphPin &p) {
            ImVec2 pp = pinScreenPos(n, p.id, ns);
            float dx = pp.x - mouse.x, dy = pp.y - mouse.y;
            if (dx * dx + dy * dy <= PIN_HIT * PIN_HIT)
            {
                hovNode = n.id;
                hovPin  = p.id;
            }
        };
        for (auto &p : n.inputs)  scan(p);
        for (auto &p : n.outputs) scan(p);
    }

    // Start a link drag from a pin. This takes priority over node movement:
    // the node body button (drawn earlier) may have started a move on the same
    // click, so cancel it when the cursor is actually over a pin. Uses the
    // manual pin hit-test rather than canvas hover, since the node body button
    // now owns ImGui hover over the node.
    if (hovNode && ImGui::IsMouseClicked(0) && !linkDragging)
    {
        kScriptGraphNode *n = graph.findNode(hovNode);
        bool isOut = false;
        if (n && getPin(n, hovPin, &isOut))
        {
            linkDragging   = true;
            dragNode       = hovNode;
            dragPin        = hovPin;
            dragFromOutput = isOut;
            movingNode     = 0; // don't drag the node while wiring a pin
        }
    }

    // Right-click a pin to clear its wires.
    if (hovNode && ImGui::IsMouseClicked(1))
    {
        graph.removeLinksByPin(hovNode, hovPin);
        graph.dirty = true;
    }

    // Finish a link drag.
    if (linkDragging && ImGui::IsMouseReleased(0))
    {
        if (hovNode)
            tryConnect(dragNode, dragPin, hovNode, hovPin);
        linkDragging = false;
    }

    // Live drag wire.
    if (linkDragging)
    {
        kScriptGraphNode *n = graph.findNode(dragNode);
        if (n)
        {
            ImVec2 src = pinScreenPos(*n, dragPin,
                                      canvasToScreen(ImVec2(n->posX, n->posY), canvasOrigin));
            if (dragFromOutput)
                drawWire(dl, src, mouse, IM_COL32(255, 220, 120, 255), 2.4f);
            else
                drawWire(dl, mouse, src, IM_COL32(255, 220, 120, 255), 2.4f);
        }
        else
        {
            linkDragging = false;
        }
    }

    // Pan with an empty-space drag.
    if (canvasActive && !linkDragging && movingNode == 0 &&
        ImGui::IsMouseDragging(0))
    {
        canvasOffset.x += ImGui::GetIO().MouseDelta.x;
        canvasOffset.y += ImGui::GetIO().MouseDelta.y;
    }

    // Delete the selected node with the Delete key.
    if (ImGui::IsWindowFocused() && selectedNode != 0 &&
        ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        graph.removeNode(selectedNode);
        selectedNode = 0;
        graph.dirty  = true;
    }

    // Empty-space right-click opens the add-node menu.
    static ImVec2 ctxSpawn(0.0f, 0.0f);
    if (canvasHovered && hovNode == 0 && ImGui::IsMouseClicked(1))
    {
        ctxSpawn = screenToCanvas(mouse, canvasOrigin);
        ImGui::OpenPopup("##addnodemenu");
    }
    if (ImGui::BeginPopup("##addnodemenu"))
    {
        drawAddNodeMenu(ctxSpawn);
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Panel entry point
// ---------------------------------------------------------------------------

void PanelScriptEditor::draw(bool &isOpened)
{
    if (!isOpened)
        return;

    ImGui::SetNextWindowSize(ImVec2(900.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Script Editor", &isOpened, ImGuiWindowFlags_NoScrollbar))
    {
        focused = false;
        ImGui::End();
        return;
    }

    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    drawToolbar();
    ImGui::Separator();
    drawVariablesPanel();
    ImGui::SameLine();
    drawCanvas();

    ImGui::End();
}
