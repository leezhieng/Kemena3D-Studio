# Kemena3D Studio

Kemena3D Studio is an open-source, cross-platform 3D game engine built with C++, powered by the underlying Kemena3D SDK rendering engine.

If you're looking for the core 3D rendering engine - Kemena3D SDK, please visit the following page instead: https://github.com/leezhieng/kemena3d

## Website

You can find the latest release, tutorials and additional information at: https://www.kemena3d.com

## Screenshots

![Editor Screenshot](https://kemena3d.com/site/wp-content/uploads/2026/04/kemena_screenshot.png)

![Editor Screenshot](https://kemena3d.com/site/wp-content/uploads/2026/04/kemena_screenshot_asset.png)

## Scripting

Gameplay is scripted with [AngelScript](https://www.angelcode.com/angelscript/),
authored either as text `.as` files or visually in the **Script Editor** node
graph. Attach a script to any object in the Inspector; pressing **Play**
compiles scripts to bytecode (under the project's `Library/Scripts` folder) and
runs their lifecycle functions. Use **Run → Build Scripts** to compile manually.

See the SDK's [Scripting guide](https://github.com/leezhieng/kemena3d/blob/main/Documentation/Scripting.md)
for the host API and node-graph reference.
