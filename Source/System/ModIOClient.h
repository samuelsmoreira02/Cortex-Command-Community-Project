#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace RTE {

	/// A mod listed on mod.io.
	struct ModIOMod {
		long long ID = 0; //!< The mod's mod.io ID.
		std::string Name; //!< The mod's name.
		std::string Summary; //!< A short description of the mod.
		std::string Author; //!< The name of whoever uploaded the mod.
		std::string ProfileURL; //!< The mod's page on mod.io.
		long long DownloadsTotal = 0; //!< How many times the mod was downloaded.
		std::string RatingText; //!< The mod's rating, as mod.io words it (e.g. "Very Positive").
		std::vector<std::string> Tags; //!< The mod's tags, like the game version it was made for and its category.
		bool HasDependencies = false; //!< Whether the mod needs other mods to work.
		long long FileID = 0; //!< The ID of the mod's current file. Changes with every update.
		std::string FileName; //!< The name of the mod's current file.
		long long FileSize = 0; //!< The size of the mod's current file, in bytes.
		std::string FileVersion; //!< The version of the mod's current file, if its author gave one.
		std::string DownloadURL; //!< Where to download the mod's current file from.
	};

	/// A page of mods listed on mod.io.
	struct ModIOModPage {
		std::vector<ModIOMod> Mods; //!< The mods on this page.
		long long Total = 0; //!< How many mods match the search in all.
		std::string Error; //!< What went wrong, if the page couldn't be fetched. Empty otherwise.
	};

	/// Browses, downloads and installs Cortex Command Community Project mods from mod.io.
	/// Everything here blocks on the network or disk, so it's meant to be called off the main thread.
	/// Mods are installed into the mods folder, and what's installed is remembered in Userdata/ModIO/Installed.txt so updates can be spotted.
	class ModIOClient {

	public:
		/// How to order search results.
		enum class SortOrder {
			Popular,
			MostDownloaded,
			RecentlyUpdated,
			SortOrderCount
		};

		static constexpr int c_GameID = 508; //!< The Cortex Command Community Project's game ID on mod.io.

		/// Gets a description of a sort order to show players.
		/// @param sortOrder The sort order.
		/// @return The description.
		static std::string GetSortOrderName(SortOrder sortOrder);

		/// Fetches a page of mods matching a search.
		/// @param apiKey The mod.io API key to use.
		/// @param searchText Text to look for in mod names. Empty lists all mods.
		/// @param sortOrder How to order the results.
		/// @param pageIndex Which page to get, starting at 0.
		/// @param pageSize How many mods per page.
		/// @return The page of mods, or an error.
		static ModIOModPage Search(const std::string& apiKey, const std::string& searchText, SortOrder sortOrder, int pageIndex, int pageSize);

		/// Reads a page of mods from a mod.io API response.
		/// @param json The response body.
		/// @return The page of mods, or an error if the response is an error or isn't valid.
		static ModIOModPage ParseModPage(const std::string& json);

		/// Downloads and installs a mod, along with any mods it depends on that aren't installed yet.
		/// @param apiKey The mod.io API key to use.
		/// @param mod The mod to install.
		/// @param bytesDownloaded If not null, updated with the bytes downloaded so far of the file currently downloading, for showing progress.
		/// @param installedModules Filled with the module folders that were installed.
		/// @param error Set to what went wrong, if the installation fails.
		/// @return Whether the mod was installed.
		static bool Install(const std::string& apiKey, const ModIOMod& mod, std::atomic<size_t>* bytesDownloaded, std::vector<std::string>& installedModules, std::string& error);

		/// Installs the modules packaged in a mod's zip file into the mods folder, replacing any older versions of them.
		/// Only .rte folders are installed. Anything that would end up outside the mods folder, or replace one of the game's own modules, is refused.
		/// @param zipPath The zip file to install.
		/// @param installedModules Filled with the module folders that were installed.
		/// @param error Set to what went wrong, if the installation fails.
		/// @return Whether the modules were installed.
		static bool InstallFromZip(const std::string& zipPath, std::vector<std::string>& installedModules, std::string& error);

		/// Gets the ID of the file a mod was last installed from through mod.io.
		/// @param modID The mod's mod.io ID.
		/// @return The file ID, or 0 if the mod wasn't installed through mod.io or its folders have since been deleted.
		static long long GetInstalledFileID(long long modID);

		/// Gets whether a mod seems to be installed, either through mod.io or by hand, judging by its file name (e.g. "neon.rte.zip" is installed as "neon.rte").
		/// @param mod The mod.
		/// @return Whether a module folder named after the mod's file exists in the mods folder.
		static bool IsModuleFolderPresent(const ModIOMod& mod);

	private:
		static constexpr long long c_MaxUncompressedBytes = 2LL * 1024 * 1024 * 1024; //!< Refuse to extract more than this from one zip, so a malicious zip can't fill the disk.

		/// Gets a mod's details.
		static ModIOMod GetMod(const std::string& apiKey, long long modID, std::string& error);

		/// Gets the IDs of the mods a mod depends on.
		static std::vector<long long> GetDependencyIDs(const std::string& apiKey, long long modID, std::string& error);

		/// Downloads and installs a single mod, without its dependencies.
		static bool InstallSingle(const std::string& apiKey, const ModIOMod& mod, std::atomic<size_t>* bytesDownloaded, std::vector<std::string>& installedModules, std::string& error);

		/// Gets why the game couldn't load an extracted module, judging by the SupportedGameVersion in its Index.ini.
		/// @param moduleDirectory The extracted module folder.
		/// @param moduleName The module's folder name, e.g. "MyMod.rte".
		/// @return Why the game couldn't load the module, or an empty string if it could.
		static std::string GetGameVersionIncompatibility(const std::filesystem::path& moduleDirectory, const std::string& moduleName);

		/// Remembers which file a mod was installed from, and which modules it installed.
		static void RecordInstall(long long modID, long long fileID, const std::vector<std::string>& modules);

		/// Gets the base URL of the API for this game, with no trailing slash.
		static std::string GetGameAPIURL();
	};
} // namespace RTE
