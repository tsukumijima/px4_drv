#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include "card_command.hpp"
#include "device_manager.hpp"
#include "server_base.hpp"
#include "smart_card.hpp"

namespace px4 {

class CardServer final : public ServerBase {
public:
	CardServer(DeviceManager &device_manager, ReceiverManager &receiver_manager) noexcept;

private:
	struct SharedCard final {
		explicit SharedCard(std::shared_ptr<DeviceBase> source_device)
			: device(std::move(source_device)), card(device) {}

		std::shared_ptr<DeviceBase> device;
		SmartCard card;
		std::recursive_mutex io_mutex;
		std::size_t shared_connections = 0;
		bool exclusive_connection = false;
	};

	class CardConnection final : public ServerBase::Connection {
	public:
		CardConnection(CardServer &parent, std::unique_ptr<PipeServer> &pipe) noexcept;

	private:
		void Worker() noexcept override;
		void SetError(card_command::Command &command, int error) const noexcept;
		void ReleaseCard() noexcept;

		CardServer &card_parent_;
		std::shared_ptr<SharedCard> card_;
		std::unordered_map<std::wstring, std::shared_ptr<SharedCard>> monitored_cards_;
		card_command::ShareMode share_mode_;
		bool transaction_;
	};

	Connection* CreateConnection(std::unique_ptr<PipeServer> &pipe) override;
	std::shared_ptr<SharedCard> GetCard(const std::wstring &reader_name, int &error);
	std::shared_ptr<SharedCard> AcquireCard(const std::wstring &reader_name,
		card_command::ShareMode share_mode, int &error);
	void ReleaseCard(const std::shared_ptr<SharedCard> &card,
		card_command::ShareMode share_mode) noexcept;
	int ChangeShareMode(const std::shared_ptr<SharedCard> &card,
		card_command::ShareMode current_mode,
		card_command::ShareMode requested_mode) noexcept;

	DeviceManager &device_manager_;
	std::mutex cards_mutex_;
	std::unordered_map<std::wstring, std::weak_ptr<SharedCard>> cards_;
};

} // namespace px4
