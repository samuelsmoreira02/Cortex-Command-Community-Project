#include "TerrainFire.h"
#include "SLTerrain.h"
#include "Material.h"
#include "MovableObject.h"
#include "SceneMan.h"
#include "MovableMan.h"
#include "PresetMan.h"
#include "TimerMan.h"
#include "Color.h"
#include "RTETools.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace RTE;

namespace {
	/// Offsets of the neighbours fire can spread to, and how likely it is to spread in that direction. Fire climbs much more readily than it creeps down.
	struct SpreadDirection {
		int X;
		int Y;
		float Weight;
	};
	constexpr std::array<SpreadDirection, 8> c_SpreadDirections = {{
	    {0, -1, 1.0F}, {-1, -1, 0.8F}, {1, -1, 0.8F},
	    {-1, 0, 0.55F}, {1, 0, 0.55F},
	    {-1, 1, 0.2F}, {1, 1, 0.2F}, {0, 1, 0.25F},
	}};

	/// The flame particle burning terrain spawns. It's the flamethrower's flame, so it glows and burns whatever it touches.
	const std::string c_FlamePresetName = "Flame Hurt Short Float";
	const std::string c_FlameModuleName = "Base.rte";
} // namespace

void TerrainFire::Clear() {
	m_BurningPixels.clear();
	m_BurningPositions.clear();
	m_TimeSinceUpdateMS = 0;
	m_FireColors.clear();
}

bool TerrainFire::IgnitePixel(SLTerrain* terrain, int x, int y) {
	if (y < 0 || y >= terrain->GetHeight() || m_BurningPixels.size() >= c_MaxBurningPixels) {
		return false;
	}
	if (x < 0 || x >= terrain->GetWidth()) {
		if (!g_SceneMan.SceneWrapsX()) {
			return false;
		}
		x = (x % terrain->GetWidth() + terrain->GetWidth()) % terrain->GetWidth();
	}
	const Material* material = g_SceneMan.GetMaterialFromID(terrain->GetMaterialPixel(x, y));
	if (!material || material->GetFlammability() <= 0) {
		return false;
	}
	if (!m_BurningPositions.insert(y * terrain->GetWidth() + x).second) {
		return false;
	}
	// Vary burn times a little so a patch of the same material doesn't all burn out in the same instant.
	m_BurningPixels.push_back({x, y, static_cast<float>(material->GetBurnTimeMS()) * RandomNum(0.75F, 1.25F)});
	return true;
}

int TerrainFire::Ignite(SLTerrain* terrain, const Vector& pos, float radius) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	int ignited = 0;
	const int centerX = pos.GetFloorIntX();
	const int centerY = pos.GetFloorIntY();
	const int reach = static_cast<int>(std::ceil(radius));
	for (int y = centerY - reach; y <= centerY + reach; ++y) {
		for (int x = centerX - reach; x <= centerX + reach; ++x) {
			if ((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY) <= radius * radius && IgnitePixel(terrain, x, y)) {
				++ignited;
			}
		}
	}
	return ignited;
}

int TerrainFire::Extinguish(const Vector& pos, float radius) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	const float radiusSquared = radius * radius;
	const size_t burningBefore = m_BurningPixels.size();
	std::erase_if(m_BurningPixels, [&](const BurningPixel& pixel) {
		if (g_SceneMan.ShortestDistance(pos, Vector(static_cast<float>(pixel.X), static_cast<float>(pixel.Y)), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() > radiusSquared) {
			return false;
		}
		m_BurningPositions.erase(pixel.Y * g_SceneMan.GetSceneWidth() + pixel.X);
		return true;
	});
	return static_cast<int>(burningBefore - m_BurningPixels.size());
}

void TerrainFire::SpawnFlame(int x, int y) const {
	const Entity* flamePreset = g_PresetMan.GetEntityPreset("MOSParticle", c_FlamePresetName, c_FlameModuleName);
	if (MovableObject* flame = flamePreset ? dynamic_cast<MovableObject*>(flamePreset->Clone()) : nullptr) {
		flame->SetPos(Vector(static_cast<float>(x), static_cast<float>(y) - 1.0F));
		flame->SetVel(Vector(RandomNum(-1.0F, 1.0F), RandomNum(-3.0F, -1.0F)));
		g_MovableMan.AddParticle(flame);
	}
}

void TerrainFire::Update(SLTerrain* terrain) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	if (m_BurningPixels.empty() || !terrain) {
		return;
	}
	m_TimeSinceUpdateMS += g_TimerMan.GetDeltaTimeMS();
	if (m_TimeSinceUpdateMS < c_UpdateIntervalMS) {
		return;
	}
	const float elapsedMS = m_TimeSinceUpdateMS;
	m_TimeSinceUpdateMS = 0;

	if (m_FireColors.empty()) {
		for (const Color& fireColor: {Color(255, 220, 80), Color(255, 150, 30), Color(240, 90, 20), Color(200, 50, 20)}) {
			m_FireColors.push_back(fireColor.GetIndex());
		}
	}

	const int width = terrain->GetWidth();
	int flamesSpawned = 0;
	bool anyBurnedOut = false;
	int minX = width, minY = terrain->GetHeight(), maxX = 0, maxY = 0;

	// Burn and spread. Pixels that catch fire this update are appended, and only start spreading next update.
	const size_t burningCount = m_BurningPixels.size();
	for (size_t i = 0; i < burningCount; ++i) {
		// Igniting neighbours appends to m_BurningPixels, which can reallocate it, so don't hold a reference to an element across that.
		const int pixelX = m_BurningPixels[i].X;
		const int pixelY = m_BurningPixels[i].Y;

		// The pixel may have been blown away or fallen with collapsing terrain since it caught fire, so stop burning it if what's there now can't burn.
		const Material* burningMaterial = g_SceneMan.GetMaterialFromID(terrain->GetMaterialPixel(pixelX, pixelY));
		if (!burningMaterial || burningMaterial->GetFlammability() <= 0) {
			m_BurningPixels[i].TimeLeftMS = 0;
			m_BurningPositions.erase(pixelY * width + pixelX);
			continue;
		}
		m_BurningPixels[i].TimeLeftMS -= elapsedMS;

		for (const SpreadDirection& direction: c_SpreadDirections) {
			int neighbourX = pixelX + direction.X;
			int neighbourY = pixelY + direction.Y;
			if (neighbourY < 0 || neighbourY >= terrain->GetHeight()) {
				continue;
			}
			if (neighbourX < 0 || neighbourX >= width) {
				if (!g_SceneMan.SceneWrapsX()) {
					continue;
				}
				neighbourX = (neighbourX + width) % width;
			}
			const Material* neighbourMaterial = g_SceneMan.GetMaterialFromID(terrain->GetMaterialPixel(neighbourX, neighbourY));
			if (neighbourMaterial && neighbourMaterial->GetFlammability() > 0 && RandomNum() < neighbourMaterial->GetFlammability() * direction.Weight * c_SpreadChancePerUpdate) {
				IgnitePixel(terrain, neighbourX, neighbourY);
			}
		}

		// Flames rise from burning pixels that are open to the air above.
		if (flamesSpawned < c_MaxFlamesPerUpdate && pixelY > 0 && terrain->GetMaterialPixel(pixelX, pixelY - 1) == MaterialColorKeys::g_MaterialAir && RandomNum() < 0.05F) {
			SpawnFlame(pixelX, pixelY);
			++flamesSpawned;
		}

		if (m_BurningPixels[i].TimeLeftMS > 0) {
			terrain->SetFGColorPixel(pixelX, pixelY, m_FireColors[RandomNum<int>(0, static_cast<int>(m_FireColors.size()) - 1)]);
		} else {
			const unsigned char burntMaterialIndex = burningMaterial->GetBurnsIntoIndex();
			const Material* burntMaterial = g_SceneMan.GetMaterialFromID(burntMaterialIndex);
			terrain->SetMaterialPixel(pixelX, pixelY, burntMaterialIndex);
			terrain->SetFGColorPixel(pixelX, pixelY, (burntMaterialIndex == MaterialColorKeys::g_MaterialAir || !burntMaterial) ? ColorKeys::g_MaskColor : burntMaterial->GetColorIndex());
			m_BurningPositions.erase(pixelY * width + pixelX);
			anyBurnedOut = true;
			if (burntMaterialIndex == MaterialColorKeys::g_MaterialAir) {
				// Burning away may leave what the pixel was holding up hanging, so let it fall.
				g_SceneMan.RegisterRemovedTerrain(pixelX, pixelY);
			}
			minX = std::min(minX, pixelX);
			minY = std::min(minY, pixelY);
			maxX = std::max(maxX, pixelX);
			maxY = std::max(maxY, pixelY);
		}
	}
	std::erase_if(m_BurningPixels, [](const BurningPixel& pixel) { return pixel.TimeLeftMS <= 0; });

	// Burnt away terrain may open up new paths, so let pathfinding know.
	if (anyBurnedOut) {
		terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(minX), static_cast<float>(minY)), static_cast<float>(maxX - minX + 1), static_cast<float>(maxY - minY + 1)));
	}
}
