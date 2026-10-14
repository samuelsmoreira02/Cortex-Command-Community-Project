#pragma once

#include "ModIOClient.h"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace RTE {

	class AllegroScreen;
	class GUIInputWrapper;
	class GUIControlManager;
	class GUICollectionBox;
	class GUIButton;
	class GUILabel;
	class GUIListBox;
	class GUITextBox;

	/// The mod browser, opened from the mod manager. Searches mod.io for mods, and downloads and installs them, along with what they depend on.
	/// Searching and installing happen in the background, so the menu stays responsive.
	class ModBrowserGUI {

	public:
#pragma region Creation
		/// Constructor method used to instantiate a ModBrowserGUI object in system memory and make it ready for use.
		/// @param guiScreen Pointer to a GUIScreen interface that will be used by this ModBrowserGUI's GUIControlManager. Ownership is NOT transferred!
		/// @param guiInput Pointer to a GUIInput interface that will be used by this ModBrowserGUI's GUIControlManager. Ownership is NOT transferred!
		ModBrowserGUI(AllegroScreen* guiScreen, GUIInputWrapper* guiInput);

		/// Destructor method used to clean up a ModBrowserGUI object before deletion from system memory. Waits for any search or installation still running.
		~ModBrowserGUI();
#pragma endregion

#pragma region Concrete Methods
		/// Handles the player interaction with the ModBrowserGUI GUI elements.
		/// @return Whether the player requested to go back to the mod manager.
		bool HandleInputEvents();

		/// Draws the ModBrowserGUI to the screen.
		void Draw() const;
#pragma endregion

	private:
		static constexpr int c_PageSize = 20; //!< How many mods to list per page.

		std::unique_ptr<GUIControlManager> m_GUIControlManager; //!< The GUIControlManager which holds all the GUIControls of this menu.

		GUIButton* m_BackButton; //!< Goes back to the mod manager.
		GUITextBox* m_SearchTextBox; //!< Text to search mod names for.
		GUIButton* m_SearchButton; //!< Starts a search.
		GUIButton* m_SortButton; //!< Cycles the order of the results.
		GUIListBox* m_ResultsListBox; //!< The mods found.
		GUILabel* m_DetailsLabel; //!< Details of the selected mod.
		GUIButton* m_InstallButton; //!< Installs or updates the selected mod.
		GUIButton* m_PreviousPageButton; //!< Goes to the previous page of results.
		GUIButton* m_NextPageButton; //!< Goes to the next page of results.
		GUILabel* m_PageLabel; //!< Which page of how many is showing.
		GUILabel* m_ApiKeyLabel; //!< Labels the API key text box.
		GUITextBox* m_ApiKeyTextBox; //!< Where the player enters their mod.io API key.
		GUIButton* m_SaveApiKeyButton; //!< Saves the entered API key.
		GUILabel* m_StatusLabel; //!< What's going on, or what went wrong.

		bool m_FirstUpdate; //!< Whether the browser hasn't been updated yet, so it should search as soon as it opens.
		ModIOClient::SortOrder m_SortOrder; //!< How results are ordered.
		int m_PageIndex; //!< The page of results showing, starting at 0.
		std::string m_SearchText; //!< The text of the current search.
		ModIOModPage m_Page; //!< The mods showing.

		std::future<ModIOModPage> m_SearchTask; //!< The search running in the background, if any.

		/// The outcome of an installation.
		struct InstallResult {
			bool Succeeded = false; //!< Whether the mod was installed.
			std::vector<std::string> Modules; //!< The module folders that were installed.
			std::string Error; //!< What went wrong, if it failed.
		};
		std::future<InstallResult> m_InstallTask; //!< The installation running in the background, if any.
		std::atomic<size_t> m_BytesDownloaded; //!< How much of the file being installed has been downloaded.
		ModIOMod m_InstallingMod; //!< The mod being installed.
		bool m_RestartNeeded; //!< Whether mods were installed this session, so the game has to be restarted to load them.

		/// Starts fetching the current page of the current search in the background.
		void StartSearch();

		/// Starts installing the selected mod in the background.
		void StartInstall();

		/// Picks up finished searches and installations, and shows download progress.
		void UpdateTasks();

		/// Gets whether a search or installation is running.
		bool IsBusy() const;

		/// Fills the results list with the current page.
		void PopulateResults();

		/// Shows the details of the selected mod, and sets up the install button for it.
		void ShowSelectedModDetails();

		/// Gets the mod selected in the results list.
		/// @return The selected mod, or nullptr if none is.
		const ModIOMod* GetSelectedMod() const;

		/// Gets how a mod's installation stands, to show players.
		/// @param mod The mod.
		/// @return "installed", "update available", or an empty string if it isn't installed.
		static std::string GetInstallState(const ModIOMod& mod);

		/// Shows or hides the API key entry, depending on whether a key is set.
		void RefreshApiKeyEntry();

		/// Saves the API key typed in the key entry, and searches with it if it's usable.
		void SaveApiKey();

		/// Sets the status line.
		void SetStatus(const std::string& status);
	};
} // namespace RTE
