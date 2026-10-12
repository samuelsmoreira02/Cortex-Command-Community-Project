#include "TerrainCollapse.h"
#include "SLTerrain.h"
#include "Material.h"
#include "MOPixel.h"
#include "Atom.h"
#include "Actor.h"
#include "SoundContainer.h"
#include "SceneMan.h"
#include "MovableMan.h"
#include "CameraMan.h"
#include "PresetMan.h"
#include "TimerMan.h"
#include "RTETools.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace RTE;

namespace {
	/// Neighbour offsets for flood filling, ordered so the search pops the ones below first, heading straight for the ground.
	constexpr std::array<std::array<int, 2>, 8> c_SearchOffsets = {{
	    {0, -1}, {-1, -1}, {1, -1},
	    {-1, 0}, {1, 0},
	    {-1, 1}, {1, 1}, {0, 1},
	}};
} // namespace

TerrainCollapse::TerrainCollapse() {
	Clear();
}

TerrainCollapse::~TerrainCollapse() = default;

void TerrainCollapse::Clear() {
	m_DirtyCells.clear();
	m_FallingChunks.clear();
	m_VisitStamps.clear();
	m_CurrentStamp = 0;
	m_CheckStartStamp = 0;
	m_TimeSinceCheckMS = 0;
}

void TerrainCollapse::MarkAreaDirty(int left, int top, int width, int height) {
	const int sceneWidth = g_SceneMan.GetSceneWidth();
	const int sceneHeight = g_SceneMan.GetSceneHeight();
	if (sceneWidth <= 0 || sceneHeight <= 0) {
		return;
	}
	const int cellsPerRow = (sceneWidth + c_CellSize - 1) / c_CellSize;
	const int cellsPerColumn = (sceneHeight + c_CellSize - 1) / c_CellSize;
	const bool wrapsX = g_SceneMan.SceneWrapsX();

	const int firstCellY = std::max(top / c_CellSize, 0);
	const int lastCellY = std::min((top + height - 1) / c_CellSize, cellsPerColumn - 1);
	const int firstCellX = static_cast<int>(std::floor(static_cast<float>(left) / static_cast<float>(c_CellSize)));
	const int lastCellX = static_cast<int>(std::floor(static_cast<float>(left + width - 1) / static_cast<float>(c_CellSize)));
	for (int cellY = firstCellY; cellY <= lastCellY; ++cellY) {
		for (int cellX = firstCellX; cellX <= lastCellX && cellX - firstCellX < cellsPerRow; ++cellX) {
			int wrappedCellX = cellX;
			if (wrappedCellX < 0 || wrappedCellX >= cellsPerRow) {
				if (!wrapsX) {
					continue;
				}
				wrappedCellX = (wrappedCellX % cellsPerRow + cellsPerRow) % cellsPerRow;
			}
			m_DirtyCells.insert(cellY * cellsPerRow + wrappedCellX);
		}
	}
}

void TerrainCollapse::RegisterRemovedPixel(int x, int y) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	MarkAreaDirty(x, y, 1, 1);
}

void TerrainCollapse::RegisterRemovedArea(int left, int top, int width, int height) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	MarkAreaDirty(left, top, width, height);
}

void TerrainCollapse::Update(SLTerrain* terrain) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	if (!terrain) {
		return;
	}
	const float deltaTimeS = g_TimerMan.GetDeltaTimeSecs();

	if (!m_FallingChunks.empty()) {
		std::erase_if(m_FallingChunks, [&](FallingChunk& chunk) { return !UpdateChunk(terrain, chunk, deltaTimeS); });
	}

	m_TimeSinceCheckMS += g_TimerMan.GetDeltaTimeMS();
	if (!m_DirtyCells.empty() && m_TimeSinceCheckMS >= c_CheckIntervalMS) {
		m_TimeSinceCheckMS = 0;
		CheckDirtyCells(terrain);
	}
}

void TerrainCollapse::CheckDirtyCells(SLTerrain* terrain) {
	const int width = terrain->GetWidth();
	const int height = terrain->GetHeight();
	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	if (m_VisitStamps.size() != pixelCount) {
		m_VisitStamps.assign(pixelCount, 0);
		m_CurrentStamp = 0;
	}
	// Every search of this check takes a new stamp, so make sure there are plenty left before running out and having to start over.
	if (m_CurrentStamp > c_MaxSearchesPerCheck) {
		std::fill(m_VisitStamps.begin(), m_VisitStamps.end(), 0);
		m_CurrentStamp = 0;
	}
	m_CheckStartStamp = m_CurrentStamp + 1;

	// Falling chunks count as supported, so they aren't found again as pieces of their own. They're checked again once they land.
	m_CurrentStamp++;
	for (const FallingChunk& chunk: m_FallingChunks) {
		for (const ChunkPixel& pixel: chunk.Pixels) {
			m_VisitStamps[static_cast<size_t>(pixel.Y) * width + pixel.X] = m_CurrentStamp;
		}
	}

	std::vector<int> cellsToCheck;
	for (auto cellItr = m_DirtyCells.begin(); cellItr != m_DirtyCells.end() && cellsToCheck.size() < c_MaxCellsPerCheck;) {
		cellsToCheck.push_back(*cellItr);
		cellItr = m_DirtyCells.erase(cellItr);
	}

	BITMAP* materialBitmap = terrain->GetMaterialBitmap();
	const bool wrapsX = g_SceneMan.SceneWrapsX();
	const int cellsPerRow = (width + c_CellSize - 1) / c_CellSize;
	std::vector<int> island;
	for (int cell: cellsToCheck) {
		if (m_CurrentStamp - m_CheckStartStamp >= c_MaxSearchesPerCheck - c_CellSize * c_CellSize * 2) {
			// Out of stamps for this check, so leave the rest for the next one.
			m_DirtyCells.insert(cell);
			continue;
		}
		const int cellLeft = (cell % cellsPerRow) * c_CellSize;
		const int cellTop = (cell / cellsPerRow) * c_CellSize;
		// Look one pixel past the cell, since terrain removed at its edge can cut loose the terrain next to it.
		for (int y = std::max(cellTop - 1, 0); y <= std::min(cellTop + c_CellSize, height - 1); ++y) {
			for (int x = cellLeft - 1; x <= cellLeft + c_CellSize; ++x) {
				int wrappedX = x;
				if (wrappedX < 0 || wrappedX >= width) {
					if (!wrapsX) {
						continue;
					}
					wrappedX = (wrappedX + width) % width;
				}
				if (_getpixel(materialBitmap, wrappedX, y) == MaterialColorKeys::g_MaterialAir || m_VisitStamps[static_cast<size_t>(y) * width + wrappedX] >= m_CheckStartStamp) {
					continue;
				}
				island.clear();
				if (FindUnsupportedPiece(terrain, wrappedX, y, island)) {
					if (island.size() >= c_MinChunkPixels && m_FallingChunks.size() >= c_MaxFallingChunks) {
						// Too much is falling already, so try this piece again later.
						m_DirtyCells.insert(cell);
					} else {
						DropPiece(terrain, island);
					}
				}
			}
		}
	}
}

bool TerrainCollapse::FindUnsupportedPiece(SLTerrain* terrain, int seedX, int seedY, std::vector<int>& island) {
	const int width = terrain->GetWidth();
	const int height = terrain->GetHeight();
	BITMAP* materialBitmap = terrain->GetMaterialBitmap();
	const bool wrapsX = g_SceneMan.SceneWrapsX();
	const uint16_t searchStamp = ++m_CurrentStamp;

	std::vector<int> toVisit;
	const int seed = seedY * width + seedX;
	m_VisitStamps[seed] = searchStamp;
	toVisit.push_back(seed);
	island.push_back(seed);

	while (!toVisit.empty()) {
		const int position = toVisit.back();
		toVisit.pop_back();
		const int x = position % width;
		const int y = position / width;
		// Terrain resting on the bottom of the scene, or on its sides if it doesn't wrap, is supported.
		if (y == height - 1 || (!wrapsX && (x == 0 || x == width - 1))) {
			return false;
		}
		for (const std::array<int, 2>& offset: c_SearchOffsets) {
			int neighbourX = x + offset[0];
			const int neighbourY = y + offset[1];
			if (neighbourY < 0) {
				continue;
			}
			if (neighbourX < 0 || neighbourX >= width) {
				neighbourX = (neighbourX + width) % width;
			}
			if (_getpixel(materialBitmap, neighbourX, neighbourY) == MaterialColorKeys::g_MaterialAir) {
				continue;
			}
			const int neighbour = neighbourY * width + neighbourX;
			const uint16_t stamp = m_VisitStamps[neighbour];
			if (stamp == searchStamp) {
				continue;
			}
			// Touching terrain an earlier search of this check visited means this piece is connected to it, so it's supported too.
			if (stamp >= m_CheckStartStamp) {
				return false;
			}
			m_VisitStamps[neighbour] = searchStamp;
			toVisit.push_back(neighbour);
			island.push_back(neighbour);
			if (island.size() > c_MaxIslandPixels) {
				return false;
			}
		}
	}
	return true;
}

void TerrainCollapse::DropPiece(SLTerrain* terrain, const std::vector<int>& island) {
	const int width = terrain->GetWidth();
	BITMAP* materialBitmap = terrain->GetMaterialBitmap();
	BITMAP* fgColorBitmap = terrain->GetFGSceneLayer()->GetBitmap();

	if (island.size() < c_MinChunkPixels) {
		// Tiny pieces just crumble.
		for (int position: island) {
			const int x = position % width;
			const int y = position / width;
			const Material* sceneMaterial = g_SceneMan.GetMaterialFromID(static_cast<uint8_t>(_getpixel(materialBitmap, x, y)));
			const Material* spawnMaterial = sceneMaterial->GetSpawnMaterial() ? g_SceneMan.GetMaterialFromID(sceneMaterial->GetSpawnMaterial()) : sceneMaterial;
			Color spawnColor;
			if (spawnMaterial->UsesOwnColor()) {
				spawnColor = spawnMaterial->GetColor();
			} else {
				spawnColor.SetRGBWithIndex(_getpixel(fgColorBitmap, x, y));
			}
			if (spawnColor.GetIndex() != ColorKeys::g_MaskColor) {
				MOPixel* pixelMO = new MOPixel(spawnColor, spawnMaterial->GetPixelDensity(), Vector(static_cast<float>(x), static_cast<float>(y)), Vector(RandomNum(-0.5F, 0.5F), RandomNum(0.0F, 1.0F)), new Atom(Vector(), spawnMaterial->GetIndex(), nullptr, spawnColor, 2), 0);
				pixelMO->SetToHitMOs(spawnMaterial->GetIndex() == c_GoldMaterialID);
				pixelMO->SetToGetHitByMOs(false);
				g_MovableMan.AddParticle(pixelMO);
			}
			_putpixel(materialBitmap, x, y, MaterialColorKeys::g_MaterialAir);
			_putpixel(fgColorBitmap, x, y, ColorKeys::g_MaskColor);
		}
		terrain->SetUpdated();
		terrain->GetFGSceneLayer()->SetUpdated();
		return;
	}

	FallingChunk chunk;
	chunk.Pixels.reserve(island.size());
	for (int position: island) {
		const int x = position % width;
		const int y = position / width;
		chunk.Pixels.push_back({x, y, static_cast<uint8_t>(_getpixel(materialBitmap, x, y)), static_cast<uint8_t>(_getpixel(fgColorBitmap, x, y))});
	}
	std::sort(chunk.Pixels.begin(), chunk.Pixels.end(), [](const ChunkPixel& a, const ChunkPixel& b) { return a.X != b.X ? a.X < b.X : a.Y < b.Y; });
	FindBottomPixels(chunk);
	chunk.Velocity = 0;
	chunk.PendingFall = 0;
	chunk.DistanceFallen = 0;
	chunk.Top = std::min_element(chunk.Pixels.begin(), chunk.Pixels.end(), [](const ChunkPixel& a, const ChunkPixel& b) { return a.Y < b.Y; })->Y;
	m_FallingChunks.push_back(std::move(chunk));
}

void TerrainCollapse::FindBottomPixels(FallingChunk& chunk) {
	chunk.BottomPixels.clear();
	for (size_t i = 0; i < chunk.Pixels.size(); ++i) {
		const bool chunkPixelBelow = i + 1 < chunk.Pixels.size() && chunk.Pixels[i + 1].X == chunk.Pixels[i].X && chunk.Pixels[i + 1].Y == chunk.Pixels[i].Y + 1;
		if (!chunkPixelBelow) {
			chunk.BottomPixels.push_back(i);
		}
	}
}

bool TerrainCollapse::UpdateChunk(SLTerrain* terrain, FallingChunk& chunk, float deltaTimeS) {
	const int height = terrain->GetHeight();
	BITMAP* materialBitmap = terrain->GetMaterialBitmap();
	BITMAP* fgColorBitmap = terrain->GetFGSceneLayer()->GetBitmap();

	const float gravity = g_SceneMan.GetGlobalAcc().GetY() * c_PPM;
	chunk.Velocity = std::min(chunk.Velocity + std::max(gravity, 0.0F) * deltaTimeS, c_TerminalVelocity);
	chunk.PendingFall += chunk.Velocity * deltaTimeS;
	const int fallWanted = static_cast<int>(chunk.PendingFall);
	chunk.PendingFall -= static_cast<float>(fallWanted);
	if (gravity <= 0) {
		Land(terrain, chunk);
		return false;
	}
	if (fallWanted == 0) {
		return true;
	}

	// Lift the chunk out of the terrain, picking up any changes made to it while it fell, and dropping pixels that were destroyed.
	bool lostPixels = false;
	for (ChunkPixel& pixel: chunk.Pixels) {
		pixel.Material = static_cast<uint8_t>(_getpixel(materialBitmap, pixel.X, pixel.Y));
		if (pixel.Material == MaterialColorKeys::g_MaterialAir) {
			lostPixels = true;
			continue;
		}
		pixel.Color = static_cast<uint8_t>(_getpixel(fgColorBitmap, pixel.X, pixel.Y));
		_putpixel(materialBitmap, pixel.X, pixel.Y, MaterialColorKeys::g_MaterialAir);
		_putpixel(fgColorBitmap, pixel.X, pixel.Y, ColorKeys::g_MaskColor);
	}
	if (lostPixels) {
		std::erase_if(chunk.Pixels, [](const ChunkPixel& pixel) { return pixel.Material == MaterialColorKeys::g_MaterialAir; });
		if (chunk.Pixels.empty()) {
			return false;
		}
		FindBottomPixels(chunk);
	}

	// Fall a pixel at a time until something's in the way.
	int fallen = 0;
	bool landed = false;
	while (fallen < fallWanted && !landed) {
		for (size_t bottomPixel: chunk.BottomPixels) {
			const ChunkPixel& pixel = chunk.Pixels[bottomPixel];
			const int belowY = pixel.Y + fallen + 1;
			if (belowY >= height || _getpixel(materialBitmap, pixel.X, belowY) != MaterialColorKeys::g_MaterialAir) {
				landed = true;
				break;
			}
		}
		if (!landed) {
			++fallen;
		}
	}

	// Crush the actors in the way. The heavier and faster the chunk, the harder it hits.
	if (fallen > 0) {
		const float crushDamage = static_cast<float>(chunk.Pixels.size()) * chunk.Velocity / 2000.0F;
		for (size_t bottomPixel: chunk.BottomPixels) {
			const ChunkPixel& pixel = chunk.Pixels[bottomPixel];
			for (int offset = 1; offset <= fallen; ++offset) {
				const MOID hitMOID = g_SceneMan.GetMOIDPixel(pixel.X, pixel.Y + offset);
				if (hitMOID == g_NoMOID) {
					continue;
				}
				MovableObject* hitMO = g_MovableMan.GetMOFromID(hitMOID);
				Actor* hitActor = hitMO ? dynamic_cast<Actor*>(hitMO->GetRootParent()) : nullptr;
				if (!hitActor || hitActor->IsSetToDelete() || !chunk.CrushedActors.insert(hitActor).second) {
					continue;
				}
				if (crushDamage > hitActor->GetMaxHealth() * 2.0F) {
					hitActor->GibThis(Vector(0, chunk.Velocity / c_PPM * static_cast<float>(chunk.Pixels.size()) * 0.01F));
				} else {
					hitActor->SetHealth(hitActor->GetHealth() - crushDamage);
					Vector actorVel = hitActor->GetVel();
					actorVel.SetY(std::max(actorVel.GetY(), chunk.Velocity / c_PPM));
					hitActor->SetVel(actorVel);
				}
			}
		}
	}

	for (ChunkPixel& pixel: chunk.Pixels) {
		pixel.Y += fallen;
		_putpixel(materialBitmap, pixel.X, pixel.Y, pixel.Material);
		_putpixel(fgColorBitmap, pixel.X, pixel.Y, pixel.Color);
	}
	chunk.DistanceFallen += fallen;
	terrain->SetUpdated();
	terrain->GetFGSceneLayer()->SetUpdated();

	if (landed) {
		Land(terrain, chunk);
		return false;
	}
	return true;
}

void TerrainCollapse::Land(SLTerrain* terrain, const FallingChunk& chunk) {
	if (chunk.Pixels.empty()) {
		return;
	}
	int minX = chunk.Pixels.front().X;
	int maxX = chunk.Pixels.back().X;
	int maxY = chunk.Top;
	Vector center;
	for (const ChunkPixel& pixel: chunk.Pixels) {
		maxY = std::max(maxY, pixel.Y);
		center += Vector(static_cast<float>(pixel.X), static_cast<float>(pixel.Y));
	}
	center /= static_cast<float>(chunk.Pixels.size());

	// The terrain the chunk fell through and the terrain around where it landed may have changed connections, so check them again, and let pathfinding know.
	MarkAreaDirty(minX - 1, chunk.Top - 1, maxX - minX + 3, maxY - chunk.Top + 3);
	terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(minX), static_cast<float>(chunk.Top)), static_cast<float>(maxX - minX + 1), static_cast<float>(maxY - chunk.Top + 1)));

	if (chunk.Velocity <= 0) {
		return;
	}
	const float impact = static_cast<float>(chunk.Pixels.size()) * chunk.Velocity;

	// Throw up dust from along the bottom of the chunk.
	const int dustCount = std::min(static_cast<int>(impact / 4000.0F), 40);
	for (int i = 0; i < dustCount; ++i) {
		const ChunkPixel& pixel = chunk.Pixels[chunk.BottomPixels[RandomNum<int>(0, static_cast<int>(chunk.BottomPixels.size()) - 1)]];
		if (pixel.Color == ColorKeys::g_MaskColor) {
			continue;
		}
		Color dustColor;
		dustColor.SetRGBWithIndex(pixel.Color);
		const float dustSpeed = std::min(chunk.Velocity / c_PPM, 8.0F);
		MOPixel* dust = new MOPixel(dustColor, 0.01F, Vector(static_cast<float>(pixel.X), static_cast<float>(pixel.Y) - 1.0F), Vector(RandomNum(-dustSpeed, dustSpeed), -RandomNum(0.2F, 0.6F) * dustSpeed), new Atom(Vector(), pixel.Material, nullptr, dustColor, 2), RandomNum<int>(300, 900));
		dust->SetToHitMOs(false);
		dust->SetToGetHitByMOs(false);
		g_MovableMan.AddParticle(dust);
	}

	g_CameraMan.AddScreenShake(std::min(impact / 10000.0F, 30.0F), center);

	if (impact > 20000.0F) {
		if (!m_LandingSound) {
			if (const SoundContainer* soundPreset = dynamic_cast<const SoundContainer*>(g_PresetMan.GetEntityPreset("SoundContainer", "Terrain Collapse Impact", "Base.rte"))) {
				m_LandingSound.reset(dynamic_cast<SoundContainer*>(soundPreset->Clone()));
			}
		}
		if (m_LandingSound) {
			m_LandingSound->Play(center);
		}
	}
}
