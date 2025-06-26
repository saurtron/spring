#ifndef _FT_LIBRARY_HANDLER_H
#define _FT_LIBRARY_HANDLER_H

struct FT_FaceRec_;
typedef struct FT_FaceRec_* FT_Face;

typedef struct _FcConfig    FcConfig;
typedef struct _FcFontSet    FcFontSet;
typedef struct _FcPattern    FcPattern;

class FtLibraryHandlerProxy {
public:
	static void InitFtLibrary();
	static bool InitFontconfig(bool console);
#ifndef HEADLESS
	static int NewMemoryFace(const unsigned char* file_base, signed long file_size, signed long face_index, FT_Face *aface);
#endif
	static bool CanUseFontConfig();
	static FcFontSet* GetGameFontSet();
	static FcPattern* GetBasePattern();
	static void ClearGameFontSet();
	static void ClearBasePattern();
	static FcConfig* GetFCConfig();
};

#endif // FT_LIBRARY_HANDLER_H
