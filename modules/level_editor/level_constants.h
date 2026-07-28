/**************************************************************************/
/*  level_constants.h                                                     */
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

#include "core/math/color.h"

// Shared geometry tolerances for brush ops (runtime side - used by
// level_modifiers.cpp, which must build in export templates; keep this
// header free of editor-only includes).
namespace LevelBrushConstants {
// Plane-side test epsilon for clip/split/weld decisions (brush units).
inline constexpr real_t PLANE_EPSILON = 0.0005;
// Max distance for welding two positions into one vertex.
inline constexpr real_t WELD_DIST = PLANE_EPSILON * 4.0;
// |dot| threshold for treating two edges as parallel/collinear.
inline constexpr real_t PARALLEL_DOT = 0.999;
// Winding side-test ambiguity threshold (fraction of the loop-normal length):
// below this the centroid lies ~in the wall plane and the bisector fallback
// decides (extrude_edge/extrude_vertex; GOTCHAS #30).
inline constexpr real_t WINDING_SIDE_EPS = 0.001;
// Bevel mitre: minimum sine of the angle between an edge pair before the
// corner is rejected as nearly parallel (level_modifiers bevel).
inline constexpr real_t BEVEL_MITRE_MIN_SIN = 0.05;
// Baked UV scale: world units per texture tile (planar projections in
// get_bake_surface_data).
inline constexpr real_t BAKE_UV_SCALE = 0.25;
} // namespace LevelBrushConstants

// Shared grid ladder (power-of-two steps) used by the toolbar dropdown and
// the [ ] keys.
namespace LevelEditorGrid {

inline constexpr real_t STEPS[] = {
	0.125,
	0.25,
	0.5,
	1,
	2,
	4,
	8,
	16,
	32,
	64,
	128,
	256,
	512,
};
inline constexpr int STEP_COUNT = (int)(sizeof(STEPS) / sizeof(STEPS[0]));

// Perspective 3D grid: world-space extent around the camera, and how far the
// camera may move before the mesh is rebuilt (half the extent).
inline constexpr real_t GRID_3D_EXTENT = 128.0;
inline constexpr real_t GRID_3D_REBUILD_DIST = GRID_3D_EXTENT / 2.0;

} // namespace LevelEditorGrid

// Shared drawing constants for the level editor overlay. All colors used by
// the viewports live here so the visual language stays consistent.
namespace LevelEditorColors {

// Grid.
inline const Color GRID_MINOR{ 1, 1, 1, 0.06f };
inline const Color GRID_MAJOR{ 1, 1, 1, 0.15f };
inline const Color GRID_AXIS{ 0.55f, 0.75f, 1.0f, 0.5f };

// Brush outlines.
inline const Color BRUSH_OUTLINE{ 0.9f, 0.9f, 0.9f, 0.35f };
inline const Color BRUSH_OUTLINE_SELECTED{ 1.0f, 0.6f, 0.1f, 0.9f };
inline const Color BRUSH_OUTLINE_HOVER{ 1.0f, 1.0f, 1.0f, 0.5f };

// Element modes: hovered brush highlight + hover/selection colors.
inline const Color HOVER_BRUSH_OUTLINE{ 0.55f, 0.8f, 1.0f, 0.8f };
inline const Color HOVER_ELEMENT{ 0.2f, 1.0f, 0.3f, 1.0f };
inline const Color HOVER_FACE_FILL{ 0.2f, 1.0f, 0.3f, 0.25f };
inline const Color SELECTED_ELEMENT{ 1.0f, 0.6f, 0.1f, 0.95f };
inline const Color SELECTED_FACE_FILL{ 1.0f, 0.45f, 0.1f, 0.22f };
inline const Color VERTEX_OUTLINE{ 0.0f, 0.0f, 0.0f, 1.0f };

// Ghost block + drag feedback (green).
inline const Color GHOST{ 0.2f, 0.9f, 0.4f, 0.9f };
inline const Color GHOST_HANDLE{ 0.2f, 0.9f, 0.4f, 0.7f };
inline const Color GHOST_HANDLE_HOT{ 0.6f, 1.0f, 0.75f, 0.95f };
inline const Color DRAG_RECT{ 0.2f, 0.9f, 0.4f, 0.6f };

// Clip tool (cyan) + keep/cut indication.
inline const Color CLIP{ 0.2f, 0.9f, 1.0f, 1.0f };
inline const Color CLIP_LINE{ 0.2f, 0.9f, 1.0f, 0.9f };
inline const Color CLIP_POINT_HOT{ 0.6f, 1.0f, 1.0f, 1.0f };
inline const Color CLIP_KEPT{ 0.2f, 0.9f, 0.4f, 0.95f };
inline const Color CLIP_CUT{ 0.95f, 0.25f, 0.2f, 0.95f };
inline const Color CLIP_HALF{ 0.4f, 0.6f, 1.0f, 0.95f };
inline const Color CLIP_MARKER{ 1.0f, 1.0f, 1.0f, 0.9f };

// Armed-action previews (bevel, etc.): same saturation/brightness profile as
// the clip cyan (low 0.2 / high 1.0) but yellow, so a live action preview
// reads as distinct from the clip/mirror tools.
inline const Color ACTION_PREVIEW{ 1.0f, 0.9f, 0.2f, 0.9f };

// Select-mode resize handles (orange family).
inline const Color SELECT_HANDLE{ 1.0f, 0.6f, 0.1f, 0.8f };
inline const Color SELECT_HANDLE_HOT{ 1.0f, 0.8f, 0.5f, 0.95f };

// Gizmos.
inline const Color GIZMO_AXIS_X{ 0.95f, 0.3f, 0.3f };
inline const Color GIZMO_AXIS_Y{ 0.4f, 0.9f, 0.4f };
inline const Color GIZMO_AXIS_Z{ 0.3f, 0.6f, 1.0f };
inline const Color GIZMO_CENTER{ 1.0f, 1.0f, 1.0f, 0.9f };

// Hovered/dragged element highlight: lighter version of the element's color
// (50% lerp to white).
inline Color hot(const Color &p_color) {
	return p_color.lerp(Color(1, 1, 1), 0.5f);
}

// Move-gizmo plane handles sit this fraction of the way along their axes
// (shared by pick + draw so the hit area matches the visual).
inline constexpr real_t GIZMO_PLANE_EXTENT = 0.45f;

// Text overlays.
inline const Color TEXT{ 1.0f, 1.0f, 1.0f, 0.9f };
inline const Color TEXT_DIM{ 1.0f, 1.0f, 1.0f, 0.8f };

// Viewport environment.
inline const Color VIEWPORT_BG{ 0.16f, 0.16f, 0.18f };
inline const Color VIEWPORT_AMBIENT{ 0.45f, 0.45f, 0.45f };

// Default brush albedo (LevelMap::default_material).
inline const Color DEFAULT_BRUSH_ALBEDO{ 0.7f, 0.7f, 0.7f };

} // namespace LevelEditorColors

// Screen-space sizes/tolerances for viewport handles (multiplied by EDSCALE
// at use). Shared by the ghost, select-mode AABB handles, and the clip/mirror
// plane points so picking and drawing always agree.
namespace LevelEditorHandles {
inline constexpr real_t FACE_PICK_TOL = 10.0; // Box face-center pick radius (px).
inline constexpr real_t CORNER_PICK_TOL = 8.0; // Box corner pick radius (px).
inline constexpr real_t POINT_PICK_TOL = 10.0; // Clip/mirror plane-point pick radius (px).
inline constexpr real_t VERTEX_PICK_TOL = 16.0; // Vertex pick radius (px).
inline constexpr real_t EDGE_PICK_TOL = 12.0; // Edge pick radius (px).
inline constexpr real_t FACE_SIZE = 4.0; // Face-handle rect half-size (px).
inline constexpr real_t CORNER_SIZE = 3.0; // Corner-handle rect half-size (px).
inline constexpr real_t POINT_SIZE = 4.0; // Clip/mirror point rect half-size (px).
inline constexpr real_t VERTEX_SIZE = 3.0; // Vertex marker rect half-size (px).
inline constexpr real_t VERTEX_HOT_SIZE = 4.5; // Hovered/selected vertex marker half-size (px).
inline constexpr real_t DROP_REPROBE_DIST_SQ = 16.0; // Re-pick material drop when cursor moved this far (px^2, ~4px).
// Marching-ants: phase advance per second, and the wrap period. The period
// must equal dash_len * 2 at EDSCALE 1 (LevelHelpers::draw_marching_segment's
// default dash); derive it from the dash constant instead of hardcoding.
inline constexpr real_t ANTS_DASH = 8.0;
inline constexpr real_t ANTS_PERIOD = ANTS_DASH * 2.0;
inline constexpr real_t ANTS_SPEED = 60.0; // Phase units per second.
} // namespace LevelEditorHandles
