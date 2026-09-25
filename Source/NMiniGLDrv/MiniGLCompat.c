/* Small compatibility pieces missing from the SDK's libmgl.a. */

#include <mgl/gl.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>

/*
 * The MiniGL headers and SDL wrapper expose GLReadPixels, but this SDK's
 * archive does not contain its implementation. Read the window RastPort into
 * an RGBA buffer, preserving OpenGL's bottom-up row order.
 */
void GLReadPixels(
	GLcontext context,
	GLint x,
	GLint y,
	GLsizei width,
	GLsizei height,
	GLenum format,
	GLenum type,
	GLvoid* pixels )
{
	GLint row;
	GLubyte* destination = (GLubyte*)pixels;

	if( !context || !context->w3dWindow || !pixels ||
		format != GL_RGBA || type != GL_UNSIGNED_BYTE || width <= 0 || height <= 0 )
		return;

	for( row = 0; row < height; ++row )
	{
		const GLint sourceY = context->w3dWindow->Height - 1 - y - row;
		if( sourceY < 0 || sourceY >= context->w3dWindow->Height )
			continue;
		ReadPixelArray(
			destination + row * width * 4,
			0,
			0,
			(UWORD)(width * 4),
			context->w3dWindow->RPort,
			(UWORD)x,
			(UWORD)sourceY,
			(UWORD)width,
			1,
			RECTFMT_RGBA );
	}
}
