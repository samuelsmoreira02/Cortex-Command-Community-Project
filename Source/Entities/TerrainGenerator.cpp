#include "TerrainGenerator.h"
#include "Material.h"
#include "RTEError.h"

#include "allegro.h"

#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>
#include <random>

using namespace RTE;

const std::string TerrainGenerator::c_ClassName = "TerrainGenerator";

std::optional<uint32_t> TerrainGenerator::s_NextSeed;
uint32_t TerrainGenerator::s_LastSeed = 0;

namespace {
	/// Reads one "AddLayer" entry of a TerrainGenerator.
	class TerrainGeneratorLayerReader : public Serializable {
	public:
		SerializableClassNameGetter
		SerializableOverrideMethods

		TerrainGenerator::Layer Layer;

	private:
		static const std::string c_ClassName;
	};

	const std::string TerrainGeneratorLayerReader::c_ClassName = "TerrainGeneratorLayer";

	int TerrainGeneratorLayerReader::ReadProperty(const std::string_view& propName, Reader& reader) {
		StartPropertyList(return Serializable::ReadProperty(propName, reader));

		MatchProperty("Material", {
			Material material;
			reader >> material;
			Layer.MaterialIndex = material.GetIndex();
			Layer.MaterialName = material.GetPresetName();
		});
		MatchProperty("Thickness", { reader >> Layer.Thickness; });

		EndPropertyList;
	}

	int TerrainGeneratorLayerReader::Save(Writer& writer) const {
		Serializable::Save(writer);

		writer.NewProperty("Material");
		writer.ObjectStart("Material");
		writer.NewPropertyWithValue("CopyOf", Layer.MaterialName);
		writer.ObjectEnd();
		writer.NewPropertyWithValue("Thickness", Layer.Thickness);

		return 0;
	}

	/// Deterministic hash of a lattice point, so the same seed always gives the same terrain on every platform.
	uint32_t HashLatticePoint(int x, int y, uint32_t seed) {
		uint32_t hash = seed ^ (static_cast<uint32_t>(x) * 0x27D4EB2Du) ^ (static_cast<uint32_t>(y) * 0x165667B1u);
		hash ^= hash >> 16;
		hash *= 0x7FEB352Du;
		hash ^= hash >> 15;
		hash *= 0x846CA68Bu;
		hash ^= hash >> 16;
		return hash;
	}

	float LatticeValue(int x, int y, uint32_t seed) {
		return static_cast<float>(HashLatticePoint(x, y, seed) & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
	}

	float SmoothStep(float t) {
		return t * t * (3.0F - 2.0F * t);
	}

	int WrapCell(int cell, int periodCells) {
		return periodCells > 0 ? ((cell % periodCells) + periodCells) % periodCells : cell;
	}

	/// Smoothly interpolated value noise, in the range [0, 1]. Coordinates are in cells; X repeats every periodCells cells if that's above 0.
	float ValueNoise(float x, float y, int periodCellsX, uint32_t seed) {
		int cellX = static_cast<int>(std::floor(x));
		int cellY = static_cast<int>(std::floor(y));
		float tx = SmoothStep(x - static_cast<float>(cellX));
		float ty = SmoothStep(y - static_cast<float>(cellY));
		int x0 = WrapCell(cellX, periodCellsX);
		int x1 = WrapCell(cellX + 1, periodCellsX);

		float top = std::lerp(LatticeValue(x0, cellY, seed), LatticeValue(x1, cellY, seed), tx);
		float bottom = std::lerp(LatticeValue(x0, cellY + 1, seed), LatticeValue(x1, cellY + 1, seed), tx);
		return std::lerp(top, bottom, ty);
	}

	/// Sum of value noise octaves, each half the size and roughness times the strength of the previous, normalized to [0, 1].
	/// If wrapWidth is above 0, the noise repeats seamlessly every wrapWidth pixels horizontally.
	float FractalNoise(float pixelX, float pixelY, float featureSize, float roughness, int octaves, int wrapWidth, uint32_t seed) {
		float total = 0;
		float strengthSum = 0;
		float strength = 1.0F;
		for (int octave = 0; octave < octaves; ++octave) {
			float cellSize = std::max(featureSize / static_cast<float>(1 << octave), 2.0F);
			int periodCells = 0;
			if (wrapWidth > 0) {
				// Stretch the cells slightly so a whole number of them fits the width exactly, which makes the noise tile.
				periodCells = std::max(1, static_cast<int>(std::round(static_cast<float>(wrapWidth) / cellSize)));
				cellSize = static_cast<float>(wrapWidth) / static_cast<float>(periodCells);
			}
			total += ValueNoise(pixelX / cellSize, pixelY / cellSize, periodCells, seed + static_cast<uint32_t>(octave) * 7919u) * strength;
			strengthSum += strength;
			strength *= roughness;
		}
		return total / strengthSum;
	}
} // namespace

void TerrainGenerator::Clear() {
	m_Width = 2400;
	m_Height = 1200;
	m_Seed = 0;
	m_SurfaceLevel = 0.35F;
	m_SurfaceAmplitude = 150;
	m_SurfaceFeatureSize = 800;
	m_SurfaceRoughness = 0.5F;
	m_CaveDensity = 0.1F;
	m_CaveFeatureSize = 150;
	m_CaveMinDepth = 80;
	m_Layers.clear();
}

int TerrainGenerator::Create(const TerrainGenerator& reference) {
	m_Width = reference.m_Width;
	m_Height = reference.m_Height;
	m_Seed = reference.m_Seed;
	m_SurfaceLevel = reference.m_SurfaceLevel;
	m_SurfaceAmplitude = reference.m_SurfaceAmplitude;
	m_SurfaceFeatureSize = reference.m_SurfaceFeatureSize;
	m_SurfaceRoughness = reference.m_SurfaceRoughness;
	m_CaveDensity = reference.m_CaveDensity;
	m_CaveFeatureSize = reference.m_CaveFeatureSize;
	m_CaveMinDepth = reference.m_CaveMinDepth;
	m_Layers = reference.m_Layers;
	return 0;
}

int TerrainGenerator::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Serializable::ReadProperty(propName, reader));

	MatchProperty("Width", { reader >> m_Width; });
	MatchProperty("Height", { reader >> m_Height; });
	MatchProperty("Seed", { reader >> m_Seed; });
	MatchProperty("SurfaceLevel", { reader >> m_SurfaceLevel; });
	MatchProperty("SurfaceAmplitude", { reader >> m_SurfaceAmplitude; });
	MatchProperty("SurfaceFeatureSize", { reader >> m_SurfaceFeatureSize; });
	MatchProperty("SurfaceRoughness", { reader >> m_SurfaceRoughness; });
	MatchProperty("CaveDensity", { reader >> m_CaveDensity; });
	MatchProperty("CaveFeatureSize", { reader >> m_CaveFeatureSize; });
	MatchProperty("CaveMinDepth", { reader >> m_CaveMinDepth; });
	MatchProperty("AddLayer", {
		TerrainGeneratorLayerReader layerReader;
		reader >> layerReader;
		m_Layers.emplace_back(layerReader.Layer);
	});

	EndPropertyList;
}

int TerrainGenerator::Save(Writer& writer) const {
	Serializable::Save(writer);

	writer.NewPropertyWithValue("Width", m_Width);
	writer.NewPropertyWithValue("Height", m_Height);
	writer.NewPropertyWithValue("Seed", m_Seed);
	writer.NewPropertyWithValue("SurfaceLevel", m_SurfaceLevel);
	writer.NewPropertyWithValue("SurfaceAmplitude", m_SurfaceAmplitude);
	writer.NewPropertyWithValue("SurfaceFeatureSize", m_SurfaceFeatureSize);
	writer.NewPropertyWithValue("SurfaceRoughness", m_SurfaceRoughness);
	writer.NewPropertyWithValue("CaveDensity", m_CaveDensity);
	writer.NewPropertyWithValue("CaveFeatureSize", m_CaveFeatureSize);
	writer.NewPropertyWithValue("CaveMinDepth", m_CaveMinDepth);
	for (const Layer& layer: m_Layers) {
		TerrainGeneratorLayerReader layerWriter;
		layerWriter.Layer = layer;
		writer.NewPropertyWithValue("AddLayer", layerWriter);
	}

	return 0;
}

BITMAP* TerrainGenerator::Generate(bool wrapsX, std::vector<int>* surfaceHeights) const {
	RTEAssert(m_Width > 0 && m_Height > 0, "TerrainGenerator has an invalid size!");
	RTEAssert(!m_Layers.empty(), "TerrainGenerator has no layers to generate terrain from!");

	uint32_t seed = m_Seed;
	if (seed == 0) {
		if (s_NextSeed.has_value()) {
			seed = s_NextSeed.value();
			s_NextSeed.reset();
		} else {
			seed = std::random_device()();
		}
	}
	s_LastSeed = seed;

	const int wrapWidth = wrapsX ? m_Width : 0;
	const float averageSurfaceY = m_SurfaceLevel * static_cast<float>(m_Height);
	const int minSurfaceY = m_Height / 10;
	const int maxSurfaceY = (m_Height * 8) / 10;

	// The surface height and the depth of each layer boundary only depend on X, so work them out once per column.
	std::vector<int> surfaceY(m_Width);
	std::vector<std::vector<int>> layerBottomDepths(m_Layers.size(), std::vector<int>(m_Width));
	for (int x = 0; x < m_Width; ++x) {
		float surfaceNoise = FractalNoise(static_cast<float>(x), 0, static_cast<float>(m_SurfaceFeatureSize), m_SurfaceRoughness, 5, wrapWidth, seed) * 2.0F - 1.0F;
		surfaceY[x] = std::clamp(static_cast<int>(averageSurfaceY + surfaceNoise * static_cast<float>(m_SurfaceAmplitude)), minSurfaceY, maxSurfaceY);

		int depth = 0;
		for (size_t layer = 0; layer < m_Layers.size(); ++layer) {
			const int thickness = m_Layers[layer].Thickness;
			// Make boundaries uneven, by up to a third of the layer's thickness either way.
			float boundaryNoise = FractalNoise(static_cast<float>(x), static_cast<float>(layer) * 1000.0F, static_cast<float>(std::max(thickness * 4, 32)), 0.5F, 3, wrapWidth, seed + 101u) * 2.0F - 1.0F;
			depth += thickness;
			layerBottomDepths[layer][x] = depth + static_cast<int>(boundaryNoise * static_cast<float>(thickness) / 3.0F);
		}
		// The last layer goes all the way down.
		layerBottomDepths.back()[x] = m_Height;
	}

	// Caves become possible below CaveMinDepth, fading in over this many pixels so they don't start with a flat ceiling.
	const float caveFadeInDepth = 40.0F;
	// Fractal value noise clusters around 0.5, so this threshold makes roughly CaveDensity of the underground into caves.
	const float caveThreshold = 0.5F + 0.22F * (1.0F - std::clamp(m_CaveDensity * 2.0F, 0.0F, 1.0F));

	BITMAP* materialBitmap = create_bitmap_ex(8, m_Width, m_Height);
	RTEAssert(materialBitmap, "Failed to create the bitmap for a generated terrain!");

	std::vector<int> rows(m_Height);
	std::iota(rows.begin(), rows.end(), 0);
	std::for_each(std::execution::par_unseq, rows.begin(), rows.end(), [&](int y) {
		unsigned char* row = materialBitmap->line[y];
		for (int x = 0; x < m_Width; ++x) {
			const int depth = y - surfaceY[x];
			if (depth < 0) {
				row[x] = MaterialColorKeys::g_MaterialAir;
				continue;
			}
			if (m_CaveDensity > 0 && depth > m_CaveMinDepth) {
				float caveNoise = FractalNoise(static_cast<float>(x), static_cast<float>(y), static_cast<float>(m_CaveFeatureSize), 0.5F, 3, wrapWidth, seed + 202u);
				float fadeIn = std::min(static_cast<float>(depth - m_CaveMinDepth) / caveFadeInDepth, 1.0F);
				if (caveNoise > caveThreshold + (1.0F - fadeIn) * 0.3F) {
					row[x] = MaterialColorKeys::g_MaterialAir;
					continue;
				}
			}
			size_t layer = 0;
			while (layer + 1 < m_Layers.size() && depth >= layerBottomDepths[layer][x]) {
				++layer;
			}
			row[x] = m_Layers[layer].MaterialIndex;
		}
	});

	if (surfaceHeights) {
		*surfaceHeights = std::move(surfaceY);
	}
	return materialBitmap;
}
