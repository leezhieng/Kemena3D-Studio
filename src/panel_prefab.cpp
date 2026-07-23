#include "panel_prefab.h"

#include <algorithm>

#include <glm/gtx/matrix_decompose.hpp>

PanelPrefab::PanelPrefab(kGuiManager *setGuiManager, Manager *setManager)
{
    gui     = setGuiManager;
    manager = setManager;
}

void PanelPrefab::draw(bool &isOpened)
{
    enabled = manager->projectOpened && manager->prefabEditing;

    // The panel auto-shows when prefab editing starts and auto-hides when it ends.
    if (enabled && !isOpened) isOpened = true;
    if (!enabled && isOpened) isOpened = false;

    if (!isOpened || manager->prefabCamera == nullptr)
        return;

    gui->beginDisabled(!enabled);
    gui->windowStart("Prefab", &isOpened);

    // If the close button was clicked, save & exit the prefab editor.
    if (!isOpened)
    {
        manager->closePrefabEditor(/*saveChanges*/ true);
        gui->windowEnd();
        gui->endDisabled();
        return;
    }

    gui->text(kString("Editing: ") + manager->editingPrefab.getName());
    gui->separator();

    // Viewport — the prefab editor uses its OWN kRenderer (separate from the
    // World panel's renderer), so it never touches the World panel's FBO.
    kVec2 availSize  = gui->getContentRegionAvail();
    width       = (int)availSize.x;
    height      = (int)availSize.y;
    aspectRatio = (height > 0.0f) ? (availSize.x / availSize.y) : 1.0f;

    panelPos        = gui->getCursorScreenPos();
    kVec2 panelSize = availSize;

    if (manager->prefabRenderer)
    {
        ImTextureRef tex_ref((ImTextureID)(uintptr_t)manager->prefabRenderer->getFboTexture());
        gui->setNextItemAllowOverlap();
        ImGui::Image(tex_ref, ImVec2(availSize.x, availSize.y), ImVec2(0, 1), ImVec2(1, 0));
    }

    hovered = gui->isWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    focused = gui->isWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Single-object gizmo on the prefab root (or whatever's selected in prefab panel).
    kObject *target = manager->prefabSelectedObject;
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
            manager->prefabManipulatorType, manager->prefabManipulatorMode,
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
