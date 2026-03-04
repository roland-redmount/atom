
#include <malloc.h>

#include "graphics/TextBlock.h"

#include "platform.h"

TextBlock * CreateTextBlock(char const * text, Point origin, float height, RGBAColor color)
{
	TextBlock * textBlock = malloc(sizeof(TextBlock));
	textBlock->string = malloc(CStringLength(text) + 1);
	CStringCopy(text, textBlock->string);
	textBlock->origin = origin;
	textBlock->height = height;
	textBlock->color = color;
	return textBlock;
}

void FreeTextBlock(TextBlock const * textBlock)
{
	free(textBlock->string);
	free((void*) textBlock);
}
