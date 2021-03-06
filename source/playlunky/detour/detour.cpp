#include "detour.h"

#include "detour_entry.h"
#include "sigscan.h"
#include "debug.h"
#include "file_io.h"
#include "logger.h"
#include "win_main.h"

#include <Windows.h>
#include <detours.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <span>

#define DLL_NAME "playlunky" DETOURS_STRINGIFY(DETOURS_BITS) ".dll"

struct ByteStr { std::string_view Str; };
template<>
struct fmt::formatter<ByteStr> {
	constexpr auto parse(format_parse_context& ctx) {
		return ctx.begin();
	}

	template <typename FormatContext>
	auto format(ByteStr byte_str, FormatContext& ctx) {
		auto out = ctx.out();

		const std::size_t num_bytes = byte_str.Str.size();
		if (num_bytes > 0) {
			const uint8_t first_byte = byte_str.Str[0];
			if (first_byte == '*') {
				out = format_to(out, "??");
			}
			else {
				out = format_to(out, "{:02x}", first_byte);
			}

			std::span<uint8_t> remainder_byte_span{ (uint8_t*)byte_str.Str.data() + 1, num_bytes - 1 };
			for (uint8_t c : remainder_byte_span) {
				if (c == '*') {
					out = format_to(out, " ??");
				}
				else {
					out = format_to(out, " {:02x}", c);
				}
			}
		}

		return out;
	}
};

std::vector<DetourEntry> CollectDetourEntries() {
	auto append = [](std::vector<DetourEntry>& dst, std::vector<DetourEntry> src) {
		std::move(src.begin(), src.end(), std::back_inserter(dst));
	};

	std::vector<DetourEntry> detour_entries;
	append(detour_entries, GetLogDetours());
	append(detour_entries, GetFileIODetours());
	append(detour_entries, GetMainDetours());

	if (IsDebuggerPresent()) {
		append(detour_entries, GetDebugDetours());
	}

	return detour_entries;
}

void Attach() {
	fmt::print(DLL_NAME ": Attaching...\n");

	std::vector<DetourEntry> detour_entries = CollectDetourEntries();

	DetourRestoreAfterWith();

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	for (auto [trampoline, detour, signature, function_name] : detour_entries) {
		if (signature != nullptr) {
			*trampoline = SigScan::FindPattern(*signature, true);
			if (*trampoline != nullptr)
			{
				fmt::print("Found function {}:\n\tsig: {}\n\t at: {}\n     offset: 0x{:x}\n", function_name, ByteStr{ .Str = *signature }, *trampoline, SigScan::GetOffset(*trampoline));
			}
		}

		if (*trampoline == nullptr) {
			fmt::print("Can not detour function {}, no valid source function specified\n", function_name);
		}
		else {
			DetourAttach(trampoline, detour);
		}
	}

	const LONG error = DetourTransactionCommit();
	if (error == NO_ERROR) {
		fmt::print(DLL_NAME ": Succees...\n");
	}
	else {
		fmt::print(DLL_NAME ": Error: {}\n", error);
	}
	std::fflush(stdout);
}

void Detach() {
	fmt::print(DLL_NAME ": Detaching...\n");

	std::vector<DetourEntry> detour_entries = CollectDetourEntries();

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	for (auto [trampoline, detour, signature, function_name] : detour_entries) {
		DetourDetach(trampoline, detour);
	}

	const LONG error = DetourTransactionCommit();
	if (error == NO_ERROR) {
		fmt::print(DLL_NAME ": Succees...\n");
	}
	else {
		fmt::print(DLL_NAME ": Error: {}\n", error);
	}
	std::fflush(stdout);
}
