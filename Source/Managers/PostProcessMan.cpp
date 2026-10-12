#include "PostProcessMan.h"

#include "CameraMan.h"
#include "WindowMan.h"
#include "FrameMan.h"
#include "Scene.h"
#include "ContentFile.h"
#include "Matrix.h"

#include "PresetMan.h"
#include "GLResourceMan.h"
#include "RenderTarget.h"

#include "GLCheck.h"
#include "glad/gl.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "tracy/Tracy.hpp"
#include "tracy/TracyOpenGL.hpp"
#include "raylib/raylib.h"

#include <array>

using namespace RTE;

PostProcessMan::PostProcessMan() {
	Clear();
}

PostProcessMan::~PostProcessMan() {
	Destroy();
}

void PostProcessMan::Clear() {
	m_PostScreenEffects.clear();
	m_PostSceneEffects.clear();
	m_YellowGlow = nullptr;
	m_YellowGlowHash = 0;
	m_RedGlow = nullptr;
	m_RedGlowHash = 0;
	m_BlueGlow = nullptr;
	m_BlueGlowHash = 0;
	m_TempEffectBitmaps.clear();
	m_BackBuffer8 = 0;
	m_Palette8Texture = 0;
	m_PostProcessFramebuffer = 0;
	m_VertexBuffer = 0;
	m_VertexArray = 0;
	for (int i = 0; i < c_MaxScreenCount; ++i) {
		m_ScreenRelativeEffects[i].clear();
	}
	m_AmbientRed = 1.0F;
	m_AmbientGreen = 1.0F;
	m_AmbientBlue = 1.0F;
	m_AmbientLightDimming = 1.0F;
	m_Lights.clear();
	m_LightsSimUpdate = -1;
	m_LastDrawnLights.clear();
	m_GlowEffectLightColors.clear();
}

int PostProcessMan::Initialize() {
	InitializeGLPointers();
	CreateGLBackBuffers();

	m_Blit8 = std::make_unique<Shader>(g_PresetMan.GetFullModulePath("Base.rte/Shaders/Blit8.vert"), g_PresetMan.GetFullModulePath("Base.rte/Shaders/Blit8.frag"));
	m_PostProcessShader = std::make_unique<Shader>(g_PresetMan.GetFullModulePath("Base.rte/Shaders/PostProcess.vert"), g_PresetMan.GetFullModulePath("Base.rte/Shaders/PostProcess.frag"));
	m_LightingShader = std::make_unique<Shader>(g_PresetMan.GetFullModulePath("Base.rte/Shaders/PostProcess.vert"), g_PresetMan.GetFullModulePath("Base.rte/Shaders/Lighting.frag"));
	// TODO: Make more robust and load more glows!
	ContentFile glowFile("Base.rte/Effects/Glows/YellowTiny.png");
	m_YellowGlow = glowFile.GetAsBitmap();
	m_YellowGlowHash = glowFile.GetHash();
	glowFile.SetDataPath("Base.rte/Effects/Glows/RedTiny.png");
	m_RedGlow = glowFile.GetAsBitmap();
	m_RedGlowHash = glowFile.GetHash();
	glowFile.SetDataPath("Base.rte/Effects/Glows/BlueTiny.png");
	m_BlueGlow = glowFile.GetAsBitmap();
	m_BlueGlowHash = glowFile.GetHash();

	// Create temporary bitmaps to rotate post effects in.
	m_TempEffectBitmaps = {
	    {16, create_bitmap(16, 16)},
	    {32, create_bitmap(32, 32)},
	    {64, create_bitmap(64, 64)},
	    {128, create_bitmap(128, 128)},
	    {256, create_bitmap(256, 256)},
	    {512, create_bitmap(512, 512)}};

	return 0;
}

void PostProcessMan::InitializeGLPointers() {
	GL_CHECK(glGenTextures(1, &m_BackBuffer8));
	GL_CHECK(glGenTextures(1, &m_Palette8Texture));
	GL_CHECK(glGenVertexArrays(1, &m_VertexArray));
	GL_CHECK(glGenBuffers(1, &m_VertexBuffer));
}

void PostProcessMan::DestroyGLPointers() {
	GL_CHECK(glDeleteTextures(1, &m_BackBuffer8));
	GL_CHECK(glDeleteTextures(1, &m_Palette8Texture));
	GL_CHECK(glDeleteVertexArrays(1, &m_VertexArray));
	GL_CHECK(glDeleteBuffers(1, &m_VertexBuffer));
}

void PostProcessMan::CreateGLBackBuffers() {
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_BackBuffer8));
	GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_FrameMan.GetBackBuffer8()->w, g_FrameMan.GetBackBuffer8()->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_Palette8Texture));
	GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, c_PaletteEntriesNumber, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	UpdatePalette();

	m_BlitFramebuffer = std::make_unique<RenderTarget>(FloatRect(0, 0 ,g_FrameMan.GetBackBuffer32()->w, g_FrameMan.GetBackBuffer32()->h), FloatRect(0, 0 ,g_FrameMan.GetBackBuffer32()->w, g_FrameMan.GetBackBuffer32()->h));
	m_PostProcessFramebuffer = std::make_unique<RenderTarget>(FloatRect(0, 0 ,g_FrameMan.GetBackBuffer32()->w, g_FrameMan.GetBackBuffer32()->h), FloatRect(0, 0 ,g_FrameMan.GetBackBuffer32()->w, g_FrameMan.GetBackBuffer32()->h));

	GL_CHECK(glActiveTexture(GL_TEXTURE0));
	m_ProjectionMatrix = std::make_unique<glm::mat4>(glm::ortho(0.0F, static_cast<float>(g_WindowMan.GetResX()), 0.0F, static_cast<float>(g_WindowMan.GetResY()), -1.0F, 1.0F));
}

void PostProcessMan::UpdatePalette() {
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_Palette8Texture));
	std::array<unsigned int, c_PaletteEntriesNumber> palette;
	for (int i = 0; i < c_PaletteEntriesNumber; ++i) {
		if (i == g_MaskColor) {
			palette[i] = 0;
			continue;
		}
		palette[i] = makeacol32(getr8(i), getg8(i), getb8(i), 255);
	}
	GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, c_PaletteEntriesNumber, 1, GL_RGBA, GL_UNSIGNED_BYTE, palette.data()));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
}

void PostProcessMan::Destroy() {
	for (std::pair<int, BITMAP*> tempBitmapEntry: m_TempEffectBitmaps) {
		destroy_bitmap(tempBitmapEntry.second);
	}
	DestroyGLPointers();
	ClearScreenPostEffects();
	ClearScenePostEffects();
	Clear();
}

void PostProcessMan::AdjustEffectsPosToPlayerScreen(int playerScreen, BITMAP* targetBitmap, const Vector& targetBitmapOffset, std::list<PostEffect>& screenRelativeEffectsList, std::list<Box>& screenRelativeGlowBoxesList) {
	int screenOcclusionOffsetX = g_CameraMan.GetScreenOcclusion(playerScreen).GetFloorIntX();
	int screenOcclusionOffsetY = g_CameraMan.GetScreenOcclusion(playerScreen).GetFloorIntY();
	int occludedOffsetX = targetBitmap->w + screenOcclusionOffsetX;
	int occludedOffsetY = targetBitmap->h + screenOcclusionOffsetY;

	// Adjust for the player screen's position on the final buffer
	for (const PostEffect& postEffect: screenRelativeEffectsList) {
		// Make sure we won't be adding any effects to a part of the screen that is occluded by menus and such
		if (postEffect.m_Pos.GetFloorIntX() > screenOcclusionOffsetX && postEffect.m_Pos.GetFloorIntY() > screenOcclusionOffsetY && postEffect.m_Pos.GetFloorIntX() < occludedOffsetX && postEffect.m_Pos.GetFloorIntY() < occludedOffsetY) {
			m_PostScreenEffects.emplace_back(postEffect.m_Pos + targetBitmapOffset, postEffect.m_Bitmap, postEffect.m_BitmapHash, postEffect.m_Strength, postEffect.m_Angle);
		}
	}
	// Adjust glow areas for the player screen's position on the final buffer
	for (const Box& glowBox: screenRelativeGlowBoxesList) {
		m_PostScreenGlowBoxes.push_back(glowBox);
		// Adjust each added glow area for the player screen's position on the final buffer
		m_PostScreenGlowBoxes.back().m_Corner += targetBitmapOffset;
	}
}

void PostProcessMan::RegisterPostEffect(const Vector& effectPos, BITMAP* effect, size_t hash, int strength, float angle) {
	// These effects get applied when there's a drawn frame that followed one or more sim updates.
	// They are not only registered on drawn sim updates; flashes and stuff could be missed otherwise if they occur on undrawn sim updates.

	if (effect && g_TimerMan.SimUpdatesSinceDrawn() >= 0) {
		m_PostSceneEffects.push_back(PostEffect(effectPos, effect, hash, strength, angle));
	}
}

bool PostProcessMan::GetPostScreenEffectsWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::list<PostEffect>& effectsList, int team) {
	bool found = false;

	// Do the first unwrapped rect
	found = GetPostScreenEffects(boxPos, boxWidth, boxHeight, effectsList, team);

	int left = boxPos.GetFloorIntX();
	int top = boxPos.GetFloorIntY();
	int right = left + boxWidth;
	int bottom = top + boxHeight;

	if (g_SceneMan.SceneWrapsX()) {
		int sceneWidth = g_SceneMan.GetScene()->GetWidth();
		if (left < 0) {
			found = GetPostScreenEffects(left + sceneWidth, top, right + sceneWidth, bottom, effectsList, team) || found;
		}
		if (right >= sceneWidth) {
			found = GetPostScreenEffects(left - sceneWidth, top, right - sceneWidth, bottom, effectsList, team) || found;
		}
	}
	if (g_SceneMan.SceneWrapsY()) {
		int sceneHeight = g_SceneMan.GetScene()->GetHeight();
		if (top < 0) {
			found = GetPostScreenEffects(left, top + sceneHeight, right, bottom + sceneHeight, effectsList, team) || found;
		}
		if (bottom >= sceneHeight) {
			found = GetPostScreenEffects(left, top - sceneHeight, right, bottom - sceneHeight, effectsList, team) || found;
		}
	}
	return found;
}

BITMAP* PostProcessMan::GetTempEffectBitmap(BITMAP* bitmap) const {
	// Get the largest dimension of the bitmap and convert it to a multiple of 16, i.e. 16, 32, etc
	int bitmapSizeNeeded = static_cast<int>(std::ceil(static_cast<float>(std::max(bitmap->w, bitmap->h)) / 16.0F)) * 16;
	std::unordered_map<int, BITMAP*>::const_iterator correspondingBitmapSizeEntry = m_TempEffectBitmaps.find(bitmapSizeNeeded);

	// If we didn't find a match then the bitmap size is greater than 512 but that's the biggest we've got, so return it
	if (correspondingBitmapSizeEntry == m_TempEffectBitmaps.end()) {
		correspondingBitmapSizeEntry = m_TempEffectBitmaps.find(512);
	}

	return correspondingBitmapSizeEntry->second;
}

void PostProcessMan::RegisterGlowDotEffect(const Vector& effectPos, DotGlowColor color, int strength) {
	// These effects only apply only once per drawn sim update, and only on the first frame drawn after one or more sim updates
	if (color != NoDot && g_TimerMan.DrawnSimUpdate() && g_TimerMan.SimUpdatesSinceDrawn() >= 0) {
		RegisterPostEffect(effectPos, GetDotGlowEffect(color), GetDotGlowEffectHash(color), strength);
	}
}

bool PostProcessMan::GetGlowAreasWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::list<Box>& areaList) const {
	bool foundAny = false;
	Vector intRectPosRelativeToBox;

	// Account for wrapping in any registered glow IntRects, as well as on the box we're testing against
	std::list<IntRect> wrappedGlowRects;

	for (const IntRect& glowArea: m_GlowAreas) {
		g_SceneMan.WrapRect(glowArea, wrappedGlowRects);
	}
	std::list<IntRect> wrappedTestRects;
	g_SceneMan.WrapRect(IntRect(boxPos.GetFloorIntX(), boxPos.GetFloorIntY(), boxPos.GetFloorIntX() + boxWidth, boxPos.GetFloorIntY() + boxHeight), wrappedTestRects);

	// Check for intersections. If any are found, cut down the intersecting IntRect to the bounds of the IntRect we're testing against, then make and store a Box out of it
	for (IntRect& wrappedTestRect: wrappedTestRects) {
		for (const IntRect& wrappedGlowRect: wrappedGlowRects) {
			if (wrappedTestRect.Intersects(wrappedGlowRect)) {
				IntRect cutRect(wrappedGlowRect);
				cutRect.IntersectionCut(wrappedTestRect);
				intRectPosRelativeToBox = Vector(static_cast<float>(cutRect.m_Left) - boxPos.m_X, static_cast<float>(cutRect.m_Top) - boxPos.m_Y);
				areaList.push_back(Box(intRectPosRelativeToBox, static_cast<float>(cutRect.m_Right - cutRect.m_Left), static_cast<float>(cutRect.m_Bottom - cutRect.m_Top)));
				foundAny = true;
			}
		}
	}
	return foundAny;
}

bool PostProcessMan::GetPostScreenEffects(Vector boxPos, int boxWidth, int boxHeight, std::list<PostEffect>& effectsList, int team) {
	bool found = false;
	bool unseen = false;
	Vector postEffectPosRelativeToBox;

	if (g_SceneMan.GetScene()) {
		for (PostEffect& scenePostEffect: m_PostSceneEffects) {
			if (team != Activity::NoTeam) {
				unseen = g_SceneMan.IsUnseen(scenePostEffect.m_Pos.GetFloorIntX(), scenePostEffect.m_Pos.GetFloorIntY(), team);
			}

			if (WithinBox(scenePostEffect.m_Pos, boxPos, static_cast<float>(boxWidth), static_cast<float>(boxHeight)) && !unseen) {
				found = true;
				postEffectPosRelativeToBox = scenePostEffect.m_Pos - boxPos;
				effectsList.push_back(PostEffect(postEffectPosRelativeToBox, scenePostEffect.m_Bitmap, scenePostEffect.m_BitmapHash, scenePostEffect.m_Strength, scenePostEffect.m_Angle));
			}
		}
	}
	return found;
}

bool PostProcessMan::GetPostScreenEffects(int left, int top, int right, int bottom, std::list<PostEffect>& effectsList, int team) {
	bool found = false;
	bool unseen = false;
	Vector postEffectPosRelativeToBox;

	for (PostEffect& scenePostEffect: m_PostSceneEffects) {
		if (team != Activity::NoTeam) {
			unseen = g_SceneMan.IsUnseen(scenePostEffect.m_Pos.GetFloorIntX(), scenePostEffect.m_Pos.GetFloorIntY(), team);
		}

		if (WithinBox(scenePostEffect.m_Pos, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)) && !unseen) {
			found = true;
			postEffectPosRelativeToBox = Vector(scenePostEffect.m_Pos.m_X - static_cast<float>(left), scenePostEffect.m_Pos.m_Y - static_cast<float>(top));
			effectsList.push_back(PostEffect(postEffectPosRelativeToBox, scenePostEffect.m_Bitmap, scenePostEffect.m_BitmapHash, scenePostEffect.m_Strength, scenePostEffect.m_Angle));
		}
	}
	return found;
}

BITMAP* PostProcessMan::GetDotGlowEffect(DotGlowColor whichColor) const {
	switch (whichColor) {
		case NoDot:
			return nullptr;
		case YellowDot:
			return m_YellowGlow;
		case RedDot:
			return m_RedGlow;
		case BlueDot:
			return m_BlueGlow;
		default:
			RTEAbort("Undefined glow dot color value passed in. See DotGlowColor enumeration for defined values.");
			return nullptr;
	}
}

size_t PostProcessMan::GetDotGlowEffectHash(DotGlowColor whichColor) const {
	switch (whichColor) {
		case NoDot:
			return 0;
		case YellowDot:
			return m_YellowGlowHash;
		case RedDot:
			return m_RedGlowHash;
		case BlueDot:
			return m_BlueGlowHash;
		default:
			RTEAbort("Undefined glow dot color value passed in. See DotGlowColor enumeration for defined values.");
			return 0;
	}
}

void PostProcessMan::PostProcess() {
	ZoneScoped;
	TracyGpuZone("PostProcess");
	UpdatePalette();

	// First copy the current 8bpp backbuffer to the 32bpp buffer; we'll add effects to it
	m_PostProcessFramebuffer->Begin(true);
	//m_Blit8->Begin();
	//int paletteUniform = m_Blit8->GetUniformLocation("rtePalette");
	//rlSetUniformSampler(paletteUniform, m_Palette8Texture);
	rlDisableColorBlend();
	rlDisableDepthTest();
	DrawTextureRec(g_FrameMan.GetBackBuffer()->GetColorTexture(), {0, 0, g_FrameMan.GetBackBuffer()->GetSize().w, -g_FrameMan.GetBackBuffer()->GetSize().h}, {0.0f, 0.0f}, {255, 255, 255, 255});
	//m_Blit8->End();

	// Set the screen blender mode for glows
	set_screen_blender(128, 128, 128, 128);
	rlEnableColorBlend();
	rlSetBlendFactorsSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD, GL_FUNC_ADD);
	rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
	m_PostProcessFramebuffer->End();
	m_PostProcessFramebuffer->Begin(false);

	m_PostProcessShader->Begin();

	DrawDotGlowEffects();
	DrawPostScreenEffects();

	// Clear the effects list for this frame
	m_PostScreenEffects.clear();
	m_PostProcessShader->End();
	m_PostProcessFramebuffer->End();
}

void PostProcessMan::DrawDotGlowEffects() {
	int startX = 0;
	int startY = 0;
	int endX = 0;
	int endY = 0;
	int testpixel = 0;

	Texture2D yellowGlow = g_GLResourceMan.GetStaticTextureFromBitmap(m_YellowGlow);

	// Randomly sample the entire backbuffer, looking for pixels to put a glow on.
	for (const Box& glowBox: m_PostScreenGlowBoxes) {
		startX = glowBox.m_Corner.GetFloorIntX();
		startY = glowBox.m_Corner.GetFloorIntY();
		endX = startX + static_cast<int>(glowBox.m_Width);
		endY = startY + static_cast<int>(glowBox.m_Height);

		// Sanity check a little at least
		if (startX < 0 || startX >= g_FrameMan.GetBackBuffer8()->w || startY < 0 || startY >= g_FrameMan.GetBackBuffer8()->h ||
		    endX < 0 || endX >= g_FrameMan.GetBackBuffer8()->w || endY < 0 || endY >= g_FrameMan.GetBackBuffer8()->h) {
			continue;
		}

#ifdef DEBUG_BUILD
		// Draw a rectangle around the glow box so we see it's position and size
		rect(g_FrameMan.GetBackBuffer32(), startX, startY, endX, endY, g_RedColor);
#endif

		for (int y = startY; y < endY; ++y) {
			for (int x = startX; x < endX; ++x) {
				testpixel = _getpixel(g_FrameMan.GetBackBuffer8(), x, y);

				// YELLOW
				if ((testpixel == g_YellowGlowColor && RandomNum() < 0.9F) || testpixel == 98 || (testpixel == 120 && RandomNum() < 0.7F)) {
					DrawTexture(yellowGlow, x - yellowGlow.width / 2, y - yellowGlow.height / 2, {255, 255, 255, 255});
				}
				// TODO: Enable and add more colors once we actually have something that needs these.
				// RED
				/*
				if (testpixel == 13) {
				    draw_trans_sprite(m_BackBuffer32, m_RedGlow, x - 2, y - 2);
				}
				// BLUE
				if (testpixel == 166) {
				    draw_trans_sprite(g_FrameMan.GetBackBuffer32(), m_BlueGlow, x - 2, y - 2);
				}
				*/
			}
		}
	}
}

void PostProcessMan::DrawPostScreenEffects() {
	int effectPosX = 0;
	int effectPosY = 0;
	unsigned char effectStrength = 0;

	GL_CHECK(glActiveTexture(GL_TEXTURE0));
	GL_CHECK(glBindVertexArray(m_VertexArray));
	m_PostProcessShader->SetInt(m_PostProcessShader->GetTextureUniform(), 0);
	m_PostProcessShader->SetMatrix4f(m_PostProcessShader->GetProjectionUniform(), *m_ProjectionMatrix);
	m_PostProcessShader->SetVector4f(m_PostProcessShader->GetColorUniform(), glm::vec4(1.0f));

	for (const PostEffect& postEffect: m_PostScreenEffects) {
		if (postEffect.m_Bitmap) {
			effectStrength = postEffect.m_Strength;
			effectPosX = postEffect.m_Pos.GetFloorIntX();
			effectPosY = postEffect.m_Pos.GetFloorIntY();
			DrawTexturePro(
			    g_GLResourceMan.GetStaticTextureFromBitmap(postEffect.m_Bitmap),
			    Rectangle(0, 0, postEffect.m_Bitmap->w, postEffect.m_Bitmap->h),
			    Rectangle(effectPosX, effectPosY, postEffect.m_Bitmap->w, postEffect.m_Bitmap->h),
				Vector2(postEffect.m_Bitmap->w / 2, postEffect.m_Bitmap->h / 2),
			    postEffect.m_Angle,
			    {.r=effectStrength, .g=effectStrength, .b=effectStrength, .a=255});
		}
	}
}

void PostProcessMan::SetAmbientLight(float red, float green, float blue) {
	m_AmbientRed = std::clamp(red, 0.0F, 1.0F);
	m_AmbientGreen = std::clamp(green, 0.0F, 1.0F);
	m_AmbientBlue = std::clamp(blue, 0.0F, 1.0F);
}

float PostProcessMan::GetAmbientLightLevel() const {
	return (m_AmbientRed + m_AmbientGreen + m_AmbientBlue) / 3.0F * m_AmbientLightDimming;
}

void PostProcessMan::RegisterLight(const Vector& pos, float radius, float intensity, float red, float green, float blue) {
	if (radius <= 0 || intensity <= 0) {
		return;
	}
	std::scoped_lock<std::mutex> lightsLock(m_LightsMutex);
	// Lights only last for the simulation update they're registered in, so a script that stops registering one turns it off.
	if (m_LightsSimUpdate != g_TimerMan.GetSimUpdateCount()) {
		m_Lights.clear();
		m_LightsSimUpdate = g_TimerMan.GetSimUpdateCount();
	}
	m_Lights.push_back({pos, radius, intensity, std::clamp(red, 0.0F, 1.0F), std::clamp(green, 0.0F, 1.0F), std::clamp(blue, 0.0F, 1.0F)});
}

float PostProcessMan::GetLightLevelAt(const Vector& pos) const {
	float lightLevel = GetAmbientLightLevel();
	std::scoped_lock<std::mutex> lightsLock(m_LightsMutex);
	for (const LightSource& light: m_LastDrawnLights) {
		float distanceRatio = g_SceneMan.ShortestDistance(pos, light.Pos, g_SceneMan.SceneWrapsX()).GetMagnitude() / light.Radius;
		if (distanceRatio < 1.0F) {
			float falloff = 1.0F - distanceRatio;
			lightLevel += light.Intensity * falloff * falloff * (light.Red + light.Green + light.Blue) / 3.0F;
		}
	}
	return lightLevel;
}

std::array<float, 3> PostProcessMan::GetGlowEffectLightColor(BITMAP* glowBitmap) {
	if (auto cachedColor = m_GlowEffectLightColors.find(glowBitmap); cachedColor != m_GlowEffectLightColors.end()) {
		return cachedColor->second;
	}
	int colorDepth = bitmap_color_depth(glowBitmap);
	int centerPixel = getpixel(glowBitmap, glowBitmap->w / 2, glowBitmap->h / 2);
	std::array<float, 3> color = {static_cast<float>(getr_depth(colorDepth, centerPixel)), static_cast<float>(getg_depth(colorDepth, centerPixel)), static_cast<float>(getb_depth(colorDepth, centerPixel))};
	// Glows fade towards their center pixel's color, so normalize it to get the light's hue at full brightness.
	float brightest = std::max({color[0], color[1], color[2], 1.0F});
	for (float& component: color) {
		component /= brightest;
	}
	m_GlowEffectLightColors.try_emplace(glowBitmap, color);
	return color;
}

void PostProcessMan::DrawLighting(const Vector& targetPos, int screenWidth, int screenHeight, const std::list<PostEffect>& screenRelativeEffects) {
	ZoneScoped;

	const float ambientRed = m_AmbientRed * m_AmbientLightDimming;
	const float ambientGreen = m_AmbientGreen * m_AmbientLightDimming;
	const float ambientBlue = m_AmbientBlue * m_AmbientLightDimming;

	std::vector<LightSource> screenLights;
	{
		std::scoped_lock<std::mutex> lightsLock(m_LightsMutex);
		m_LastDrawnLights.clear();
		// Lights registered in an older simulation update are stale; whatever registered them has stopped.
		if (m_LightsSimUpdate == g_TimerMan.GetSimUpdateCount()) {
			m_LastDrawnLights = m_Lights;
		}
		for (const PostEffect& effect: screenRelativeEffects) {
			if (effect.m_Bitmap) {
				std::array<float, 3> color = GetGlowEffectLightColor(effect.m_Bitmap);
				float radius = static_cast<float>(std::max(effect.m_Bitmap->w, effect.m_Bitmap->h)) * 1.5F;
				m_LastDrawnLights.push_back({effect.m_Pos + targetPos, radius, static_cast<float>(effect.m_Strength) / 255.0F, color[0], color[1], color[2]});
			}
		}
		if (ambientRed >= 1.0F && ambientGreen >= 1.0F && ambientBlue >= 1.0F) {
			// Full daylight, nothing to darken.
			return;
		}

		// Convert the lights to screen coordinates, accounting for wrapping, and keep those that reach the screen.
		const float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
		for (const LightSource& light: m_LastDrawnLights) {
			Vector screenPos = light.Pos - targetPos;
			if (g_SceneMan.SceneWrapsX()) {
				if (screenPos.m_X < -light.Radius) {
					screenPos.m_X += sceneWidth;
				} else if (screenPos.m_X > static_cast<float>(screenWidth) + light.Radius) {
					screenPos.m_X -= sceneWidth;
				}
			}
			if (screenPos.m_X > -light.Radius && screenPos.m_X < static_cast<float>(screenWidth) + light.Radius && screenPos.m_Y > -light.Radius && screenPos.m_Y < static_cast<float>(screenHeight) + light.Radius) {
				screenLights.push_back({screenPos, light.Radius, light.Intensity, light.Red, light.Green, light.Blue});
				if (screenLights.size() >= c_MaxLightsPerScreen) {
					break;
				}
			}
		}
	}

	std::vector<float> posRadiusIntensity;
	std::vector<float> colors;
	posRadiusIntensity.reserve(screenLights.size() * 4);
	colors.reserve(screenLights.size() * 3);
	for (const LightSource& light: screenLights) {
		posRadiusIntensity.insert(posRadiusIntensity.end(), {light.Pos.m_X, light.Pos.m_Y, light.Radius, light.Intensity});
		colors.insert(colors.end(), {light.Red, light.Green, light.Blue});
	}

	// Whatever was drawn so far has to be flushed with its own shader before switching.
	rlDrawRenderBatchActive();
	m_LightingShader->Begin();
	m_LightingShader->SetVector2f("rteScreenSize", glm::vec2(static_cast<float>(screenWidth), static_cast<float>(screenHeight)));
	m_LightingShader->SetVector3f("rteAmbientLight", glm::vec3(ambientRed, ambientGreen, ambientBlue));
	m_LightingShader->SetInt("rteLightCount", static_cast<int>(screenLights.size()));
	if (!screenLights.empty()) {
		rlSetUniform(m_LightingShader->GetUniformLocation("rteLightPosRadiusIntensity"), posRadiusIntensity.data(), RL_SHADER_UNIFORM_VEC4, static_cast<int>(screenLights.size()));
		rlSetUniform(m_LightingShader->GetUniformLocation("rteLightColor"), colors.data(), RL_SHADER_UNIFORM_VEC3, static_cast<int>(screenLights.size()));
	}

	// Multiply what's on the screen by the light, keeping its alpha.
	rlDisableDepthTest();
	rlEnableColorBlend();
	rlSetBlendFactorsSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE, GL_FUNC_ADD, GL_FUNC_ADD);
	rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);

	const float width = static_cast<float>(screenWidth);
	const float height = static_cast<float>(screenHeight);
	rlBegin(RL_QUADS);
	rlColor4ub(255, 255, 255, 255);
	rlTexCoord2f(0.0F, 0.0F);
	rlVertex2f(0.0F, 0.0F);
	rlTexCoord2f(0.0F, 1.0F);
	rlVertex2f(0.0F, height);
	rlTexCoord2f(1.0F, 1.0F);
	rlVertex2f(width, height);
	rlTexCoord2f(1.0F, 0.0F);
	rlVertex2f(width, 0.0F);
	rlEnd();
	rlDrawRenderBatchActive();

	m_LightingShader->End();
	rlSetBlendMode(RL_BLEND_ALPHA);
	rlEnableDepthTest();
}
