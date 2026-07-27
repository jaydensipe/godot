/**************************************************************************/
/*  level_editor_materials.cpp                                            */
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

#include "level_editor_materials.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "scene/resources/texture.h"

Ref<Material> LevelEditorMaterialCache::get_or_create(const String &p_texture_path) {
	const Ref<Material> *cached = cache.getptr(p_texture_path);
	if (cached && cached->is_valid()) {
		return *cached;
	}
	Ref<Texture2D> tex = ResourceLoader::load(p_texture_path);
	ERR_FAIL_COND_V_MSG(tex.is_null(), Ref<Material>(), "Cannot load texture from path '" + p_texture_path + "'.");
	Ref<StandardMaterial3D> std_mat;
	std_mat.instantiate();
	std_mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
	std_mat->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_NEAREST_WITH_MIPMAPS);
	std_mat->set_name(tex->get_path().get_file());
	cache[p_texture_path] = std_mat;
	return std_mat;
}

bool LevelEditorMaterials::path_is_material_or_texture(const String &p_path) {
	const String res_type = ResourceLoader::get_resource_type(p_path);
	return ClassDB::is_parent_class(res_type, "Material") || ClassDB::is_parent_class(res_type, "Texture2D");
}

bool LevelEditorMaterials::drag_data_is_material(const Variant &p_data) {
	Dictionary d = p_data;
	if (!d.has("type")) {
		return false;
	}
	const String type = d["type"];
	if (type == "resource") {
		Ref<Material> mat = d["resource"];
		return mat.is_valid();
	}
	if (type == "files") {
		Vector<String> files = d["files"];
		if (files.size() != 1) {
			return false;
		}
		return path_is_material_or_texture(files[0]);
	}
	return false;
}

Ref<Material> LevelEditorMaterials::material_from_drag_data(const Variant &p_data, LevelEditorMaterialCache &p_cache) {
	Dictionary d = p_data;
	if (!d.has("type")) {
		return Ref<Material>();
	}
	const String type = d["type"];

	if (type == "resource") {
		Ref<Material> mat = d["resource"];
		return mat; // Null if the dragged resource isn't a material.
	}

	if (type == "files") {
		Vector<String> files = d["files"];
		if (files.size() != 1) {
			return Ref<Material>();
		}
		const String res_type = ResourceLoader::get_resource_type(files[0]);
		if (ClassDB::is_parent_class(res_type, "Material")) {
			Ref<Material> mat = ResourceLoader::load(files[0]);
			ERR_FAIL_COND_V_MSG(mat.is_null(), Ref<Material>(), "Cannot load material from path '" + files[0] + "'.");
			return mat;
		}
		if (ClassDB::is_parent_class(res_type, "Texture2D")) {
			return p_cache.get_or_create(files[0]);
		}
	}

	return Ref<Material>();
}
