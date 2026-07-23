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

} // namespace LevelEditorColors
