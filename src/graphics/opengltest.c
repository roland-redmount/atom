
#include "graphics/color.h"
#include "graphics/Graphics.h"
#include "platform.h"


char const * fontFile = "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf";

#define CORNER_SQUARE_SIDE_CM	1.0


/**
 * Main entry point
 */
int main(int argc, char **argv)
{
	InitGraphics();

	PrintF("Screen width = %f cm, height = %f cm\n", SCREEN_WIDTH_CM, SCREEN_HEIGHT_CM);
	PrintF("Window width = %f cm, height = %f cm\n", WINDOW_WIDTH_CM, WINDOW_HEIGHT_CM);
			

	// load a font
	LoadFont(fontFile);


	// White squares at the window corners
	for(index32 i = 0; i <= 1; i++) {
		float xSign = 1.0 - 2*i;
		for(index32 j = 0; j <= 1; j++) {
			float ySign = 1.0 - 2*j;

			Point topRight;
			topRight.x = xSign * WINDOW_RIGHT_EDGE_CM + (xSign < 0 ? CORNER_SQUARE_SIDE_CM : 0);
			topRight.y = ySign * WINDOW_TOP_EDGE_CM + (ySign < 0 ? CORNER_SQUARE_SIDE_CM : 0);
		
			Point bottomLeft;
			bottomLeft.x = topRight.x - CORNER_SQUARE_SIDE_CM;
			bottomLeft.y = topRight.y - CORNER_SQUARE_SIDE_CM;
			
			AddMesh(CreateRectangleMesh(bottomLeft, topRight, RGBA_COLOR_WHITE));
		}
	}

	// A polygon
	Point polygonPoints[5] = {
		{-3.0, 5.0},
		{3.0, 1.0},
		{0.0, 0.0},
		{1.0, -1.2},
		{-4.0, -1.0}
	};	
	AddPolygon(
		CreatePolygon(
			polygonPoints, 5,
			(RGBAColor) {0.8, 0.8, 0.8, 0.0}
		)
	);

	// A blue square
	AddMesh(
		CreateRectangleMesh(
			(Point) {1.0, 1.0}, (Point) {2.0, 2.0},
			(RGBAColor) {0.2, 0.4, 1.0, 0.0}
		)
	);

	// A pink rectangle
	AddMesh(
		CreateRectangleMesh(
			(Point) {5.0, -1.0}, (Point) {6.0, 3.0},
			(RGBAColor) {1.0, 0.2, 0.4, 0.0}
		)
	);

	/// Text blocks in varying size in the bottom half of the screen
	char const * text = "The quick brown fox jumps over the lazy dog";
	Point textBottomLeft = (Point) { -1.0 * WINDOW_RIGHT_EDGE_CM, -2.0 };
	float textHeight = 1.0;
	for(index32 i = 0; i < 4; i++) {
		AddTextBlock(
			CreateTextBlock(text, textBottomLeft, textHeight, RGBA_COLOR_WHITE)
		);
		textBottomLeft.y -= textHeight;
		textHeight = textHeight * 0.8;
	}

	// A rectangle below the text area to test transparency
	AddMesh(
		CreateRectangleMesh(
			(Point) {-3.0, -6.0}, (Point) {0.0, -2.0},
			(RGBAColor) {0.4, 0.4, 0.4, 0.0}
		)
	);

	// enter event loop 
	WaitUntilQuit();

	// received quit signal, cleanup and exit
	CloseGraphics();
	return 0;
}

