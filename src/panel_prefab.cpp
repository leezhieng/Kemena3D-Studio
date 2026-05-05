#include "panel_prefab.h"

#include <algorithm>

#include <glm/gtx/matrix_decompose.hpp>

PanelPrefab::PanelPrefab(kGuiManager *setGuiManager, Manager *setManager)
{
    gui     = setGuiManager;
    manager = setManager;
}

void PanelPrefab::draw(bool &isOpened, kRenderer *renderer)
{
    enabled = manager->projectOpened && manager->prefabEditing;

    // The panel auto-shows when prefab editing starts and auto-hides when it ends.
    if (enabled && !isOpened) isOpened = true;
    if (!enabled && isOpened) isOpened = false;

    if (!isOpened || renderer == nullptr || manager->prefabCamera == nullptr)
        return;

    gui->beginDisabled(!enabled);
    gui->windowStart("Prefab");

    // Toolbar — Save & Close, Discard.
    gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(6, 2));

    if (gui->button("Save & Close", kIvec2(110, 22)))
    {
        manager->closePrefabEditor(/*saveChanges*/ true);
        gui->popStyleVar();
        gui->windowEnd();
        gui->endDisabled();
        return;
    }
    if (gui->isItemHovered()) gui->setItemTooltip("Save changes back to the .prefab file and exit the prefab editor");

    gui->sameLine();

    if (gui->button("Discard", kIvec2(80, 22)))
    {
        manager->closePrefabEditor(/*saveChanges*/ false);
        gui->popStyleVar();
        gui->windowEnd();
        gui->endDisabled();
        return;
    }
    if (gui->isItemHovered()) gui->setItemTooltip("Exit without saving changes");

    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    gui->text(kString("Editing: ") + manager->editingPrefab.getName());

    gui->popStyleVar();
    gui->separator();

    // Viewport — same texture pipeline as PanelWorld; main.cpp renders the
    // prefab scene to the renderer's FBO when manager->prefabEditing is true.
    kVec2 availSize  = gui->getContentRegionAvail();
    width       = (int)availSize.x;
    height      = (int)availSize.y;
    aspectRatio = (height > 0.0f) ? (availSize.x / availSize.y) : 1.0f;

    panelPos        = gui->getCursorScreenPos();
    kVec2 panelSize = availSize;

    ImTextureRef tex_ref((ImTextureID)(uintptr_t)renderer->getFboTexture());
    gui->setNextItemAllowOverlap();
    ImGui::Image(tex_ref, ImVec2(availSize.x, availSize.y), ImVec2(0, 1), ImVec2(1, 0));

    hovered = gui->isWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    focused = gui->isWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Single-object gizmo on the prefab root (or whatever's selected).
    kObject *target = manager->selectedObject;
    if (target && target->getActive())
    {
        glm::mat4 view = manager->prefabCamera->getViewMatrix();
        glm::mat4 proj = manager->prefabCamera->getProjectionMatrix();

        glm::mat4 m = target->getModelMatrixWorld();
        glm::mat4 mCopy = m;

        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(panelPos.x, panelPos.y, panelSize.x, panelSize.y);

        ImGuizmo::Manipulate(
            glm::value_ptr(view), glm::value_ptr(proj),
            manager->manipulatorType, manager->manipulatorMode,
            glm::value_ptr(mCopy));

        bool isUsingNow = ImGuizmo::IsUsing();
        if (!wasGizmoUsing && isUsingNow)
            gizmoStartStates = manager->captureSelectedTransforms();

        if (isUsingNow)
        {
            glm::vec3 pos, scale, skew;
            glm::quat rot;
            glm::vec4 persp;
            glm::decompose(mCopy, scale, rot, pos, skew, persp);
            target->setPosition(pos);
            target->setRotation(glm::normalize(rot));
            target->setScale(scale);
        }

        if (wasGizmoUsing && !isUsingNow)
        {
            auto after = manager->captureSelectedTransforms();
            manager->undoRedo.push(std::make_unique<TransformCommand>(
                manager, gizmoStartStates, std::move(after)));
        }

        wasGizmoUsing = isUsingNow;
    }

    gui->windowEnd();
    gui->endDisabled();
}
