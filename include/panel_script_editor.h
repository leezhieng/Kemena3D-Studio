#ifndef PANEL_SCRIPT_EDITOR_H
#define PANEL_SCRIPT_EDITOR_H

#include "kemena/kemena.h"
#include "kemena/kscriptgraph.h"
#include "manager.h"

#include "imgui.h"

#include <string>

using namespace kemena;

/**
 * @brief Visual-scripting node-graph editor panel.
 *
 * Edits a kScriptGraph on a pannable ImGui canvas (custom draw-list rendering,
 * matching the shader editor's style). Saving writes the editable @c .logic
 * JSON to the project's Assets folder and regenerates the AngelScript source
 * to @c Library/GeneratedScripts/<logic-uuid>.as via kScriptGraphCompiler —
 * that generated script lives outside Assets/ (a temp build artifact) and
 * flows through the normal bytecode pipeline (Manager::buildScripts()).
 */
class PanelScriptEditor
{
public:
    bool focused = false; ///< Set each draw() — used by main.cpp's Ctrl+S routing.

    PanelScriptEditor(kGuiManager *setGui, Manager *setManager);

    /** @brief Draws the panel; @p isOpened is cleared when the window closes. */
    void draw(bool &isOpened);

    /** @brief Loads a @c .logic file for editing. */
    void openFile(const std::string &path);

    /** @brief Save the current graph (Ctrl+S target). No-op when nothing loaded. */
    void saveCurrent() { saveGraph(); }

private:
    void newGraph();
    bool loadGraph(const std::string &path);
    bool saveGraph();    ///< Writes .logic + regenerates .as; prompts if untitled.
    bool saveGraphAs();
    void regenerateScript(); ///< Compiles the graph and writes the generated .as.

    // Canvas <-> screen coordinate mapping (pan only; zoom is fixed at 1:1).
    ImVec2 canvasToScreen(ImVec2 canvasPos, ImVec2 origin) const;
    ImVec2 screenToCanvas(ImVec2 screenPos, ImVec2 origin) const;

    void drawToolbar();
    void drawVariablesPanel();
    void drawCanvas();
    void drawNode(ImDrawList *dl, kScriptGraphNode &node, ImVec2 origin);
    void drawLinks(ImDrawList *dl);
    void drawAddNodeMenu(ImVec2 canvasSpawnPos);

    // Attempts to connect two pins; silently ignores invalid pairings.
    void tryConnect(int nodeA, int pinA, int nodeB, int pinB);

    kGuiManager *gui     = nullptr;
    Manager     *manager = nullptr;

    kScriptGraph graph;
    std::string  filePath; ///< Current .logic path ("" = untitled).

    ImVec2 canvasOffset = ImVec2(0.0f, 0.0f);
    ImVec2 canvasOrigin = ImVec2(0.0f, 0.0f); ///< Canvas top-left, refreshed each frame.

    int selectedNode = 0;

    // Link-drag state.
    bool linkDragging   = false;
    int  dragNode       = 0;
    int  dragPin        = 0;
    bool dragFromOutput = false;

    // Node-move state.
    int movingNode = 0;

    std::string statusLine; ///< Last save/compile result shown in the toolbar.
};

#endif // PANEL_SCRIPT_EDITOR_H
