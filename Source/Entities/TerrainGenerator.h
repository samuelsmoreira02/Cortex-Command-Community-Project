#pragma once

#include "Serializable.h"

#include <cstdint>
#include <optional>
#include <vector>

struct BITMAP;

namespace RTE {

	/// Generates the material layer of an SLTerrain procedurally from a seed: a fractal surface, material layers by depth with uneven boundaries, and caves.
	/// The SLTerrain then texturizes, frosts and scatters debris on it exactly as it does for terrain loaded from a file.
	class TerrainGenerator : public Serializable {

	public:
		SerializableClassNameGetter
		SerializableOverrideMethods

		/// A band of material at a given depth below the surface.
		struct Layer {
			unsigned char MaterialIndex = 0; //!< The material this layer is made of.
			std::string MaterialName; //!< The material's PresetName, for saving.
			int Thickness = 0; //!< The average thickness of this layer, in pixels. The last layer extends to the bottom of the terrain regardless.
		};

#pragma region Creation
		/// Constructor method used to instantiate a TerrainGenerator object in system memory and make it ready for use.
		TerrainGenerator() { Clear(); }

		/// Creates a TerrainGenerator to be identical to another, by deep copy.
		/// @param reference A reference to the TerrainGenerator to deep copy.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create(const TerrainGenerator& reference);
#pragma endregion

#pragma region Getters and Setters
		/// Gets the width of the terrain this generates, in pixels.
		/// @return The width of the generated terrain.
		int GetWidth() const { return m_Width; }

		/// Gets the height of the terrain this generates, in pixels.
		/// @return The height of the generated terrain.
		int GetHeight() const { return m_Height; }

		/// Sets the seed the next generated terrain will use, instead of a random one. Used once, by the next call to Generate.
		/// @param seed The seed to use.
		static void SetNextSeed(uint32_t seed) { s_NextSeed = seed; }

		/// Gets the seed used by the most recently generated terrain, so the same terrain can be generated again.
		/// @return The last seed used.
		static uint32_t GetLastSeed() { return s_LastSeed; }
#pragma endregion

#pragma region Concrete Methods
		/// Generates a new material bitmap.
		/// @param wrapsX Whether the terrain wraps horizontally, in which case the generated terrain also wraps seamlessly.
		/// @param surfaceHeights If not null, filled with the Y of the surface for each column. Anything below it is underground, including caves.
		/// @return A new 8bpp material BITMAP. Ownership IS transferred!
		BITMAP* Generate(bool wrapsX, std::vector<int>* surfaceHeights = nullptr) const;
#pragma endregion

	private:
		static const std::string c_ClassName; //!< A string with the friendly-formatted type name of this object.

		static std::optional<uint32_t> s_NextSeed; //!< Seed to use for the next generated terrain, if one was requested.
		static uint32_t s_LastSeed; //!< The seed used by the most recently generated terrain.

		int m_Width; //!< Width of the generated terrain, in pixels.
		int m_Height; //!< Height of the generated terrain, in pixels.
		uint32_t m_Seed; //!< Fixed seed for this terrain. 0 means a different terrain every time, unless one is requested with SetNextSeed.

		float m_SurfaceLevel; //!< Average height of the surface, as a ratio of the terrain height from the top.
		int m_SurfaceAmplitude; //!< How far the surface goes above and below its average height, in pixels.
		int m_SurfaceFeatureSize; //!< The width of the largest hills, in pixels.
		float m_SurfaceRoughness; //!< How much smaller hills add to larger ones, from 0 (smooth) to 1 (very rugged).

		float m_CaveDensity; //!< Roughly how much of the underground is caves, from 0 to 1.
		int m_CaveFeatureSize; //!< The size of caves, in pixels.
		int m_CaveMinDepth; //!< How far below the surface caves start, in pixels, so the surface isn't riddled with holes.

		std::vector<Layer> m_Layers; //!< The material layers, from the surface down.

		/// Clears all the member variables of this TerrainGenerator, effectively resetting the members of this abstraction level only.
		void Clear();
	};
} // namespace RTE
