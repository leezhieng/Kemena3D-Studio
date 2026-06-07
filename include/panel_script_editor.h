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

    /**
     * @brief Constructs the editor and starts with an empty graph.
     * @param setGui     GUI manager used for ImGui rendering context.
     * @param setManager Studio manager (project paths, script build pipeline).
     */
    PanelScriptEditor(kGuiManager *setGui, Manager *setManager);

    /** @brief Draws the panel; @p isOpened is cleared when the window closes. */
    void draw(bool &isOpened);

    /** @brief Loads a @c .logic file for editing. */
    void openFile(const std::string &path);

    /** @brief Save the current graph (Ctrl+S target). No-op when nothing loaded. */
    void saveCurrent() { saveGraph(); }

    /**
     * @brief Notifies the editor that an asset was renamed/moved on disk.
     *
     * If the currently-open .logic is the one that moved, its tracked path is
     * updated so a subsequent Save writes to the new location instead of
     * recreating the file at the old path.
     */
    void notifyAssetMoved(const std::string &oldPath, const std::string &newPath);

private:
    /** @brief Resets to a fresh, untitled graph with a new UUID. */
    void newGraph();

    /**
     * @brief Loads and parses a @c .logic file into the current graph.
     * @param path Filesystem path to the @c .logic file.
     * @return @c true on success; @c false if the file cannot be opened/parsed.
     */
    bool loadGraph(const std::string &path);

    /**
     * @brief Writes .logic + regenerates .as; prompts if untitled.
     * @return @c true if the graph was saved.
     */
    bool saveGraph();

    /**
     * @brief Prompts for a destination and saves the graph under a new path.
     * @return @c true if a path was chosen and the save succeeded.
     */
    bool saveGraphAs();

    /** @brief Compiles the graph and writes the generated .as. */
    void regenerateScript();

    // Canvas <-> screen coordinate mapping (pan only; zoom is fixed at 1:1).

    /**
     * @brief Maps a canvas-space point to screen-space (applies pan offset).
     * @param canvasPos Point in canvas coordinates.
     * @param origin    Screen position of the canvas top-left.
     * @return The corresponding screen-space point.
     */
    ImVec2 canvasToScreen(ImVec2 canvasPos, ImVec2 origin) const;

    /**
     * @brief Maps a screen-space point back to canvas-space (removes pan offset).
     * @param screenPos Point in screen coordinates.
     * @param origin    Screen position of the canvas top-left.
     * @return The corresponding canvas-space point.
     */
    ImVec2 screenToCanvas(ImVec2 screenPos, ImVec2 origin) const;

    /** @brief Draws the top toolbar (New/Open/Save buttons and status line). */
    void drawToolbar();

    /** @brief Draws the side panel listing the graph's variables. */
    void drawVariablesPanel();

    /** @brief Draws the pannable node-graph canvas and handles its interactions. */
    void drawCanvas();

    /**
     * @brief Draws a single node with its header, pins and payload rows.
     * @param dl     Draw list to render into.
     * @param node   Node to draw (mutated for drag/selection state).
     * @param origin Screen position of the canvas top-left.
     */
    void drawNode(ImDrawList *dl, kScriptGraphNode &node, ImVec2 origin);

    /**
     * @brief Draws the bezier links connecting node pins.
     * @param dl Draw list to render into.
     */
    void drawLinks(ImDrawList *dl);

    /**
     * @brief Draws the right-click "Add Node" context menu.
     * @param canvasSpawnPos Canvas-space position where a new node is placed.
     */
    void drawAddNodeMenu(ImVec2 canvasSpawnPos);

    /**
     * @brief Attempts to connect two pins; silently ignores invalid pairings.
     * @param nodeA Source node id.
     * @param pinA  Source pin index.
     * @param nodeB Destination node id.
     * @param pinB  Destination pin index.
     */
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
