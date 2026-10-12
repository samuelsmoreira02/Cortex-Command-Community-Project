#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace RTE {

	class SLTerrain;
	class SoundContainer;

	/// Makes terrain that has lost its connection to the ground fall.
	/// Terrain removal is registered as it happens, and the areas around it are periodically checked for pieces of terrain that no longer connect to the bottom of the scene (or its sides, if it doesn't wrap).
	/// Tiny pieces crumble into loose particles, bigger ones fall as rigid chunks that keep every pixel's material and color, crush whatever is under them and settle where they land.
	/// Only checks around removed terrain, so terrain nobody touches costs nothing.
	class TerrainCollapse {

	public:
#pragma region Creation
		/// Constructor method used to instantiate a TerrainCollapse object in system memory and make it ready for use.
		TerrainCollapse();

		/// Destructor method used to clean up a TerrainCollapse object before deletion from system memory.
		~TerrainCollapse();
#pragma endregion

#pragma region Concrete Methods
		/// Registers that a terrain pixel was removed, so the terrain around it gets checked for pieces that lost their support.
		/// @param x The X position of the removed pixel.
		/// @param y The Y position of the removed pixel.
		void RegisterRemovedPixel(int x, int y);

		/// Registers that an area of terrain was removed, so the terrain around it gets checked for pieces that lost their support.
		/// @param left The left edge of the area. It doesn't need to be wrapped.
		/// @param top The top edge of the area.
		/// @param width The width of the area, in pixels.
		/// @param height The height of the area, in pixels.
		void RegisterRemovedArea(int left, int top, int width, int height);

		/// Finds unsupported terrain around removed terrain and makes it fall, and moves falling chunks. Call every simulation update.
		/// @param terrain The terrain to collapse. Ownership is NOT transferred!
		void Update(SLTerrain* terrain);

		/// Gets how many chunks of terrain are currently falling.
		/// @return The number of falling chunks.
		int GetFallingChunkCount() const { return static_cast<int>(m_FallingChunks.size()); }

		/// Stops all falling chunks where they are and forgets all removed terrain.
		void Clear();
#pragma endregion

	private:
		/// A pixel of a falling chunk, in scene coordinates.
		struct ChunkPixel {
			int X; //!< X position, wrapped. Chunks only fall straight down, so it never changes.
			int Y; //!< Y position.
			uint8_t Material; //!< Material index.
			uint8_t Color; //!< Foreground color index.
		};

		/// A rigid piece of terrain falling straight down. It stays part of the terrain while it falls, so everything collides with it as usual.
		struct FallingChunk {
			std::vector<ChunkPixel> Pixels; //!< The pixels of the chunk, sorted by column and then from top to bottom.
			std::vector<size_t> BottomPixels; //!< Indices of the pixels with no chunk pixel directly below them, which are the ones that can land on something.
			float Velocity; //!< Falling speed, in pixels per second.
			float PendingFall; //!< Fractional pixels of fall accumulated but not moved yet.
			int DistanceFallen; //!< How many pixels the chunk has fallen so far.
			int Top; //!< The topmost Y position of the chunk when it started falling, to update pathfinding over the whole area it swept when it lands.
			std::unordered_set<const void*> CrushedActors; //!< Actors this chunk has already hurt, so each one is only hurt once.
		};

		static constexpr int c_CellSize = 16; //!< Size of the cells removed terrain is grouped in to be checked, in pixels.
		static constexpr float c_CheckIntervalMS = 100.0F; //!< How often removed terrain is checked for unsupported pieces.
		static constexpr size_t c_MaxCellsPerCheck = 256; //!< Cap on cells checked per check. Cells over it wait for the next check.
		static constexpr int c_MaxIslandPixels = 40000; //!< Pieces bigger than this are considered supported, so huge floating landmasses don't cost a fortune to check, and don't all come down at once.
		static constexpr int c_MinChunkPixels = 40; //!< Pieces smaller than this crumble into loose particles instead of falling as a chunk.
		static constexpr size_t c_MaxFallingChunks = 24; //!< Cap on chunks falling at once. Pieces found over it wait until some land.
		static constexpr float c_TerminalVelocity = 600.0F; //!< Maximum falling speed, in pixels per second.
		static constexpr int c_MaxSearchesPerCheck = 30000; //!< Cap on flood fills per check, so stamps never run out in the middle of one.

		std::unordered_set<int> m_DirtyCells; //!< Cells with removed terrain that need checking, as cell Y * cells per row + cell X.
		std::vector<FallingChunk> m_FallingChunks; //!< All the chunks currently falling.
		std::vector<uint16_t> m_VisitStamps; //!< Per pixel stamp of the last search that visited it, so pixels are visited at most once per search without clearing a scene-sized buffer every time.
		uint16_t m_CurrentStamp; //!< The stamp of the current search. Every search gets a new one.
		uint16_t m_CheckStartStamp; //!< The first stamp of the current check. Pixels stamped since were visited by an earlier search of this check, which found them supported or made them fall.
		float m_TimeSinceCheckMS; //!< Simulation time accumulated since the last check.
		std::unique_ptr<SoundContainer> m_LandingSound; //!< The sound chunks make when they land. Loaded the first time one lands.
		std::mutex m_Mutex; //!< Terrain can be removed from multiple threads.

		/// Checks the cells with removed terrain for pieces that lost their support, and makes those fall.
		/// @param terrain The terrain to check.
		void CheckDirtyCells(SLTerrain* terrain);

		/// Flood fills the piece of terrain a pixel belongs to, to find out whether it's supported.
		/// @param terrain The terrain to search.
		/// @param seedX The X position of a solid pixel of the piece.
		/// @param seedY The Y position of a solid pixel of the piece.
		/// @param island Filled with the positions of the pixels of the piece, as Y * terrain width + X, if it's unsupported.
		/// @return Whether the piece is unsupported, i.e. it doesn't reach the ground or anything known to be supported, and isn't too big to fall.
		bool FindUnsupportedPiece(SLTerrain* terrain, int seedX, int seedY, std::vector<int>& island);

		/// Makes an unsupported piece of terrain fall, as loose particles if it's tiny or as a chunk otherwise.
		/// @param terrain The terrain the piece is part of.
		/// @param island The positions of the pixels of the piece, as Y * terrain width + X.
		void DropPiece(SLTerrain* terrain, const std::vector<int>& island);

		/// Moves a falling chunk down, crushing actors under it, and makes it land when it hits something.
		/// @param terrain The terrain the chunk is part of.
		/// @param chunk The chunk to move.
		/// @param deltaTimeS How much simulation time has passed, in seconds.
		/// @return Whether the chunk is still falling.
		bool UpdateChunk(SLTerrain* terrain, FallingChunk& chunk, float deltaTimeS);

		/// Finds the pixels of a chunk that have no chunk pixel directly below them.
		/// @param chunk The chunk, with its pixels sorted by column and then from top to bottom.
		static void FindBottomPixels(FallingChunk& chunk);

		/// Throws up dust, shakes the screen and updates pathfinding when a chunk lands.
		/// @param terrain The terrain the chunk landed on.
		/// @param chunk The chunk that landed.
		void Land(SLTerrain* terrain, const FallingChunk& chunk);

		/// Marks the cells covering an area as needing a check. Doesn't lock, unlike RegisterRemovedArea.
		/// @param left The left edge of the area. It doesn't need to be wrapped.
		/// @param top The top edge of the area.
		/// @param width The width of the area, in pixels.
		/// @param height The height of the area, in pixels.
		void MarkAreaDirty(int left, int top, int width, int height);
	};
} // namespace RTE
