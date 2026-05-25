#include "panel_game.h"
#include <algorithm>

PanelGame::PanelGame(kGuiManager* setGui, Manager* setManager)
    : gui(setGui), manager(setManager)
{
}

PanelGame::~PanelGame()
{
    delete gameRenderer;
}

// ---------------------------------------------------------------------------
// Camera helpers
// ---------------------------------------------------------------------------

kCamera* PanelGame::findGameCamera() const
{
    kWorld* world = manager->getWorld();
    if (!world) return nullptr;

    const auto& cams = world->getCameras();

    // Prefer the explicitly-set default — but verify it is still registered in
    // the world and is not the editor camera (handles deletion and edge cases).
    if (manager->defaultGameCamera &&
        manager->defaultGameCamera != manager->editorCamera &&
        std::find(cams.begin(), cams.end(), manager->defaultGameCamera) != cams.end())
    {
        return manager->defaultGameCamera;
    }

    // Fall back to the first non-editor camera registered in the world.
    for (kCamera* cam : cams)
    {
        if (cam != manager->editorCamera)
            return cam;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Simulation deltaTime
// ---------------------------------------------------------------------------

float PanelGame::getEffectiveDeltaTime(float dt) const
{
    return (playState == GamePlayState::Paused) ? 0.0f : dt;
}

// ---------------------------------------------------------------------------
// Scene snapshot helpers
// ---------------------------------------------------------------------------

void PanelGame::captureNodeRecursive(kObject* node)
{
    if (!node) return;
    ObjectTransformSnapshot snap;
    snap.uuid   = node->getUuid();
    snap.pos    = node->getPosition();
    snap.rot    = node->getRotation();
    snap.scale  = node->getScale();
    snap.active = node->getActive();
    sceneSnapshot.push_back(snap);
    for (kObject* child : node->getChildren())
        captureNodeRecursive(child);
}

void PanelGame::captureSnapshot()
{
    sceneSnapshot.clear();
    kScene* scene = manager->getScene();
    if (scene)
        captureNodeRecursive(scene->getRootNode());
}

void PanelGame::restoreSnapshot()
{
    for (const auto& snap : sceneSnapshot)
    {
        kObject* obj = manager->findObjectByUuid(snap.uuid);
        if (obj)
        {
            obj->setPosition(snap.pos);
            obj->setRotation(snap.rot);
            obj->setScale(snap.scale);
            obj->setActive(snap.active);
        }
    }
    sceneSnapshot.clear();
}

// ---------------------------------------------------------------------------
// Play state transitions
// ---------------------------------------------------------------------------

void PanelGame::pressPlay()
{
    if (playState == GamePlayState::Stopped)
    {
        kWorld* world = manager->getWorld();

        // Clear defaultGameCamera if it is the editor camera or has been deleted
        // from the world (stale pointer), so the auto-pick below can refresh it.
        if (manager->defaultGameCamera && world)
        {
            const auto& cams = world->getCameras();
            bool isEditorCam = (manager->defaultGameCamera == manager->editorCamera);
            bool isStale     = (std::find(cams.begin(), cams.end(),
                                          manager->defaultGameCamera) == cams.end());
            if (isEditorCam || isStale)
                manager->defaultGameCamera = nullptr;
        }

        // Auto-pick the first non-editor camera as default if none is set.
        if (!manager->defaultGameCamera && world)
        {
            for (kCamera* cam : world->getCameras())
            {
                if (cam != manager->editorCamera)
                {
                    manager->defaultGameCamera = cam;
                    break;
                }
            }
        }

        captureSnapshot();
        // Spawn physics bodies for every object that opted in. Must happen
        // AFTER captureSnapshot so the snapshot records the editor-authored
        // transforms (not whatever physics moves them to during update).
        manager->startPhysicsSimulation();
        // Compile attached scripts to bytecode and dispatch Awake()/Start().
        manager->startScripts();
        playState = GamePlayState::Playing;
    }
    else if (playState == GamePlayState::Paused)
    {
        playState = GamePlayState::Playing;
    }
}

void PanelGame::pressPause()
{
    if (playState == GamePlayState::Playing)
        playState = GamePlayState::Paused;
}

void PanelGame::pressStop()
{
    if (playState != GamePlayState::Stopped)
    {
        // Tear down physics BEFORE restoring transforms so the bodies don't
        // overwrite our restored positions on a final sync.
        manager->stopPhysicsSimulation();
        // Dispatch OnDestroy() and release every script instance.
        manager->stopScripts();
        restoreSnapshot();
        playState = GamePlayState::Stopped;
    }
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void PanelGame::draw(bool& isOpened)
{
    if (!isOpened) return;

    bool enabled   = manager->projectOpened;
    bool isStopped = (playState == GamePlayState::Stopped);
    bool isPlaying = (playState == GamePlayState::Playing);
    bool isPaused  = (playState == GamePlayState::Paused);

    gui->beginDisabled(!enabled);
    gui->windowStart("Game");

    gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(4, 2));

    // ---- Play button -------------------------------------------------------
    if (isPlaying)
    {
        gui->pushStyleColor(ImGuiCol_Button,        kVec4(0.26f, 0.59f, 0.98f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
    }
    if (gui->button("Play", kIvec2(54, 22)) && !isPlaying)
        pressPlay();
    if (isPlaying)
        gui->popStyleColor(2);
    if (gui->isItemHovered())
        gui->setItemTooltip(isPaused ? "Resume" : "Play");

    gui->sameLine();

    // ---- Pause button ------------------------------------------------------
    if (isPaused)
    {
        gui->pushStyleColor(ImGuiCol_Button,        kVec4(0.85f, 0.65f, 0.10f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.95f, 0.75f, 0.20f, 1.00f));
    }
    gui->beginDisabled(isStopped);
    if (gui->button("Pause", kIvec2(54, 22)))
    {
        if (isPlaying)       pressPause();
        else if (isPaused)   pressPlay(); // resume
    }
    gui->endDisabled();
    if (isPaused)
        gui->popStyleColor(2);
    if (gui->isItemHovered())
        gui->setItemTooltip(isPaused ? "Resume" : "Pause");

    gui->sameLine();

    // ---- Stop button -------------------------------------------------------
    gui->beginDisabled(isStopped);
    if (!isStopped)
    {
        gui->pushStyleColor(ImGuiCol_Button,        kVec4(0.72f, 0.16f, 0.16f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.88f, 0.26f, 0.26f, 1.00f));
    }
    if (gui->button("Stop", kIvec2(54, 22)))
        pressStop();
    if (!isStopped)
        gui->popStyleColor(2);
    gui->endDisabled();
    if (gui->isItemHovered())
        gui->setItemTooltip("Stop and reset scene");

    // ---- Status text -------------------------------------------------------
    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    if (isPlaying)
        gui->textColored(kVec4(0.35f, 0.90f, 0.35f, 1.0f), "Playing");
    else if (isPaused)
        gui->textColored(kVec4(1.00f, 0.80f, 0.20f, 1.0f), "Paused");
    else
        gui->textDisabled("Stopped");

    gui->popStyleVar();
    gui->separator();

    // ---- Game viewport -----------------------------------------------------
    kVec2 avail = gui->getContentRegionAvail();
    if (avail.x > 0 && avail.y > 0)
    {
        int newW = (int)avail.x;
        int newH = (int)avail.y;

        // Create or resize the offscreen renderer to match this panel
        if (!gameRenderer)
        {
            gameRenderer  = new kOffscreenRenderer(newW, newH);
            gameRenderer->setBackgroundColor(kVec4(0.0f, 0.0f, 0.0f, 1.0f));
            lastRendererW = newW;
            lastRendererH = newH;
        }
        else if (newW != lastRendererW || newH != lastRendererH)
        {
            gameRenderer->resize(newW, newH);
            lastRendererW = newW;
            lastRendererH = newH;
        }

        kCamera* gameCamera = findGameCamera();

        // Find the scene this camera is assigned to, falling back to manager->getScene()
        kScene* gameScene = nullptr;
        if (gameCamera && !gameCamera->getSceneUuid().empty())
        {
            kWorld *world = manager->getWorld();
            if (world)
            {
                for (kScene *s : world->getScenes())
                {
                    if (s->getUuid() == gameCamera->getSceneUuid())
                    {
                        gameScene = s;
                        break;
                    }
                }
            }
        }
        if (!gameScene)
            gameScene = manager->getScene();

        if (gameCamera && gameScene)
        {
            // Keep camera aspect ratio in sync with the panel
            gameCamera->setAspectRatio((float)newW / (float)newH);

            // Render scene only — no editor overlay, no outlines, no debug shapes
            gameRenderer->render(manager->getWorld(), gameScene, gameCamera);

            ImTextureRef tex((ImTextureID)(uintptr_t)gameRenderer->getTexture());
            gui->setNextItemAllowOverlap();
            ImGui::Image(tex, ImVec2((float)newW, (float)newH), ImVec2(0, 1), ImVec2(1, 0));
        }
        else
        {
            // No game camera → black screen with centered message
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                pos, ImVec2(pos.x + (float)newW, pos.y + (float)newH),
                IM_COL32(0, 0, 0, 255));

            const char* msg = "No Camera";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(pos.x + ((float)newW - ts.x) * 0.5f,
                       pos.y + ((float)newH - ts.y) * 0.5f),
                IM_COL32(150, 150, 150, 255), msg);

            ImGui::Dummy(ImVec2((float)newW, (float)newH));
        }
    }

    gui->windowEnd();
    gui->endDisabled();
}
