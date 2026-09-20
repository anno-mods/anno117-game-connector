#include "statistics_server.h"

#include <chrono>
#include <iostream>
#include <system_error>
#include <utility>

namespace anno_server
{

nlohmann::json BuildStatisticsPayload(const int sessionID, const int islandID, const int areaIndex, const std::int32_t sessionGUID,
	const std::string& areaName, const std::int64_t timeStamp, const std::vector<anno_pipe::ProductionEntryData>& entries)
{
	nlohmann::json entriesJson = nlohmann::json::array();
	for (const auto& entry : entries)
	{
		nlohmann::json workforceJson = nlohmann::json::object();
		for (const auto& [guid, amount] : entry.WorkforceGUIDtoAmount)
		{
			workforceJson[std::to_string(guid)] = amount;
		}

		nlohmann::json buildingsByGuidJson = nlohmann::json::object();
		for (const auto& [guid, amount] : entry.BuildingGUIDtoAmount)
		{
			buildingsByGuidJson[std::to_string(guid)] = amount;
		}

		entriesJson.push_back({
			{ "productGuid", entry.ProductGuid },
			{ "generation", entry.ProductGeneration },
			{ "consumption", entry.ProductConsumption },
			{ "delta", entry.ProductDelta },
			{ "perfectGeneration", entry.PerfectProductGeneration },
			{ "perfectConsumption", entry.PerfectProductConsumption },
			{ "buildings", entry.AmountOfBuildings },
			{ "totalMaintenance", entry.TotalMaintenance },
			{ "totalIncome", entry.TotalIncome },
			{ "totalProfit", entry.TotalProfit },
			{ "summedProductivity", entry.SummedProductivity },
			{ "averageProductivity", entry.AverageProductivity },
			{ "workforce", std::move(workforceJson) },
			{ "buildingsByGuid", std::move(buildingsByGuidJson) },
		});
	}

	return {
		{ "version", StatisticsSchemaVersion },
		{ "areaName", areaName },
		{ "timeStamp", timeStamp },
		{ "sessionId", sessionID },
		{ "sessionGuid", sessionGUID },
		{ "islandId", islandID },
		{ "areaIndex", areaIndex },
		{ "entries", std::move(entriesJson) },
	};
}

//////////////////////////////////////////////////////////////////////////

StatisticsServer::StatisticsServer(std::string host, const int port, std::filesystem::path staticRoot)
	: host_(std::move(host))
	, port_(port)
{
	if (!staticRoot.empty())
	{
		std::error_code ec;
		if (std::filesystem::is_directory(staticRoot, ec))
		{
			if (server_.set_mount_point("/", staticRoot.string()))
			{
				std::cout << "Serving " << staticRoot.string() << " at http://" << host_ << ":" << port_ << "/\n";
			}
			else
			{
				std::cout << "Could not mount static files from " << staticRoot.string() << "\n";
			}
		}
		else
		{
			std::cout << "Calculator checkout not found at " << staticRoot.string()
				<< " - only the /statistics feed will be served.\n";
		}
	}

	server_.Get("/statistics", [this](const httplib::Request& req, httplib::Response& res)
	{
		HandleStatistics(req, res);
	});
}

StatisticsServer::~StatisticsServer()
{
	Stop();
}

void StatisticsServer::Start()
{
	if (listenerThread_.joinable())
	{
		return;
	}

	listenerThread_ = std::thread([this]
	{
		std::cout << "Statistics server listening on http://" << host_ << ":" << port_ << "/statistics\n";
		if (!server_.listen(host_, port_))
		{
			std::cout << "Statistics server failed to bind " << host_ << ":" << port_ << " - is another instance already running?\n";
		}
	});
}

void StatisticsServer::Stop()
{
	DisconnectAll();
	server_.stop();
	if (listenerThread_.joinable())
	{
		listenerThread_.join();
	}
}

void StatisticsServer::Broadcast(const int islandId, const int areaIndex, const nlohmann::json& payload)
{
	{
		std::lock_guard lock{ cacheMutex_ };
		lastSnapshots_[{ islandId, areaIndex }] = payload;
	}

	std::vector<std::shared_ptr<Connection>> snapshot;
	{
		std::lock_guard lock{ connectionsMutex_ };
		snapshot = connections_;
	}

	if (snapshot.empty())
	{
		return;
	}

	const std::string message = "data: " + payload.dump() + "\n\n";

	for (const auto& connection : snapshot)
	{
		{
			std::lock_guard lock{ connection->mutex };
			connection->queue.push_back(message);

			// Bound the backlog per connection: a slow/stalled client should drop old snapshots
			// rather than let the queue (and memory) grow without limit.
			constexpr std::size_t maxQueuedMessages{ 4 };
			while (connection->queue.size() > maxQueuedMessages)
			{
				connection->queue.pop_front();
			}
		}
		connection->cv.notify_all();
	}
}

void StatisticsServer::DisconnectAll()
{
	std::vector<std::shared_ptr<Connection>> snapshot;
	{
		std::lock_guard lock{ connectionsMutex_ };
		snapshot = connections_;
	}

	for (const auto& connection : snapshot)
	{
		{
			std::lock_guard lock{ connection->mutex };
			connection->closed = true;
		}
		connection->cv.notify_all();
	}
}

void StatisticsServer::ClearCache()
{
	std::lock_guard lock{ cacheMutex_ };
	lastSnapshots_.clear();
}

void StatisticsServer::HandleStatistics(const httplib::Request&, httplib::Response& res)
{
	auto connection = std::make_shared<Connection>();
	{
		std::lock_guard lock{ connectionsMutex_ };
		connections_.push_back(connection);
	}

	// Catch the new client up on the current state of every known island/area before it sees any
	// live broadcast, so it doesn't render blank until the next pipe tick (R1/R2 in
	// docs/plans/2026-08-11-001-feat-island-stats-replay-cache-plan.md).
	{
		std::lock_guard lock{ cacheMutex_ };
		std::lock_guard connectionLock{ connection->mutex };
		for (const auto& [key, payload] : lastSnapshots_)
		{
			connection->queue.push_back("data: " + payload.dump() + "\n\n");
		}
	}

	// Loopback-only, read-only, no cookies/credentials involved - permissive CORS lets the page be
	// opened directly (file://) instead of only when served from this same process (survivor #5 /
	// R11-R12 in docs/live-statistics-server-handoff.md).
	res.set_header("Access-Control-Allow-Origin", "*");
	res.set_header("Cache-Control", "no-cache");

	res.set_chunked_content_provider(
		"text/event-stream",
		[connection](std::size_t /*offset*/, httplib::DataSink& sink)
		{
			std::unique_lock lock{ connection->mutex };
			// Wakes on new data or on close; the timeout is just a periodic liveness check so a
			// connection that outlives every future Broadcast()/DisconnectAll() call still notices
			// the client going away via is_writable() below instead of blocking forever.
			connection->cv.wait_for(lock, std::chrono::seconds(15),
				[&connection] { return connection->closed || !connection->queue.empty(); });

			if (connection->closed)
			{
				lock.unlock();
				sink.done();
				return false;
			}

			while (!connection->queue.empty())
			{
				const std::string message = std::move(connection->queue.front());
				connection->queue.pop_front();
				lock.unlock();

				if (!sink.is_writable() || !sink.write(message.data(), message.size()))
				{
					return false;
				}

				lock.lock();
			}

			return sink.is_writable();
		},
		[this, connection](bool /*success*/)
		{
			RemoveConnection(connection);
		});
}

void StatisticsServer::RemoveConnection(const std::shared_ptr<Connection>& connection)
{
	std::lock_guard lock{ connectionsMutex_ };
	std::erase(connections_, connection);
}

}
