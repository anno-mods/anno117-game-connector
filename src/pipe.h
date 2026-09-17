#ifndef PIPE_H
#define PIPE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <span>
#include <string_view>
#include <string>
#include <thread>
#include <unordered_map>

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

	void RunPipe(std::stop_token stop, std::string& headline, std::map<std::string, std::map<std::int32_t, std::deque<anno_pipe::ProductionEntryData>>>& productionData,
		std::mutex& productionDataMutex);

}

#endif