#ifndef PANEL_WORLD_H
#define PANEL_WORLD_H

#include <algorithm>
#include <vector>

#include "kemena/kemena.h"

#include <glm/gtx/matrix_decompose.hpp>

#include "manager.h"
#include "commands.h"
#include <ImGuizmo.h>

using namespace kemena;

class Manager;

class PanelWorld
{
public:
    bool enabled = false;
    bool hovered = false;
    bool focused = false;

    int   width       = 0;
    int   height      = 0;
    float aspectRatio = 0;
    kVec2 panelPos   = kVec2(0.f, 0.f);

    PanelWorld(kGuiManager *setGuiManager, Manager *setManager);
    void draw(bool &isOpened, kRenderer *renderer, kCamera *editorCamera);

    Manager    *manager;
    kGuiManager *gui;

private:
    // Gizmo undo state
    bool wasGizmoUsing = false;
    std::vector<TransformState> gizmoStartStates;

    // Lazy-initialised offscreen renderer used for the camera-preview overlay
    // shown at the bottom-right when a kCamera is the primary selection.
    kOffscreenRenderer *cameraPreview = nullptr;
};

#endif
