
#include <GL/glew.h>	// this must occur *before* including SDL
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <freetype2/ft2build.h>			// use FreeType for font loading
#include FT_FREETYPE_H

#include "graphics/Graphics.h"
#include "util/LinkedList.h"
#include "util/resources.h"
#include "util/utilities.h"

#include "platform.h"

// TODO: how to best store vertices in buffers?
// we need a strategy for allocating/placing vertices in buffers
// what happens when objects are removed from the draw list,
// this will generate "holes" in the buffer (fragmenting)

// OpenGL buffer offsets are specified as void* for historical reasons,
// but are in practice an integer offset. this macro improves readability
#define GL_BUF_OFFSET(x) ((const void*) (x))


/**
 * Common information used by all functions in this file
 */
static struct {
	SDL_Window* window;
	SDL_GLContext context;

	// vertex-array objects (VAOs) and corresponding buffer objects (VBOs)
	// these hold descriptions of vertex buffers
	// see https://www.khronos.org/opengl/wiki/Vertex_Specification
	//     https://www.khronos.org/opengl/wiki/Buffer_Object

	// buffer for 2D vertices with color info, {x y r g b}
	GLuint vao2DColor;
	GLuint vertices2DColor;		// vertex buffer, also known as Vertex Buffer Object (VBO)

	// associated element buffer
	GLuint elements2DColor;

	// color shaders and program
	GLuint colorVertexShader;
	GLuint colorFragmentShader;
	GLuint colorShaderProgram;

	// buffer for 2D with texture coords, {x y u v}
	GLuint vao2DTexture;
	GLuint vertices2DTexture;

	// texture shaders and program
	GLuint textureVertexShader;
	GLuint textureFragmentShader;
	GLuint textureShaderProgram;

	// list of primitives to draw on screen
	LinkedList* polygons;
	LinkedList* meshes;
	LinkedList* textBlocks;

} graphics;

#define VERTEXBUFSIZE		1000		// capacity of each vertex buffer, in number of vertices
#define ELEMENTBUFSIZE		1000		// capacity of each element buffer


/**
 * Static function prototypes
 */
static GLuint compileShader(const char* sourceFileName, GLenum shaderType);
static GLuint compileShaderFromFile(const char* sourceFileName, GLenum shaderType);

static void drawAll(void);
static void drawPolygons(void);
static void drawMeshes(void);
static void drawTextBlocks(void);
static void swapWindow(void);
static void setup2DColorBuffers(void);
static void setupTextureBuffers(void);
static void setScaling(float scaleX, float scaleY);

/**
 * Graphics initialization
 */
void InitGraphics()
{
	// initialize SDL, video module only
	SDL_Init(SDL_INIT_VIDEO);

	// set OpenGL attributes: version 3.2, "core" functionality (forward compatible)
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	// I think this has to do with the "stencil buffer", not used
	// see e.g. https://open.gl/depthstencils
//	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
//	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

	// create window, ready for OpenGL
	graphics.window = SDL_CreateWindow("OpenGL",
		100, 100, WINDOW_WIDTH_PIXELS, WINDOW_HEIGHT_PIXELS,
		SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
	ASSERT(graphics.window != NULL);

	// create OpenGL context
	graphics.context = SDL_GL_CreateContext(graphics.window);
	ASSERT(graphics.context != NULL);

    // Set the viewport
    glViewport(0.0f, 0.0f, WINDOW_WIDTH_PIXELS, WINDOW_HEIGHT_PIXELS);

	// Clear the screen black
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);		// set color used by glClear()
	glClear(GL_COLOR_BUFFER_BIT);			// clear the current buffer
	SDL_GL_SwapWindow(graphics.window);
	
	// initialize GLEW library to acquire function pointers to OpenGL methods
	glewExperimental = GL_TRUE;		// use modern method for checking OpenGL functions
	ASSERT(glewInit() == GLEW_OK);

	// setup 2D color rendering
	setup2DColorBuffers();
	// setup 2D texture rendering
	setupTextureBuffers();

	setScaling(WINDOW_WIDTH_PIXELS, WINDOW_HEIGHT_PIXELS);
	
	// zero draw lists
	graphics.polygons = NULL;
	graphics.meshes = NULL;
	graphics.textBlocks = NULL;
}


/**
 * Set scale factors so that vertex coordinates are given in cm,
 * with origin at the center of the window
 */
static void setScaling(float scaleX, float scaleY)
{
	glUseProgram(graphics.colorShaderProgram);
	glUniform2f(
		glGetUniformLocation(graphics.colorShaderProgram, "scaling"),
		1.0 / WINDOW_RIGHT_EDGE_CM,
		1.0 / WINDOW_TOP_EDGE_CM
	);

	glUseProgram(graphics.textureShaderProgram);
	glUniform2f(
		glGetUniformLocation(graphics.textureShaderProgram, "scaling"),
		1.0 / WINDOW_RIGHT_EDGE_CM,
		1.0 / WINDOW_TOP_EDGE_CM
	);
}


// TODO: the below is lenghty and repetitive, can we simplify somehow ....
static void setup2DColorBuffers()
{
	// read and compile vertex shader
	graphics.colorVertexShader = compileShaderFromFile(
		"src/graphics/shaders/colorVertexShader.glsl", GL_VERTEX_SHADER);

	// read and compile fragment shader
	graphics.colorFragmentShader = compileShaderFromFile(
		"src/graphics/shaders/colorFragmentShader.glsl", GL_FRAGMENT_SHADER);

	// create shader program
	graphics.colorShaderProgram = glCreateProgram();
	// attach shaders
	glAttachShader(graphics.colorShaderProgram, graphics.colorVertexShader);
	glAttachShader(graphics.colorShaderProgram, graphics.colorFragmentShader);
	// specify fragment shader output
	glBindFragDataLocation(graphics.colorShaderProgram, 0, "fragmentColor");
	// link program and enable
	glLinkProgram(graphics.colorShaderProgram);

	// A Vertex Array Object (VAO) contains 1 or more Vertex Buffer Objects (VBO)
	glGenVertexArrays(1, &graphics.vao2DColor);
	glBindVertexArray(graphics.vao2DColor);

	// create vertex buffer
	glGenBuffers(1, &graphics.vertices2DColor);
	glBindBuffer(GL_ARRAY_BUFFER, graphics.vertices2DColor);
	// allocate a fixed size buffer
	// we upload subsets of data later with glBufferSubData()
	glBufferData(GL_ARRAY_BUFFER, VERTEXBUFSIZE * 5 * sizeof(float), NULL, GL_STATIC_DRAW);

	// enable the vertex attribute for current VAO (should be done before glVertexAttribPointer())
	GLint posAttrib = glGetAttribLocation(graphics.colorShaderProgram, "vertexPosition");
	glEnableVertexAttribArray(posAttrib);
	// configure vertexPosition attibute
	// format: 2-dim vector of floats, no normalization, stride = 5 floats, offset = 0
	// stored in currently bound VAO
	glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), GL_BUF_OFFSET(0));
	
	// enable the color attribute (should be done before glVertexAttribPointer())
	GLint colorAttrib = glGetAttribLocation(graphics.colorShaderProgram, "vertexColor");
	glEnableVertexAttribArray(colorAttrib);
	// configure vertexColor attribute
	// format: r-dim vector of floats, no normalization, stride = 5 floats, offset = 2 floats
	glVertexAttribPointer(colorAttrib, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), GL_BUF_OFFSET(2*sizeof(float)));

	// create element buffer objects
	glGenBuffers(1, &graphics.elements2DColor);
	// create element buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, graphics.elements2DColor);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, ELEMENTBUFSIZE * sizeof(GLuint), NULL, GL_STATIC_DRAW);

	// unbind VAO to prevent accidental re-use
	glBindVertexArray(0);
}


static void setupTextureBuffers()
{
	// read and compile vertex shader
	graphics.textureVertexShader = compileShaderFromFile(
		"src/graphics/shaders/textureVertexShader.glsl", GL_VERTEX_SHADER);

	// read and compile fragment shader
	graphics.textureFragmentShader = compileShaderFromFile(
		"src/graphics/shaders/textureFragmentShader.glsl", GL_FRAGMENT_SHADER);

	// create shader program
	graphics.textureShaderProgram = glCreateProgram();

	// attach shaders to program
	glAttachShader(graphics.textureShaderProgram, graphics.textureVertexShader);
	glAttachShader(graphics.textureShaderProgram, graphics.textureFragmentShader);
	// specify fragment shader output
	glBindFragDataLocation(graphics.textureShaderProgram, 0, "fragmentColor");
	// link program 
	glLinkProgram(graphics.textureShaderProgram);

	// create VAO
	glGenVertexArrays(1, &graphics.vao2DTexture);
	glBindVertexArray(graphics.vao2DTexture);
	// create vertex buffer
	glGenBuffers(1, &graphics.vertices2DTexture);
	glBindBuffer(GL_ARRAY_BUFFER, graphics.vertices2DTexture);
	// allocate a fixed size buffer
	// we upload subsets of data later with glBufferSubData()
	glBufferData(GL_ARRAY_BUFFER, VERTEXBUFSIZE * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

	// get vertex attribute
	GLint vertexAttrib = glGetAttribLocation(graphics.textureShaderProgram, "vertex");
	// enable the vertex attribute for current VAO (should be done before glVertexAttribPointer())
	glEnableVertexAttribArray(vertexAttrib);
	// configure vertex attibute
	// format: 4-dim vector of floats, no normalization, stride = 4 floats, offset = 0
	// stored in currently bound VAO
	glVertexAttribPointer(vertexAttrib, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), GL_BUF_OFFSET(0));
	// enable the color attribute (should be done before glVertexAttribPointer())

	// unbind VAO to prevent accidental re-use
	glBindVertexArray(0);
}


/**
 * Close the graphics system
 */

void CloseGraphics()
{
	// TODO: free OpenGL objects, VAO, VBO etc

	// TODO: deallocate meshes, textblocks ...

	// delete OpenGL context
	SDL_GL_DeleteContext(graphics.context);
	// quit SDL, destroying windows & cleaning up	
	SDL_Quit();
}


/**
 * Compile a shader from source file, given as a relative path
 * Returns handle to created shader, or 0 if an error occurred
 *
 * TODO: separate out the text file loading part, take a char string instead
 */
static GLuint compileShaderFromFile(const char * sourceFileName, GLenum shaderType)
{
	char sourceFilePath[maxPathLength + 1];
	CStringCopyLimited(sourceFileName, sourceFilePath, maxPathLength + 1);
	ToAbsolutePath(sourceFilePath, maxPathLength + 1);

	// read source file
	FileHandle fileHandle = OpenFile(sourceFileName);
 	size64 fileSize = GetFileSize(fileHandle);
	char source[fileSize + 1];
    ReadFromFile(fileHandle, source, fileSize);
    CloseFile(fileHandle);
    source[fileSize] = '\0';    // ensure string is null terminated 

	GLuint shader = compileShader(source, shaderType);
	ASSERT(shader);
	return shader;
}


static GLuint compileShader(const char* source, GLenum shaderType)
{
	// create vertex shader
	GLuint shader = glCreateShader(shaderType);
	// upload shader source code
	glShaderSource(shader, 1, (const char* const*) &source, NULL);
	// compile shader
	glCompileShader(shader);
	// check for shader compilation error
	GLint compileStatus;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
	if(compileStatus != GL_TRUE) {
		// get compilation log
		char buffer[512];
		glGetShaderInfoLog(shader, 512, NULL, buffer);
		printf("shader compilation error, log: %s\n", buffer);
		printf("source:\n%s\n", source);
		return 0;
	}
	// else success
	return shader;
}


/**
 * Swap the window buffers after OpenGL rendering
 * This makes the previously rendered image visible
 * The "hidden" buffer is cleared, and rendering must start over
 */
static void swapWindow()
{
	SDL_GL_SwapWindow(graphics.window);
}


/**
 * Wait for SDL quit signal
 * TODO: this is a quick fix, should be replaced with a proper
 * event handling system. Doesn't belong in a Graphics module
 * obviously, relates to user input, but UI and graphics are intertwined ...
 */
void WaitUntilQuit()
{
	// for now run a simple event loop checking for close-window event
	SDL_Event windowEvent;
	while(true)	{
		// wait until an event is received
		SDL_WaitEvent(&windowEvent);
		// check for quit event
		if(windowEvent.type == SDL_QUIT)
			break;
		// ignore other events, redraw and loop
		// TODO: we should only do this when refresh is needed.
		// SDL_WINDOWEVENT seems to be the case to check for
		// possibly only SDL_WINDOWEVENT_EXPOSED needs redraw ?
		// https://wiki.libsdl.org/SDL_WindowEvent
		if(windowEvent.type == SDL_WINDOWEVENT) {
			// redraw everything
			drawAll();
			swapWindow();
		}
	}
}


/**
 * Add a polygon to the draw list
 */
void AddPolygon(const Polygon* polygon)
{
	AppendToLinkedList(&(graphics.polygons), polygon);
}


/**
 * Add a mesh to the draw list
 * Takes ownership of the Mesh, caller must not deallocate
 */
void AddMesh(const Mesh* mesh)
{
	AppendToLinkedList(&(graphics.meshes), mesh);
}

/**
 * Add a text block to the draw list
 * Takes ownership of the TextBlock, caller must not deallocate
 */
void AddTextBlock(const TextBlock* textBlock)
{
	AppendToLinkedList(&(graphics.textBlocks), textBlock);

}


static void drawAll()
{
	// TODO: keeping three separate lists does not
	// allow for arbitrary z-ordering ...
	if(graphics.polygons != NULL)
		drawPolygons();
	if(graphics.meshes != NULL)
		drawMeshes();
	if(graphics.textBlocks != NULL)
		drawTextBlocks();
}


/**
 * Draw a filled mesh using the OpenGL GL_TRIANGLES primitive
 */
static void drawMesh(Mesh const * mesh)
{
	size_t nVertices = mesh->nTriangles * 3;
	// make an buffer for vertices {x y r g b}
	float buffer[nVertices * 5];
	// loop over mesh triangles
	for(index32 i = 0, k = 0; i < mesh->nTriangles; i++) {
		// loop over triangle vertices
		for(index32 j = 0; j < 3; j++, k++) {
			buffer[k*5 + 0] = mesh->coords[k*2 + 0];
			buffer[k*5 + 1] = mesh->coords[k*2 + 1];
			buffer[k*5 + 2] = mesh->color.red;
			buffer[k*5 + 3] = mesh->color.green;
			buffer[k*5 + 4] = mesh->color.blue;
		}
	}
	// TODO: for now we just upload to the beginning of GL_ARRAY_BUFFER
	glBindBuffer(GL_ARRAY_BUFFER, graphics.vertices2DColor);
	glBufferSubData(GL_ARRAY_BUFFER, 0, nVertices * 5 * sizeof(float), buffer);
	
	// create corresponding elements
	// NOTE: this just draws triangles from vertices 1-2-3, 4-5-6 ...
	// we should reuse common vertices
	GLuint elements[nVertices];
	for(index32 i = 0; i < nVertices; i++) {
		elements[i] = i;
	}
	// upload to element buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, graphics.elements2DColor);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, nVertices * sizeof(GLuint), elements);
	// draw triangle primitives using the element buffer
	glDrawElements(GL_TRIANGLES, nVertices, GL_UNSIGNED_INT, GL_BUF_OFFSET(0));

}


/**
 * Draw all meshes on the draw list
 */
static void drawMeshes()
{
	// enable color shader
	glUseProgram(graphics.colorShaderProgram);
	// select VAO
	glBindVertexArray(graphics.vao2DColor);

	LinkedList* list = graphics.meshes;
	// traverse list
	while(list != NULL) {
		Mesh const * mesh = GetLinkedListItem(list);
		drawMesh(mesh);
		list = GetNextLinkedList(list);
	}
	// unbind VAO
	glBindVertexArray(0);
}


/**
 * Draw an outline closed polygon using the GL_LINES primitive
 */
static void drawPolygon(Polygon const * polygon)
{
	// make an array of vertices
	float buffer[polygon->nVertices * 5];
	for(index32 i = 0; i < polygon->nVertices; i++) {
		Point vertex = GetPolygonVertex(polygon, i);
		buffer[i*5 + 0] = vertex.x;
		buffer[i*5 + 1] = vertex.y;
		// set all vertices to white
		buffer[i*5 + 2] = polygon->color.red;
		buffer[i*5 + 3] = polygon->color.green;
		buffer[i*5 + 4] = polygon->color.blue;
	}
	// TODO: for now we just upload to the beginning of the line buffer
	glBindBuffer(GL_ARRAY_BUFFER, graphics.vertices2DColor);
	glBufferSubData(GL_ARRAY_BUFFER, 0, polygon->nVertices * 5 * sizeof(float), buffer);
	// create elements for line coordinates
	// we draw from 0->1, 1->2, ... (n-2)->(n-1), (n-1)->0  (closed polygon)
	size_t nLines = polygon->nVertices;
	GLuint elements[nLines*2];
	for(index32 i = 0; i < nLines-1; i++) {
		elements[2*i + 0] = i;
		elements[2*i + 1] = i+1;
	}
	// last line, closing the polygon
	elements[2*(nLines-1) + 0] = nLines-1;
	elements[2*(nLines-1) + 1] = 0;
	// upload to element buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, graphics.elements2DColor);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, nLines*2 * sizeof(GLuint), elements);

	glEnable(GL_LINE_SMOOTH);
	glLineWidth(2.0);

	// draw lines using the element array
	// NOTE: could also use GL_LINE_LOOP
	glDrawElements(GL_LINES, nLines*2, GL_UNSIGNED_INT, GL_BUF_OFFSET(0));
}


/**
 * Draw polygons
 * TODO: add line color to Polygon primitive
 */
static void drawPolygons()
{
	// select VAO
	glBindVertexArray(graphics.vao2DColor);
	// enable color shader
	glUseProgram(graphics.colorShaderProgram);

	LinkedList* list = graphics.polygons;
	// iterate over polygons
	while(list != NULL) {
		Polygon const * polygon = GetLinkedListItem(list);

		drawPolygon(polygon);

		// next polygon
		list = GetNextLinkedList(list);
	}
	// unbind VAO
	glBindVertexArray(0);
}

/**
 * This structure describes a glyph, dimensons and texture
 */
typedef struct {
	GLuint texture;				// ID for the glyph's texture
	uint32 width, height;		// size of glyph's bitmap
	int32 bearingX;				// left bearing in pixels (may be negative)
	int32 bearingY;				// top bearing in pixels = top - baseline
	uint32 advance;				// distance from origin to next glyph, in 1/64 pixels
} GlyphTexture;

// array of glyphs, indexed by ascii code
static GlyphTexture glyphs[128];

// We store glyphs rendered into textures at a specific pixel size.
// NOTE: text rendered larger than this size will be pixelated
#define GLYPHTEXTURE_HEIGHT		100		// height of glyph textures in pixels


/**
 * Load a TTF font and render into textures using FreeType
 */
void LoadFont(const char* fontFile)
{
	// initialize the FreeType library
	FT_Library ftLibrary;
	FT_Error error = FT_Init_FreeType(&ftLibrary);
	if(error) {
		printf("Could not initialize FreeType library\n");
		ASSERT(false);
	}
	// load a font
	FT_Face face;
	error = FT_New_Face(ftLibrary, fontFile, 0, &face);
	if(error) {
		printf("Failed to load font file: %s\n", fontFile);
		ASSERT(false);
	}
	// set font size in pixels for texture rendering
	// using width = 0 calculates width from aspect ratio
	FT_Set_Pixel_Sizes(face, 0, GLYPHTEXTURE_HEIGHT);

	// set pixel row memory alignment requirement to 1 byte
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	// loop over the 7-bit ascii subset and load textures
	for(uint8 c = 0; c < 128; c++)
	{
		// load the glyph for a given ASCII character
		if(FT_Load_Char(face, c, FT_LOAD_RENDER))
		{
			printf("Failed to load Glyph for c = %u'n", c);
			continue;
		}
		// store glyph data
		glyphs[c].width = face->glyph->bitmap.width;
		glyphs[c].height = face->glyph->bitmap.rows;
		glyphs[c].bearingX = face->glyph->bitmap_left;
		glyphs[c].bearingY = face->glyph->bitmap_top;
		glyphs[c].advance =	face->glyph->advance.x;
//		printf("Glyph %c width = %u height = %u bearingX %d bearingY %d advance = %u\n", c,
//			glyphs[c].width, glyphs[c].height, glyphs[c].bearingX, glyphs[c].bearingY, glyphs[c].advance);
		// generate texture
		glGenTextures(1, &(glyphs[c].texture));
		glBindTexture(GL_TEXTURE_2D, glyphs[c].texture);
		// load image to texture object with given parameters
		glTexImage2D(
			GL_TEXTURE_2D,
			0,					// mipmap level (0 = base image)
			GL_RED,				// internal format, same as format
			glyphs[c].width,
			glyphs[c].height,
			0,					// reserved
			GL_RED,				// format is red channel only
			GL_UNSIGNED_BYTE,	// memory size of each pixel
			face->glyph->bitmap.buffer
		);
		// set texture options
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	// close down FreeType
	FT_Done_Face(face);
	FT_Done_FreeType(ftLibrary);
}


/**
 * Draw a text block using pre-rendered textures
 */
static void drawTextBlock(TextBlock const * text)
{
	ASSERT(text->height > 0);
	// scale factor for texture pixel coordinates
	float scale = text->height / GLYPHTEXTURE_HEIGHT;

	// set text color
	glUniform3f(
		glGetUniformLocation(graphics.textureShaderProgram, "textColor"),
		text->color.red, text->color.green, text->color.blue
	);

	// iterate through text string
	size_t length = CStringLength(text->string);
	// x-position of next glyph origin
	float xNext = text->origin.x;
	for(index32 i = 0; i < length; i++)
	{
		uint8 c = text->string[i];
		// position of lower left corner of glyph bounding box
		float xpos = xNext + glyphs[c].bearingX * scale;
		float ypos = text->origin.y - (glyphs[c].height - glyphs[c].bearingY) * scale;
		float w = glyphs[c].width * scale;
		float h = glyphs[c].height * scale;
		//printf("%c : xpos = %f ypos = %f, w = %f, h = %f\n", c, xpos, ypos, w, h);

		// vertices for two triangles, making a square (quad)
		// NOTE: texture y-coordinates increase downward
		float vertices[6][4] = {
			{ xpos,     ypos + h,   0.0f, 0.0f },            
			{ xpos,     ypos,       0.0f, 1.0f },
			{ xpos + w, ypos,       1.0f, 1.0f },

			{ xpos,     ypos + h,   0.0f, 0.0f },
			{ xpos + w, ypos,       1.0f, 1.0f },
			{ xpos + w, ypos + h,   1.0f, 0.0f }           
		};

		// render glyph texture over quad
		glBindTexture(GL_TEXTURE_2D, glyphs[c].texture);
		// update content of VBO memory
		glBindBuffer(GL_ARRAY_BUFFER, graphics.vertices2DTexture);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices); 
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		// render text
		// TODO: could move this out of the loop?
		// generate all quads first (for multiple text primitives),
		// then draw all in one go
		glDrawArrays(GL_TRIANGLES, 0, 6);
		// advance to next glyph
		xNext += ((float) glyphs[c].advance) / 64 * scale;
	}
}



/**
 * Draw all text primitives, using the currently loaded font
 */
static void drawTextBlocks()
{
	// enable texture shader
	glUseProgram(graphics.textureShaderProgram);
	// enable blending, for transparency to work
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  
	// set first object texture
	glActiveTexture(GL_TEXTURE0);
	// select VAO
	glBindVertexArray(graphics.vao2DTexture);

	// iterate over text blocks
	LinkedList* list = graphics.textBlocks;
	while(list != NULL) {
		TextBlock const * text = GetLinkedListItem(list);

		drawTextBlock(text);

		// next text block
		list = GetNextLinkedList(list);
	}
	// unbind VAO and texture
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);	
}

