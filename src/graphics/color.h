
#ifndef COLOR_H
#define COLOR_H

typedef struct {
	float red;
	float green;
	float blue;
	float alpha;
} RGBAColor;


#define RGBA_COLOR_WHITE 	((RGBAColor) {1.0, 1.0, 1.0, 0.0})


#endif	// COLOR_H
