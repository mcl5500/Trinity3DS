/*
 * gl_stubs.c - Stubs for OpenGL rendering functions when running in software mode on 3DS
 */

#include "doomtype.h"
#include "doomstat.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_main.h"
#include "p_spec.h"
#include "gl_struct.h"

// Variables referenced outside GL renderer
TAnimItemParam *anim_flats = NULL;
TAnimItemParam *anim_textures = NULL;
int gl_drawskys = skytype_none;
dboolean gl_use_stencil = false;
dboolean gl_ext_texture_filter_anisotropic = false;
dboolean gl_arb_texture_compression = false;
dboolean gl_ext_framebuffer_object = false;
dboolean gl_ext_packed_depth_stencil = false;
dboolean gl_ext_blend_color = false;
dboolean gl_ext_arb_vertex_buffer_object = false;
dboolean gl_arb_pixel_buffer_object = false;
dboolean gl_arb_shader_objects = false;
int gl_max_texture_size = 512;

// dsda_GL stubs
void dsda_GLSetRenderViewportParams(void) {}
void dsda_GLSetRenderViewport(void) {}
void dsda_GLSetRenderViewportScissor(void) {}
void dsda_GLSetRenderSceneScissor(void) {}
void dsda_GLSetScreenSpaceScissor(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void dsda_GLUpdateStatusBarVisible(void) {}
void dsda_GLLetterboxClear(void) {}
void dsda_GLStartMeltRenderTexture(void) {}
void dsda_GLEndMeltRenderTexture(void) {}
void dsda_GLFullscreenOrtho2D(void) {}

// Stubs
void gld_MultisamplingInit(void) {}
void gld_MultisamplingSet(void) {}
void gld_ResetAutomapTransparency(void) {}
void gld_PreprocessLevel(void) {}
void gld_CleanMemory(void) {}
void gld_CleanStaticMemory(void) {}
void gld_UpdateSplitData(sector_t *sector) { (void)sector; }
void gld_InitCommandLine(void) {}
void gld_Init(int width, int height) { (void)width; (void)height; }
void gld_InitDrawScene(void) {}
void gld_StartDrawScene(void) {}
void gld_DrawScene(player_t *player) { (void)player; }
void gld_EndDrawScene(void) {}
void gld_FrustumSetup(void) {}
void gld_DrawMapLines(void) {}
void gld_ProcessTexturedMap(void) {}
void gld_ResetTexturedAutomap(void) {}
void gld_MapDrawSubsectors(player_t *plr, int fx, int fy, fixed_t mx, fixed_t my, int fw, int fh, fixed_t scale) {
  (void)plr; (void)fx; (void)fy; (void)mx; (void)my; (void)fw; (void)fh; (void)scale;
}

int gld_wipe_StartScreen(void) { return 0; }
int gld_wipe_EndScreen(void) { return 0; }
int gld_wipe_doMelt(int ticks, int *y_lookup) { (void)ticks; (void)y_lookup; return 0; }
int gld_wipe_exitMelt(int ticks) { (void)ticks; return 0; }

void gld_AddPlane(int subsectornum, visplane_t *floor, visplane_t *ceiling) {
  (void)subsectornum; (void)floor; (void)ceiling;
}
void gld_AddWall(seg_t *seg) { (void)seg; }
void gld_ProjectSprite(mobj_t* thing, int lightlevel) { (void)thing; (void)lightlevel; }
void gld_DrawWeapon(int weaponlump, vissprite_t *vis, int lightlevel) {
  (void)weaponlump; (void)vis; (void)lightlevel;
}

void gld_DrawShaded(int x, int y, int width, int height, int shade) {
  (void)x; (void)y; (void)width; (void)height; (void)shade;
}
void gld_DrawLine(int x0, int y0, int x1, int y1, int BaseColor) {
  (void)x0; (void)y0; (void)x1; (void)y1; (void)BaseColor;
}
void gld_DrawLine_f(float x0, float y0, float x1, float y1, int BaseColor) {
  (void)x0; (void)y0; (void)x1; (void)y1; (void)BaseColor;
}
void gld_FillRaw(int lump, int x, int y, int src_width, int src_height, int dst_width, int dst_height, enum patch_translation_e flags) {
  (void)lump; (void)x; (void)y; (void)src_width; (void)src_height; (void)dst_width; (void)dst_height; (void)flags;
}
void gld_FillPatch(int lump, int x, int y, int width, int height, enum patch_translation_e flags) {
  (void)lump; (void)x; (void)y; (void)width; (void)height; (void)flags;
}
void gld_FillBlock(int x, int y, int width, int height, int col) {
  (void)x; (void)y; (void)width; (void)height; (void)col;
}
void gld_DrawNumPatch(int x, int y, int lump, dboolean center, int cm, enum patch_translation_e flags) {
  (void)x; (void)y; (void)lump; (void)center; (void)cm; (void)flags;
}
void gld_DrawNumPatch_f(float x, float y, int lump, dboolean center, int cm, enum patch_translation_e flags) {
  (void)x; (void)y; (void)lump; (void)center; (void)cm; (void)flags;
}

void gld_BeginUIDraw(void) {}
void gld_EndUIDraw(void) {}
void gld_BeginAutomapDraw(void) {}
void gld_EndAutomapDraw(void) {}
void gld_BeginMenuDraw(void) {}
void gld_EndMenuDraw(void) {}

void gld_SetPalette(int palette) { (void)palette; }
void gld_FlushTextures(void) {}

dboolean gld_clipper_SafeCheckRange(angle_t startAngle, angle_t endAngle) {
  (void)startAngle; (void)endAngle;
  return false;
}
void gld_clipper_SafeAddClipRange(angle_t startangle, angle_t endangle) {
  (void)startangle; (void)endangle;
}
