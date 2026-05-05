#ifndef COMMANDS_H
#define COMMANDS_H

#include <functional>
#include <memory>
#include <deque>
#include <vector>

#include <kemena/kdatatype.h>
#include <kemena/kobject.h>
#include <kemena/kscene.h>

using namespace kemena;

class Manager;  // forward declaration — full definition in manager.h

// ---------------------------------------------------------------------------
// Pivot mode for multi-object gizmo
// ---------------------------------------------------------------------------
enum class PivotMode
{
    Individual,   // each object transforms around its own centre
    Center,       // transforms around the centroid of all selected objects
    LastSelected  // transforms around the last-selected object's pivot
};

// ---------------------------------------------------------------------------
// Per-object transform snapshot (used by TransformCommand)
// ---------------------------------------------------------------------------
struct TransformState
{
    kString uuid;
    kVec3   pos;
    kQuat   rot;
    kVec3   scale;
};

// ---------------------------------------------------------------------------
// Base command interface
// ---------------------------------------------------------------------------
struct ICommand
{
    virtual ~ICommand() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
};

// ---------------------------------------------------------------------------
// Undo / Redo manager
// ---------------------------------------------------------------------------
class UndoRedoManager
{
public:
    static constexpr size_t MaxHistory = 100;

    void push(std::unique_ptr<ICommand> cmd); // clears redo stack, trims history
    void undo();
    void redo();
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    void clear()         { undoStack.clear(); redoStack.clear(); }

private:
    std::deque<std::unique_ptr<ICommand>> undoStack;
    std::deque<std::unique_ptr<ICommand>> redoStack;
};

// ---------------------------------------------------------------------------
// PropertyCommand — generic lambda pair, no Manager access needed
// ---------------------------------------------------------------------------
struct PropertyCommand : ICommand
{
    std::function<void()> undoFn;
    std::function<void()> redoFn;

    PropertyCommand(std::function<void()> u, std::function<void()> r)
        : undoFn(std::move(u)), redoFn(std::move(r)) {}

    void undo() override { undoFn(); }
    void redo() override { redoFn(); }
};

// ---------------------------------------------------------------------------
// TransformCommand — gizmo / inspector drag that may span several objects
// ---------------------------------------------------------------------------
struct TransformCommand : ICommand
{
    std::vector<TransformState> before;
    std::vector<TransformState> after;
    Manager *manager;

    TransformCommand(Manager *mgr,
                     std::vector<TransformState> b,
                     std::vector<TransformState> a)
        : manager(mgr), before(std::move(b)), after(std::move(a)) {}

    void undo() override;
    void redo() override;
};

// ---------------------------------------------------------------------------
// DeleteCommand — stores detached objects so delete is undoable
// ---------------------------------------------------------------------------
struct DeletedObjectInfo
{
    kObject  *object = nullptr;
    kNodeType type   = NODE_TYPE_OBJECT;
    kScene   *scene  = nullptr;
};

struct DeleteCommand : ICommand
{
    std::vector<DeletedObjectInfo> deleted;
    std::vector<kString>           selectionBefore;   // UUIDs before deletion
    kObject                       *selectedObjBefore = nullptr;
    Manager                       *manager;
    bool                           ownsObjects = true; // true → destructor frees memory

    DeleteCommand(Manager                       *mgr,
                  std::vector<DeletedObjectInfo> del,
                  std::vector<kString>           selBefore,
                  kObject                       *selObjBefore)
        : manager(mgr)
        , deleted(std::move(del))
        , selectionBefore(std::move(selBefore))
        , selectedObjBefore(selObjBefore) {}

    ~DeleteCommand() override;
    void undo() override;
    void redo() override;
};

// ---------------------------------------------------------------------------
// ReparentCommand — undoable parent-change OR sibling-reorder
//
// The "next sibling" approach captures *what comes after* the moved object in
// each parent's children list. When restoring state we insert the object
// before that sibling (or append if the sibling is null), which sidesteps
// fragile index math when other operations have shuffled siblings.
// ---------------------------------------------------------------------------
struct ReparentCommand : ICommand
{
    Manager *manager;
    kString  objUuid;

    kObject *oldParent       = nullptr;
    kObject *oldNextSibling  = nullptr;
    kVec3    oldLocalPos     = kVec3(0);

    kObject *newParent       = nullptr;
    kObject *newNextSibling  = nullptr;
    kVec3    newLocalPos     = kVec3(0);

    void undo() override;
    void redo() override;
};

// ---------------------------------------------------------------------------
// MaterialCommand — undoable material swap on a single object
// ---------------------------------------------------------------------------
struct MaterialCommand : ICommand
{
    Manager   *manager;
    kString    objUuid;
    kMaterial *before = nullptr;
    kMaterial *after  = nullptr;

    void undo() override;
    void redo() override;
};

// ---------------------------------------------------------------------------
// InstantiateCommand — undoable spawn of a subtree into a scene
//
// Mirrors DeleteCommand: when undone, the subtree is detached and held in
// memory so a redo can re-attach. The destructor reclaims memory if the
// command is dropped while the subtree is still detached.
// ---------------------------------------------------------------------------
struct InstantiateCommand : ICommand
{
    Manager *manager;
    kObject *root         = nullptr;
    kObject *parent       = nullptr;
    kObject *nextSibling  = nullptr;
    kScene  *scene        = nullptr;
    bool     attached     = true;   // current attachment state
    bool     ownsSubtree  = false;  // true while the subtree is detached

    ~InstantiateCommand() override;
    void undo() override;
    void redo() override;
};

// ---------------------------------------------------------------------------
// CreatePrefabCommand — undoable Create-Prefab-from-selection
//
// Captures every in-scene structural change (multi-select wrapper empty,
// reparenting, position offsets, prefab_ref/template_uuid stamps) so undo
// can revert them. The .prefab file written to disk is NOT deleted on undo.
// ---------------------------------------------------------------------------
struct CreatePrefabCommand : ICommand
{
    struct ChildMove {
        kString  objUuid;
        kObject *oldParent       = nullptr;
        kObject *oldNextSibling  = nullptr;
        kVec3    oldLocalPos     = kVec3(0);
    };
    struct StampChange {
        kString objUuid;
        kString oldPrefabRef;
        kString oldTemplateUuid;
        kString newPrefabRef;
        kString newTemplateUuid;
    };

    Manager *manager = nullptr;

    // Multi-select: a fresh empty wrapper was created and attached to the scene.
    // For single-select this stays nullptr.
    kObject *createdEmpty       = nullptr;
    kObject *createdEmptyParent = nullptr; // scene root unless overridden later
    kVec3    createdEmptyPos    = kVec3(0);

    std::vector<ChildMove>   childMoves;   // empty for single-select
    std::vector<StampChange> stamps;       // applies for both modes

    void undo() override;
    void redo() override;
};

// ---------------------------------------------------------------------------
// UnpackPrefabCommand — undoable strip of prefab_ref / template_uuid
// ---------------------------------------------------------------------------
struct UnpackPrefabCommand : ICommand
{
    struct Entry {
        kString objUuid;
        kString prefabRef;
        kString templateUuid;
    };

    Manager *manager = nullptr;
    std::vector<Entry> entries;

    void undo() override;
    void redo() override;
};

// ---------------------------------------------------------------------------
// SelectCommand — records a selection change for undo/redo
// ---------------------------------------------------------------------------
struct SelectCommand : ICommand
{
    std::vector<kString> before;
    std::vector<kString> after;
    kObject             *selectedObjBefore = nullptr;
    kObject             *selectedObjAfter  = nullptr;
    Manager             *manager;

    SelectCommand(Manager            *mgr,
                  std::vector<kString> b, kObject *selB,
                  std::vector<kString> a, kObject *selA)
        : manager(mgr)
        , before(std::move(b)), selectedObjBefore(selB)
        , after(std::move(a)),  selectedObjAfter(selA) {}

    void undo() override;
    void redo() override;
};

#endif // COMMANDS_H
