#include "TerrainFluids.h"
#include "TerrainFire.h"
#include "SLTerrain.h"
#include "SceneLayer.h"
#include "Material.h"
#include "Actor.h"
#include "Activity.h"
#include "ActivityMan.h"
#include "SceneMan.h"
#include "MovableMan.h"
#include "PresetMan.h"
#include "TimerMan.h"
#include "Color.h"
#include "RTETools.h"

#include <algorithm>
#include <cmath>

using namespace RTE;

namespace {
	/// The flame particle burning oil spawns. It's the same one burning terrain spawns, so it glows and burns whatever it touches.
	const std::string c_FlamePresetName = "Flame Hurt Short Float";
	const std::string c_FlameModuleName = "Base.rte";

	constexpr float c_AcidEatChancePerUpdate = 0.06F; //!< Chance per update of a pixel of acid eating a pixel of terrain next to it, before taking how tough the terrain is into account.
	constexpr float c_AcidDamagePerSecond = 25.0F; //!< Health an actor fully in acid loses per second.
	constexpr float c_ToxicGasDamagePerSecond = 6.0F; //!< Health an actor with its head in toxic gas loses per second.
	constexpr int c_MaxDisplacementRise = 8; //!< How far up a liquid can be pushed when terrain lands on it, before it's squashed out of existence.
} // namespace

const std::array<TerrainFluids::FluidProperties, static_cast<size_t>(TerrainFluids::FluidType::FluidTypeCount)> TerrainFluids::c_FluidProperties = {{
    {"", 0, 0, 0.0F, 0.0F, {0, 0, 0}},
    {"Water", 2, 8, 1.3F, 0.05F, {28, 68, 160}},
    {"Oil", 1, 3, 1.15F, 0.1F, {58, 44, 30}},
    {"Acid", 3, 4, 1.2F, 0.06F, {120, 220, 40}},
    {"Toxic Gas", 0, 0, 0.0F, 0.0F, {140, 190, 60}},
    {"Steam", 0, 0, 0.0F, 0.0F, {215, 215, 225}},
}};

TerrainFluids::TerrainFluids() {
	m_LiquidBitmap = nullptr;
	m_GasBitmap = nullptr;
	Clear();
}

TerrainFluids::~TerrainFluids() {
	Clear();
}

void TerrainFluids::Clear() {
	// The layers own their bitmaps once made. Before that, the bitmaps are ours to destroy.
	if (m_LiquidLayer) {
		m_LiquidLayer.reset();
	} else if (m_LiquidBitmap) {
		destroy_bitmap(m_LiquidBitmap);
	}
	if (m_GasLayer) {
		m_GasLayer.reset();
	} else if (m_GasBitmap) {
		destroy_bitmap(m_GasBitmap);
	}
	m_LiquidBitmap = nullptr;
	m_GasBitmap = nullptr;

	m_Width = 0;
	m_Height = 0;
	m_WrapsX = false;
	// Swap with empty vectors to actually free the memory, which is a few bytes for every pixel of the scene.
	std::vector<FluidType>().swap(m_Types);
	std::vector<uint8_t>().swap(m_Data);
	std::vector<uint8_t>().swap(m_Stamps);
	std::vector<uint8_t>().swap(m_Rest);
	std::vector<int>().swap(m_Pixels);
	std::vector<int>().swap(m_SortedPixels);
	std::vector<int>().swap(m_RowCounts);
	m_CurrentStamp = 1;
	m_FluidPixelCount = 0;
	m_UpdateCount = 0;
	for (std::vector<int>& colors: m_Colors) {
		colors.clear();
	}
	m_FireColors.clear();
	m_SurfaceColors.fill(0);
}

TerrainFluids::FluidType TerrainFluids::GetFluidTypeFromName(const std::string& name) {
	for (size_t type = 1; type < c_FluidProperties.size(); ++type) {
		if (c_FluidProperties[type].Name == name) {
			return static_cast<FluidType>(type);
		}
	}
	return FluidType::None;
}

const std::string& TerrainFluids::GetFluidTypeName(FluidType type) {
	return c_FluidProperties[static_cast<size_t>(type)].Name;
}

void TerrainFluids::EnsureCreated() {
	if (!m_Types.empty()) {
		return;
	}
	m_Width = g_SceneMan.GetSceneWidth();
	m_Height = g_SceneMan.GetSceneHeight();
	m_WrapsX = g_SceneMan.SceneWrapsX();
	const size_t pixelCount = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height);
	m_Types.assign(pixelCount, FluidType::None);
	m_Data.assign(pixelCount, 0);
	m_Stamps.assign(pixelCount, 0);
	m_Rest.assign(pixelCount, 0);
	m_RowCounts.assign(m_Height + 1, 0);

	m_LiquidBitmap = create_bitmap_ex(8, m_Width, m_Height);
	clear_to_color(m_LiquidBitmap, ColorKeys::g_MaskColor);
	m_GasBitmap = create_bitmap_ex(8, m_Width, m_Height);
	clear_to_color(m_GasBitmap, ColorKeys::g_MaskColor);

	// A few shades of each fluid's color, so a pool of it isn't one flat color.
	for (size_t type = 1; type < c_FluidProperties.size(); ++type) {
		const std::array<int, 3>& color = c_FluidProperties[type].Color;
		for (float shade: {0.85F, 1.0F, 1.12F}) {
			m_Colors[type].push_back(Color(std::min(255, static_cast<int>(static_cast<float>(color[0]) * shade)), std::min(255, static_cast<int>(static_cast<float>(color[1]) * shade)), std::min(255, static_cast<int>(static_cast<float>(color[2]) * shade))).GetIndex());
		}
		m_SurfaceColors[type] = Color(std::min(255, color[0] + 70), std::min(255, color[1] + 70), std::min(255, color[2] + 60)).GetIndex();
	}
	for (const Color& fireColor: {Color(255, 220, 80), Color(255, 150, 30), Color(240, 90, 20)}) {
		m_FireColors.push_back(fireColor.GetIndex());
	}
}

bool TerrainFluids::WrapAndCheck(int& x, int y) const {
	if (y < 0 || y >= m_Height) {
		return false;
	}
	if (x < 0 || x >= m_Width) {
		if (!m_WrapsX) {
			return false;
		}
		x = (x % m_Width + m_Width) % m_Width;
	}
	return true;
}

void TerrainFluids::PaintPixel(int index) {
	const FluidType type = m_Types[index];
	if (type == FluidType::None) {
		return;
	}
	const std::vector<int>& colors = (type == FluidType::Oil && m_Data[index] > 0) ? m_FireColors : m_Colors[static_cast<size_t>(type)];
	GetBitmapFor(type)->line[index / m_Width][index % m_Width] = static_cast<unsigned char>(colors[RandomNum<int>(0, static_cast<int>(colors.size()) - 1)]);
}

void TerrainFluids::SetFluid(int index, FluidType type, uint8_t data) {
	if (m_Types[index] == FluidType::None) {
		++m_FluidPixelCount;
		m_Pixels.push_back(index);
	}
	m_Types[index] = type;
	m_Rest[index] = 0;
	m_Data[index] = data;
	PaintPixel(index);
}

void TerrainFluids::RemoveFluid(int index) {
	const FluidType type = m_Types[index];
	if (type == FluidType::None) {
		return;
	}
	GetBitmapFor(type)->line[index / m_Width][index % m_Width] = ColorKeys::g_MaskColor;
	m_Types[index] = FluidType::None;
	m_Data[index] = 0;
	m_Rest[index] = 0;
	--m_FluidPixelCount;
	WakeAround(index);
}

void TerrainFluids::WakeAround(int index) {
	const int x = index % m_Width;
	const int y = index / m_Width;
	if (x > 0 && x < m_Width - 1 && y > 0 && y < m_Height - 1) {
		for (int row = index - m_Width; row <= index + m_Width; row += m_Width) {
			m_Rest[row - 1] = 0;
			m_Rest[row] = 0;
			m_Rest[row + 1] = 0;
		}
		return;
	}
	for (int offsetY = -1; offsetY <= 1; ++offsetY) {
		for (int offsetX = -1; offsetX <= 1; ++offsetX) {
			int neighbourX = x + offsetX;
			if (WrapAndCheck(neighbourX, y + offsetY)) {
				m_Rest[(y + offsetY) * m_Width + neighbourX] = 0;
			}
		}
	}
}

void TerrainFluids::MoveFluid(int fromIndex, int toIndex) {
	const FluidType movingType = m_Types[fromIndex];
	const FluidType displacedType = m_Types[toIndex];
	const int fromX = fromIndex % m_Width;
	const int fromY = fromIndex / m_Width;
	const int toX = toIndex % m_Width;
	const int toY = toIndex / m_Width;

	BITMAP* movingBitmap = GetBitmapFor(movingType);
	BITMAP* displacedBitmap = displacedType != FluidType::None ? GetBitmapFor(displacedType) : nullptr;
	const unsigned char movingColor = movingBitmap->line[fromY][fromX];
	const unsigned char displacedColor = displacedBitmap ? displacedBitmap->line[toY][toX] : 0;
	movingBitmap->line[fromY][fromX] = ColorKeys::g_MaskColor;
	if (displacedBitmap) {
		displacedBitmap->line[toY][toX] = ColorKeys::g_MaskColor;
		displacedBitmap->line[fromY][fromX] = displacedColor;
	}
	movingBitmap->line[toY][toX] = movingColor;

	std::swap(m_Types[fromIndex], m_Types[toIndex]);
	std::swap(m_Data[fromIndex], m_Data[toIndex]);
	m_Rest[fromIndex] = 0;
	m_Rest[toIndex] = 0;
	WakeAround(fromIndex);
	if (displacedType != FluidType::None) {
		// The fluid that got pushed aside needs keeping track of at its new spot.
		m_Pixels.push_back(fromIndex);
	}
}

int TerrainFluids::AddFluid(SLTerrain* terrain, FluidType type, const Vector& pos, float radius) {
	if (type == FluidType::None || type == FluidType::FluidTypeCount || !terrain) {
		return 0;
	}
	std::scoped_lock<std::mutex> lock(m_Mutex);
	EnsureCreated();
	int added = 0;
	const int centerX = pos.GetFloorIntX();
	const int centerY = pos.GetFloorIntY();
	const int reach = static_cast<int>(std::ceil(radius));
	for (int y = centerY - reach; y <= centerY + reach; ++y) {
		for (int x = centerX - reach; x <= centerX + reach; ++x) {
			int wrappedX = x;
			if ((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY) > radius * radius || !WrapAndCheck(wrappedX, y) || m_FluidPixelCount >= c_MaxFluidPixels) {
				continue;
			}
			const int index = y * m_Width + wrappedX;
			if (m_Types[index] != FluidType::None || terrain->GetMaterialPixel(wrappedX, y) != MaterialColorKeys::g_MaterialAir) {
				continue;
			}
			uint8_t data = 0;
			switch (type) {
				case FluidType::Acid:
					data = static_cast<uint8_t>(RandomNum(3, 6)); // How many terrain pixels it can eat.
					break;
				case FluidType::ToxicGas:
					data = static_cast<uint8_t>(RandomNum(200, 255)); // How long it lasts.
					break;
				case FluidType::Steam:
					data = static_cast<uint8_t>(RandomNum(40, 70));
					break;
				default:
					break;
			}
			SetFluid(index, type, data);
			++added;
		}
	}
	return added;
}

int TerrainFluids::RemoveFluids(const Vector& pos, float radius) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	if (m_FluidPixelCount == 0) {
		return 0;
	}
	int removed = 0;
	const int centerX = pos.GetFloorIntX();
	const int centerY = pos.GetFloorIntY();
	const int reach = static_cast<int>(std::ceil(radius));
	for (int y = centerY - reach; y <= centerY + reach; ++y) {
		for (int x = centerX - reach; x <= centerX + reach; ++x) {
			int wrappedX = x;
			if ((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY) <= radius * radius && WrapAndCheck(wrappedX, y) && m_Types[y * m_Width + wrappedX] != FluidType::None) {
				RemoveFluid(y * m_Width + wrappedX);
				++removed;
			}
		}
	}
	return removed;
}

TerrainFluids::FluidType TerrainFluids::GetFluidAt(const Vector& pos) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	int x = pos.GetFloorIntX();
	const int y = pos.GetFloorIntY();
	if (m_FluidPixelCount == 0 || !WrapAndCheck(x, y)) {
		return FluidType::None;
	}
	return m_Types[y * m_Width + x];
}

int TerrainFluids::IgniteOil(const Vector& pos, float radius) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	if (m_FluidPixelCount == 0) {
		return 0;
	}
	int ignited = 0;
	const int centerX = pos.GetFloorIntX();
	const int centerY = pos.GetFloorIntY();
	const int reach = static_cast<int>(std::ceil(radius));
	for (int y = centerY - reach; y <= centerY + reach; ++y) {
		for (int x = centerX - reach; x <= centerX + reach; ++x) {
			int wrappedX = x;
			if ((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY) > radius * radius || !WrapAndCheck(wrappedX, y)) {
				continue;
			}
			const int index = y * m_Width + wrappedX;
			if (m_Types[index] == FluidType::Oil && m_Data[index] == 0) {
				m_Data[index] = static_cast<uint8_t>(RandomNum(80, 140)); // How long it burns.
				PaintPixel(index);
				++ignited;
			}
		}
	}
	return ignited;
}

bool TerrainFluids::TouchedByFire(int x, int y) {
	std::scoped_lock<std::mutex> lock(m_Mutex);
	if (m_FluidPixelCount == 0 || !WrapAndCheck(x, y)) {
		return false;
	}
	const int index = y * m_Width + x;
	switch (m_Types[index]) {
		case FluidType::Water:
			RemoveFluid(index);
			SetFluid(index, FluidType::Steam, static_cast<uint8_t>(RandomNum(40, 70)));
			return true;
		case FluidType::Oil:
			if (m_Data[index] == 0) {
				m_Data[index] = static_cast<uint8_t>(RandomNum(80, 140));
				PaintPixel(index);
			}
			return false;
		default:
			return false;
	}
}

int TerrainFluids::UpdateLiquid(SLTerrain* terrain, int index, std::vector<Vector>& firesToStart, int& flamesSpawned, EatenArea& eatenArea) {
	BITMAP* materials = terrain->GetMaterialBitmap();
	const FluidType type = m_Types[index];
	const FluidProperties& properties = c_FluidProperties[static_cast<size_t>(type)];
	int x = index % m_Width;
	int y = index / m_Width;

	// Terrain that landed on the liquid pushes it up, or squashes it out of existence if there's nowhere for it to go.
	if (materials->line[y][x] != MaterialColorKeys::g_MaterialAir) {
		for (int rise = 1; rise <= c_MaxDisplacementRise && y - rise >= 0; ++rise) {
			const int aboveIndex = (y - rise) * m_Width + x;
			if (materials->line[y - rise][x] == MaterialColorKeys::g_MaterialAir && m_Types[aboveIndex] == FluidType::None) {
				MoveFluid(index, aboveIndex);
				m_Stamps[aboveIndex] = m_CurrentStamp;
				return aboveIndex;
			}
		}
		RemoveFluid(index);
		return -1;
	}

	// Acid eats into the terrain around it, the softer the faster, until it's used up.
	if (type == FluidType::Acid && RandomNum() < c_AcidEatChancePerUpdate) {
		for (const auto& [offsetX, offsetY]: {std::pair{0, 1}, std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}}) {
			int neighbourX = x + offsetX;
			const int neighbourY = y + offsetY;
			if (!WrapAndCheck(neighbourX, neighbourY) || materials->line[neighbourY][neighbourX] == MaterialColorKeys::g_MaterialAir) {
				continue;
			}
			const Material* material = g_SceneMan.GetMaterialFromID(materials->line[neighbourY][neighbourX]);
			const float integrity = material ? material->GetIntegrity() : -1.0F;
			// Negative integrity means indestructible, and anything this tough is meant to be.
			if (integrity < 0 || integrity >= 1000.0F) {
				break;
			}
			if (RandomNum() < 60.0F / (60.0F + integrity)) {
				terrain->SetMaterialPixel(neighbourX, neighbourY, MaterialColorKeys::g_MaterialAir);
				terrain->SetFGColorPixel(neighbourX, neighbourY, ColorKeys::g_MaskColor);
				g_SceneMan.RegisterRemovedTerrain(neighbourX, neighbourY);
				if (eatenArea.MaxX < eatenArea.MinX) {
					eatenArea = {neighbourX, neighbourY, neighbourX, neighbourY};
				} else {
					eatenArea = {std::min(eatenArea.MinX, neighbourX), std::min(eatenArea.MinY, neighbourY), std::max(eatenArea.MaxX, neighbourX), std::max(eatenArea.MaxY, neighbourY)};
				}
				if (--m_Data[index] == 0) {
					// Spent acid fizzles away, sometimes into a puff of toxic fumes.
					RemoveFluid(index);
					if (RandomNum() < 0.4F) {
						SetFluid(index, FluidType::ToxicGas, static_cast<uint8_t>(RandomNum(120, 200)));
						m_Stamps[index] = m_CurrentStamp;
						return index;
					}
					return -1;
				}
			}
			break;
		}
	}

	// Burning oil burns away, spreads its fire to the oil and flammable terrain around it, and is put out by water.
	if (type == FluidType::Oil && m_Data[index] > 0) {
		if ((m_UpdateCount + index) % 2 == 0 && --m_Data[index] == 0) {
			RemoveFluid(index);
			return -1;
		}
		bool touchingWater = false;
		for (const auto& [offsetX, offsetY]: {std::pair{0, 1}, std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}}) {
			int neighbourX = x + offsetX;
			const int neighbourY = y + offsetY;
			if (!WrapAndCheck(neighbourX, neighbourY)) {
				continue;
			}
			const int neighbourIndex = neighbourY * m_Width + neighbourX;
			if (m_Types[neighbourIndex] == FluidType::Water) {
				touchingWater = true;
			} else if (m_Types[neighbourIndex] == FluidType::Oil && m_Data[neighbourIndex] == 0 && RandomNum() < 0.1F) {
				m_Data[neighbourIndex] = static_cast<uint8_t>(RandomNum(80, 140));
				PaintPixel(neighbourIndex);
			}
		}
		if (touchingWater) {
			m_Data[index] = 0;
		} else {
			if (flamesSpawned < c_MaxFlamesPerUpdate && y > 0 && RandomNum() < 0.03F && materials->line[y - 1][x] == MaterialColorKeys::g_MaterialAir && m_Types[index - m_Width] == FluidType::None) {
				const Entity* flamePreset = g_PresetMan.GetEntityPreset("MOSParticle", c_FlamePresetName, c_FlameModuleName);
				if (MovableObject* flame = flamePreset ? dynamic_cast<MovableObject*>(flamePreset->Clone()) : nullptr) {
					flame->SetPos(Vector(static_cast<float>(x), static_cast<float>(y) - 1.0F));
					flame->SetVel(Vector(RandomNum(-1.0F, 1.0F), RandomNum(-3.0F, -1.0F)));
					g_MovableMan.AddParticle(flame);
					++flamesSpawned;
				}
			}
			if (RandomNum() < 0.01F) {
				firesToStart.emplace_back(static_cast<float>(x), static_cast<float>(y));
			}
		}
		PaintPixel(index);
	}

	// Whether the liquid can flow into a pixel: 0 if it can't, 1 if it's free, 2 if it can swap places with the fluid there, 3 if it flows off the edge of the scene.
	auto canFlowInto = [&](int& targetX, int targetY, bool sinking) {
		if (targetY < 0 || targetY >= m_Height) {
			return 0;
		}
		if (targetX < 0 || targetX >= m_Width) {
			if (!m_WrapsX) {
				return 3;
			}
			targetX = (targetX % m_Width + m_Width) % m_Width;
		}
		if (materials->line[targetY][targetX] != MaterialColorKeys::g_MaterialAir) {
			return 0;
		}
		const FluidType targetType = m_Types[targetY * m_Width + targetX];
		if (targetType == FluidType::None) {
			return 1;
		}
		if (IsGas(targetType) || (sinking && c_FluidProperties[static_cast<size_t>(targetType)].Density < properties.Density)) {
			return 2;
		}
		return 0;
	};

	int currentIndex = index;
	bool moved = false;

	// Fall straight down, sinking through lighter liquids.
	for (int fall = 0; fall < c_MaxFallPerUpdate; ++fall) {
		int targetX = x;
		const int result = canFlowInto(targetX, y + 1, true);
		if (result == 0) {
			break;
		}
		if (result == 3) {
			RemoveFluid(currentIndex);
			return -1;
		}
		const int targetIndex = (y + 1) * m_Width + targetX;
		MoveFluid(currentIndex, targetIndex);
		currentIndex = targetIndex;
		x = targetX;
		++y;
		moved = true;
		if (result == 2) {
			break;
		}
	}

	const int firstDirection = RandomNum() < 0.5F ? -1 : 1;

	// Then slide down diagonally.
	if (!moved) {
		for (int direction: {firstDirection, -firstDirection}) {
			int targetX = x + direction;
			const int result = canFlowInto(targetX, y + 1, false);
			if (result == 3) {
				RemoveFluid(currentIndex);
				return -1;
			}
			if (result != 0) {
				const int targetIndex = (y + 1) * m_Width + targetX;
				MoveFluid(currentIndex, targetIndex);
				currentIndex = targetIndex;
				moved = true;
				break;
			}
		}
	}

	// Then spread out sideways, as far as the liquid is runny, stopping at any drop so it pours over ledges.
	if (!moved) {
		for (int direction: {firstDirection, -firstDirection}) {
			int furthestIndex = -1;
			for (int step = 1; step <= properties.Dispersion; ++step) {
				int targetX = x + direction * step;
				const int result = canFlowInto(targetX, y, false);
				if (result == 3) {
					RemoveFluid(currentIndex);
					return -1;
				}
				if (result != 1) {
					break;
				}
				furthestIndex = y * m_Width + targetX;
				int belowX = targetX;
				if (canFlowInto(belowX, y + 1, false) == 1) {
					break;
				}
			}
			if (furthestIndex >= 0) {
				MoveFluid(currentIndex, furthestIndex);
				currentIndex = furthestIndex;
				moved = true;
				break;
			}
		}
	}

	// Liquid with more of it on top is under pressure, which pushes it along through the liquid around it to any open space level with it.
	// Without this, a pile of liquid could only spread from its edges, and would slump like sand rather than level out.
	// It's only checked every few updates, since it means looking along the whole width of a pool.
	if (!moved && y > 0 && m_Types[currentIndex - m_Width] == type && (m_UpdateCount + currentIndex) % 3 == 0) {
		for (int direction: {firstDirection, -firstDirection}) {
			int openIndex = -1;
			for (int step = 1; step <= c_MaxPressureReach; ++step) {
				int targetX = x + direction * step;
				const int result = canFlowInto(targetX, y, false);
				if (result == 1) {
					openIndex = y * m_Width + targetX;
					break;
				}
				if (result != 0 || m_Types[y * m_Width + targetX] != type || materials->line[y][targetX] != MaterialColorKeys::g_MaterialAir) {
					break;
				}
			}
			if (openIndex >= 0) {
				MoveFluid(currentIndex, openIndex);
				currentIndex = openIndex;
				break;
			}
		}
	}

	// Shade the top of the liquid lighter, so its surface shows.
	if (!(type == FluidType::Oil && m_Data[currentIndex] > 0)) {
		const bool isSurface = currentIndex < m_Width || m_Types[currentIndex - m_Width] != type;
		unsigned char& color = m_LiquidBitmap->line[currentIndex / m_Width][currentIndex % m_Width];
		const unsigned char surfaceColor = static_cast<unsigned char>(m_SurfaceColors[static_cast<size_t>(type)]);
		if (isSurface) {
			color = surfaceColor;
		} else if (color == surfaceColor) {
			PaintPixel(currentIndex);
		}
	}

	if (currentIndex == index && m_Rest[index] < 255) {
		++m_Rest[index];
	}
	m_Stamps[currentIndex] = m_CurrentStamp;
	return currentIndex;
}

int TerrainFluids::UpdateGas(SLTerrain* terrain, int index) {
	BITMAP* materials = terrain->GetMaterialBitmap();
	const int x = index % m_Width;
	const int y = index / m_Width;

	// Gases thin out and vanish over time, and are snuffed out by terrain landing on them.
	if (((m_UpdateCount + index) % 4 == 0 && --m_Data[index] == 0) || materials->line[y][x] != MaterialColorKeys::g_MaterialAir) {
		RemoveFluid(index);
		return -1;
	}

	// Drift upwards, wafting from side to side, and escape out the top of the scene.
	const int sideways = RandomNum() < 0.5F ? -1 : 1;
	std::array<std::pair<int, int>, 3> moves = {{{0, -1}, {sideways, -1}, {sideways, 0}}};
	if (RandomNum() < 0.25F) {
		moves = {{{sideways, 0}, {0, -1}, {sideways, -1}}};
	} else if (RandomNum() < 0.3F) {
		// Gases rise unevenly, so they billow rather than stream straight up.
		m_Stamps[index] = m_CurrentStamp;
		return index;
	}
	for (const auto& [offsetX, offsetY]: moves) {
		int targetX = x + offsetX;
		const int targetY = y + offsetY;
		if (targetY < 0 || (!m_WrapsX && (targetX < 0 || targetX >= m_Width))) {
			RemoveFluid(index);
			return -1;
		}
		WrapAndCheck(targetX, targetY);
		const int targetIndex = targetY * m_Width + targetX;
		if (materials->line[targetY][targetX] == MaterialColorKeys::g_MaterialAir && m_Types[targetIndex] == FluidType::None) {
			MoveFluid(index, targetIndex);
			m_Stamps[targetIndex] = m_CurrentStamp;
			return targetIndex;
		}
	}
	m_Stamps[index] = m_CurrentStamp;
	return index;
}

void TerrainFluids::ActOnMovableObjects() {
	const float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	const Vector gravity = g_SceneMan.GetGlobalAcc();
	constexpr int sampleCount = 6;

	auto actOn = [&](MovableObject* movableObject, Actor* actor) {
		if (!movableObject || movableObject->GetPinStrength() > 0) {
			return;
		}
		const Vector& pos = movableObject->GetPos();
		const float radius = std::max(2.0F, movableObject->GetRadius() * 0.8F);

		// Sample a line from the top to the bottom of the object, to judge how deep in liquid it is.
		int submergedSamples = 0;
		FluidType liquid = FluidType::None;
		bool breathingToxicGas = false;
		for (int sample = 0; sample < sampleCount; ++sample) {
			const float sampleY = pos.GetY() - radius + 2.0F * radius * (static_cast<float>(sample) + 0.5F) / static_cast<float>(sampleCount);
			int x = pos.GetFloorIntX();
			const int y = static_cast<int>(std::floor(sampleY));
			if (!WrapAndCheck(x, y)) {
				continue;
			}
			const FluidType type = m_Types[y * m_Width + x];
			if (type == FluidType::None) {
				continue;
			}
			if (!IsGas(type)) {
				++submergedSamples;
				if (liquid == FluidType::None || c_FluidProperties[static_cast<size_t>(type)].Density > c_FluidProperties[static_cast<size_t>(liquid)].Density) {
					liquid = type;
				}
			} else if (type == FluidType::ToxicGas && sampleY < pos.GetY()) {
				breathingToxicGas = true;
			}
		}

		if (submergedSamples > 0) {
			const float submergedFraction = static_cast<float>(submergedSamples) / static_cast<float>(sampleCount);
			const FluidProperties& properties = c_FluidProperties[static_cast<size_t>(liquid)];
			Vector vel = movableObject->GetVel();
			vel -= gravity * (properties.Buoyancy * submergedFraction * deltaTime * movableObject->GetGlobalAccScalar());
			vel *= 1.0F - properties.Drag * submergedFraction;
			movableObject->SetVel(vel);
			movableObject->SetAngularVel(movableObject->GetAngularVel() * (1.0F - properties.Drag * submergedFraction));
			if (actor && liquid == FluidType::Acid && !actor->IsDead()) {
				actor->AddHealth(-c_AcidDamagePerSecond * submergedFraction * deltaTime);
			}
		}
		if (actor && breathingToxicGas && !actor->IsDead()) {
			actor->AddHealth(-c_ToxicGasDamagePerSecond * deltaTime);
		}
	};

	for (Actor* actor: g_MovableMan.GetActors()) {
		actOn(actor, actor);
	}
	for (MovableObject* item: g_MovableMan.GetItems()) {
		actOn(item, nullptr);
	}
}

void TerrainFluids::Update(SLTerrain* terrain, TerrainFire& fire) {
	std::vector<Vector> firesToStart;
	{
		std::scoped_lock<std::mutex> lock(m_Mutex);
		if (m_FluidPixelCount == 0 || !terrain) {
			return;
		}
		if (const Activity* activity = g_ActivityMan.GetActivity(); activity && activity->IsPaused()) {
			return;
		}

		// The layers make GPU textures, which has to happen on the main thread, so they're made here rather than when fluids are first added.
		if (!m_LiquidLayer) {
			m_LiquidLayer = std::make_unique<SceneLayer>();
			m_LiquidLayer->Create(m_LiquidBitmap, true, Vector(), m_WrapsX, g_SceneMan.SceneWrapsY(), Vector(1.0F, 1.0F));
			m_LiquidLayer->SetDrawAlpha(c_LiquidAlpha);
		}
		if (!m_GasLayer) {
			m_GasLayer = std::make_unique<SceneLayer>();
			m_GasLayer->Create(m_GasBitmap, true, Vector(), m_WrapsX, g_SceneMan.SceneWrapsY(), Vector(1.0F, 1.0F));
			m_GasLayer->SetDrawAlpha(c_GasAlpha);
		}

		++m_UpdateCount;
		if (++m_CurrentStamp == 0) {
			std::fill(m_Stamps.begin(), m_Stamps.end(), 0);
			m_CurrentStamp = 1;
		}

		// Sort the fluid pixels by height, so liquids can be moved bottom to top and gases top to bottom. Each moves into space the ones before it just left.
		std::fill(m_RowCounts.begin(), m_RowCounts.end(), 0);
		for (int index: m_Pixels) {
			if (m_Types[index] != FluidType::None) {
				++m_RowCounts[index / m_Width + 1];
			}
		}
		for (int row = 1; row <= m_Height; ++row) {
			m_RowCounts[row] += m_RowCounts[row - 1];
		}
		m_SortedPixels.resize(m_RowCounts[m_Height]);
		for (int index: m_Pixels) {
			if (m_Types[index] != FluidType::None) {
				m_SortedPixels[m_RowCounts[index / m_Width]++] = index;
			}
		}
		m_Pixels.clear();

		int flamesSpawned = 0;
		EatenArea eatenArea;
		// A pixel that's already been acted on this update is a repeated entry, or holds a fluid that just moved in. Either way it's skipped, which also drops the extra entry.
		for (size_t sortedIndex = m_SortedPixels.size(); sortedIndex-- > 0;) {
			const int index = m_SortedPixels[sortedIndex];
			const FluidType type = m_Types[index];
			if (type != FluidType::None && !IsGas(type) && m_Stamps[index] != m_CurrentStamp) {
				// Settled liquid is left be most updates. Acid and burning oil are always busy eating and burning.
				const bool alwaysActive = type == FluidType::Acid || (type == FluidType::Oil && m_Data[index] > 0);
				if (!alwaysActive && m_Rest[index] >= c_UpdatesBeforeResting && (m_UpdateCount + index) % c_RestingCheckInterval != 0) {
					m_Stamps[index] = m_CurrentStamp;
					m_Pixels.push_back(index);
					continue;
				}
				if (int newIndex = UpdateLiquid(terrain, index, firesToStart, flamesSpawned, eatenArea); newIndex >= 0) {
					m_Pixels.push_back(newIndex);
				}
			}
		}
		for (int index: m_SortedPixels) {
			if (IsGas(m_Types[index]) && m_Stamps[index] != m_CurrentStamp) {
				if (int newIndex = UpdateGas(terrain, index); newIndex >= 0) {
					m_Pixels.push_back(newIndex);
				}
			}
		}

		// Eaten away terrain may open up new paths, so let pathfinding know.
		if (eatenArea.MaxX >= eatenArea.MinX) {
			terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(eatenArea.MinX), static_cast<float>(eatenArea.MinY)), static_cast<float>(eatenArea.MaxX - eatenArea.MinX + 1), static_cast<float>(eatenArea.MaxY - eatenArea.MinY + 1)));
		}

		ActOnMovableObjects();
	}

	// Fire is started outside the lock, since fire touching fluids locks it too.
	for (const Vector& firePos: firesToStart) {
		fire.Ignite(terrain, firePos, 2.0F);
	}
}

void TerrainFluids::SetOffset(const Vector& offset) {
	if (m_LiquidLayer) {
		m_LiquidLayer->SetOffset(offset);
	}
	if (m_GasLayer) {
		m_GasLayer->SetOffset(offset);
	}
}

void TerrainFluids::Draw(const Box& targetDimensions, Box& targetBox) {
	if (m_FluidPixelCount == 0) {
		return;
	}
	if (m_LiquidLayer) {
		m_LiquidLayer->Draw(targetDimensions, targetBox);
	}
	if (m_GasLayer) {
		m_GasLayer->Draw(targetDimensions, targetBox);
	}
}
