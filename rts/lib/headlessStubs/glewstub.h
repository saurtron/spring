/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _GLEW_STUB_H_
#define _GLEW_STUB_H_

#undef GL_GLEXT_LEGACY
#define GL_GLEXT_PROTOTYPES
#ifdef _WIN32
# define _GDI32_
# ifdef _DLL
#  undef _DLL
# endif
# include <windows.h>
#endif

#if defined(__APPLE__)
	#include <OpenGL/glu.h>
	#include <OpenGL/glext.h>
#else
	#include <GL/glu.h>
	#include <GL/glext.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define GLEW_GET_FUN(x) x

#define GLEW_VERSION 1

#define GLEW_VERSION_1_1   GL_TRUE
#define GLEW_VERSION_1_2   GL_TRUE
#define GLEW_VERSION_1_2_1 GL_TRUE
#define GLEW_VERSION_1_3   GL_TRUE
#define GLEW_VERSION_1_4   GL_TRUE
#define GLEW_VERSION_1_5   GL_FALSE
#define GLEW_VERSION_2_0   GL_FALSE
#define GLEW_VERSION_2_1   GL_FALSE
#define GLEW_VERSION_3_0   GL_FALSE
#define GLEW_VERSION_3_1   GL_FALSE
#define GLEW_VERSION_3_2   GL_FALSE
#define GLEW_VERSION_3_3   GL_FALSE
#define GLEW_VERSION_4_0   GL_FALSE
#define GLEW_VERSION_4_1   GL_FALSE
#define GLEW_VERSION_4_2   GL_FALSE
#define GLEW_VERSION_4_3   GL_FALSE
#define GLEW_VERSION_4_4   GL_FALSE
#define GLEW_VERSION_4_5   GL_FALSE
#define GLEW_VERSION_4_6   GL_FALSE

#define GLEW_NV_vertex_program2 GL_FALSE
#define GLEW_ARB_depth_clamp GL_FALSE
#define GLEW_EXT_framebuffer_blit GL_FALSE
#define GLEW_EXT_framebuffer_object GL_FALSE
#define GLEW_EXT_stencil_two_side GL_TRUE
#define GLEW_ARB_draw_buffers GL_FALSE
#define GLEW_EXT_pixel_buffer_object GL_FALSE
#define GLEW_ARB_map_buffer_range GL_FALSE
#define GLEW_EXT_texture_filter_anisotropic GL_FALSE
#define GLEW_ARB_texture_float GL_TRUE
#define GLEW_ARB_texture_non_power_of_two GL_TRUE
#define GLEW_ARB_texture_env_combine GL_TRUE
#define GLEW_ARB_texture_rectangle GL_TRUE
#define GLEW_ARB_texture_compression GL_TRUE
#define GLEW_ARB_texture_env_dot3 GL_FALSE
#define GLEW_EXT_texture_edge_clamp GL_FALSE
#define GLEW_ARB_texture_border_clamp GL_TRUE
#define GLEW_EXT_texture_rectangle GL_TRUE
#define GLEW_ARB_texture_query_lod GL_TRUE
#define GLEW_ARB_multisample GL_FALSE
#define GLEW_ARB_multitexture GL_TRUE
#define GLEW_ARB_depth_texture GL_TRUE
#define GLEW_ARB_vertex_buffer_object GL_FALSE
#define GLEW_ARB_vertex_array_object GL_FALSE
#define GLEW_ARB_vertex_shader GL_FALSE
#define GLEW_ARB_vertex_program GL_FALSE
#define GLEW_ARB_shader_objects GL_FALSE
#define GLEW_ARB_shading_language_100 GL_FALSE
#define GLEW_ARB_fragment_shader GL_FALSE
#define GLEW_ARB_fragment_program GL_FALSE
#define GLEW_ARB_shadow GL_FALSE
#define GLEW_ARB_shadow_ambient GL_FALSE
#define GLEW_ARB_imaging GL_FALSE
#define GLEW_ARB_occlusion_query GL_FALSE
#define GLEW_ARB_geometry_shader4 GL_FALSE
#define GLEW_ARB_instanced_arrays GL_FALSE
#define GLEW_ARB_transform_feedback_instanced GL_FALSE
#define GLEW_ARB_uniform_buffer_object GL_FALSE
#define GLEW_ARB_shader_storage_buffer_object GL_FALSE
#define GLEW_ARB_transform_feedback3 GL_FALSE
#define GLEW_EXT_blend_equation_separate GL_FALSE
#define GLEW_EXT_blend_func_separate GL_FALSE
#define GLEW_ARB_framebuffer_object GL_TRUE
#define GLEW_ARB_shading_language_420pack GL_FALSE
#define GLEW_ARB_buffer_storage GL_FALSE
#define GLEW_ARB_draw_elements_base_vertex GL_FALSE
#define GLEW_ARB_copy_buffer GL_FALSE
#define	GLEW_ARB_multi_draw_indirect GL_FALSE
#define	GLEW_ARB_sync GL_FALSE
#define GLEW_ARB_timer_query GL_FALSE
#define GLEW_ARB_explicit_attrib_location GL_FALSE
#define GLEW_EXT_packed_float GL_FALSE
#define GLEW_ARB_texture_storage GL_FALSE
#define GLEW_ARB_copy_image GL_FALSE
#define GLEW_EXT_texture_array GL_FALSE
#define GLEW_ARB_draw_indirect GL_FALSE
#define GLEW_ARB_base_instance GL_FALSE

#define GLXEW_SGI_video_sync GL_FALSE

GLenum glewInit();

const GLubyte* glewGetString(GLenum name);

GLboolean glewIsSupported(const char* name);
GLboolean glewIsExtensionSupported(const char* name);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _GLEW_STUB_H_

