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

class PanelHierarchy
{
	public:
		bool focused = false;

	private:
		GLuint iconAdd = 0;
		GLuint iconMag = 0;

        GLuint iconWorld  = 0;
        GLuint iconScene  = 0;
        GLuint iconMesh   = 0;
        GLuint iconEmpty  = 0;
        GLuint iconLight  = 0;
        GLuint iconCamera = 0;
        GLuint iconPrefab = 0;

		kString searchBuffer;

		kVec4 addTint = kVec4(1, 1, 1, 1);

		struct Node
		{
			kString name;
			bool isSelected = false;
			std::vector<std::unique_ptr<Node>> children;

			kString uuid;
			GLuint icon = 0;
			kString type = ""; // world, scene, mesh, etc.

			// True for nodes that live underneath a prefab instance root.
			// They render greyed-out and don't accept drag/drop or selection-
			// based edits — structural changes inside a prefab must be done
			// in the prefab editor.
			bool isPrefabDescendant = false;

			Node(const kString& n, const kString& g, GLuint i = 0, const kString& t = "")
            : name(n), uuid(g), icon(i), type(t) {}
		};

		Node root;

	public:
	    PanelHierarchy(kGuiManager* setGuiManager, Manager* setManager, kAssetManager* assetManager, kWorld* setWorld);
		void deselectAll(Node& root);
		void drawNode(Node& node, Node& root, int level);
		void drawHierarchyPanel(Node& root, bool* opened);
		void draw(bool& opened);
		void refreshList();

		Manager* manager;
		kGuiManager* gui;

		kWorld* world;
};

#endif

