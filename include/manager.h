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

/**
 * @brief Live shader-preview state shared between PanelShaderEditor and PanelInspector.
 *
 * Holds the most recently compiled shader so the inspector can show a preview
 * that tracks edits made in the shader editor.
 */
struct ShaderPreviewState
{
    bool        active     = false;  ///< True while a shader is being previewed.
    std::string uuid;                ///< UUID of the shader asset being previewed.
    std::string glslSource;          ///< Compiled GLSL; empty until first compile.
    std::string shaderType;          ///< "Flat", "Phong", "PBR".
    std::string shaderName;          ///< Display name.
};

/**
 * @brief Describes a single project asset file tracked by the project panel.
 */
struct FileInfo
{
    kString path;     ///< Relative path/name.ext of the asset.
    kString checksum; ///< Content checksum used to detect changes.
    kString type;     ///< Asset kind: model, image, prefab, etc.
};

/**
 * @brief A queued asset conversion/import job (mesh or image).
 *
 * Produced by the project panel and processed on a background import thread.
 */
struct ImportTask
{
    fs::path inputPath;     ///< Source file selected for import.
    fs::path outputPath;    ///< Converted output written into the project.
    kString  type;          ///< Asset kind: mesh, image, etc.
    kString  uuid;          ///< UUID assigned to the imported asset.
    fs::path thumbnailPath; ///< Where to save uuid.png after import.
    bool success  = false;  ///< Set true once the conversion succeeds.
    bool reported = false;  ///< True once the result has been logged to the console.
};

/**
 * @brief A queued thumbnail render, processed on the main thread after batch import.
 *
 * Thumbnail rendering needs the GL context, so it cannot run on the import
 * worker thread; tasks are drained on the main thread instead.
 */
struct ThumbnailTask
{
    kString  uuid;          ///< Asset UUID the thumbnail belongs to.
    fs::path srcPath;       ///< GLB path for mesh, original image path for image.
    fs::path thumbnailPath; ///< Destination thumbnail image path.
    kString  type;          ///< "mesh" or "image".
};

/**
 * @brief Lightweight reference to a scene object, used by the hierarchy panel.
 */
struct ObjectInfo
{
    kObject *object; ///< Pointer to the referenced scene object.
};

class PanelProject;
class PanelHierarchy;
class PanelConsole;
class PanelGame;

/**
 * @brief Central editor controller for a Kemena3D Studio project.
 *
 * Owns and coordinates the editor's project, world, scene and selection state.
 * Acts as the hub for project lifecycle (new/open/save), asset management and
 * import, object creation and editing, prefab authoring, material assignment,
 * navigation-mesh baking, physics simulation, scripting, and game export. The
 * UI panels delegate their actions here.
 */
class Manager
{
public:
    /**
     * @brief Constructs the manager and binds it to the editor's core systems.
     * @param setWindow   The application window.
     * @param setWorld    The world container holding scenes and the asset manager.
     * @param setRenderer The renderer used by the editor viewport.
     */
    Manager(kWindow *setWindow, kWorld *setWorld, kRenderer *setRenderer);

    /** @brief Destroys the manager and releases editor-owned resources. */
    virtual ~Manager();

    /** @brief Sets the active editor scene. */
    void setScene(kScene *s)  { scene = s; }

    /** @brief Returns the active editor scene. */
    kScene    *getScene()     { return scene; }

    /** @brief Returns the renderer used by the editor viewport. */
    kRenderer *getRenderer()  { return renderer; }

    /** @brief Finds a scene object by its UUID.
     *  @param uuid Object UUID to look up.
     *  @return The matching object, or nullptr if none. */
    kObject *findObjectByUuid(const kString &uuid);

    /** @brief Deletes all currently selected objects (undoable). */
    void deleteSelectedObjects();

    /**
     * @brief Duplicates every selected object as a new top-level node in the
     *        active scene. Sub-trees are cloned recursively; every uuid in the
     *        copied JSON is regenerated so duplicates and originals stay
     *        independent. New objects become the active selection and each one
     *        gets its own InstantiateCommand so it round-trips through undo.
     */
    void duplicateSelectedObjects();

    /** @brief Captures the current transforms of all selected objects.
     *  @return One TransformState per selected object (for undo snapshots). */
    std::vector<TransformState> captureSelectedTransforms();

    // --- Accessors ----------------------------------------------------------

    /** @brief Returns the application window. */
    kWindow       *getWindow()       { return window; }

    /** @brief Returns the world container. */
    kWorld        *getWorld()        { return world; }

    /** @brief Returns the world's asset manager, or nullptr if no world. */
    kAssetManager *getAssetManager() { return world ? world->getAssetManager() : nullptr; }

    // --- Edit actions -------------------------------------------------------

    /** @brief Selects every object in the active scene. */
    void selectAll();

    /** @brief Clears the current selection. */
    void deselectAll();

    /** @brief Inverts the current selection within the active scene. */
    void invertSelection();

    // --- Object creation ----------------------------------------------------

    /** @brief Creates a new empty scene as a child of the world. */
    void createSceneObject();

    /** @brief Creates a new empty object in the active scene. */
    void createEmpty();

    /** @brief Creates an object from a primitive mesh.
     *  @param mesh The primitive mesh to instance.
     *  @param name Display name for the new object. */
    void createMeshPrimitive(kMesh *mesh, const kString &name);

    /** @brief Prompts for a mesh file and creates an object from it. */
    void createMeshFromFile();

    /** @brief Creates a light object of the given type.
     *  @param type The light type (directional, point, spot, etc.). */
    void createLight(kLightType type);

    /** @brief Creates a camera object in the active scene. */
    void createCamera();

    /** @brief Creates a navigation-mesh object in the active scene. */
    void createNavMesh();

    // --- Game export --------------------------------------------------------

    /** @brief Settings for building a distributable game from the project. */
    struct ExportSettings
    {
        int          platform   = 0;        ///< 0=Windows, 1=Linux, 2=macOS.
        std::string  gameName   = "Game";   ///< Output executable name.
        std::string  title      = "My Game";///< Window title (game.config).
        int          width      = 1280;     ///< Initial window width.
        int          height     = 720;      ///< Initial window height.
        bool         fullscreen = false;    ///< Launch the game fullscreen.
        std::string  outputDir;             ///< Destination folder for the build.
        std::string  templateDir;           ///< Folder holding the prebuilt runtime template.
        std::string  iconPath;              ///< Optional .ico for the Windows exe.
    };

    ExportSettings exportSettings;       ///< Persisted between dialog opens.
    bool           showExportDialog = false; ///< Controls visibility of the export dialog.

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

    /** @brief Creates a new material (.mat) asset in the current folder. */
    void createNewMaterial();

    /** @brief Creates a new folder in the current project directory. */
    void createNewFolder();

    /** @brief Creates a new shader asset in the current folder. */
    void createNewShader();

    /** @brief Creates a new AngelScript (.as) script asset in the current folder. */
    void createNewScript();

    /** @brief Creates a new visual logic-graph asset in the current folder. */
    void createNewLogicGraph();

    /** @brief Deletes the given project assets from disk and tracking maps.
     *  @param paths Filesystem paths of the assets to delete. */
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

    /** @brief Returns the path of the currently open project folder. */
    kString getCurrentDirPath();

    /** @brief Navigates the project browser into the named subfolder.
     *  @param name Subfolder name to enter. */
    void openFolder(kString name);

    /** @brief Navigates the project browser up to the parent folder. */
    void closeFolder();

    /** @brief Prompts for a location and creates a new project.
     *  @return true if a project was created. */
    bool newProject();

    /** @brief Prompts for a project folder and opens it.
     *  @return true if a project was opened. */
    bool openProject();

    /** @brief Opens a project from an explicit path.
     *  @param path Project folder path.
     *  @return true if the project was opened. */
    bool openProjectFromPath(const kString& path);

    // Recent projects (persisted to <exeDir>/recent_projects.json)
    std::vector<kString> recentProjects; ///< Most-recently-opened project paths.

    /** @brief Loads the recent-projects list from disk. */
    void loadRecentProjects();

    /** @brief Persists the recent-projects list to disk. */
    void saveRecentProjects();

    /** @brief Adds a project path to the recent list (de-duplicated, moved to front).
     *  @param path Project path to record. */
    void addRecentProject(const kString& path);

    // Per-project config (persisted to <projectPath>/Config/project.json).
    // Holds "last_world" — the relative path of the world file that was
    // open the last time the project was saved, so reopening the project
    // can auto-load it.
    /** @brief Writes the per-project config (including "last_world") to disk. */
    void saveProjectConfig();

    /** @brief Reads the last-open world path from the project config.
     *  @return Absolute path or empty when none recorded. */
    fs::path loadLastWorldPath() const;

    /** @brief Rescans project assets and updates tracking maps for changes. */
    void checkAssetChange();

    /** @brief Updates the window title to reflect project/world/dirty state. */
    void refreshWindowTitle();

    /** @brief Closes the editor, prompting to save unsaved changes if needed. */
    void closeEditor();

    /** @brief Clears the current world's scenes and objects.
     *  @param forced When true, skips the unsaved-changes prompt. */
    void clearWorld(bool forced = false);

    /** @brief Recursively deletes an object and all its descendants.
     *  @param node Root of the subtree to delete. */
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

    /** @brief Loads a world from disk into the editor, replacing the current one.
     *  @param path Filesystem path of the .world file to load. */
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

    /**
     * @brief Applies a material asset (.mat) to @p obj.
     * @param materialUuid Source asset UUID, recorded on the object so the
     *        assignment is saved/loaded. Pass "" to clear the stored UUID.
     */
    bool applyMaterialToObject(kObject *obj, const fs::path &materialPath,
                               const kString &materialUuid = "");

    /**
     * @brief Resets @p obj to a fresh default material and clears its material UUID.
     *
     * Used when the inspector's material picker is set to "None" so the object
     * visibly reverts to the engine default instead of keeping the last applied
     * material.
     */
    void applyDefaultMaterialToObject(kObject *obj);

    /**
     * @brief Rebuilds runtime materials from each object's stored material UUID.
     *
     * Walks every game scene after a world load and re-applies the .mat asset
     * referenced by each object's materialUuid (resolved via fileMap). Objects
     * whose material asset is missing keep their reference but fall back to the
     * default material.
     */
    void reapplyStoredMaterials();

    // --- Material drag-preview state ----------------------------------------
    // While a .mat payload is hovering over a scene object, we swap the material
    // for live preview. If the drop is committed we keep it (with undo); if the
    // user drags away or cancels we restore the original.
    kObject   *matPreviewObject       = nullptr;
    kMaterial *matPreviewOriginal     = nullptr;
    kString    matPreviewOriginalUuid; ///< Object's material UUID before preview.
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

    kPhysicsManager        *physicsManager = nullptr; ///< Lazily-created Jolt physics world, reused across Play cycles.
    std::vector<kObject *>  physicsBodies;   ///< Objects with live physics this Play session.
    std::vector<kObject *>  characterBodies; ///< Objects with a live character controller this session.

    /// Baked navigation meshes keyed by the owning object's UUID. Editor-owned;
    /// regenerated via Bake and never serialised.
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
    fs::path exePath;  ///< Absolute path of the editor executable.
    fs::path baseDir;  ///< Directory containing the editor executable.

    // Project info
    kString projectName;          ///< Display name of the open project.
    bool projectOpened = false;   ///< True while a project is open.
    bool projectSaved = true;     ///< False when the project has unsaved changes.

    // Project path and directory
    fs::path projectPath;            ///< Root folder of the open project.
    std::vector<kString> currentDir; ///< Project-browser path components from the project root.

    // World info
    kString worldName = "";  ///< Name of the currently open world.
    fs::path worldPath;      ///< Filesystem path of the currently open world.
    // kString worldUuid = "";

    PanelProject   *panelProject   = nullptr; ///< Project (asset browser) panel.
    PanelHierarchy *panelHierarchy = nullptr; ///< Scene hierarchy panel.
    PanelGame      *panelGame      = nullptr; ///< Game/play panel.

    /// Camera used by the editor viewport (excluded from game camera candidates).
    kCamera *editorCamera      = nullptr;
    /// Explicitly-chosen default camera for the Game panel (nullptr = auto-pick).
    kCamera *defaultGameCamera = nullptr;

    // Editor-camera orbit state. main.cpp owns the live values and mirrors them
    // here each frame so saveWorld can persist them; loadWorld writes the
    // restored values back and raises editorCamLoadPending so main.cpp adopts
    // them (its locals are otherwise authoritative).
    kVec3 editorCamOrbitPivot    = kVec3(0.0f, 3.5f, 0.0f); ///< Orbit pivot of the editor camera.
    float editorCamOrbitDistance = 14.0f;  ///< Distance of the editor camera from its pivot.
    bool  editorCamLoadPending   = false;  ///< Set by loadWorld so main.cpp adopts restored camera values.

    std::vector<ImportTask> importQueue;   ///< Pending import jobs awaiting batch processing.
    std::future<void> importFuture;        ///< Handle for the background import task.
    std::atomic<int> filesProcessed{0};    ///< Count of files processed by the running batch import.
    std::atomic<bool> batchDone{false};    ///< Set true when the background batch import finishes.
    std::mutex queueMutex;                 ///< Guards shared import state across threads.

    /// Prevents asset checks from running while a message box is open, even if the application remains focused.
    bool showingMessageBox = false;

    // For batch import
    bool showImportPopup = false;                       ///< Controls the import-progress popup visibility.
    std::chrono::steady_clock::time_point importEndTime; ///< Time the batch import completed (for popup auto-dismiss).

    std::vector<ImportTask>   importTasks;     ///< Tasks for the currently running batch import.
    std::vector<ThumbnailTask> thumbnailQueue; ///< Thumbnails awaiting main-thread rendering.
    kOffscreenRenderer        thumbnailRenderer{128, 128}; ///< Offscreen renderer used to generate asset thumbnails.

    /** @brief Draws the batch-import progress popup.
     *  @param console Console panel to log results into. */
    void drawImportPopup(PanelConsole *console);

    /** @brief Renders queued thumbnails on the main thread after import.
     *  @param console Console panel to log results into. */
    void processThumbnailQueue(PanelConsole *console);

    std::unordered_map<kString, FileInfo> fileMap; ///< Project assets keyed by UUID.
    std::unordered_map<kString, kString> uuidMap;  ///< Reverse lookup: filename to UUID.

    std::unordered_map<kString, ObjectInfo> objectMap; ///< Active world objects keyed by UUID.

    std::vector<kString> selectedObjects; ///< UUIDs of the currently selected objects.

    /** @brief Adds an object to the selection.
     *  @param uuid Object UUID to select.
     *  @param clearList When true, clears the existing selection first. */
    void selectObject(const kString uuid, bool clearList = false);

    /** @brief Removes an object from the selection.
     *  @param uuid Object UUID to deselect. */
    void deselectObject(const kString uuid);

    kObject *selectedObject = nullptr; ///< Primary (last) selected object, or nullptr.
    kScene  *selectedScene  = nullptr; ///< Currently selected scene, or nullptr.
    bool     worldSelected  = false;   ///< True when the world node itself is selected.

    ImGuizmo::OPERATION manipulatorType = ImGuizmo::TRANSLATE; ///< Active gizmo operation (translate/rotate/scale).
    ImGuizmo::MODE manipulatorMode = ImGuizmo::LOCAL;          ///< Gizmo space (local or world).

    UndoRedoManager undoRedo;                       ///< Undo/redo command stack for the editor.
    PivotMode pivotMode = PivotMode::LastSelected;  ///< Pivot mode used for multi-object manipulation.

    ShaderPreviewState shaderPreview; ///< Shared shader-preview state for the inspector.

private:
    kWindow   *window;             ///< Application window.
    kWorld    *world;              ///< World container owning scenes and assets.
    kRenderer     *renderer;       ///< Editor viewport renderer.
    kScene    *scene = nullptr;    ///< Active editor scene.
    kString    initialWindowTitle; ///< Window title captured at startup, used as a base.

    // int initialResizeCount = 0;

    // std::map<kString, kString> fileDirty;   // Files that need to be put into fileGUID, or refresh checksum into fileMD5, or regenerate thumbnail etc.
    // kString latestFileUuid = "";

    /** @brief Determines the asset type ("model", "image", etc.) of a file.
     *  @param p File to classify.
     *  @return Asset type string. */
    kString checkAssetType(const fs::path &p);

    /** @brief Starts a background batch import of the given tasks.
     *  @param tasks Import jobs to process. */
    void startBatchImport(const std::vector<ImportTask> &tasks);
};

#endif // FILEMANAGER_H
