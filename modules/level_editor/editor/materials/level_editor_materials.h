/**************************************************************************/
/*  level_editor_materials.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/templates/hash_map.h"
#include "scene/resources/material.h"

// Material helpers for the Level editor: texture -> material wrapping (with
// a shared cache) and drag-and-drop payload handling, shared by the dock
// (Browse / active-material preview) and the viewport drop flow.
//
// Wrapping: picking a texture file (Browse or drop) wraps it in a generated
// StandardMaterial3D. LevelEditorMaterialCache returns ONE wrapper per
// texture path so repeated picks share the material - no duplicate embedded
// sub-resources, and shared faces bake into a single surface.
//
// Drag payloads come in two shapes (same convention as EditorResourcePicker):
//  - "files":    FileSystem dock drag; a single material/texture path.
//  - "resource": in-editor resource drag (dock preview, inspector pickers).

class LevelEditorMaterialCache {
	HashMap<String, Ref<Material>> cache;

public:
	// Returns the shared generated material for a texture path, creating it
	// on first use. Null if the path does not load as a Texture2D.
	Ref<Material> get_or_create(const String &p_texture_path);
};

namespace LevelEditorMaterials {

// True if a drag payload can yield a material (cheap - file payloads are
// validated by reported resource type only, no loading).
bool drag_data_is_material(const Variant &p_data);

// Extract a material from a drag payload. Texture files are wrapped via
// p_cache; anything that is neither material nor texture returns null.
Ref<Material> material_from_drag_data(const Variant &p_data, LevelEditorMaterialCache &p_cache);

// True if a project file path is a material or texture resource (reported
// type only, no loading).
bool path_is_material_or_texture(const String &p_path);

} // namespace LevelEditorMaterials
