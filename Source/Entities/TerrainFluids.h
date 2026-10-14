#pragma once

#include "Vector.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct BITMAP;

namespace RTE {

	class SLTerrain;
	class SceneLayer;
	class TerrainFire;
	class Box;

	/// Liquids and gases that flow through the open spaces of the terrain, one pixel at a time.
	/// Liquids fall, spread out and pool, lighter ones floating on heavier ones. Gases drift upwards and fade away.
	/// Water puts out fire and boils into steam, oil burns, acid eats through terrain, and toxic gas hurts whoever breathes it.
	/// Fluids live in their own layers rather than in the terrain, so they never block movement: whatever's in a liquid is slowed down and pushed up by it instead.
	/// Only pixels holding a fluid are simulated, so a scene without fluids costs nothing.
	class TerrainFluids {

	public:
		/// The kinds of fluid.
		enum class FluidType : uint8_t {
			None,
			Water,
			Oil,
			Acid,
			ToxicGas,
			Steam,
			FluidTypeCount
		};

#pragma region Creation
		/// Constructor method used to instantiate a TerrainFluids object in system memory and make it ready for use.
		TerrainFluids();

		/// Destructor method used to clean up a TerrainFluids object before deletion from system memory.
		~TerrainFluids();
#pragma endregion

#pragma region Concrete Methods
		/// Gets the fluid type with a given name, as used by scripts.
		/// @param name The name, e.g. "Water" or "Toxic Gas".
		/// @return The fluid type, or None if no fluid has that name.
		static FluidType GetFluidTypeFromName(const std::string& name);

		/// Gets the name of a fluid type, as used by scripts.
		/// @param type The fluid type.
		/// @return The name, or an empty string for None.
		static const std::string& GetFluidTypeName(FluidType type);

		/// Gets whether a fluid type is a gas.
		/// @param type The fluid type.
		/// @return Whether it's a gas.
		static bool IsGas(FluidType type) { return type == FluidType::ToxicGas || type == FluidType::Steam; }

		/// Fills the open space within a radius with a fluid. Space already holding a fluid is left alone.
		/// @param terrain The terrain the fluid flows through. Ownership is NOT transferred!
		/// @param type The fluid to add.
		/// @param pos The center of the area to fill, in scene coordinates.
		/// @param radius The radius of the area to fill, in pixels.
		/// @return How many pixels were filled.
		int AddFluid(SLTerrain* terrain, FluidType type, const Vector& pos, float radius);

		/// Removes all fluids within a radius.
		/// @param pos The center of the area, in scene coordinates.
		/// @param radius The radius of the area, in pixels.
		/// @return How many pixels of fluid were removed.
		int RemoveFluids(const Vector& pos, float radius);

		/// Gets the fluid at a position.
		/// @param pos The position, in scene coordinates.
		/// @return The fluid there, or None.
		FluidType GetFluidAt(const Vector& pos);

		/// Gets how many pixels hold a fluid.
		/// @return The number of fluid pixels.
		int GetFluidPixelCount() const { return m_FluidPixelCount; }

		/// Sets the oil within a radius on fire.
		/// @param pos The center of the area, in scene coordinates.
		/// @param radius The radius of the area, in pixels.
		/// @return How many pixels of oil caught fire.
		int IgniteOil(const Vector& pos, float radius);

		/// Lets fire act on the fluid at a terrain pixel next to it: water boils into steam, and oil catches fire.
		/// @param x The X position of the pixel next to the fire.
		/// @param y The Y position of the pixel next to the fire.
		/// @return Whether the fluid there puts the fire out, i.e. whether it was water.
		bool TouchedByFire(int x, int y);

		/// Moves the fluids, lets them act on the terrain and on what's in them. Call every simulation update.
		/// @param terrain The terrain the fluids flow through. Ownership is NOT transferred!
		/// @param fire The terrain's fire, which burning oil spreads to.
		void Update(SLTerrain* terrain, TerrainFire& fire);

		/// Sets the scroll offset the fluids are drawn with, for the screen about to be drawn.
		/// @param offset The scroll offset.
		void SetOffset(const Vector& offset);

		/// Draws the fluids. They're see-through, so call it after drawing whatever's in them.
		/// @param targetDimensions The dimensions of the bitmap being drawn to.
		/// @param targetBox The area of the bitmap the scene is drawn to.
		void Draw(const Box& targetDimensions, Box& targetBox);

		/// Removes all fluids.
		void Clear();
#pragma endregion

	private:
		/// The area acid ate away terrain in during an update.
		struct EatenArea {
			int MinX = 0; //!< Left edge.
			int MinY = 0; //!< Top edge.
			int MaxX = -1; //!< Right edge. Less than MinX while nothing was eaten.
			int MaxY = -1; //!< Bottom edge.
		};

		/// What sets each kind of fluid apart.
		struct FluidProperties {
			std::string Name; //!< The name scripts use for the fluid.
			int Density; //!< Heavier liquids sink below lighter ones.
			int Dispersion; //!< How many pixels a liquid can flow sideways per update. Runnier liquids flow further.
			float Buoyancy; //!< How strongly the liquid pushes up what's in it, as a fraction of gravity when fully submerged.
			float Drag; //!< How much of the speed of what's in the liquid it takes away per update when fully submerged.
			std::array<int, 3> Color; //!< The color of the fluid, as red, green and blue.
		};

		static const std::array<FluidProperties, static_cast<size_t>(FluidType::FluidTypeCount)> c_FluidProperties; //!< The properties of each kind of fluid.

		static constexpr int c_MaxFluidPixels = 40000; //!< Cap on fluid pixels, so a flood doesn't grind the game to a halt.
		static constexpr int c_MaxFallPerUpdate = 4; //!< How many pixels a liquid can fall per update.
		static constexpr int c_MaxPressureReach = 48; //!< How far through a pool liquid under pressure can be pushed to open space in one update.
		static constexpr int c_UpdatesBeforeResting = 10; //!< How many updates liquid has to stay put before it's considered settled.
		static constexpr int c_RestingCheckInterval = 8; //!< How often settled liquid is checked, in updates, in case the terrain around it changed.
		static constexpr int c_MaxFlamesPerUpdate = 2; //!< Cap on flame particles burning oil spawns per update.
		static constexpr unsigned char c_LiquidAlpha = 200; //!< How opaque liquids are drawn.
		static constexpr unsigned char c_GasAlpha = 120; //!< How opaque gases are drawn.

		int m_Width; //!< The width of the scene, in pixels.
		int m_Height; //!< The height of the scene, in pixels.
		bool m_WrapsX; //!< Whether the scene wraps around horizontally.
		std::vector<FluidType> m_Types; //!< The fluid at each pixel of the scene, if any.
		std::vector<uint8_t> m_Data; //!< Per pixel state of the fluid there: how long a gas lasts, how much more acid can eat, or how long oil burns.
		std::vector<uint8_t> m_Stamps; //!< The update each pixel's fluid last acted in, so no fluid acts twice in one update.
		std::vector<uint8_t> m_Rest; //!< How many updates in a row the fluid at each pixel hasn't moved. Settled liquid is only checked now and then, until something next to it moves.
		uint8_t m_CurrentStamp; //!< The stamp of the current update.
		std::vector<int> m_Pixels; //!< The positions of the fluid pixels, as Y * width + X. May hold stale or repeated positions, which are dropped as they're come across.
		std::vector<int> m_SortedPixels; //!< Scratch space to sort the fluid pixels by height each update.
		std::vector<int> m_RowCounts; //!< Scratch space to sort the fluid pixels by height each update.
		int m_FluidPixelCount; //!< How many pixels hold a fluid.
		int m_UpdateCount; //!< How many updates have happened, to stagger things that don't need to happen every update.

		BITMAP* m_LiquidBitmap; //!< The colors of the liquids. Owned by m_LiquidLayer once that's created.
		BITMAP* m_GasBitmap; //!< The colors of the gases. Owned by m_GasLayer once that's created.
		std::unique_ptr<SceneLayer> m_LiquidLayer; //!< Draws the liquids. Created on the main thread, as it makes a GPU texture.
		std::unique_ptr<SceneLayer> m_GasLayer; //!< Draws the gases. Created on the main thread, as it makes a GPU texture.
		std::array<std::vector<int>, static_cast<size_t>(FluidType::FluidTypeCount)> m_Colors; //!< Palette indices each kind of fluid is drawn with.
		std::array<int, static_cast<size_t>(FluidType::FluidTypeCount)> m_SurfaceColors; //!< Palette indices the surface of each kind of liquid is drawn with.
		std::vector<int> m_FireColors; //!< Palette indices burning oil flickers between.

		std::mutex m_Mutex; //!< Scripts can add fluids from multiple threads.

		/// Makes the fluid grid and bitmaps for the current scene, if they haven't been made already.
		void EnsureCreated();

		/// Wraps an X position around the scene if it wraps, and checks whether a position is within the scene.
		/// @return Whether the position is within the scene.
		bool WrapAndCheck(int& x, int y) const;

		/// Puts a fluid at a pixel, without checking whether it's free.
		void SetFluid(int index, FluidType type, uint8_t data);

		/// Removes the fluid at a pixel.
		void RemoveFluid(int index);

		/// Moves the fluid at one pixel to another, swapping it with whatever fluid is there.
		void MoveFluid(int fromIndex, int toIndex);

		/// Wakes up the settled liquid around a pixel, since something there moved.
		void WakeAround(int index);

		/// Draws the fluid at a pixel in its color.
		void PaintPixel(int index);

		/// Gets the bitmap a fluid is drawn on.
		BITMAP* GetBitmapFor(FluidType type) const { return IsGas(type) ? m_GasBitmap : m_LiquidBitmap; }

		/// Moves a liquid one update's worth.
		/// @return The pixel the liquid ended up at, or -1 if it's gone.
		int UpdateLiquid(SLTerrain* terrain, int index, std::vector<Vector>& firesToStart, int& flamesSpawned, EatenArea& eatenArea);

		/// Moves a gas one update's worth.
		/// @return The pixel the gas ended up at, or -1 if it's gone.
		int UpdateGas(SLTerrain* terrain, int index);

		/// Slows down, pushes up and hurts the actors and items in the fluids.
		void ActOnMovableObjects();
	};
} // namespace RTE
