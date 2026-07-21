/**************************************************************************/
/*  level_map.h                                                           */
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

#include "level_brush.h"

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"

// A node that holds a Hammer-style brush-based level. Each brush is a
// LevelBrush child node. The map shows a live combined preview in the
// editor and can be baked to a standalone MeshInstance3D with trimesh
// collision and an ArrayOccluder3D.
class LevelMap : public Node3D {
	GDCLASS(LevelMap, Node3D);

	Ref<StandardMaterial3D> default_material;

	// Live preview.
	MeshInstance3D *preview_mesh_instance = nullptr;
	bool preview_dirty = false;

	void _update_preview();

	Ref<Material> _get_face_material_or_default(LevelBrush *p_brush, int p_face) const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	Vector<LevelBrush *> get_brushes() const;
	int get_brush_count() const;

	void set_default_material(const Ref<Material> &p_material);
	Ref<Material> get_default_material() const;

	void refresh();

	// Bake everything into a fresh node hierarchy rooted at a new
	// MeshInstance3D (name "<LevelMap name>_Baked"), containing:
	//  - ArrayMesh grouped per material
	//  - StaticBody3D + CollisionShape3D (ConcavePolygonShape3D)
	//  - OccluderInstance3D (ArrayOccluder3D)
	// The returned node is NOT added to the tree.
	Node3D *bake() const;

	PackedStringArray get_configuration_warnings() const override;

	LevelMap();
	virtual ~LevelMap() {}
};
