#include "ModIOClient.h"
#include "HttpClient.h"
#include "JsonValue.h"
#include "SettingsMan.h"
#include "PresetMan.h"
#include "System.h"
#include "DataModule.h"

#include "System/Semver200/semver200.h"

#ifdef SYSTEM_MINIZIP
#include <minizip/unzip.h>
#else
#include "unzip.h"
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <fstream>
#include <sstream>

using namespace RTE;

namespace {
	std::string ToLower(std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		return text;
	}

	std::string UrlEncode(const std::string& text) {
		static const char* hexDigits = "0123456789ABCDEF";
		std::string encoded;
		for (unsigned char character: text) {
			if (std::isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~') {
				encoded += static_cast<char>(character);
			} else {
				encoded += '%';
				encoded += hexDigits[character >> 4];
				encoded += hexDigits[character & 0xF];
			}
		}
		return encoded;
	}

	/// Gets the error message out of a mod.io API error response, or describes the status code if there isn't one.
	std::string DescribeAPIError(const HttpClient::Response& response) {
		if (!response.Error.empty()) {
			return response.Error;
		}
		std::string message = JsonValue::Parse(response.Body)["error"]["message"].AsString();
		if (response.StatusCode == 401) {
			return "mod.io refused the API key. Check the key you entered. " + message;
		}
		return "mod.io answered with error " + std::to_string(response.StatusCode) + (message.empty() ? "" : ": " + message);
	}

	ModIOMod ParseMod(const JsonValue& modData) {
		ModIOMod mod;
		mod.ID = modData["id"].AsInteger();
		mod.Name = modData["name"].AsString();
		mod.Summary = modData["summary"].AsString();
		mod.Author = modData["submitted_by"]["username"].AsString();
		mod.ProfileURL = modData["profile_url"].AsString();
		mod.DownloadsTotal = modData["stats"]["downloads_total"].AsInteger();
		mod.RatingText = modData["stats"]["ratings_display_text"].AsString();
		mod.HasDependencies = modData["dependencies"].AsBool();
		const JsonValue& tags = modData["tags"];
		for (size_t index = 0; index < tags.Size(); ++index) {
			mod.Tags.emplace_back(tags[index]["name"].AsString());
		}
		const JsonValue& file = modData["modfile"];
		mod.FileID = file["id"].AsInteger();
		mod.FileName = file["filename"].AsString();
		mod.FileSize = file["filesize"].AsInteger();
		mod.FileVersion = file["version"].AsString();
		mod.DownloadURL = file["download"]["binary_url"].AsString();
		return mod;
	}

	std::filesystem::path GetModIODataDirectory() {
		return std::filesystem::path(System::GetUserdataDirectory()) / "ModIO";
	}

	/// Finds where the module folder starts in a zip entry's path, so mods zipped with a parent folder around their .rte folder still install.
	/// @return The path from the .rte folder on, or an empty string if the entry isn't inside one.
	std::string GetPathFromModuleFolder(const std::string& entryPath) {
		size_t componentStart = 0;
		while (componentStart < entryPath.size()) {
			size_t componentEnd = entryPath.find('/', componentStart);
			std::string component = entryPath.substr(componentStart, componentEnd == std::string::npos ? std::string::npos : componentEnd - componentStart);
			if (component.size() > 4 && ToLower(component.substr(component.size() - 4)) == ".rte") {
				return entryPath.substr(componentStart);
			}
			if (componentEnd == std::string::npos) {
				break;
			}
			componentStart = componentEnd + 1;
		}
		return "";
	}
} // namespace

std::string ModIOClient::GetSortOrderName(SortOrder sortOrder) {
	switch (sortOrder) {
		case SortOrder::Popular:
			return "Most Popular";
		case SortOrder::MostDownloaded:
			return "Most Downloaded";
		case SortOrder::RecentlyUpdated:
			return "Recently Updated";
		default:
			return "";
	}
}

std::string ModIOClient::GetGameAPIURL() {
	return g_SettingsMan.GetModIOApiURL() + "/games/" + std::to_string(c_GameID);
}

ModIOModPage ModIOClient::ParseModPage(const std::string& json) {
	ModIOModPage page;
	std::string parseError;
	JsonValue document = JsonValue::Parse(json, &parseError);
	if (!parseError.empty()) {
		page.Error = "mod.io sent a response that couldn't be read (" + parseError + ")";
		return page;
	}
	if (!document["error"].IsNull()) {
		page.Error = document["error"]["message"].AsString("mod.io sent an error");
		return page;
	}
	const JsonValue& mods = document["data"];
	for (size_t index = 0; index < mods.Size(); ++index) {
		page.Mods.emplace_back(ParseMod(mods[index]));
	}
	page.Total = document["result_total"].AsInteger(static_cast<long long>(page.Mods.size()));
	return page;
}

ModIOModPage ModIOClient::Search(const std::string& apiKey, const std::string& searchText, SortOrder sortOrder, int pageIndex, int pageSize) {
	static const char* sortParameters[] = {"popular", "-downloads_total", "-date_updated"};
	std::string url = GetGameAPIURL() + "/mods?api_key=" + UrlEncode(apiKey) + "&_limit=" + std::to_string(pageSize) + "&_offset=" + std::to_string(pageIndex * pageSize) + "&_sort=" + sortParameters[static_cast<int>(sortOrder)];
	if (!searchText.empty()) {
		url += "&_q=" + UrlEncode(searchText);
	}
	HttpClient::Response response = HttpClient::Get(url);
	if (!response.Succeeded()) {
		ModIOModPage page;
		page.Error = DescribeAPIError(response);
		return page;
	}
	return ParseModPage(response.Body);
}

ModIOMod ModIOClient::GetMod(const std::string& apiKey, long long modID, std::string& error) {
	HttpClient::Response response = HttpClient::Get(GetGameAPIURL() + "/mods/" + std::to_string(modID) + "?api_key=" + UrlEncode(apiKey));
	if (!response.Succeeded()) {
		error = DescribeAPIError(response);
		return ModIOMod();
	}
	return ParseMod(JsonValue::Parse(response.Body));
}

std::vector<long long> ModIOClient::GetDependencyIDs(const std::string& apiKey, long long modID, std::string& error) {
	std::vector<long long> dependencyIDs;
	HttpClient::Response response = HttpClient::Get(GetGameAPIURL() + "/mods/" + std::to_string(modID) + "/dependencies?api_key=" + UrlEncode(apiKey));
	if (!response.Succeeded()) {
		error = DescribeAPIError(response);
		return dependencyIDs;
	}
	const JsonValue document = JsonValue::Parse(response.Body);
	const JsonValue& dependencies = document["data"];
	for (size_t index = 0; index < dependencies.Size(); ++index) {
		dependencyIDs.emplace_back(dependencies[index]["mod_id"].AsInteger());
	}
	return dependencyIDs;
}

bool ModIOClient::Install(const std::string& apiKey, const ModIOMod& mod, std::atomic<size_t>* bytesDownloaded, std::vector<std::string>& installedModules, std::string& error) {
	// Dependencies first, so the mod has what it needs the first time it loads. Only direct dependencies are followed, which is all mods here use in practice.
	if (mod.HasDependencies) {
		for (long long dependencyID: GetDependencyIDs(apiKey, mod.ID, error)) {
			if (GetInstalledFileID(dependencyID) != 0) {
				continue;
			}
			ModIOMod dependency = GetMod(apiKey, dependencyID, error);
			if (!error.empty()) {
				return false;
			}
			if (!IsModuleFolderPresent(dependency) && !InstallSingle(apiKey, dependency, bytesDownloaded, installedModules, error)) {
				error = "Couldn't install " + dependency.Name + ", which " + mod.Name + " needs: " + error;
				return false;
			}
		}
		if (!error.empty()) {
			return false;
		}
	}
	return InstallSingle(apiKey, mod, bytesDownloaded, installedModules, error);
}

bool ModIOClient::InstallSingle(const std::string& apiKey, const ModIOMod& mod, std::atomic<size_t>* bytesDownloaded, std::vector<std::string>& installedModules, std::string& error) {
	if (mod.DownloadURL.empty()) {
		error = mod.Name + " has no file to download.";
		return false;
	}
	std::filesystem::path downloadDirectory = GetModIODataDirectory() / "Downloads";
	std::error_code errorCode;
	std::filesystem::create_directories(downloadDirectory, errorCode);
	const std::string zipPath = (downloadDirectory / (std::to_string(mod.ID) + ".zip")).string();

	// Only hand the API key to mod.io's own servers, never to wherever a mod's download link happens to point.
	auto hostOf = [](const std::string& url) {
		const size_t hostStart = url.find("://") == std::string::npos ? 0 : url.find("://") + 3;
		return ToLower(url.substr(hostStart, url.find_first_of(":/?", hostStart) - hostStart));
	};
	const std::string downloadHost = hostOf(mod.DownloadURL);
	const bool isModIOHost = downloadHost == hostOf(GetGameAPIURL()) || downloadHost.ends_with(".mod.io") || downloadHost.ends_with(".modapi.io");
	std::string url = mod.DownloadURL;
	if (isModIOHost) {
		url += (url.find('?') == std::string::npos ? "?" : "&") + std::string("api_key=") + UrlEncode(apiKey);
	}
	HttpClient::Response response = HttpClient::DownloadToFile(url, zipPath, bytesDownloaded);
	if (!response.Succeeded()) {
		error = DescribeAPIError(response);
		return false;
	}

	std::vector<std::string> modules;
	bool installed = InstallFromZip(zipPath, modules, error);
	std::filesystem::remove(zipPath, errorCode);
	if (installed) {
		RecordInstall(mod.ID, mod.FileID, modules);
		installedModules.insert(installedModules.end(), modules.begin(), modules.end());
	}
	return installed;
}

std::string ModIOClient::GetGameVersionIncompatibility(const std::filesystem::path& moduleDirectory, const std::string& moduleName) {
	std::ifstream indexFile(moduleDirectory / "Index.ini");
	if (!indexFile) {
		return moduleName + " has no Index.ini, so the game can't load it.";
	}
	// Find the SupportedGameVersion the same way the game's reader would, skipping commented out lines.
	std::unique_ptr<version::Semver200_version> supportedGameVersion;
	std::string line;
	while (!supportedGameVersion && std::getline(indexFile, line)) {
		line = line.substr(0, line.find("//"));
		const size_t equalsPosition = line.find('=');
		if (equalsPosition == std::string::npos) {
			continue;
		}
		auto trim = [](std::string text) {
			const size_t start = text.find_first_not_of(" \t\r");
			const size_t end = text.find_last_not_of(" \t\r");
			return start == std::string::npos ? std::string() : text.substr(start, end - start + 1);
		};
		if (trim(line.substr(0, equalsPosition)) == "SupportedGameVersion") {
			try {
				supportedGameVersion = std::make_unique<version::Semver200_version>(trim(line.substr(equalsPosition + 1)));
			} catch (const version::Parse_error&) {
				return moduleName + " has a SupportedGameVersion the game can't read, so it can't load it.";
			}
		}
	}
	return DataModule::GetGameVersionIncompatibility(moduleName, supportedGameVersion.get());
}

bool ModIOClient::InstallFromZip(const std::string& zipPath, std::vector<std::string>& installedModules, std::string& error) {
	unzFile zipFile = unzOpen(zipPath.c_str());
	if (!zipFile) {
		error = "The downloaded file isn't a valid zip file.";
		return false;
	}

	// Extract everything to a staging folder first, so a failure halfway through doesn't leave a broken mod behind.
	const std::filesystem::path stagingDirectory = GetModIODataDirectory() / "Staging";
	std::error_code errorCode;
	std::filesystem::remove_all(stagingDirectory, errorCode);
	std::filesystem::create_directories(stagingDirectory, errorCode);

	std::vector<std::string> modules;
	long long bytesExtracted = 0;
	std::vector<char> buffer(64 * 1024);
	bool succeeded = true;
	for (int result = unzGoToFirstFile(zipFile); result == UNZ_OK && succeeded; result = unzGoToNextFile(zipFile)) {
		unz_file_info fileInfo;
		char entryName[1024] = {};
		if (unzGetCurrentFileInfo(zipFile, &fileInfo, entryName, sizeof(entryName) - 1, nullptr, 0, nullptr, 0) != UNZ_OK) {
			error = "The zip file is damaged.";
			succeeded = false;
			break;
		}
		std::string entryPath = entryName;
		std::replace(entryPath.begin(), entryPath.end(), '\\', '/');

		// Never write anywhere but inside the module folder.
		if (entryPath.empty() || entryPath.front() == '/' || entryPath.find(':') != std::string::npos || entryPath.find("..") != std::string::npos) {
			error = "The zip file contains an unsafe path (" + entryPath + "), so it wasn't installed.";
			succeeded = false;
			break;
		}
		std::string pathInModule = GetPathFromModuleFolder(entryPath);
		if (pathInModule.empty()) {
			// Readmes and the like next to the module folder.
			continue;
		}
		std::string moduleName = pathInModule.substr(0, pathInModule.find('/'));
		if (g_PresetMan.IsModuleOfficial(moduleName)) {
			error = "The mod tries to replace the game's own " + moduleName + ", so it wasn't installed.";
			succeeded = false;
			break;
		}
		if (std::find(modules.begin(), modules.end(), moduleName) == modules.end()) {
			modules.emplace_back(moduleName);
		}

		const std::filesystem::path targetPath = stagingDirectory / std::filesystem::path(pathInModule);
		if (pathInModule.back() == '/') {
			std::filesystem::create_directories(targetPath, errorCode);
			continue;
		}
		bytesExtracted += static_cast<long long>(fileInfo.uncompressed_size);
		if (bytesExtracted > c_MaxUncompressedBytes) {
			error = "The mod is far too big once extracted, so it wasn't installed.";
			succeeded = false;
			break;
		}
		std::filesystem::create_directories(targetPath.parent_path(), errorCode);
		if (unzOpenCurrentFile(zipFile) != UNZ_OK) {
			error = "The zip file is damaged.";
			succeeded = false;
			break;
		}
		std::ofstream outputFile(targetPath, std::ios::binary | std::ios::trunc);
		int bytesRead = 0;
		while ((bytesRead = unzReadCurrentFile(zipFile, buffer.data(), static_cast<unsigned int>(buffer.size()))) > 0) {
			outputFile.write(buffer.data(), bytesRead);
		}
		unzCloseCurrentFile(zipFile);
		if (bytesRead < 0 || !outputFile) {
			error = "Couldn't extract " + pathInModule + ".";
			succeeded = false;
		}
	}
	unzClose(zipFile);

	if (succeeded && modules.empty()) {
		error = "The mod doesn't contain a .rte module folder, so it can't be installed.";
		succeeded = false;
	}
	// The game refuses to start with a mod made for another version of it, so don't install one.
	for (size_t index = 0; succeeded && index < modules.size(); ++index) {
		std::string incompatibility = GetGameVersionIncompatibility(stagingDirectory / std::filesystem::path(modules[index]), modules[index]);
		if (!incompatibility.empty()) {
			error = incompatibility + " It wasn't installed, because the game wouldn't start with it.";
			succeeded = false;
		}
	}
	if (succeeded) {
		// Swap each module in, replacing any older version of it.
		const std::filesystem::path modsDirectory(System::GetModDirectory());
		std::filesystem::create_directories(modsDirectory, errorCode);
		for (const std::string& moduleName: modules) {
			const std::filesystem::path installedPath = modsDirectory / std::filesystem::path(moduleName);
			std::filesystem::remove_all(installedPath, errorCode);
			std::filesystem::rename(stagingDirectory / std::filesystem::path(moduleName), installedPath, errorCode);
			if (errorCode) {
				error = "Couldn't move " + moduleName + " into the mods folder (" + errorCode.message() + ").";
				succeeded = false;
				break;
			}
			installedModules.emplace_back(moduleName);
		}
	}
	std::filesystem::remove_all(stagingDirectory, errorCode);
	return succeeded;
}

void ModIOClient::RecordInstall(long long modID, long long fileID, const std::vector<std::string>& modules) {
	const std::filesystem::path recordPath = GetModIODataDirectory() / "Installed.txt";
	std::vector<std::string> lines;
	{
		std::ifstream recordFile(recordPath);
		std::string line;
		while (std::getline(recordFile, line)) {
			if (line.rfind(std::to_string(modID) + "|", 0) != 0) {
				lines.emplace_back(line);
			}
		}
	}
	std::string moduleList;
	for (const std::string& module: modules) {
		moduleList += (moduleList.empty() ? "" : ",") + module;
	}
	lines.emplace_back(std::to_string(modID) + "|" + std::to_string(fileID) + "|" + moduleList);

	std::error_code errorCode;
	std::filesystem::create_directories(recordPath.parent_path(), errorCode);
	std::ofstream recordFile(recordPath, std::ios::trunc);
	for (const std::string& line: lines) {
		recordFile << line << '\n';
	}
}

long long ModIOClient::GetInstalledFileID(long long modID) {
	std::ifstream recordFile(GetModIODataDirectory() / "Installed.txt");
	std::string line;
	const std::string prefix = std::to_string(modID) + "|";
	while (std::getline(recordFile, line)) {
		if (line.rfind(prefix, 0) == 0) {
			std::istringstream fields(line.substr(prefix.size()));
			long long fileID = 0;
			fields >> fileID;
			// A mod whose folders were deleted by hand isn't installed anymore, whatever the record says.
			std::string moduleList;
			if (fields.get() == '|' && std::getline(fields, moduleList)) {
				std::istringstream modules(moduleList);
				std::string module;
				while (std::getline(modules, module, ',')) {
					std::error_code errorCode;
					if (!module.empty() && !std::filesystem::is_directory(std::filesystem::path(System::GetModDirectory()) / module, errorCode)) {
						return 0;
					}
				}
			}
			return fileID;
		}
	}
	return 0;
}

bool ModIOClient::IsModuleFolderPresent(const ModIOMod& mod) {
	std::string moduleName = ToLower(mod.FileName);
	if (moduleName.size() > 4 && moduleName.substr(moduleName.size() - 4) == ".zip") {
		moduleName.resize(moduleName.size() - 4);
	}
	if (moduleName.size() <= 4 || moduleName.substr(moduleName.size() - 4) != ".rte") {
		return false;
	}
	std::error_code errorCode;
	for (const std::filesystem::directory_entry& entry: std::filesystem::directory_iterator(System::GetModDirectory(), errorCode)) {
		if (entry.is_directory() && ToLower(entry.path().filename().string()) == moduleName) {
			return true;
		}
	}
	return false;
}
