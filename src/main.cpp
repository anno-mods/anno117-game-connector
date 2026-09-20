// cpp-httplib (pulled in transitively via statistics_server.h) needs <winsock2.h>, which conflicts
// with the legacy <winsock.h> that <Windows.h> pulls in by default - WIN32_LEAN_AND_MEAN keeps
// Windows.h from doing that.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>

#include "imgui_impl.h"

#include "pipe.h"
#include "statistics_server.h"

namespace
{

	// The calculator is vendored as a git submodule at external/anno-117-calculator (see
	// .gitmodules) - ANNO117_CALCULATOR_DIR is baked in by CMakeLists.txt as an absolute,
	// source-tree-relative path, since this is a build-from-source example app rather than
	// something installed away from its source. Override with --calculator-dir for any other layout.
	std::filesystem::path DefaultCalculatorDir()
	{
		return ANNO117_CALCULATOR_DIR;
	}

	std::filesystem::path ResolveCalculatorDir(const int argc, char** const argv)
	{
		constexpr std::string_view flagWithEquals{ "--calculator-dir=" };
		for (int i = 1; i < argc; ++i)
		{
			const std::string_view arg{ argv[i] };
			if (arg.starts_with(flagWithEquals))
			{
				return std::filesystem::path(arg.substr(flagWithEquals.size()));
			}
			if (arg == "--calculator-dir" && i + 1 < argc)
			{
				return std::filesystem::path(argv[++i]);
			}
		}
		return DefaultCalculatorDir();
	}

	// `--replay-file <path>`/`--replay-file=<path>` points at a JSONL file of statistics-feed
	// payloads (one per line, schema matching anno_server::BuildStatisticsPayload's output - see
	// docs/test-replay-data.jsonl for a fixture derived from docs/example_responses.txt). When set,
	// each line is broadcast once at startup so the replay cache is primed and a client connecting
	// to /statistics is caught up immediately, without needing the game running - see
	// docs/plans/2026-08-11-001-feat-island-stats-replay-cache-plan.md.
	std::optional<std::filesystem::path> ResolveReplayFile(const int argc, char** const argv)
	{
		constexpr std::string_view flagWithEquals{ "--replay-file=" };
		for (int i = 1; i < argc; ++i)
		{
			const std::string_view arg{ argv[i] };
			if (arg.starts_with(flagWithEquals))
			{
				return std::filesystem::path(arg.substr(flagWithEquals.size()));
			}
			if (arg == "--replay-file" && i + 1 < argc)
			{
				return std::filesystem::path(argv[++i]);
			}
		}
		return std::nullopt;
	}

	void BroadcastReplayFile(const std::filesystem::path& path, anno_server::StatisticsServer& statisticsServer)
	{
		std::ifstream file{ path };
		if (!file)
		{
			std::cout << "Could not open replay file " << path.string() << "\n";
			return;
		}

		std::size_t broadcastCount{ 0 };
		std::string line;
		while (std::getline(file, line))
		{
			if (line.find_first_not_of(" \t\r\n") == std::string::npos)
			{
				continue;
			}

			try
			{
				const auto payload = nlohmann::json::parse(line);
				statisticsServer.Broadcast(payload.at("islandId").get<int>(), payload.at("areaIndex").get<int>(), payload);
				++broadcastCount;
			}
			catch (const nlohmann::json::exception& e)
			{
				std::cout << "Skipping malformed replay line: " << e.what() << "\n";
			}
		}

		std::cout << "Loaded " << broadcastCount << " island(s) from replay file " << path.string() << "\n";
	}

}

void DrawImGui(std::mutex& productionDataMutex, std::map<std::string, std::map<std::int32_t, std::deque<anno_pipe::ProductionEntryData>>>& productionData, const std::string& headline)
{
	std::lock_guard lock{ productionDataMutex };

	ImGui::SetNextWindowSize({ 1280.f, 800.f }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos({ 0.f, 0.f }, ImGuiCond_FirstUseEver);

	ImGui::Begin("Production");

	ImGui::Text(headline.c_str());
	for (auto& [name, products] : productionData)
	{
		if (ImGui::CollapsingHeader(name.c_str()))
		{
			ImGui::PushID(name.c_str());
			for (auto& [guid, entries] : products)
			{
				ImGui::PushID(guid);
				ImGui::PlotLines((std::to_string(guid) + std::string(" ProductGeneration")).c_str(), [](void* const data, const int index) -> float
					{
						const auto* const q = static_cast<const std::deque<anno_pipe::ProductionEntryData>*>(data);
						return q->at(index).ProductGeneration;
					}, static_cast<void*>(&entries), static_cast<int>(entries.size()), 0, std::to_string(entries.back().ProductGeneration).c_str(), 0.f, FLT_MAX, { 400.f, 30.f });
				ImGui::PopID();
			}
			ImGui::PopID();
		}
	}

	ImGui::End();
}

// Starts a new Dear ImGui frame and renders it - the per-frame counterpart to the one-time
// SetupImgui() call. NewFrame()/Render() must run every frame regardless of what's drawn: the
// main loop's ImGui_ImplDX12_RenderDrawData() call always runs unconditionally afterwards and
// needs valid (if empty) draw data from Render(), or it crashes on GetDrawData(). Only the debug
// window content (DrawImGui - Ubisoft's default raw-data window) is disabled, now that the
// calculator/statistics.html pages are the primary UI.
void RenderImGuiFrame([[maybe_unused]] std::mutex& productionDataMutex, [[maybe_unused]] std::map<std::string, std::map<std::int32_t, std::deque<anno_pipe::ProductionEntryData>>>& productionData, [[maybe_unused]] const std::string& headline)
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// DrawImGui(productionDataMutex, productionData, headline);

	ImGui::Render();
}

int main(int argc, char** argv)
{
	HWND hwnd;
	WNDCLASSEXW wc;
	if (SetupImgui(hwnd, wc) != 0)
	{
		return EXIT_FAILURE;
	}

	// SetupImgui() shows the window (Ubisoft's default behavior); hide it immediately since the
	// calculator/statistics.html pages are the primary UI now. The window/D3D12 device/render loop
	// stay alive regardless - they're still needed to keep the process running and for clean
	// shutdown - they just never become visible.
	::ShowWindow(hwnd, SW_HIDE);

	std::mutex productionDataMutex;
	std::map<std::string, std::map<std::int32_t, std::deque<anno_pipe::ProductionEntryData>>> productionData;
	std::string headline;

	const auto calculatorDir = ResolveCalculatorDir(argc, argv);
	anno_server::StatisticsServer statisticsServer{ anno_server::DefaultHost, anno_server::DefaultPort, calculatorDir };
	statisticsServer.Start();
	std::cout << "Open http://" << anno_server::DefaultHost << ":" << anno_server::DefaultPort
		<< "/statistics.html in a browser for the live statistics page.\n";

	if (const auto replayFile = ResolveReplayFile(argc, argv))
	{
		BroadcastReplayFile(*replayFile, statisticsServer);
	}

	std::jthread pipe_thread{ anno_pipe::RunPipe, std::ref(headline), std::ref(productionData), std::ref(productionDataMutex),
		[&statisticsServer](int sessionID, int islandID, int areaIndex, std::int32_t sessionGUID, const std::string& areaName,
			std::int64_t timeStamp, const std::vector<anno_pipe::ProductionEntryData>& entries)
		{
			statisticsServer.Broadcast(islandID, areaIndex, anno_server::BuildStatisticsPayload(sessionID, islandID, areaIndex, sessionGUID, areaName, timeStamp, entries));
		},
		[&statisticsServer]
		{
			statisticsServer.DisconnectAll();
			statisticsServer.ClearCache();
		},
		[&statisticsServer]
		{
			statisticsServer.ClearCache();
		} };

	bool done = false;
	while (!done)
	{
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
			{
				done = true;
			}
		}
		if (done)
		{
			break;
		}

		if ((g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || ::IsIconic(hwnd))
		{
			::Sleep(10);
			continue;
		}
		g_SwapChainOccluded = false;

		RenderImGuiFrame(productionDataMutex, productionData, headline);

		FrameContext* const frameCtx = WaitForNextFrameResources();
		const UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
		frameCtx->CommandAllocator->Reset();

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
		g_pd3dCommandList->ResourceBarrier(1, &barrier);

		// Render Dear ImGui graphics
		constexpr ImVec4 clear_color(0.35f, 0.43f, 0.53f, 1.00f);
		const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
		g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
		g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
		g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		g_pd3dCommandList->ResourceBarrier(1, &barrier);
		g_pd3dCommandList->Close();

		std::array<ID3D12CommandList*, 1> commands{ g_pd3dCommandList };
		g_pd3dCommandQueue->ExecuteCommandLists(static_cast<UINT>(commands.size()), commands.data());

		// Present
		const HRESULT hr = g_pSwapChain->Present(1, 0);
		g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

		const UINT64 fenceValue = g_fenceLastSignaledValue + 1;
		g_pd3dCommandQueue->Signal(g_fence, fenceValue);
		g_fenceLastSignaledValue = fenceValue;
		frameCtx->FenceValue = fenceValue;
	}

	pipe_thread.request_stop();
	statisticsServer.Stop();

	CleanupImgui(hwnd, wc);

	if (pipe_thread.joinable())
	{
		std::this_thread::sleep_for(std::chrono::seconds{ 1 });
		std::exit(EXIT_SUCCESS);
	}
}