#pragma once

#include "Vector.h"

#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace RTE {

	class SLTerrain;

	/// Fire that spreads through flammable terrain. Pixels of materials with a Flammability above 0 can catch fire from burning neighbours,
	/// burn for their material's BurnTime, then turn into their BurnsInto material (air by default, burning them away).
	/// Only burning pixels are tracked, so terrain that isn't on fire costs nothing.
	class TerrainFire {

	public:
#pragma region Creation
		/// Constructor method used to instantiate a TerrainFire object in system memory and make it ready for use.
		TerrainFire() { Clear(); }
#pragma endregion

#pragma region Concrete Methods
		/// Sets the flammable terrain within a radius on fire.
		/// @param terrain The terrain to set on fire. Ownership is NOT transferred!
		/// @param pos The center of the area to set on fire, in scene coordinates.
		/// @param radius The radius of the area to set on fire, in pixels.
		/// @return How many pixels caught fire.
		int Ignite(SLTerrain* terrain, const Vector& pos, float radius);

		/// Puts out burning terrain within a radius. The pixels keep what's left of their material.
		/// @param pos The center of the area, in scene coordinates.
		/// @param radius The radius of the area, in pixels.
		/// @return How many pixels were put out.
		int Extinguish(const Vector& pos, float radius);

		/// Spreads fire, burns out pixels and spawns flames. Call every simulation update.
		/// @param terrain The terrain that's burning. Ownership is NOT transferred!
		void Update(SLTerrain* terrain);

		/// Gets how many terrain pixels are on fire.
		/// @return The number of burning pixels.
		int GetBurningPixelCount() const { return static_cast<int>(m_BurningPixels.size()); }

		/// Puts out all fires.
		void Clear();
#pragma endregion

	private:
		/// A terrain pixel that's on fire.
		struct BurningPixel {
			int X; //!< X position in the terrain.
			int Y; //!< Y position in the terrain.
			float TimeLeftMS; //!< How long it burns before burning out.
		};

		static constexpr float c_UpdateIntervalMS = 50.0F; //!< Fire only spreads this often, which is plenty for how slowly it spreads and keeps it cheap.
		static constexpr float c_SpreadChancePerUpdate = 0.18F; //!< Base chance per update of fire spreading to a fully flammable neighbour directly above it.
		static constexpr int c_MaxFlamesPerUpdate = 12; //!< Cap on flame particles spawned per update, so huge fires don't flood the scene with particles.
		static constexpr int c_MaxBurningPixels = 20000; //!< Cap on burning pixels, so a whole forest burning doesn't grind the game to a halt.

		std::vector<BurningPixel> m_BurningPixels; //!< All the pixels currently on fire.
		std::unordered_set<int> m_BurningPositions; //!< The positions of burning pixels, as Y * terrain width + X, to quickly check if a pixel is already burning.
		float m_TimeSinceUpdateMS; //!< Simulation time accumulated since fire last spread.
		std::vector<int> m_FireColors; //!< Palette indices burning pixels flicker between.
		std::mutex m_Mutex; //!< Scripts can set terrain on fire from multiple threads.

		/// Sets a single pixel on fire if it's flammable and not burning already.
		/// @return Whether the pixel caught fire.
		bool IgnitePixel(SLTerrain* terrain, int x, int y);

		/// Spawns a flame particle at a burning pixel, which glows, lights up its surroundings and burns whatever it touches.
		void SpawnFlame(int x, int y) const;
	};
} // namespace RTE
