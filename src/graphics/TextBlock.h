/**
 * A graphics primitive for rendering a text string
 */

#ifndef TEXTBLOCK_H
#define TEXTBLOCK_H

#include "graphics/color.h"
#include "graphics/Point.h"

typedef struct s_TextBlock {
	char * string;
	
	// origin of leftmost glyph, where y is the text baseline
	// NOTE: glyph may extend to the left of x if bearing of first glyph is < 0
	Point origin;

	float height;
	// TODO: calculate and store bounding box ?
	RGBAColor color;
} TextBlock;

TextBlock * CreateTextBlock(char const * text, Point origin, float height, RGBAColor color);

void FreeTextBlock(TextBlock const * textBlock);


#endif	// TEXTBLOCK_H

