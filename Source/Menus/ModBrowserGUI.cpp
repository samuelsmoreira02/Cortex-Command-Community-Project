#include "ModBrowserGUI.h"

#include "SettingsMan.h"
#include "WindowMan.h"
#include "UInputMan.h"
#include "HttpClient.h"

#include "GUI.h"
#include "AllegroScreen.h"
#include "GUIInputWrapper.h"
#include "GUICollectionBox.h"
#include "GUILabel.h"
#include "GUIButton.h"
#include "GUIListBox.h"
#include "GUITextBox.h"

#include <chrono>
#include <cstdio>

using namespace RTE;

namespace {
	std::string FormatMegabytes(double bytes) {
		char text[32];
		if (bytes < 1024.0 * 1024.0) {
			std::snprintf(text, sizeof(text), "%.0f KB", std::max(1.0, bytes / 1024.0));
		} else {
			std::snprintf(text, sizeof(text), "%.1f MB", bytes / (1024.0 * 1024.0));
		}
		return text;
	}

	/// Converts UTF-8 text from mod.io into the plain ASCII the game's fonts can draw. Accented letters lose their accents, typographic punctuation becomes its plain equivalent, and anything else (like emoji) is dropped.
	/// @param text The UTF-8 text.
	/// @return The text in plain ASCII.
	std::string ToDisplayText(const std::string& text) {
		// The plain equivalents of the Latin-1 letters from U+00C0 to U+00FF.
		static const char* const latinLetters[64] = {
		    "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
		    "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
		    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
		    "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y"};

		std::string result;
		result.reserve(text.size());
		auto append = [&result](const char* plain) {
			// Don't let dropped characters leave runs of spaces behind.
			if (plain[0] == ' ' && (result.empty() || result.back() == ' ' || result.back() == '\n')) {
				return;
			}
			result += plain;
		};
		for (size_t i = 0; i < text.size();) {
			const unsigned char lead = static_cast<unsigned char>(text[i]);
			int length = 1;
			uint32_t codePoint = lead;
			if (lead >= 0xF0) {
				length = 4;
				codePoint = lead & 0x07;
			} else if (lead >= 0xE0) {
				length = 3;
				codePoint = lead & 0x0F;
			} else if (lead >= 0xC0) {
				length = 2;
				codePoint = lead & 0x1F;
			}
			for (int continuation = 1; continuation < length && i + continuation < text.size(); ++continuation) {
				codePoint = (codePoint << 6) | (static_cast<unsigned char>(text[i + continuation]) & 0x3F);
			}
			i += length;

			if (codePoint == '\n') {
				result += '\n';
			} else if (codePoint == '\t' || codePoint == 0xA0) {
				append(" ");
			} else if (codePoint >= 0x20 && codePoint < 0x7F) {
				const char character[2] = {static_cast<char>(codePoint), '\0'};
				append(character);
			} else if (codePoint >= 0xC0 && codePoint <= 0xFF) {
				append(latinLetters[codePoint - 0xC0]);
			} else if (codePoint == 0x2018 || codePoint == 0x2019) {
				append("'");
			} else if (codePoint == 0x201C || codePoint == 0x201D) {
				append("\"");
			} else if (codePoint == 0x2013 || codePoint == 0x2014) {
				append("-");
			} else if (codePoint == 0x2026) {
				append("...");
			}
		}
		while (!result.empty() && result.back() == ' ') {
			result.pop_back();
		}
		return result;
	}

	template <typename T>
	bool IsReady(const std::future<T>& task) {
		return task.valid() && task.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}
} // namespace

ModBrowserGUI::ModBrowserGUI(AllegroScreen* guiScreen, GUIInputWrapper* guiInput) {
	m_GUIControlManager = std::make_unique<GUIControlManager>();
	RTEAssert(m_GUIControlManager->Create(guiScreen, guiInput, "Base.rte/GUIs/Skins/Menus", "MainMenuSubMenuSkin.ini"), "Failed to create GUI Control Manager and load it from Base.rte/GUIs/Skins/Menus/MainMenuSubMenuSkin.ini");
	m_GUIControlManager->Load("Base.rte/GUIs/ModBrowserGUI.ini");

	int rootBoxMaxWidth = g_WindowMan.FullyCoversAllDisplays() ? g_WindowMan.GetPrimaryWindowDisplayWidth() / g_WindowMan.GetResMultiplier() : g_WindowMan.GetResX();
	GUICollectionBox* rootBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("root"));
	rootBox->Resize(rootBoxMaxWidth, g_WindowMan.GetResY());

	GUICollectionBox* browserBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("CollectionBoxModBrowser"));
	browserBox->CenterInParent(true, true);
	browserBox->SetPositionAbs(browserBox->GetXPos(), (rootBox->GetHeight() < 540) ? browserBox->GetYPos() - 15 : 140);

	m_BackButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonBackToModManager"));
	m_BackButton->SetPositionAbs((rootBox->GetWidth() - m_BackButton->GetWidth()) / 2, browserBox->GetYPos() + browserBox->GetHeight() + 10);

	m_SearchTextBox = dynamic_cast<GUITextBox*>(m_GUIControlManager->GetControl("TextBoxSearch"));
	m_SearchButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonSearch"));
	m_SortButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonSort"));
	m_ResultsListBox = dynamic_cast<GUIListBox*>(m_GUIControlManager->GetControl("ListBoxResults"));
	m_ResultsListBox->SetMouseScrolling(true);
	m_ResultsListBox->SetScrollBarThickness(15);
	m_ResultsListBox->SetScrollBarPadding(2);
	m_DetailsLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelDetails"));
	m_InstallButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonInstall"));
	m_PreviousPageButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonPreviousPage"));
	m_NextPageButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonNextPage"));
	m_PageLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelPage"));
	m_ApiKeyLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelApiKey"));
	m_ApiKeyTextBox = dynamic_cast<GUITextBox*>(m_GUIControlManager->GetControl("TextBoxApiKey"));
	m_SaveApiKeyButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonSaveApiKey"));
	m_StatusLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelStatus"));

	m_FirstUpdate = true;
	m_SortOrder = ModIOClient::SortOrder::Popular;
	m_PageIndex = 0;
	m_BytesDownloaded = 0;
	m_RestartNeeded = false;

	m_SortButton->SetText(ModIOClient::GetSortOrderName(m_SortOrder));
	m_InstallButton->SetVisible(false);
	m_DetailsLabel->SetText("");
	RefreshApiKeyEntry();
}

ModBrowserGUI::~ModBrowserGUI() {
	// The tasks write to this object, so they have to finish before it goes away.
	if (m_SearchTask.valid()) {
		m_SearchTask.wait();
	}
	if (m_InstallTask.valid()) {
		m_InstallTask.wait();
	}
}

bool ModBrowserGUI::IsBusy() const {
	return m_SearchTask.valid() || m_InstallTask.valid();
}

void ModBrowserGUI::SetStatus(const std::string& status) {
	m_StatusLabel->SetText(ToDisplayText(status));
}

void ModBrowserGUI::RefreshApiKeyEntry() {
	bool needsKey = g_SettingsMan.GetModIOApiKey().empty();
	m_ApiKeyLabel->SetVisible(needsKey);
	m_ApiKeyTextBox->SetVisible(needsKey);
	m_SaveApiKeyButton->SetVisible(needsKey);
	if (needsKey) {
		SetStatus("To browse mods, get a free API key at mod.io/me/access, paste it below and press Save Key.");
	}
}

void ModBrowserGUI::StartSearch() {
	if (IsBusy()) {
		return;
	}
	if (!HttpClient::IsSupported()) {
		SetStatus("Browsing mods isn't supported on this platform yet.");
		return;
	}
	const std::string apiKey = g_SettingsMan.GetModIOApiKey();
	if (apiKey.empty()) {
		RefreshApiKeyEntry();
		return;
	}
	SetStatus("Searching mod.io...");
	m_SearchTask = std::async(std::launch::async, [apiKey, searchText = m_SearchText, sortOrder = m_SortOrder, pageIndex = m_PageIndex]() {
		return ModIOClient::Search(apiKey, searchText, sortOrder, pageIndex, c_PageSize);
	});
}

void ModBrowserGUI::StartInstall() {
	const ModIOMod* mod = GetSelectedMod();
	if (!mod || IsBusy()) {
		return;
	}
	m_InstallingMod = *mod;
	m_BytesDownloaded = 0;
	m_InstallButton->SetEnabled(false);
	SetStatus("Downloading " + m_InstallingMod.Name + "...");
	m_InstallTask = std::async(std::launch::async, [this, apiKey = g_SettingsMan.GetModIOApiKey(), mod = m_InstallingMod]() {
		InstallResult result;
		result.Succeeded = ModIOClient::Install(apiKey, mod, &m_BytesDownloaded, result.Modules, result.Error);
		return result;
	});
}

void ModBrowserGUI::UpdateTasks() {
	if (IsReady(m_SearchTask)) {
		m_Page = m_SearchTask.get();
		m_Page.Error = ToDisplayText(m_Page.Error);
		for (ModIOMod& mod: m_Page.Mods) {
			for (std::string* text: {&mod.Name, &mod.Summary, &mod.Author, &mod.RatingText, &mod.FileVersion}) {
				*text = ToDisplayText(*text);
			}
			for (std::string& tag: mod.Tags) {
				tag = ToDisplayText(tag);
			}
		}
		if (!m_Page.Error.empty()) {
			SetStatus(m_Page.Error);
		} else if (m_Page.Mods.empty()) {
			SetStatus("No mods found.");
		} else {
			SetStatus("Found " + std::to_string(m_Page.Total) + " mods. Select one to see its details.");
		}
		PopulateResults();
	}

	if (m_InstallTask.valid()) {
		if (IsReady(m_InstallTask)) {
			InstallResult result = m_InstallTask.get();
			if (result.Succeeded) {
				std::string modules;
				for (const std::string& module: result.Modules) {
					modules += (modules.empty() ? "" : ", ") + module;
				}
				m_RestartNeeded = true;
				SetStatus("Installed " + m_InstallingMod.Name + " (" + modules + "). Restart the game to load it.");
				g_GUISound.ConfirmSound()->Play();
			} else {
				SetStatus("Couldn't install " + m_InstallingMod.Name + ": " + result.Error);
				g_GUISound.UserErrorSound()->Play();
			}
			m_InstallButton->SetEnabled(true);
			PopulateResults();
		} else {
			std::string progress = "Downloading " + m_InstallingMod.Name + "... " + FormatMegabytes(static_cast<double>(m_BytesDownloaded.load()));
			if (m_InstallingMod.FileSize > 0) {
				progress += " of " + FormatMegabytes(static_cast<double>(m_InstallingMod.FileSize));
			}
			SetStatus(progress);
		}
	}
}

std::string ModBrowserGUI::GetInstallState(const ModIOMod& mod) {
	long long installedFileID = ModIOClient::GetInstalledFileID(mod.ID);
	if (installedFileID != 0) {
		return installedFileID == mod.FileID ? "installed" : "update available";
	}
	return ModIOClient::IsModuleFolderPresent(mod) ? "installed" : "";
}

void ModBrowserGUI::PopulateResults() {
	int selectedIndex = m_ResultsListBox->GetSelectedIndex();
	m_ResultsListBox->ClearList();
	for (int index = 0; index < static_cast<int>(m_Page.Mods.size()); ++index) {
		m_ResultsListBox->AddItem(m_Page.Mods[index].Name, GetInstallState(m_Page.Mods[index]), nullptr, nullptr, index);
	}
	if (selectedIndex >= 0 && selectedIndex < static_cast<int>(m_Page.Mods.size())) {
		m_ResultsListBox->SetSelectedIndex(selectedIndex);
	} else {
		m_ResultsListBox->ScrollToTop();
	}

	long long pageCount = std::max(1LL, (m_Page.Total + c_PageSize - 1) / c_PageSize);
	m_PageLabel->SetText("Page " + std::to_string(m_PageIndex + 1) + " of " + std::to_string(pageCount));
	m_PreviousPageButton->SetEnabled(m_PageIndex > 0);
	m_NextPageButton->SetEnabled(m_PageIndex + 1 < pageCount);
	ShowSelectedModDetails();
}

const ModIOMod* ModBrowserGUI::GetSelectedMod() const {
	const GUIListBox::Item* selectedItem = m_ResultsListBox->GetSelected();
	if (!selectedItem || selectedItem->m_ExtraIndex < 0 || selectedItem->m_ExtraIndex >= static_cast<int>(m_Page.Mods.size())) {
		return nullptr;
	}
	return &m_Page.Mods[selectedItem->m_ExtraIndex];
}

void ModBrowserGUI::ShowSelectedModDetails() {
	const ModIOMod* mod = GetSelectedMod();
	if (!mod) {
		m_DetailsLabel->SetText("");
		m_InstallButton->SetVisible(false);
		return;
	}
	std::string details = mod->Name + "\nby " + mod->Author + "\n\n" + mod->Summary + "\n\n";
	details += std::to_string(mod->DownloadsTotal) + " downloads";
	if (!mod->RatingText.empty()) {
		details += ", rated " + mod->RatingText;
	}
	details += "\nSize: " + FormatMegabytes(static_cast<double>(mod->FileSize));
	if (!mod->FileVersion.empty()) {
		details += ", version " + mod->FileVersion;
	}
	if (!mod->Tags.empty()) {
		std::string tags;
		for (const std::string& tag: mod->Tags) {
			tags += (tags.empty() ? "" : ", ") + tag;
		}
		details += "\nTags: " + tags;
	}
	if (mod->HasDependencies) {
		details += "\nNeeds other mods, which are installed with it.";
	}
	m_DetailsLabel->SetText(details);

	std::string installState = GetInstallState(*mod);
	m_InstallButton->SetVisible(true);
	m_InstallButton->SetText(installState.empty() ? "Install" : (installState == "update available" ? "Update" : "Reinstall"));
	m_InstallButton->SetEnabled(!m_InstallTask.valid());
}

bool ModBrowserGUI::HandleInputEvents() {
	if (m_FirstUpdate) {
		m_FirstUpdate = false;
		StartSearch();
	}
	UpdateTasks();
	m_GUIControlManager->Update();

	GUIEvent guiEvent;
	while (m_GUIControlManager->GetEvent(&guiEvent)) {
		if (guiEvent.GetType() == GUIEvent::Command) {
			if (guiEvent.GetControl() == m_BackButton) {
				g_GUISound.BackButtonPressSound()->Play();
				return true;
			} else if (guiEvent.GetControl() == m_SearchButton) {
				g_GUISound.ButtonPressSound()->Play();
				m_SearchText = m_SearchTextBox->GetText();
				m_PageIndex = 0;
				StartSearch();
			} else if (guiEvent.GetControl() == m_SortButton) {
				g_GUISound.ButtonPressSound()->Play();
				m_SortOrder = static_cast<ModIOClient::SortOrder>((static_cast<int>(m_SortOrder) + 1) % static_cast<int>(ModIOClient::SortOrder::SortOrderCount));
				m_SortButton->SetText(ModIOClient::GetSortOrderName(m_SortOrder));
				m_PageIndex = 0;
				StartSearch();
			} else if (guiEvent.GetControl() == m_PreviousPageButton && m_PageIndex > 0) {
				g_GUISound.ButtonPressSound()->Play();
				--m_PageIndex;
				StartSearch();
			} else if (guiEvent.GetControl() == m_NextPageButton) {
				g_GUISound.ButtonPressSound()->Play();
				++m_PageIndex;
				StartSearch();
			} else if (guiEvent.GetControl() == m_InstallButton) {
				g_GUISound.ButtonPressSound()->Play();
				StartInstall();
			} else if (guiEvent.GetControl() == m_SaveApiKeyButton) {
				SaveApiKey();
			}
		} else if (guiEvent.GetType() == GUIEvent::Notification) {
			if (guiEvent.GetMsg() == GUIButton::Focused && dynamic_cast<GUIButton*>(guiEvent.GetControl())) {
				g_GUISound.SelectionChangeSound()->Play();
			}
			if (guiEvent.GetControl() == m_ResultsListBox && guiEvent.GetMsg() == GUIListBox::Select) {
				g_GUISound.SelectionChangeSound()->Play();
				ShowSelectedModDetails();
			}
		}
	}

	// In the menus, Enter doubles as a click for keyboard and controller players, so text boxes never see it. Check for it directly instead, like the console does.
	if (g_UInputMan.KeyPressed(SDLK_RETURN) || g_UInputMan.KeyPressed(SDLK_KP_ENTER)) {
		if (m_SearchTextBox->HasFocus()) {
			m_SearchText = m_SearchTextBox->GetText();
			m_PageIndex = 0;
			StartSearch();
		} else if (m_ApiKeyTextBox->HasFocus()) {
			SaveApiKey();
		}
	}
	return false;
}

void ModBrowserGUI::SaveApiKey() {
	g_SettingsMan.SetModIOApiKey(m_ApiKeyTextBox->GetText());
	if (g_SettingsMan.GetModIOApiKey().empty()) {
		g_GUISound.UserErrorSound()->Play();
	} else {
		g_GUISound.ConfirmSound()->Play();
		m_ApiKeyTextBox->SetText("");
		RefreshApiKeyEntry();
		StartSearch();
	}
}

void ModBrowserGUI::Draw() const {
	m_GUIControlManager->Draw();
}
