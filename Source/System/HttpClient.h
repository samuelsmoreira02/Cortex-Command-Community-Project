#pragma once

#include <atomic>
#include <string>

namespace RTE {

	/// Blocking HTTP(S) GET requests, for talking to web APIs and downloading files. Meant to be called off the main thread.
	/// Implemented with WinHTTP on Windows. Other platforms don't support it yet, and their requests fail with an error saying so.
	class HttpClient {

	public:
		/// The result of a request.
		struct Response {
			int StatusCode = 0; //!< The HTTP status code, or 0 if the request couldn't be made at all.
			std::string Body; //!< The response body, when it was kept in memory.
			std::string Error; //!< What went wrong, if the request couldn't be made. Empty otherwise, even for error status codes.

			/// Gets whether the request was made and the server answered with success.
			/// @return Whether the status code is 2xx.
			bool Succeeded() const { return Error.empty() && StatusCode >= 200 && StatusCode < 300; }
		};

		/// Gets whether HTTP requests are supported on this platform.
		/// @return Whether requests can be made.
		static bool IsSupported();

		/// Makes a GET request and keeps the response body in memory. Redirects are followed.
		/// @param url The URL to request, http or https.
		/// @return The response.
		static Response Get(const std::string& url);

		/// Makes a GET request and writes the response body to a file, if the request succeeds. Redirects are followed.
		/// @param url The URL to request, http or https.
		/// @param filePath Where to write the body. Written only on success.
		/// @param bytesReceived If not null, updated as the body arrives, for showing progress.
		/// @return The response, without the body.
		static Response DownloadToFile(const std::string& url, const std::string& filePath, std::atomic<size_t>* bytesReceived = nullptr);
	};
} // namespace RTE
