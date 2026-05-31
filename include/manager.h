#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <string>
#include <vector>
#include <map>
#include <random>
#include <iostream>
#include <filesystem>
#include <fstream>

#include <sstream>
#include <iomanip>

#include <kemena/kwindow.h>
#include <kemena/kworld.h>
#include <kemena/krenderer.h>
#include <kemena/kscene.h>
#include <kemena/kmeshgenerator.h>
#include <kemena/klight.h>
#include <kemena/kcamera.h>
#include <kemena/kassetmanager.h>
#include <kemena/koffscreenrenderer.h>
#include <kemena/kprefab.h>

#include "commands.h"
#include <portable-file-dialogs.h>
#include <kemena/kguimanager.h>
#include <ImGuizmo.h>

#include "panel_project.h"
#include "panel_hierarchy.h"
#include "panel_console.h"
#include "panel_game.h"

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include <mach-o/dyld.h>
#elif __linux__
#include <unistd.h>
#include <limits.h>
#endif

using namespace kemena;
namespace fs = std::filesystem;
using json = nlohmann::json;

// Live state shared between PanelShaderEditor and PanelInspector
struct ShaderPreviewState
{
    bool        active     = false;
    std::string uuid;
    std::string glslSource;   // compiled GLSL; empty until first compile
    std::string shaderType;   // "Flat", "Phong", "PBR"
    std::string shaderName;   // display name
};

// For project panel
struct FileInfo
{
    kString path; // path/name.ext
    kString checksum;
    kString type; // model, image, prefab, etc.
};

// For converting meshes or images
struct ImportTask
{
    fs::path inputPath;
    fs::path outputPath;
    kString  type;    // mesh, image, etc.
    kString  uuid;
    fs::path thumbnailPath; // where to save uuid.png after import
    bool success  = false;
    bool reported = false;
};

// Queued thumbnail render (processed on main thread after batch import)
struct ThumbnailTask
{
    kString  uuid;
    fs::path srcPath;       // GLB path for mesh, original image path for image
    fs::path thumbnailPath;
    kString  type;          // "mesh" or "image"
};

// For hierarchy panel
struct ObjectInfo
{
    kObject *object; // Pointer to the object
};

class PanelProject;
class PanelHierarchy;
class PanelConsole;
class PanelGame;

class Manager
{
public:
    Manager(kWindow *setWindow, kWorld *setWorld, kRenderer *setRenderer);
    virtual ~Manager();

    void setScene(kScene *s)  { scene = s; }
    kScene    *getScene()     { return scene; }
    kRenderer *getRenderer()  { return renderer; }

    kObject *findObjectByUuid(const kString &uuid);
    void deleteSelectedObjects();

    /**
     * @brief Duplicates every selected object as a new top-level node in the
     *        active scene. Sub-trees are cloned recursively; every uuid in the
     *        copied JSON is regenerated so duplicates and originals stay
     *        independent. New objects become the active selection and each one
     *        gets its own InstantiateCommand so it round-trips through undo.
     */
    void duplicateSelectedObjects();
    std::vector<TransformState> captureSelectedTransforms();

    // --- Accessors ----------------------------------------------------------
    kWindow       *getWindow()       { return window; }
    kWorld        *getWorld()        { return world; }
    kAssetManager *getAssetManager() { return world ? world->getAssetManager() : nullptr; }

    // --- Edit actions -------------------------------------------------------
    void selectAll();
    void deselectAll();
    void invertSelection();

    // --- Object creation ----------------------------------------------------
    void createSceneObject();
    void createEmpty();
    void createMeshPrimitive(kMesh *mesh, const kString &name);
    void createMeshFromFile();
    void createLight(kLightType type);
    void createCamera();
    void createNavMesh();

    // --- Game export --------------------------------------------------------

    /** @brief Settings for building a distributable game from the project. */
    struct ExportSettings
    {
        int          platform   = 0;        ///< 0=Windows, 1=Linux, 2=macOS.
        std::string  gameName   = "Game";   ///< Output executable name.
        std::string  title      = "My Game";///< Window title (game.config).
        int          width      = 1280;
        int          height     = 720;
        bool         fullscreen = false;
        std::string  outputDir;             ///< Destination folder for the build.
        std::string  templateDir;           ///< Folder holding the prebuilt runtime template.
        std::string  iconPath;              ///< Optional .ico for the Windows exe.
    };

    ExportSettings exportSettings;       ///< Persisted between dialog opens.
    bool           showExportDialog = false;

    /**
     * @brief Builds a distributable game: copies the runtime template, bundles
     *        the project data (scene.world, imported assets, shader + script
     *        bytecode), writes game.config, and applies per-OS metadata.
     * @return true on success.
     */
    bool exportGame(const ExportSettings &settings);

    // --- Navigation baking --------------------------------------------------

    /** @brief Bakes @p navObj's navigation surface from the scene's static
     *         meshes — all of them, or only those overlapping its area box. */
    void bakeNavMesh(kObject *navObj);

    /** @brief Discards the baked nav mesh for @p navObj. */
    void clearNavMesh(kObject *navObj);

    /** @brief Frees every baked nav mesh (called when a world is (re)loaded). */
    void clearAllNavMeshes();

    /** @brief Returns true if @p navObj currently has a baked nav mesh. */
    bool isNavMeshBaked(kObject *navObj) const;

    /** @brief Returns the baked nav mesh for @p navObj, or nullptr. */
    kNavMesh *getBakedNavMesh(kObject *navObj) const;

    // --- Asset creation -----------------------------------------------------
    void createNewMaterial();
    void createNewFolder();
    void createNewShader();
    void createNewScript();
    void createNewLogicGraph();
    void deleteAssets(const std::vector<fs::path> &paths);

    /** @brief Renames a project asset (file or folder) in place.
     *  @return true on success; false if the target name already exists. */
    bool renameAsset(const fs::path &oldPath, const kString &newName);

    /** @brief Moves a project asset into another folder, preserving its filename
     *         (and therefore its embedded UUID).
     *  @return true on success; false if the destination already has a file
     *          with the same name, the source is missing, or @p destDir is not
     *          a directory. */
    bool moveAsset(const fs::path &srcPath, const fs::path &destDir);

    kString getCurrentDirPath();

    void openFolder(kString name);
    void closeFolder();

    bool newProject();
    bool openProject();
    bool openProjectFromPath(const kString& path);

    // Recent projects (persisted to <exeDir>/recent_projects.json)
    std::vector<kString> recentProjects;
    void loadRecentProjects();
    void saveRecentProjects();
    void addRecentProject(const kString& path);

    // Per-project config (persisted to <projectPath>/Config/project.json).
    // Holds "last_world" — the relative path of the world file that was
    // open the last time the project was saved, so reopening the project
    // can auto-load it.
    void saveProjectConfig();
    fs::path loadLastWorldPath() const; ///< Absolute path or empty when none recorded.

    void checkAssetChange();

    void refreshWindowTitle();
    void closeEditor();

    void clearWorld(bool forced = false);
    void deleteObjectRecursive(kObject *node);

    /**
     * @brief Saves the current world.
     *
     * If the world is untitled — or its current path lives outside the
     * project's Assets folder — opens a file dialog (defaulted to
     * @c <project>/Assets) and asks the user where to put it. Saves outside
     * Assets are refused with a message box.
     */
    void saveWorld();

    /** @brief Save-As with an explicit destination path. Rejects paths
     *         outside @c <project>/Assets. */
    void saveWorldAs(const kString &path);

    /** @brief Save-As that always prompts for a destination via dialog,
     *         constrained to @c <project>/Assets. */
    void saveWorldAs();

    /** @brief Opens the save dialog (defaulted to Assets) and validates the
     *         chosen path. Returns the resolved absolute path with @c .world
     *         appended if missing, or empty on cancel / invalid pick. */
    kString promptWorldSavePath();

    /** @brief Applies the bundled SHADER_SKYBOX + TEXTURE_SKYBOX_* cubemap
     *         to the given scene. Used at startup and re-fired by the
     *         "Apply Default Skybox" button on the Scene inspector — also
     *         called automatically by main.cpp when the active scene pointer
     *         changes (e.g. after opening a project) so the editor view
     *         doesn't end up sky-less. */
    void applyDefaultSkybox(kScene *target);

    void loadWorld(const kString &path);

    /**
     * @brief Populates @p target with the contents of the embedded WORLD_DEFAULT
     *        RCDATA resource (cube + sun light + game camera + ambient settings).
     *
     * Used at editor startup so the default placeholder scene comes from a real
     * .world file shipped as RCDATA, rather than hand-coded object creation in
     * main.cpp. The target scene is NOT cleared — caller is responsible for
     * ensuring it's empty (or fine to merge into).
     */
    void loadDefaultWorldInto(kScene *target);

    // --- Prefabs ------------------------------------------------------------

    /**
     * @brief Saves the currently-selected object subtree as a new .prefab asset
     *        in the current project folder.
     *
     * @param prefabName Display name and base filename for the new prefab.
     * @return true on success.
     */
    bool saveSelectedAsPrefab(const kString &prefabName);

    /**
     * @brief Loads a .prefab file and instantiates it as a new top-level object
     *        in the current scene, with fresh per-node UUIDs.
     *
     * @param prefabPath Filesystem path of the .prefab file to load.
     * @return Pointer to the instance root, or nullptr on failure.
     */
    kObject *instantiatePrefabInScene(const fs::path &prefabPath);

    /**
     * @brief Opens the prefab editor for the given .prefab file. The main world
     *        view is hidden while the prefab editor is active.
     */
    void editPrefab(const fs::path &prefabPath);

    /**
     * @brief Closes the prefab editor. If @p saveChanges is true, serializes
     *        the editor scene back into the .prefab file.
     */
    void closePrefabEditor(bool saveChanges);

    bool prefabEditing = false;        ///< True while the prefab editor panel is active.
    fs::path editingPrefabPath;        ///< Path to the .prefab file being edited.
    kPrefab editingPrefab;             ///< Loaded prefab data while editing.
    kScene *prefabScene = nullptr;     ///< Isolated scene used by the prefab editor.
    kCamera *prefabCamera = nullptr;   ///< Editor camera for the prefab scene.
    kObject *prefabRoot = nullptr;     ///< Root instance of the prefab inside prefabScene.

    /// Dedicated renderer for the prefab editor so it never shares the World
    /// panel's render target. Background is dark grey.
    kOffscreenRenderer prefabRenderer{512, 512};

    // --- Drag-and-drop helpers ----------------------------------------------

    /** @brief Spawns a project-asset (mesh/prefab/audio) into the current scene. */
    kObject *instantiateAssetFromUuid(const kString &assetUuid, const kVec3 &positionHint = kVec3(0));

    /** @brief Locates the on-disk path of a project asset by its UUID. */
    fs::path findAssetPathByUuid(const kString &assetUuid);

    /** @brief Reparents @p uuid under @p newParentUuid. Empty newParentUuid = scene root. */
    void reparentObject(const kString &uuid, const kString &newParentUuid);

    /** @brief Reorders an object within its parent's children list, before @p siblingUuid. */
    void reorderBefore(const kString &uuid, const kString &siblingUuid);

    /** @brief Returns the prefab-instance root that owns @p obj, or nullptr if @p obj is not in a prefab. */
    kObject *findPrefabInstanceRoot(kObject *obj);

    /** @brief Creates a .prefab from the current selection, plus an instance link in the scene. */
    bool createPrefabFromSelection();

    /** @brief Pushes the in-scene state of @p instanceRoot back into its source .prefab template. */
    bool applyPrefabInstance(kObject *instanceRoot);

    /**
     * @brief Replaces every prefab instance of @p prefabUuid in every scene
     *        with a fresh instantiation of the (possibly-just-rewritten)
     *        .prefab template. World transform and parent of each instance
     *        are preserved; per-instance edits are discarded.
     */
    void refreshAllPrefabInstances(const kString &prefabUuid);

    /** @brief Clears prefab linkage from @p instanceRoot and all descendants. */
    void unpackPrefabInstance(kObject *instanceRoot);

    /** @brief Applies a material asset (.mat) to @p obj. */
    bool applyMaterialToObject(kObject *obj, const fs::path &materialPath);

    // --- Material drag-preview state ----------------------------------------
    // While a .mat payload is hovering over a scene object, we swap the material
    // for live preview. If the drop is committed we keep it (with undo); if the
    // user drags away or cancels we restore the original.
    kObject   *matPreviewObject       = nullptr;
    kMaterial *matPreviewOriginal     = nullptr;
    kString    matPreviewSourceUuid;

    // --- Drag-hover highlight ----------------------------------------------
    // While any drag-and-drop is over the World viewport, the panel records
    // the picked object here so the main render loop can paint an outline on
    // it, telling the user which object their cursor is over.
    kString    dragHoverObjectUuid;

    // --- Physics simulation -------------------------------------------------
    //
    // The physics manager is created lazily on the first Play and kept alive
    // afterwards so subsequent Play/Stop cycles reuse the same Jolt world.
    // Physics ONLY runs while PanelGame is in the Playing state — never
    // during normal editor work or while editing a prefab.

    /** @brief Walks the active scene, builds physics bodies for every object
     *         that has a physics descriptor, and attaches them via kObject::attachPhysics. */
    void startPhysicsSimulation();

    /** @brief Destroys all physics bodies built by startPhysicsSimulation and
     *         detaches them from their kObjects. */
    void stopPhysicsSimulation();

    /** @brief Steps the physics simulation by @p dt seconds and syncs
     *         transforms back into the scene-graph nodes. */
    void stepPhysics(float dt);

    kPhysicsManager        *physicsManager = nullptr;
    std::vector<kObject *>  physicsBodies;   // objects with live physics this Play session
    std::vector<kObject *>  characterBodies; // objects with a live character controller this session

    // Baked navigation meshes keyed by the owning object's UUID. Editor-owned;
    // regenerated via Bake and never serialised.
    std::map<kString, kNavMesh *> bakedNavMeshes;

    // --- Scripting --------------------------------------------------------
    // Attached AngelScript files are compiled to bytecode under
    // Library/Scripts/<scriptUuid>.kbc. Play loads that bytecode; the source
    // .as file stays editable and is only recompiled when it changes.

    /** @brief Compiles every attached script to Library/Scripts bytecode.
     *         Skips scripts whose source checksum is unchanged. */
    void buildScripts();

    /** @brief Builds bytecode then starts script lifecycle dispatch (on Play). */
    void startScripts();

    /** @brief Stops script lifecycle dispatch and releases instances (on Stop). */
    void stopScripts();

    /** @brief File-watch tick: periodically recompiles changed script sources
     *         while the editor is idle. Call once per frame. */
    void pollScriptChanges(float dt);

    float scriptWatchTimer = 0.0f;   ///< Accumulator for the script file-watch poll.

    // Editor path and directory
    fs::path exePath;
    fs::path baseDir;

    // Project info
    kString projectName;
    bool projectOpened = false;
    bool projectSaved = true;

    // Project path and directory
    fs::path projectPath;
    std::vector<kString> currentDir;

    // World info
    kString worldName = "";
    fs::path worldPath;
    // kString worldUuid = "";

    PanelProject   *panelProject   = nullptr;
    PanelHierarchy *panelHierarchy = nullptr;
    PanelGame      *panelGame      = nullptr;

    // Camera used by the editor viewport (excluded from game camera candidates)
    kCamera *editorCamera      = nullptr;
    // Explicitly-chosen default camera for the Game panel (nullptr = auto-pick)
    kCamera *defaultGameCamera = nullptr;

    std::vector<ImportTask> importQueue;
    std::future<void> importFuture;
    std::atomic<int> filesProcessed{0};
    std::atomic<bool> batchDone{false};
    std::mutex queueMutex;

    // Prevents asset checks from running while a message box is open, even if the application remains focused
    bool showingMessageBox = false;

    // For batch import
    bool showImportPopup = false;
    std::chrono::steady_clock::time_point importEndTime;

    std::vector<ImportTask>   importTasks;
    std::vector<ThumbnailTask> thumbnailQueue;
    kOffscreenRenderer        thumbnailRenderer{128, 128};

    void drawImportPopup(PanelConsole *console);
    void processThumbnailQueue(PanelConsole *console);

    // Check project files
    std::unordered_map<kString, FileInfo> fileMap; // Key = uuid
    std::unordered_map<kString, kString> uuidMap;  // Reverse lookup, key = filename

    // Check world objects
    std::unordered_map<kString, ObjectInfo> objectMap; // Key = uuid

    std::vector<kString> selectedObjects;
    void selectObject(const kString uuid, bool clearList = false);
    void deselectObject(const kString uuid);

    kObject *selectedObject = nullptr;
    kScene  *selectedScene  = nullptr;
    bool     worldSelected  = false;

    ImGuizmo::OPERATION manipulatorType = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE manipulatorMode = ImGuizmo::LOCAL;

    UndoRedoManager undoRedo;
    PivotMode pivotMode = PivotMode::LastSelected;

    ShaderPreviewState shaderPreview;

private:
    kWindow   *window;
    kWorld    *world;
    kRenderer     *renderer;
    kScene    *scene = nullptr;
    kString    initialWindowTitle;

    // int initialResizeCount = 0;

    // std::map<kString, kString> fileDirty;   // Files that need to be put into fileGUID, or refresh checksum into fileMD5, or regenerate thumbnail etc.
    // kString latestFileUuid = "";

    kString checkAssetType(const fs::path &p);
    void startBatchImport(const std::vector<ImportTask> &tasks);
};

#endif // FILEMANAGER_H
