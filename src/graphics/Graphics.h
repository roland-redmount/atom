/**
 * Graphics interface using OpenGL over SDL2
 *
 * We would like to write this so that all OpenGL and SDL2 details are
 * hidden, and we instead provide our own graphic primitives
 * such as DrawLine(), PrintText() etc.
 * Ultimately, we want a graphics node that accepts actions
 * to place icons on the screen, e.g.
 * assert [icon i display d]
 * and handles all the rendering stuff under the hood.
 *
 * The SDL layer also handles user input (keyboard, mouse etc) which
 * should be mapped to user actions.
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "graphics/Polygon.h"
#include "graphics/Mesh.h"
#include "graphics/TextBlock.h"


/**
 * I think draw coordinates should be specified in physical units (cm) to allow
 * rendering at a given size, independent of screen dimensions. This of course
 * requires knowing the screen dimensions, and changing layout if the screen size
 * changes (e.g. docking a laptop to a larger screen).
 * I currently have a 27" screen with 16:9 aspect ratio with 3840 x 2160 resolution.
 * 
 * TODO: these values must be determined from the OS by the platform layer
 * 
 * NOTE: there are also obvious use cases where we want to specify only a shape,
 * in relative coordinates, without specifying the size. The drawing / layout system
 * should be responsible for translating such shapes to screen coordinates in some
 * suitable way. Also, drawing objects at specified physical size is of course not
 * always feasible (if there is not enough screen space available). The system may
 * choose to zoom or clip such objects. 
 */

#define SCREEN_WIDTH_PIXELS		3840
#define SCREEN_HEIGHT_PIXELS	2160
#define SCREEN_ASPECT_RATIO		(SCREEN_WIDTH_PIXELS / SCREEN_HEIGHT_PIXELS)

// we need to know the physical screen size
#define SCREEN_DIAGONAL_INCHES	27

// TODO: these should be calculated from diagonal and aspect ratio
#define SCREEN_WIDTH_INCHES		23.53
#define SCREEN_HEIGHT_INCHES	13.24

#define CM_PER_INCH				2.54
#define SCREEN_WIDTH_CM			(SCREEN_WIDTH_INCHES * CM_PER_INCH)
#define SCREEN_HEIGHT_CM		(SCREEN_HEIGHT_INCHES * CM_PER_INCH)

#define PIXELS_PER_CM			(SCREEN_WIDTH_PIXELS / SCREEN_WIDTH_CM)

#define WINDOW_WIDTH_PIXELS		1200
#define WINDOW_HEIGHT_PIXELS	800

#define WINDOW_WIDTH_CM			(WINDOW_WIDTH_PIXELS / PIXELS_PER_CM)
#define WINDOW_HEIGHT_CM		(WINDOW_HEIGHT_PIXELS / PIXELS_PER_CM)

#define WINDOW_RIGHT_EDGE_CM	(WINDOW_WIDTH_CM / 2)
#define WINDOW_TOP_EDGE_CM		(WINDOW_HEIGHT_CM / 2)



// for specifying type size in points
#define POINT_PER_MM			0.352778


void InitGraphics(void);

// load a font, used for text drawing
void LoadFont(const char* fontFile);

// add a polygon to the draw list, transfers ownership
void AddPolygon(const Polygon* polygon);

// add a mesh to the draw list, transfers ownership
void AddMesh(const Mesh* mesh);

// add a mesh to the draw list, transfers ownership
void AddTextBlock(const TextBlock* textBlock);

void CloseGraphics(void);

// wait for SDL quit signal
void WaitUntilQuit(void);



#endif	// GRAPHICS_H

