#ifndef PANEL_PREFAB_H
#define PANEL_PREFAB_H

#include <vector>

#include "kemena/kemena.h"

#include "manager.h"
#include "commands.h"
#include <ImGuizmo.h>

using namespace kemena;

class Manager;

/**
 * @brief Prefab editor panel.
 *
 * Mirrors PanelWorld but renders an isolated kScene that contains only the
 * prefab subtree being edited. While this panel is open the main world view
 * is hidden so the user focuses on the prefab in isolation.
 *
 * The actual scene + camera live on the Manager (manager->prefabScene /
 * manager->prefabCamera) so that main.cpp can render them through the
 * existing single-FBO render path.
 */
class PanelPrefab
{
public:
    bool enabled = false;
    bool hovered = false;
    bool focused = false;

    int   width       = 0;
    int   height      = 0;
    float aspectRatio = 0;
    kVec2 panelPos   = kVec2(0.f, 0.f);

    PanelPrefab(kGuiManager *setGuiManager, Manager *setManager);
    void draw(bool &isOpened);

    Manager     *manager;
    kGuiManager *gui;

private:
    bool wasGizmoUsing = false;
    std::vector<TransformState> gizmoStartStates;
};

#endif
