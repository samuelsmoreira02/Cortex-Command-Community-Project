#include "HttpClient.h"

#include <cstdio>
#include <fstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

using namespace RTE;

#ifdef _WIN32
namespace {
	/// Closes a WinHTTP handle when it goes out of scope.
	struct WinHttpHandle {
		HINTERNET Handle = nullptr;
		explicit WinHttpHandle(HINTERNET handle) :
		    Handle(handle) {}
		~WinHttpHandle() {
			if (Handle) {
				WinHttpCloseHandle(Handle);
			}
		}
		WinHttpHandle(const WinHttpHandle&) = delete;
		WinHttpHandle& operator=(const WinHttpHandle&) = delete;
	};

	/// Widens a URL for WinHTTP. URLs are plain ASCII, with anything else percent-encoded, so each character maps directly.
	std::wstring ToWide(const std::string& text) {
		return std::wstring(text.begin(), text.end());
	}

	std::string LastErrorText(const std::string& what) {
		return what + " failed (error " + std::to_string(GetLastError()) + ")";
	}

	/// Makes a GET request, and hands each chunk of the body to a callback as it arrives.
	/// @param onData Called with each chunk of the body. Returning false aborts the request.
	template <typename DataCallback>
	HttpClient::Response Request(const std::string& url, DataCallback onData) {
		HttpClient::Response response;

		std::wstring wideURL = ToWide(url);
		URL_COMPONENTS components{};
		components.dwStructSize = sizeof(components);
		wchar_t hostName[256] = {};
		wchar_t urlPath[4096] = {};
		components.lpszHostName = hostName;
		components.dwHostNameLength = static_cast<DWORD>(std::size(hostName));
		components.lpszUrlPath = urlPath;
		components.dwUrlPathLength = static_cast<DWORD>(std::size(urlPath));
		components.dwExtraInfoLength = 1;
		if (!WinHttpCrackUrl(wideURL.c_str(), 0, 0, &components)) {
			response.Error = "Invalid URL: " + url;
			return response;
		}
		// The query string is part of the path as far as the request is concerned.
		std::wstring pathAndQuery = std::wstring(components.lpszUrlPath, components.dwUrlPathLength);
		if (components.lpszExtraInfo && components.dwExtraInfoLength > 0) {
			pathAndQuery += std::wstring(components.lpszExtraInfo, components.dwExtraInfoLength);
		}

		WinHttpHandle session(WinHttpOpen(L"CortexCommand/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
		if (!session.Handle) {
			response.Error = LastErrorText("Opening an HTTP session");
			return response;
		}
		WinHttpSetTimeouts(session.Handle, 10000, 10000, 15000, 30000);
		WinHttpHandle connection(WinHttpConnect(session.Handle, hostName, components.nPort, 0));
		if (!connection.Handle) {
			response.Error = LastErrorText("Connecting to the server");
			return response;
		}
		DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
		WinHttpHandle request(WinHttpOpenRequest(connection.Handle, L"GET", pathAndQuery.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
		if (!request.Handle) {
			response.Error = LastErrorText("Opening the request");
			return response;
		}
		if (!WinHttpSendRequest(request.Handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.Handle, nullptr)) {
			response.Error = LastErrorText("Sending the request");
			return response;
		}

		DWORD statusCode = 0;
		DWORD statusCodeSize = sizeof(statusCode);
		WinHttpQueryHeaders(request.Handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
		response.StatusCode = static_cast<int>(statusCode);

		std::vector<char> buffer(64 * 1024);
		while (true) {
			DWORD bytesRead = 0;
			if (!WinHttpReadData(request.Handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead)) {
				response.Error = LastErrorText("Reading the response");
				return response;
			}
			if (bytesRead == 0) {
				break;
			}
			if (!onData(buffer.data(), bytesRead)) {
				response.Error = "The download was aborted";
				return response;
			}
		}
		return response;
	}
} // namespace
#endif

bool HttpClient::IsSupported() {
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

HttpClient::Response HttpClient::Get(const std::string& url) {
#ifdef _WIN32
	std::string body;
	Response response = Request(url, [&body](const char* data, size_t size) {
		body.append(data, size);
		return true;
	});
	response.Body = std::move(body);
	return response;
#else
	Response response;
	response.Error = "Web requests aren't supported on this platform yet";
	return response;
#endif
}

HttpClient::Response HttpClient::DownloadToFile(const std::string& url, const std::string& filePath, std::atomic<size_t>* bytesReceived) {
#ifdef _WIN32
	// Download to a temporary file first, so a failed download never leaves a broken file behind.
	const std::string temporaryPath = filePath + ".part";
	Response response;
	{
		std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
		if (!file) {
			response.Error = "Couldn't create " + temporaryPath;
			return response;
		}
		size_t totalReceived = 0;
		response = Request(url, [&](const char* data, size_t size) {
			file.write(data, static_cast<std::streamsize>(size));
			totalReceived += size;
			if (bytesReceived) {
				*bytesReceived = totalReceived;
			}
			return static_cast<bool>(file);
		});
	}
	if (response.Succeeded()) {
		std::remove(filePath.c_str());
		if (std::rename(temporaryPath.c_str(), filePath.c_str()) != 0) {
			response.Error = "Couldn't move the download to " + filePath;
		}
	} else {
		std::remove(temporaryPath.c_str());
	}
	return response;
#else
	Response response;
	response.Error = "Web requests aren't supported on this platform yet";
	return response;
#endif
}
