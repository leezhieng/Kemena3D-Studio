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
 * matching the shader editor's style). Saving writes the editable @c .kgraph
 * JSON to the project's Assets folder and regenerates a sibling @c .as file
 * via kScriptGraphCompiler — that generated script flows through the normal
 * bytecode pipeline (Manager::buildScripts()).
 */
class PanelScriptEditor
{
public:
    PanelScriptEditor(kGuiManager *setGui, Manager *setManager);

    /** @brief Draws the panel; @p isOpened is cleared when the window closes. */
    void draw(bool &isOpened);

    /** @brief Loads a @c .kgraph file for editing. */
    void openFile(const std::string &path);

private:
    void newGraph();
    bool loadGraph(const std::string &path);
    bool saveGraph();    ///< Writes .kgraph + regenerates .as; prompts if untitled.
    bool saveGraphAs();
    void regenerateScript(); ///< Compiles the graph and writes the sibling .as.

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
    std::string  filePath; ///< Current .kgraph path ("" = untitled).

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
