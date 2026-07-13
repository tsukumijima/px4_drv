#pragma once

#include <memory>

#include "card_command.hpp"
#include "pipe_client.hpp"

namespace px4 {

class CardClient final {
public:
	CardClient() noexcept = default;

	bool Connect() noexcept;
	bool Call(card_command::Command &command) noexcept;

private:
	bool StartDriverHost() noexcept;

	std::unique_ptr<PipeClient> pipe_;
};

} // namespace px4
