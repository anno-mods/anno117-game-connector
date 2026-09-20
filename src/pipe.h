#ifndef PIPE_H
#define PIPE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string_view>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace anno_pipe
{

	constexpr const char* pipeName = R"XXX(\\.\pipe\anno117)XXX";

	constexpr std::int32_t PROTOCOL_VERSION = 2;

	enum class Message : std::uint8_t
	{
		Version,
		SessionStart,
		SessionEnd,
		AreaProductionStatistics,
	};

	[[nodiscard]] std::int32_t ReadInt32(std::size_t& pos, std::span<const unsigned char> data);
	[[nodiscard]] std::int64_t ReadInt64(std::size_t& pos, std::span<const unsigned char> data);
	[[nodiscard]] float ReadFloat(std::size_t& pos, std::span<const unsigned char> data);
	[[nodiscard]] std::string_view ReadString(std::size_t& pos, std::span<const unsigned char> data);

	//////////////////////////////////////////////////////////////////////////

	struct ProductionEntryData
	{
		std::int32_t ProductGuid;

		float ProductGeneration;
		float ProductConsumption;
		float ProductDelta;
		float PerfectProductGeneration;
		float PerfectProductConsumption;
		std::int32_t AmountOfBuildings;
		std::int32_t TotalMaintenance;
		float TotalIncome;
		std::int32_t TotalProfit;
		float SummedProductivity;
		float AverageProductivity;

		std::unordered_map<std::int32_t, std::int32_t> WorkforceGUIDtoAmount;
		std::unordered_map<std::int32_t, std::int32_t> BuildingGUIDtoAmount;

		void read(std::size_t& pos, std::span<const unsigned char> data);
	};

	//////////////////////////////////////////////////////////////////////////

	// Fired once per parsed AreaProductionStatistics message, after productionData/productionDataMutex
	// have been updated for it. Lets a caller (e.g. the local statistics server) mirror the pipe
	// stream without pipe.cpp knowing anything about HTTP/SSE.
	using AreaStatisticsCallback = std::function<void(int sessionID, int islandID, int areaIndex, std::int32_t sessionGUID,
		const std::string& areaName, std::int64_t timeStamp, const std::vector<ProductionEntryData>& entries)>;

	// Fired on SessionEnd and on the pipe breaking/closing - anything that should end a live client's
	// view of "connected".
	using DisconnectCallback = std::function<void()>;

	// Fired on SessionStart, i.e. a new game session beginning. Distinct from DisconnectCallback:
	// unlike SessionEnd/broken-pipe, a SessionStart does not necessarily mean an existing live client
	// should be dropped - it's for callers that need to reset session-scoped state (e.g. a cache) that
	// SessionStart doesn't otherwise signal, since it isn't guaranteed to always follow a SessionEnd.
	using SessionStartCallback = std::function<void()>;

	void RunPipe(std::stop_token stop, std::string& headline, std::map<std::string, std::map<std::int32_t, std::deque<anno_pipe::ProductionEntryData>>>& productionData,
		std::mutex& productionDataMutex, AreaStatisticsCallback onAreaStatistics = {}, DisconnectCallback onDisconnect = {},
		SessionStartCallback onSessionStart = {});

}

#endif