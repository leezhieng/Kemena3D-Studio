#include "panel_world.h"

PanelWorld::PanelWorld(kGuiManager *setGuiManager, Manager *setManager)
{
    gui     = setGuiManager;
    manager = setManager;

    // Disable the hatched/dashed overlay on gizmo axis lines
    ImGuizmo::GetStyle().HatchedAxisLineThickness = 0.0f;
}

// ---------------------------------------------------------------------------
// Pivot toolbar helpers
// ---------------------------------------------------------------------------

static void pivotButton(kGuiManager *gui, const char *label, PivotMode mode, PivotMode &current)
{
    bool active = (current == mode);
    if (active)
    {
        gui->pushStyleColor(ImGuiCol_Button,        kVec4(0.26f, 0.59f, 0.98f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
    }
    if (gui->button(label, kIvec2(26, 22)))
        current = mode;
    if (active)
        gui->popStyleColor(2);
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void PanelWorld::draw(bool &isOpened, kRenderer *renderer, kCamera *editorCamera)
{
    enabled = manager->projectOpened;

    if (!isOpened || renderer == nullptr || editorCamera == nullptr)
        return;

    gui->beginDisabled(!enabled);
    gui->windowStart("World");

    gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(2, 2));

    pivotButton(gui, "I", PivotMode::Individual,   manager->pivotMode);
    if (gui->isItemHovered()) gui->setItemTooltip("Individual pivot");
    gui->sameLine();
    pivotButton(gui, "C", PivotMode::Center, manager->pivotMode);
    if (gui->isItemHovered()) gui->setItemTooltip("Center pivot");
    gui->sameLine();
    pivotButton(gui, "L", PivotMode::LastSelected, manager->pivotMode);
    if (gui->isItemHovered()) gui->setItemTooltip("Last selected pivot");

    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    // Render mode selector
    static const char *kRenderModeNames[] = {
        "Full", "Albedo", "Normals", "Wireframe", "Depth", "Object IDs", "Full+Wire"
    };
    int currentMode = (int)renderer->getRenderMode();
    gui->setNextItemWidth(110.0f);
    if (ImGui::Combo("##RenderMode", &currentMode, kRenderModeNames, 7))
        renderer->setRenderMode((kRenderMode)currentMode);

    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    // Octree debug toggle
    {
        bool octreeDebug = renderer->getOctreeDebugEnabled();
        if (gui->checkbox("Octree", &octreeDebug))
            renderer->setOctreeDebugEnabled(octreeDebug);
        if (gui->isItemHovered()) gui->setItemTooltip("Show octree node bounds");
    }

    gui->popStyleVar();

    gui->separator();

    kVec2 availSize  = gui->getContentRegionAvail();
    width       = (int)availSize.x;
    height      = (int)availSize.y;
    aspectRatio = (height > 0.0f) ? (availSize.x / availSize.y) : 1.0f;

    panelPos         = gui->getCursorScreenPos();
    kVec2 panelSize  = availSize;

    // Display framebuffer texture
    ImTextureRef tex_ref((ImTextureID)(uintptr_t)renderer->getFboTexture());
    gui->setNextItemAllowOverlap();
    ImGui::Image(tex_ref, ImVec2(availSize.x, availSize.y), ImVec2(0, 1), ImVec2(1, 0));

    // ----- Drop target -------------------------------------------------------
    // Project assets dragged onto the viewport: meshes/prefabs/audio spawn on
    // release; materials live-preview on hover and commit on release.
    // Regardless of payload type, we record the picked object in
    // manager->dragHoverObjectUuid so the main render loop can paint an
    // outline showing the user which object is under their cursor.
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
                "PROJECT_ASSET", ImGuiDragDropFlags_AcceptBeforeDelivery))
        {
            kString assetUuid((const char *)payload->Data);
            // The project panel may pack several newline-separated UUIDs when
            // multiple files are dragged; this target acts on the first one.
            { auto nl = assetUuid.find('\n'); if (nl != kString::npos) assetUuid = assetUuid.substr(0, nl); }
            auto it = manager->fileMap.find(assetUuid);
            if (it != manager->fileMap.end())
            {
                const auto &info = it->second;

                // Pick the object under the mouse for the drag-hover outline.
                // Material drops also use this for live preview targeting.
                kVec2 imMouse = gui->getMousePos();
                int vpMouseX = (int)((imMouse.x - panelPos.x) * 2.0f);
                int vpMouseY = (int)((imMouse.y - panelPos.y) * 2.0f);
                // The exact object (sub-mesh) under the cursor, by Object ID.
                // Material drops apply to THIS node directly — not the whole
                // model — so each sub-mesh of a multi-mesh import can have its
                // own material.
                kObject *hovObj = renderer->pickObject(
                    manager->getWorld(), manager->getScene(),
                    vpMouseX, vpMouseY, width * 2, height * 2);
                manager->dragHoverObjectUuid = hovObj ? hovObj->getUuid() : kString("");

                if (info.type == "material")
                {
                    fs::path matPath = manager->projectPath / "Assets" / info.path;

                    // If the hovered object changed since last frame, restore
                    // the prior preview's whole subtree before applying to the
                    // new one.
                    if (hovObj != manager->matPreviewObject)
                    {
                        if (manager->matPreviewObject)
                            manager->restoreMaterialSubtree(manager->matPreviewSnapshot);

                        manager->matPreviewObject     = hovObj;
                        manager->matPreviewSnapshot   = hovObj
                            ? manager->captureMaterialSubtree(hovObj)
                            : std::vector<MaterialSnapshot>();
                        manager->matPreviewSourceUuid = assetUuid;

                        if (hovObj)
                            manager->applyMaterialToObject(hovObj, matPath, assetUuid);
                    }

                    // Commit on release: leave the new material in place, push a
                    // subtree-wide undo, then forget the preview so the post-
                    // target restore below won't revert it.
                    if (payload->IsDelivery() && manager->matPreviewObject)
                    {
                        auto cmd = std::make_unique<MaterialCommand>();
                        cmd->manager = manager;
                        cmd->before  = manager->matPreviewSnapshot;
                        cmd->after   = manager->captureMaterialSubtree(manager->matPreviewObject);
                        manager->undoRedo.push(std::move(cmd));

                        manager->matPreviewObject = nullptr;
                        manager->matPreviewSnapshot.clear();
                        manager->matPreviewSourceUuid = "";
                    }
                }
                else if (payload->IsDelivery())
                {
                    // Spawn at a point in front of the editor camera so the new
                    // object appears in view rather than at the world origin.
                    kVec3 spawn = editorCamera->getPosition()
                                 + editorCamera->calculateForward() * 5.0f;
                    manager->instantiateAssetFromUuid(assetUuid, spawn);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // If a material preview is dangling (drag left the viewport or was
    // cancelled), restore the original material so the scene isn't left with
    // a half-applied preview.
    if (!ImGui::IsDragDropActive() && manager->matPreviewObject)
    {
        manager->restoreMaterialSubtree(manager->matPreviewSnapshot);
        manager->matPreviewObject = nullptr;
        manager->matPreviewSnapshot.clear();
        manager->matPreviewSourceUuid = "";
    }

    // Clear the drag-hover highlight when no drag is in progress.
    if (!ImGui::IsDragDropActive() && !manager->dragHoverObjectUuid.empty())
        manager->dragHoverObjectUuid.clear();

    hovered = gui->isWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    focused = gui->isWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // ------------------------------------------------------------------
    // Multi-object gizmo
    // ------------------------------------------------------------------
    if (!manager->selectedObjects.empty())
    {
        // Gather valid selected object pointers
        std::vector<kObject *> selObjs;
        for (const auto &uuid : manager->selectedObjects)
        {
            kObject *obj = manager->findObjectByUuid(uuid);
            if (obj && obj->getActive()) selObjs.push_back(obj);
        }

        if (!selObjs.empty())
        {
            glm::mat4 view = editorCamera->getViewMatrix();
            glm::mat4 proj = editorCamera->getProjectionMatrix();

            // Compute pivot matrix
            glm::mat4 pivotMatrix;
            if (manager->pivotMode == PivotMode::Center)
            {
                glm::vec3 center(0.0f);
                for (kObject *obj : selObjs)
                    center += obj->getPosition();
                center /= (float)selObjs.size();
                pivotMatrix = glm::translate(glm::mat4(1.0f), center);
            }
            else
            {
                // LastSelected or Individual → pivot at last selected
                kObject *pivot = manager->selectedObject ? manager->selectedObject : selObjs.back();
                pivotMatrix = pivot->getModelMatrixWorld();
            }

            glm::mat4 pivotCopy = pivotMatrix;

            ImGuizmo::BeginFrame();
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(panelPos.x, panelPos.y, panelSize.x, panelSize.y);

            ImGuizmo::Manipulate(
                glm::value_ptr(view), glm::value_ptr(proj),
                manager->manipulatorType, manager->manipulatorMode,
                glm::value_ptr(pivotCopy));

            bool isUsingNow = ImGuizmo::IsUsing();

            // Snapshot start state when drag begins
            if (!wasGizmoUsing && isUsingNow)
                gizmoStartStates = manager->captureSelectedTransforms();

            if (isUsingNow)
            {
                glm::mat4 delta = pivotCopy * glm::inverse(pivotMatrix);

                for (kObject *obj : selObjs)
                {
                    if (selObjs.size() == 1)
                    {
                        // pivotMatrix started as this object's world matrix,
                        // so pivotCopy IS the new world matrix — use it directly.
                        glm::vec3 pos, scale, skew;
                        glm::quat rot;
                        glm::vec4 persp;
                        glm::decompose(pivotCopy, scale, rot, pos, skew, persp);
                        obj->setPosition(pos);
                        obj->setRotation(glm::normalize(rot));
                        obj->setScale(scale);
                    }
                    else if (manager->pivotMode == PivotMode::Individual)
                    {
                        // Each object around its own centre — extract pure deltas
                        // from the pivot matrices to avoid the T*R*T^-1 drift.
                        glm::vec3 dPos = glm::vec3(pivotCopy[3]) - glm::vec3(pivotMatrix[3]);

                        glm::vec3 sOld(glm::length(glm::vec3(pivotMatrix[0])),
                                       glm::length(glm::vec3(pivotMatrix[1])),
                                       glm::length(glm::vec3(pivotMatrix[2])));
                        glm::vec3 sNew(glm::length(glm::vec3(pivotCopy[0])),
                                       glm::length(glm::vec3(pivotCopy[1])),
                                       glm::length(glm::vec3(pivotCopy[2])));

                        glm::mat3 rOld(glm::vec3(pivotMatrix[0]) / sOld.x,
                                       glm::vec3(pivotMatrix[1]) / sOld.y,
                                       glm::vec3(pivotMatrix[2]) / sOld.z);
                        glm::mat3 rNew(glm::vec3(pivotCopy[0]) / sNew.x,
                                       glm::vec3(pivotCopy[1]) / sNew.y,
                                       glm::vec3(pivotCopy[2]) / sNew.z);
                        glm::quat dRot = glm::normalize(glm::quat_cast(rNew * glm::transpose(rOld)));

                        obj->setPosition(obj->getPosition() + dPos);
                        obj->setRotation(glm::normalize(dRot * obj->getRotation()));
                        obj->setScale(obj->getScale() * (sNew / sOld));
                    }
                    else
                    {
                        // Center / LastSelected: apply full delta to world matrix
                        glm::mat4 newWorld = delta * obj->getModelMatrixWorld();
                        glm::vec3 pos, scale, skew;
                        glm::quat rot;
                        glm::vec4 persp;
                        glm::decompose(newWorld, scale, rot, pos, skew, persp);
                        obj->setPosition(pos);
                        obj->setRotation(glm::normalize(rot));
                        obj->setScale(scale);
                    }
                }
            }

            // Push undo command when drag ends + mark the world dirty so
            // the window title gets its asterisk and Ctrl+S has something
            // to save.
            if (wasGizmoUsing && !isUsingNow)
            {
                auto after = manager->captureSelectedTransforms();
                manager->undoRedo.push(std::make_unique<TransformCommand>(
                    manager, gizmoStartStates, std::move(after)));
                manager->projectSaved = false;
                manager->refreshWindowTitle();
            }

            wasGizmoUsing = isUsingNow;
        }
    }

    // ------------------------------------------------------------------
    // Camera preview overlay — when the primary selection is a kCamera
    // (and not the editor camera), render its view via a dedicated
    // kOffscreenRenderer and composite the result at the bottom-right.
    // ------------------------------------------------------------------
    if (manager->selectedObject &&
        manager->selectedObject->getType() == NODE_TYPE_CAMERA &&
        manager->selectedObject != editorCamera)
    {
        kCamera *previewCam = static_cast<kCamera *>(manager->selectedObject);

        int targetW = std::max(96, std::min((int)(panelSize.x * 0.30f), 480));
        float aspect = previewCam->getAspectRatio();
        if (aspect <= 0.0f) aspect = 16.0f / 9.0f;
        int targetH = (int)((float)targetW / aspect);
        const int maxH = std::max(64, (int)(panelSize.y * 0.40f));
        if (targetH > maxH)
        {
            targetH = maxH;
            targetW = (int)((float)targetH * aspect);
        }

        if (targetW >= 64 && targetH >= 36 &&
            targetW < (int)panelSize.x && targetH < (int)panelSize.y)
        {
            if (!cameraPreview)
                cameraPreview = new kOffscreenRenderer(targetW, targetH);
            if (cameraPreview->getWidth()  != targetW ||
                cameraPreview->getHeight() != targetH)
                cameraPreview->resize(targetW, targetH);

            // Render at the preview rect's aspect, not whatever was left on
            // the camera by the game viewport (avoids stretched output).
            float savedAspect = previewCam->getAspectRatio();
            previewCam->setAspectRatio((float)targetW / (float)targetH);
            cameraPreview->render(manager->getWorld(), manager->getScene(), previewCam);
            previewCam->setAspectRatio(savedAspect);

            const float margin = 12.0f;
            ImVec2 a((float)panelPos.x + (float)panelSize.x - (float)targetW - margin,
                     (float)panelPos.y + (float)panelSize.y - (float)targetH - margin);
            ImVec2 b(a.x + (float)targetW, a.y + (float)targetH);

            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImTextureRef previewTex((ImTextureID)(uintptr_t)cameraPreview->getTexture());
            // Same vertical flip as the main world image — kOffscreenRenderer
            // writes top-down GL convention.
            dl->AddImage(previewTex, a, b, ImVec2(0, 1), ImVec2(1, 0));
            dl->AddRect(a, b, IM_COL32(220, 220, 230, 200), 2.0f, 0, 1.5f);

            // Label strip across the top so the user knows which camera.
            std::string label = previewCam->getName();
            if (label.empty()) label = "Camera";
            ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            ImVec2 labelMin(a.x, a.y - ts.y - 4.0f);
            ImVec2 labelMax(a.x + ts.x + 8.0f, a.y);
            dl->AddRectFilled(labelMin, labelMax, IM_COL32(0, 0, 0, 160));
            dl->AddText(ImVec2(a.x + 4.0f, a.y - ts.y - 2.0f),
                        IM_COL32(235, 235, 240, 230), label.c_str());
        }
    }

    gui->windowEnd();
    gui->endDisabled();
}
