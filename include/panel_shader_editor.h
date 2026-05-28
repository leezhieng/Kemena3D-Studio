#pragma once
#include "kemena/kemena.h"
#include "kemena/kshadernode.h"
#include "manager.h"
#include <SDL3/SDL_dialog.h>
#include <string>
#include <filesystem>

using namespace kemena;
namespace fs = std::filesystem;

class PanelShaderEditor
{
public:
    bool focused = false;       ///< Set each draw() — used by main.cpp's Ctrl+S routing.

    PanelShaderEditor(kGuiManager* setGui, Manager* setManager);
    void draw(bool& isOpened);

    // Open a .shader file (called from project panel double-click)
    void openFile(const std::string& path);

    // Save the current graph (no-op when nothing is loaded). Public so the
    // global Ctrl+S handler can route here when this panel has focus.
    void saveCurrent() { saveGraph(); }

private:
    // -----------------------------------------------------------------------
    // Core state
    // -----------------------------------------------------------------------
    kGuiManager* gui     = nullptr;
    Manager*     manager = nullptr;

    kShaderGraph graph;
    std::string  filePath;          // current .shader file path (empty = unsaved)
    std::string  lastCompileError;
    bool         compiled = false;

    // -----------------------------------------------------------------------
    // Canvas navigation
    // -----------------------------------------------------------------------
    ImVec2 canvasOffset = { 0.f, 0.f };  // pan offset in canvas space
    float  canvasZoom   = 1.f;
    bool   isPanning    = false;
    ImVec2 panStartMouse;
    ImVec2 panStartOffset;

    // -----------------------------------------------------------------------
    // Interaction state
    // -----------------------------------------------------------------------
    int  selectedNode  = -1;      // node being moved
    bool isDraggingNode= false;
    ImVec2 dragNodeOffset;        // cursor offset within node at drag start

    // Connection drag
    bool   isDraggingLink = false;
    int    dragFromNode   = -1;
    int    dragFromPin    = -1;
    bool   dragFromOutput = true;
    kPinType dragPinType  = kPinType::Float;

    // Context menu
    ImVec2 nodeMenuPos;           // canvas position where right-click happened

    // -----------------------------------------------------------------------
    // Node size constants
    // -----------------------------------------------------------------------
    static constexpr float NODE_WIDTH     = 160.f;
    static constexpr float NODE_HEADER_H  = 24.f;
    static constexpr float PIN_RADIUS     =  5.f;
    static constexpr float PIN_ROW_H      = 20.f;
    static constexpr float PIN_PAD_X      = 10.f;

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------
    void drawToolbar();
    void drawCanvas();
    void drawNode(ImDrawList* dl, kShaderNode& node, ImVec2 origin);
    void drawLinks(ImDrawList* dl, ImVec2 origin);
    void drawDragLink(ImDrawList* dl);
    void drawNodeContextMenu();

    // Coordinate helpers
    ImVec2 canvasToScreen(ImVec2 canvasPos, ImVec2 origin) const;
    ImVec2 screenToCanvas(ImVec2 screenPos, ImVec2 origin) const;

    // Pin screen position (set during drawNode)
    ImVec2 getPinScreenPos(int nodeId, int pinId, ImVec2 origin) const;

    // Returns nodeId + pinId under cursor, or {-1,-1}
    struct HitPin { int nodeId = -1; int pinId = -1; bool isOutput = false; kPinType type = kPinType::Float; };
    HitPin hitTestPins(ImVec2 mouseScreen, ImVec2 origin) const;

    // File I/O
    void newGraph();
    void saveGraph();
    void saveGraphAs();
    void loadGraph(const std::string& path);
    void compileAndExport();

    static void SDLCALL saveShaderCallback(void* userdata, const char* const* filelist, int filter);

    static std::string generateUuid();
};
