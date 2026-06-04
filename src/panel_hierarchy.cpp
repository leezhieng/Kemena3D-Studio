#include "panel_hierarchy.h"
#include "panel_project.h"
#include "commands.h"

using namespace kemena;

PanelHierarchy::PanelHierarchy(kGuiManager *setGuiManager, Manager *setManager, kAssetManager *assetManager, kWorld *setWorld)
	: root("World", "world", 0, "world")
{
	gui = setGuiManager;
	manager = setManager;
	manager->panelHierarchy = this;

	kTexture2D *tex_add = assetManager->loadTexture2DFromResource("ICON_ADD_ROUND_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconAdd = tex_add->getTextureID();

	kTexture2D *tex_mag = assetManager->loadTexture2DFromResource("ICON_MAGNIFIER_LABEL", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconMag = tex_mag->getTextureID();

	// Object type icons
	kTexture2D *tex_world = assetManager->loadTexture2DFromResource("ICON_OBJECT_WORLD", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconWorld = tex_world->getTextureID();

	kTexture2D *tex_scene = assetManager->loadTexture2DFromResource("ICON_OBJECT_SCENE", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconScene = tex_scene->getTextureID();

	kTexture2D *tex_mesh = assetManager->loadTexture2DFromResource("ICON_OBJECT_MESH", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconMesh = tex_mesh->getTextureID();

	kTexture2D *tex_empty = assetManager->loadTexture2DFromResource("ICON_OBJECT_EMPTY", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconEmpty = tex_empty->getTextureID();

	kTexture2D *tex_light = assetManager->loadTexture2DFromResource("ICON_OBJECT_LIGHT", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconLight = tex_light->getTextureID();

	kTexture2D *tex_camera = assetManager->loadTexture2DFromResource("ICON_OBJECT_CAMERA", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconCamera = tex_camera->getTextureID();

	kTexture2D *tex_prefab = assetManager->loadTexture2DFromResource("ICON_OBJECT_PREFAB", "icon", kTextureFormat::TEX_FORMAT_RGBA);
	iconPrefab = tex_prefab->getTextureID();

	world = setWorld;

	root = Node("World", "world", iconWorld, "world");
}

void PanelHierarchy::deselectAll(Node &root)
{
	root.isSelected = false;
	for (auto &child : root.children)
	{
		deselectAll(*child);
	}
}

void PanelHierarchy::drawNode(Node &node, Node &root, int level)
{
	// Sync selection state from manager so viewport picks are reflected here.
	node.isSelected = std::find(manager->selectedObjects.begin(),
	                             manager->selectedObjects.end(),
	                             node.uuid) != manager->selectedObjects.end();

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (node.isSelected)
		flags |= ImGuiTreeNodeFlags_Selected;
	if (node.children.empty())
		flags |= ImGuiTreeNodeFlags_Leaf;

	// Only expand world and scene by default
	if (level <= 1)
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	// Draw the icon
	gui->image(node.icon, kVec2(16, 16));

	gui->sameLine();

	// Prefab descendants render greyed-out and don't accept clicks/drag-drop;
	// structural changes inside a prefab go through the prefab editor.
	if (node.isPrefabDescendant)
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

	bool nodeOpen = gui->treeStartEx(node.uuid, node.name, flags);

	if (node.isPrefabDescendant)
		ImGui::PopStyleColor();

	bool isObjectRow = (level >= 2);  // 0 = world, 1 = scene, 2+ = objects

	// Drag source: only for real scene objects, never prefab descendants.
	if (isObjectRow && !node.isPrefabDescendant &&
		ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		ImGui::SetDragDropPayload("SCENE_OBJECT",
			node.uuid.c_str(), node.uuid.size() + 1);
		ImGui::TextUnformatted(node.name.c_str());
		ImGui::EndDragDropSource();
	}

	// Drop target: split the row vertically into a top "drop-before"
	// (reorder as preceding sibling) and a bottom "drop-as-child" (reparent).
	// We use AcceptBeforeDelivery so the payload is accessible during hover,
	// letting us draw a custom indicator (line for drop-before, rect for
	// drop-as-child) rather than ImGui's default highlight.
	if (!node.isPrefabDescendant && ImGui::BeginDragDropTarget())
	{
		ImVec2 rmin = ImGui::GetItemRectMin();
		ImVec2 rmax = ImGui::GetItemRectMax();
		float midY = (rmin.y + rmax.y) * 0.5f;
		bool dropBefore = isObjectRow && (ImGui::GetMousePos().y < midY);

		const ImGuiDragDropFlags hoverFlags =
			ImGuiDragDropFlags_AcceptBeforeDelivery |
			ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

		const ImGuiPayload *pProj = ImGui::AcceptDragDropPayload("PROJECT_ASSET", hoverFlags);
		const ImGuiPayload *pScn  = ImGui::AcceptDragDropPayload("SCENE_OBJECT",  hoverFlags);
		const ImGuiPayload *p     = pProj ? pProj : pScn;

		if (p)
		{
			// --- Visual indicator -----------------------------------------
			ImU32 col = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
			if (dropBefore)
			{
				// Thin horizontal line above the row → "insert as previous sibling".
				ImGui::GetForegroundDrawList()->AddLine(
					ImVec2(rmin.x, rmin.y), ImVec2(rmax.x, rmin.y),
					col, 2.0f);
			}
			else
			{
				// Outlined rectangle around the row → "drop as child".
				ImGui::GetForegroundDrawList()->AddRect(
					rmin, rmax, col, 0.0f, 0, 2.0f);
			}

			// --- Delivery -------------------------------------------------
			if (p->IsDelivery())
			{
				if (pProj)
				{
					kString assetUuid((const char *)p->Data);
					// Multi-file project drags pack newline-separated UUIDs; spawn the first.
					{ auto nl = assetUuid.find('\n'); if (nl != kString::npos) assetUuid = assetUuid.substr(0, nl); }
					kObject *spawned = manager->instantiateAssetFromUuid(assetUuid);
					if (spawned && isObjectRow)
					{
						if (dropBefore && manager->objectMap.count(node.uuid))
						{
							kObject *target = manager->objectMap[node.uuid].object;
							kObject *targetParent = target ? target->getParent() : nullptr;
							kString parentUuid = (targetParent && targetParent != manager->getScene()->getRootNode())
								? targetParent->getUuid() : kString("");
							manager->reparentObject(spawned->getUuid(), parentUuid);
							manager->reorderBefore(spawned->getUuid(), node.uuid);
						}
						else
						{
							manager->reparentObject(spawned->getUuid(), node.uuid);
						}
					}
				}
				else /* pScn */
				{
					kString draggedUuid((const char *)p->Data);
					if (isObjectRow)
					{
						if (dropBefore && manager->objectMap.count(node.uuid))
						{
							kObject *target = manager->objectMap[node.uuid].object;
							kObject *targetParent = target ? target->getParent() : nullptr;
							kString parentUuid = (targetParent && targetParent != manager->getScene()->getRootNode())
								? targetParent->getUuid() : kString("");
							manager->reparentObject(draggedUuid, parentUuid);
							manager->reorderBefore(draggedUuid, node.uuid);
						}
						else
						{
							manager->reparentObject(draggedUuid, node.uuid);
						}
					}
					else if (level == 1)
					{
						// Dropped onto a scene row → reparent to scene root.
						manager->reparentObject(draggedUuid, kString(""));
					}
				}
			}
		}

		ImGui::EndDragDropTarget();
	}

	// Item clicked
	if (gui->isItemClicked() && !node.isPrefabDescendant)
	{
		// Snapshot selection before change (for undo)
		auto selBefore    = manager->selectedObjects;
		auto selObjBefore = manager->selectedObject;

		if (!gui->isKeyShift())
		{
			deselectAll(root);
		}
		node.isSelected = !node.isSelected || gui->isKeyShift();

		if (manager->panelProject != nullptr)
			manager->panelProject->clearSelection();

		if (gui->isKeyShift())
			manager->selectObject(node.uuid, false);
		else
			manager->selectObject(node.uuid, true);

		std::cout << "Object clicked: " << node.uuid.c_str() << " ,Level:" << level << std::endl;

		kObject *newSelObj = manager->selectedObject;

		if (level == 0)
		{
			// World
			manager->worldSelected  = true;
			manager->selectedObject = nullptr;
			manager->selectedScene  = nullptr;
		}
		else if (level == 1)
		{
			// Scene — find and expose it for the inspector
			manager->worldSelected  = false;
			manager->selectedObject = nullptr;
			manager->selectedScene  = nullptr;
			for (kScene *s : world->getScenes())
			{
				if (s->getUuid() == node.uuid)
				{
					manager->selectedScene = s;
					break;
				}
			}
		}
		else
		{
			// Objects
			manager->worldSelected = false;
			manager->selectedScene = nullptr;
			if (manager->objectMap[node.uuid.c_str()].object != nullptr)
			{
				std::cout << "FOUND: " << node.uuid.c_str() << std::endl;
				manager->selectedObject = manager->objectMap[node.uuid.c_str()].object;
				newSelObj = manager->selectedObject;
			}
			else
			{
				std::cout << "NOT FOUND: " << node.uuid.c_str() << std::endl;
			}
		}

		// Push selection undo command
		auto selAfter    = manager->selectedObjects;
		auto selObjAfter = newSelObj;
		if (selBefore != selAfter || selObjBefore != selObjAfter)
		{
			manager->undoRedo.push(std::make_unique<SelectCommand>(
				manager,
				selBefore,    selObjBefore,
				selAfter,     selObjAfter));
		}
	}

	if (nodeOpen)
	{
		level++;

		for (auto &child : node.children)
		{
			drawNode(*child, root, level);
		}
		gui->treePop();
	}
}

void PanelHierarchy::drawHierarchyPanel(Node &root, bool *opened)
{
	gui->beginDisabled(!manager->projectOpened);

	gui->windowStart("Hierarchy", opened);
	{
		focused = gui->isWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		gui->pushStyleColor(ImGuiCol_Button, kVec4(0, 0, 0, 0));		// Background
		gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0, 0, 0, 0)); // Hover
		gui->pushStyleColor(ImGuiCol_ButtonActive, kVec4(0, 0, 0, 0));	// Pressed

		gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(2, 0)); // smaller gap (2px horizontal, 0 vertical)

		// Add button
		{
			if (gui->imageButton("AddButton", iconAdd, kVec2(16, 16), kVec2(0, 0), kVec2(1, 1), addTint))
			{
			}
			addTint = gui->isItemActive() ? kVec4(1, 1, 1, 0.5f) : kVec4(1, 1, 1, 1);
		}

		gui->popStyleVar(); // Restore spacing
		gui->popStyleColor(3);

		// Put search box on the same line
		gui->sameLine();

		// Search bar
		gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(4, (22 - gui->getFontSize()) * 0.5f));
		gui->pushItemWidth(-FLT_MIN);

		gui->groupStart();
		{
			float iconSize = gui->getFontSize() * 0.8f; // scale relative to text height
			kVec2 cursor = gui->getCursorScreenPos();

			// Draw the icon over the input box (aligned left-center)
			gui->drawListAddImage(
				iconMag,
				kVec2(cursor.x + 4, cursor.y + (gui->getFrameHeight() - iconSize) * 0.5f),
				kVec2(cursor.x + 4 + iconSize, cursor.y + (gui->getFrameHeight() + iconSize) * 0.5f)
			);

			gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(iconSize + 8, 3));

			// Input aligned with button height
			gui->setNextItemWidth(-FLT_MIN);
			gui->inputTextWithHint("##SearchHierarchy", "Search...", searchBuffer);
		}
		gui->groupEnd();

		gui->popItemWidth();
		gui->popStyleVar(2);

		gui->spacing();

		// Tree view
		{
			float availableHeight = gui->getContentRegionAvail().y - 4; // 4 px spacing

			gui->childStart("HierarchyTree", kVec2(0, availableHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			{
				if (manager->projectOpened)
				{
					drawNode(root, root, 0);
				}
				else
				{
					kString text = "No active world";
					float textWidth = gui->calcTextSize(text).x;
					float columnWidth = gui->getColumnWidth();
					float textX = gui->getCursorPosX() + (columnWidth - textWidth) * 0.5f; // center horizontally
					gui->setCursorPosX(textX);

					gui->text(text);
				}
			}
			gui->childEnd();
		}
	}
	gui->windowEnd();

	gui->endDisabled();
}

void PanelHierarchy::draw(bool &opened)
{
	if (opened)
		drawHierarchyPanel(root, &opened);
}

void PanelHierarchy::refreshList()
{
	manager->objectMap.clear();
	root.children.clear();

	auto pickIcon = [&](kObject *obj, kString &outType) -> GLuint
	{
		// Prefab-instance roots use the prefab icon regardless of the
		// underlying object type, so they're visually distinct in the tree.
		if (!obj->getPrefabRef().empty())
		{
			outType = "prefab";
			return iconPrefab;
		}
		switch (obj->getType())
		{
			case kNodeType::NODE_TYPE_OBJECT: outType = "object"; return iconEmpty;
			case kNodeType::NODE_TYPE_MESH:   outType = "mesh";   return iconMesh;
			case kNodeType::NODE_TYPE_LIGHT:  outType = "light";  return iconLight;
			case kNodeType::NODE_TYPE_CAMERA: outType = "camera"; return iconCamera;
			default:                          outType = "unknown"; return iconEmpty;
		}
	};

	// Walk the scene-graph subtree below `obj` and mirror it into the
	// hierarchy panel. `insidePrefab` propagates the disabled state down to
	// every descendant of a prefab instance root.
	std::function<void(Node *, kObject *, bool)> addObject =
		[&](Node *parent, kObject *obj, bool insidePrefab)
	{
		kString type;
		GLuint icon = pickIcon(obj, type);

		auto childNode = std::make_unique<Node>(obj->getName(), obj->getUuid(), icon, type);
		childNode->isPrefabDescendant = insidePrefab;
		Node *raw = childNode.get();
		parent->children.emplace_back(std::move(childNode));

		manager->objectMap[obj->getUuid()] = ObjectInfo{ obj };

		// Anything below a prefab instance root is a prefab descendant.
		bool nextInside = insidePrefab || !obj->getPrefabRef().empty();

		for (kObject *c : obj->getChildren())
			addObject(raw, c, nextInside);
	};

	if (world->getScenes().size() > 1)
	{
		for (size_t i = 1; i < world->getScenes().size(); ++i)
		{
			kScene *scene = world->getScenes().at(i);

			auto &sceneNode = root.children.emplace_back(
				std::make_unique<Node>(scene->getName(), scene->getUuid(), iconScene, "scene"));

			kObject *rootNode = scene->getRootNode();
			if (rootNode == nullptr) continue;
			for (kObject *child : rootNode->getChildren())
				addObject(sceneNode.get(), child, /*insidePrefab*/ false);
		}
	}
}
