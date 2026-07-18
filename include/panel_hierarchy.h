#ifndef PANEL_HIERARCHY_H
#define PANEL_HIERARCHY_H

#include "kemena/kemena.h"
#include "kemena/kworld.h"

#include "manager.h"

#include <imgui.h>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>

using namespace kemena;

class Manager;

/**
 * @brief Editor panel that displays the world/scene object graph as a tree.
 *
 * Mirrors the runtime scene graph into an ImGui tree view, letting the user
 * browse, select, search, reorder and reparent scene objects via drag-and-drop.
 * Selection state is kept in sync with the Manager so viewport picks and
 * hierarchy clicks reflect each other. Prefab-instance subtrees are shown
 * greyed-out and are read-only from the hierarchy.
 */
class PanelHierarchy
{
public:
	bool focused = false; ///< True while the hierarchy window owns ImGui focus.

private:
	uint32_t iconAdd = 0; ///< Texture handle for the "add object" toolbar button.
	uint32_t iconMag = 0; ///< Texture handle for the search-bar magnifier icon.

	uint32_t iconWorld = 0;	///< Icon for the world root node.
	uint32_t iconScene = 0;	///< Icon for scene nodes.
	uint32_t iconMesh = 0;	///< Icon for mesh objects.
	uint32_t iconEmpty = 0;	///< Icon for empty/generic objects.
	uint32_t iconLight = 0;	///< Icon for light objects.
	uint32_t iconCamera = 0;	///< Icon for camera objects.
	uint32_t iconAudio = 0;	///< Icon for audio emitter objects.
	uint32_t iconPrefab = 0;	///< Icon for prefab-instance root nodes.
	uint32_t iconTerrain = 0; ///< Icon for terrain tile objects.

	kString searchBuffer; ///< Current text in the hierarchy search box.

	kVec4 addTint = kVec4(1, 1, 1, 1); ///< Tint applied to the add button (dims while pressed).

	/**
	 * @brief A single row in the hierarchy tree.
	 *
	 * Represents the world root, a scene, or a scene object, mirroring one
	 * entry of the underlying scene graph and carrying the UI state needed
	 * to draw and interact with it.
	 */
	struct Node
	{
		kString name;								 ///< Display name shown in the tree.
		bool isSelected = false;					 ///< Whether this row is currently selected.
		std::vector<std::unique_ptr<Node>> children; ///< Child rows, owned by this node.

		kString uuid;	   ///< UUID of the represented object/scene (used as ImGui ID and selection key).
		uint32_t icon = 0;   ///< Texture handle drawn beside the name.
		kString type = ""; ///< Node category: world, scene, mesh, etc.

		// True for nodes that live underneath a prefab instance root.
		// They render greyed-out and don't accept drag/drop or selection-
		// based edits — structural changes inside a prefab must be done
		// in the prefab editor.
		bool isPrefabDescendant = false; ///< True if this row sits under a prefab instance root (read-only, greyed-out).

		/**
		 * @brief Construct a tree node.
		 * @param n Display name.
		 * @param g UUID of the represented object/scene.
		 * @param i Icon texture handle (0 for none).
		 * @param t Node type string (world, scene, mesh, ...).
		 */
		Node(const kString &n, const kString &g, uint32_t i = 0, const kString &t = "")
			: name(n), uuid(g), icon(i), type(t) {}
	};

	Node root; ///< Root of the hierarchy tree (the "World" node).

public:
	/**
	 * @brief Construct the hierarchy panel and load its icons.
	 * @param setGuiManager GUI manager used for all ImGui drawing.
	 * @param setManager Editor manager providing selection state and scene access.
	 * @param assetManager Asset manager used to load the panel's icon textures.
	 * @param setWorld World whose scenes and objects are mirrored into the tree.
	 */
	PanelHierarchy(kGuiManager *setGuiManager, Manager *setManager, kAssetManager *assetManager, kWorld *setWorld);

	/**
	 * @brief Clear the selection flag on a node and all of its descendants.
	 * @param root Subtree root to deselect recursively.
	 */
	void deselectAll(Node &root);

	/**
	 * @brief Draw a single tree node and recurse into its children.
	 *
	 * Renders the row (icon, label, expand arrow), syncs selection from the
	 * manager, handles drag-and-drop source/target for reorder/reparent, and
	 * processes clicks (updating world/scene/object selection and undo).
	 * @param node Node to draw.
	 * @param root Tree root, used for whole-tree deselection on click.
	 * @param level Depth in the tree (0 = world, 1 = scene, 2+ = objects).
	 */
	void drawNode(Node &node, Node &root, int level);

	/**
	 * @brief Draw the full hierarchy window: toolbar, search bar and tree view.
	 * @param root Root node to render.
	 * @param opened Pointer to the window's open flag (toggled by ImGui's close button).
	 */
	void drawHierarchyPanel(Node &root, bool *opened);

	/**
	 * @brief Draw the panel if it is open.
	 * @param opened Whether the panel is currently visible.
	 */
	void draw(bool &opened);

	/**
	 * @brief Rebuild the tree from the current world/scene graph.
	 *
	 * Clears the manager's object map and the tree, then re-walks every
	 * scene's object hierarchy, picking icons by object type and flagging
	 * prefab descendants as read-only.
	 */
	void refreshList();

	Manager *manager; ///< Editor manager (selection, object map, undo/redo).
	kGuiManager *gui; ///< GUI manager used for ImGui drawing.

	kWorld *world; ///< World whose scenes are displayed in the tree.
};

#endif
