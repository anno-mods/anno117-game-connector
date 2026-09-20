#ifndef STATISTICS_SERVER_H
#define STATISTICS_SERVER_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "pipe.h"

namespace anno_server
{

	// Must match the calculator's `DEFAULT_STATISTICS_FEED_URL` in `statistics-feed.ts`
	// (`http://127.0.0.1:53117/statistics`) - changing it here requires a matching change there.
	constexpr const char* DefaultHost = "127.0.0.1";
	constexpr int DefaultPort = 53117;

	// Versions the SSE JSON payload shape (docs/live-statistics-server-handoff.md §5). Distinct
	// from anno_pipe::PROTOCOL_VERSION, which versions the pipe's own binary wire format - the two
	// evolve independently even though they start out numerically equal.
	constexpr int StatisticsSchemaVersion = 1;

	// Builds one SSE message's JSON payload from a parsed AreaProductionStatistics message, per the
	// confirmed contract in docs/live-statistics-server-handoff.md §5. `sessionID` is forwarded
	// as-is; the calculator's own session/region concept is a different axis (§3.1, still open) -
	// this repo does not attempt to resolve or fake it, the client's resolveSession() already
	// degrades gracefully to "no session shown" for a value it cannot match.
	nlohmann::json BuildStatisticsPayload(int sessionID, int islandID, int areaIndex, std::int32_t sessionGUID,
		const std::string& areaName, std::int64_t timeStamp, const std::vector<anno_pipe::ProductionEntryData>& entries);

	// Embedded local HTTP server for the calculator's `statistics.html` companion page: serves the
	// live production-statistics feed as Server-Sent Events on GET /statistics, and (when a
	// calculator checkout directory is supplied) the calculator's already-built static files
	// alongside it, same-origin, so the page and its live data come from one process/port. Binds to
	// a loopback address only.
	class StatisticsServer
	{
	public:
		// `staticRoot` may be empty or non-existent - the server then serves only /statistics and
		// logs that static file serving is skipped. Nothing from `staticRoot` is copied; it is read
		// from disk on demand for as long as the server runs.
		StatisticsServer(std::string host, int port, std::filesystem::path staticRoot);
		~StatisticsServer();

		StatisticsServer(const StatisticsServer&) = delete;
		StatisticsServer& operator=(const StatisticsServer&) = delete;

		// Starts the background listener thread. No-op if already started.
		void Start();

		// Ends all connected clients' streams and stops listening. Safe to call from the destructor
		// or explicitly; safe to call when never started.
		void Stop();

		// Sends one JSON payload to every currently connected SSE client, and caches it as the
		// latest known snapshot for (islandId, areaIndex) so a client connecting later can be
		// caught up immediately - see HandleStatistics().
		void Broadcast(int islandId, int areaIndex, const nlohmann::json& payload);

		// Ends every currently connected client's stream without stopping the server, so the
		// browser's EventSource fires `onerror` and drives the calculator's Live/Reconnecting/
		// Offline state (R12-R14) from the game's own session lifecycle rather than from silence.
		void DisconnectAll();

		// Drops every cached last-known snapshot. Called on session boundaries (SessionStart,
		// SessionEnd, pipe disconnect) so a client connecting after one of those never gets replayed
		// data from a session that's no longer current.
		void ClearCache();

	private:
		struct Connection
		{
			std::mutex mutex;
			std::condition_variable cv;
			std::deque<std::string> queue;
			bool closed = false;
		};

		void HandleStatistics(const httplib::Request& req, httplib::Response& res);
		void RemoveConnection(const std::shared_ptr<Connection>& connection);

		std::string host_;
		int port_;

		httplib::Server server_;
		std::thread listenerThread_;

		std::mutex connectionsMutex_;
		std::vector<std::shared_ptr<Connection>> connections_;

		std::mutex cacheMutex_;
		std::map<std::pair<int, int>, nlohmann::json> lastSnapshots_;
	};

}

#endif
