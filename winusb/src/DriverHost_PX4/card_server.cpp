#include "card_server.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include "card_command.hpp"

namespace px4 {

CardServer::CardServer(DeviceManager &device_manager,
			       ReceiverManager &receiver_manager) noexcept
	: ServerBase(L"px4_card_pipe", receiver_manager),
	device_manager_(device_manager),
	cards_mutex_()
{
	pipe_config_.in_buffer_size = sizeof(card_command::Command);
	pipe_config_.out_buffer_size = sizeof(card_command::Command);
	pipe_config_.stream_pipe = false;
	pipe_config_.stream_read = false;
	pipe_config_.default_timeout = 5000;
}

ServerBase::Connection* CardServer::CreateConnection(std::unique_ptr<PipeServer> &pipe)
{
	return new CardConnection(*this, pipe);
}

std::shared_ptr<CardServer::SharedCard> CardServer::GetCard(
	const std::wstring &reader_name, int &error)
{
	std::lock_guard<std::mutex> lock(cards_mutex_);
	auto device = device_manager_.FindCardReader(reader_name);
	if (!device) {
		error = -ENOENT;
		return nullptr;
	}

	auto existing = cards_.find(reader_name);
	if (existing != cards_.end()) {
		auto existing_card = existing->second.lock();
		/* 同名デバイスの再接続時は取り外し前の USB ハンドルを再利用しない */
		if (existing_card && existing_card->device == device) {
			error = 0;
			return existing_card;
		}
		cards_.erase(existing);
	}

	auto card = std::make_shared<SharedCard>(std::move(device));
	error = card->card.Open();
	if (error)
		return nullptr;

	cards_.emplace(reader_name, card);
	return card;
}

std::shared_ptr<CardServer::SharedCard> CardServer::AcquireCard(
	const std::wstring &reader_name, card_command::ShareMode share_mode,
	int &error)
{
	auto card = GetCard(reader_name, error);
	if (!card)
		return nullptr;

	std::lock_guard<std::mutex> lock(cards_mutex_);
	if (share_mode == card_command::ShareMode::EXCLUSIVE) {
		if (card->exclusive_connection || card->shared_connections) {
			error = -EBUSY;
			return nullptr;
		}
		card->exclusive_connection = true;
	} else if (share_mode == card_command::ShareMode::SHARED) {
		if (card->exclusive_connection) {
			error = -EBUSY;
			return nullptr;
		}
		card->shared_connections++;
	} else {
		error = -EINVAL;
		return nullptr;
	}

	error = 0;
	return card;
}

void CardServer::ReleaseCard(const std::shared_ptr<SharedCard> &card,
	card_command::ShareMode share_mode) noexcept
{
	std::lock_guard<std::mutex> lock(cards_mutex_);
	if (share_mode == card_command::ShareMode::EXCLUSIVE)
		card->exclusive_connection = false;
	else if (share_mode == card_command::ShareMode::SHARED && card->shared_connections)
		card->shared_connections--;
}

int CardServer::ChangeShareMode(const std::shared_ptr<SharedCard> &card,
	card_command::ShareMode current_mode,
	card_command::ShareMode requested_mode) noexcept
{
	if (current_mode == requested_mode)
		return 0;
	if (requested_mode != card_command::ShareMode::SHARED &&
		requested_mode != card_command::ShareMode::EXCLUSIVE)
		return -EINVAL;

	std::lock_guard<std::mutex> lock(cards_mutex_);
	if (requested_mode == card_command::ShareMode::EXCLUSIVE) {
		/* 自分自身以外の共有接続が残っている間は排他へ変更できない */
		if (card->exclusive_connection || card->shared_connections != 1)
			return -EBUSY;
		card->shared_connections = 0;
		card->exclusive_connection = true;
	} else {
		if (!card->exclusive_connection)
			return -EINVAL;
		card->exclusive_connection = false;
		card->shared_connections = 1;
	}
	return 0;
}

CardServer::CardConnection::CardConnection(CardServer &parent,
					   std::unique_ptr<PipeServer> &pipe) noexcept
	: Connection(parent, pipe),
	card_parent_(parent),
	share_mode_(card_command::ShareMode::UNDEFINED),
	transaction_(false)
{
}

void CardServer::CardConnection::ReleaseCard() noexcept
{
	if (!card_)
		return;
	if (transaction_) {
		card_->io_mutex.unlock();
		transaction_ = false;
	}
	card_parent_.ReleaseCard(card_, share_mode_);
	card_.reset();
	share_mode_ = card_command::ShareMode::UNDEFINED;
}

void CardServer::CardConnection::SetError(card_command::Command &command,
					  int error) const noexcept
{
	command.status = card_command::Status::FAILED;

	switch (error) {
	case -EINVAL:
	case -ENAMETOOLONG:
		command.error = card_command::Error::INVALID_PARAMETER;
		break;
	case -EBUSY:
	case -EALREADY:
		command.error = card_command::Error::BUSY;
		break;
	case SMART_CARD_NO_MEDIUM:
		command.error = card_command::Error::NO_CARD;
		break;
	case -ENODEV:
	case -ENXIO:
		command.error = card_command::Error::REMOVED;
		break;
	case -ETIMEDOUT:
		command.error = card_command::Error::TIMEOUT;
		break;
	case -ENOBUFS:
	case -EMSGSIZE:
		command.error = card_command::Error::INSUFFICIENT_BUFFER;
		break;
	case -EPROTO:
	case -EBADMSG:
		command.error = card_command::Error::PROTOCOL;
		break;
	default:
		command.error = card_command::Error::INTERNAL;
		break;
	}
}

void CardServer::CardConnection::Worker() noexcept
{
	card_command::Command command = {};

	while (true) {
		std::size_t read = 0;
		if (!conn_->Read(&command, sizeof(command), read, quit_event_))
			break;
		if (read != sizeof(command))
			break;

		command.status = card_command::Status::SUCCEEDED;
		command.error = card_command::Error::NONE;

		try {
			switch (command.code) {
			case card_command::Code::GET_VERSION:
				command.version = card_command::VERSION;
				break;

			case card_command::Code::LIST_READERS:
			{
				auto readers = card_parent_.device_manager_.ListCardReaders();
				/* 固定長 IPC へコピーする前に全件を検査し、切り詰めた別名を公開しない */
				bool has_invalid_name = std::any_of(readers.begin(), readers.end(),
					[](const std::wstring &reader) {
						return reader.size() >= card_command::MAX_READER_NAME;
					});
				if (has_invalid_name) {
					SetError(command, -ENAMETOOLONG);
					break;
				}
				command.reader_count = static_cast<std::uint32_t>(
					std::min<std::size_t>(readers.size(), card_command::MAX_READERS));
				command.reader_generation =
					card_parent_.device_manager_.GetCardReaderGeneration();
				for (std::size_t index = 0; index < command.reader_count; index++)
					wcscpy_s(command.readers[index], readers[index].c_str());
				break;
			}

			case card_command::Code::CONNECT:
			{
				if (!wmemchr(command.reader_name, L'\0',
					card_command::MAX_READER_NAME)) {
					SetError(command, -EINVAL);
					break;
				}
				int ret = 0;
				auto share_mode = static_cast<card_command::ShareMode>(command.share_mode);
				if (share_mode != card_command::ShareMode::SHARED &&
					share_mode != card_command::ShareMode::EXCLUSIVE) {
					SetError(command, -EINVAL);
					break;
				}
				ReleaseCard();
				auto card = card_parent_.AcquireCard(command.reader_name, share_mode, ret);
				if (!card) {
					command.status = card_command::Status::FAILED;
					if (ret == -ENOENT)
						command.error = card_command::Error::NO_READER;
					else
						SetError(command, ret);
					break;
				}
				card_ = std::move(card);
				share_mode_ = share_mode;
				break;
			}

			case card_command::Code::RECONNECT:
			{
				if (!card_) {
					SetError(command, -ENODEV);
					break;
				}
				auto requested_mode =
					static_cast<card_command::ShareMode>(command.share_mode);
				int ret = card_parent_.ChangeShareMode(card_, share_mode_, requested_mode);
				if (ret) {
					SetError(command, ret);
					break;
				}
				share_mode_ = requested_mode;
				break;
			}

			case card_command::Code::DISCONNECT:
				ReleaseCard();
				break;

			case card_command::Code::STATUS:
			{
				if (!wmemchr(command.reader_name, L'\0',
					card_command::MAX_READER_NAME)) {
					SetError(command, -EINVAL);
					break;
				}
				std::vector<std::uint8_t> atr;
				int ret;
				std::shared_ptr<SharedCard> status_card;
				if (card_ && card_->device->GetCardReaderName() == command.reader_name)
					status_card = card_;
				if (!status_card) {
					auto monitored = monitored_cards_.find(command.reader_name);
					if (monitored != monitored_cards_.end()) {
						/* USB 再接続後は同じ名前でも新しいデバイスを監視する */
						auto current_device = card_parent_.device_manager_.FindCardReader(
							command.reader_name);
						if (current_device && monitored->second->device == current_device)
							status_card = monitored->second;
						else
							monitored_cards_.erase(monitored);
					}
				}
				if (!status_card) {
					status_card = card_parent_.GetCard(command.reader_name, ret);
					if (!status_card) {
						command.status = card_command::Status::FAILED;
						if (ret == -ENOENT)
							command.error = card_command::Error::NO_READER;
						else
							SetError(command, ret);
						break;
					}
					/* 監視用パイプが生きている間だけカード状態と ATR を保持する */
					monitored_cards_.emplace(command.reader_name, status_card);
				}
				std::lock_guard<std::recursive_mutex> lock(status_card->io_mutex);
				ret = status_card->card.GetStatus(command.card_present,
					command.card_initialized, atr);
				if (ret) {
					SetError(command, ret);
					break;
				}
				command.atr_length = static_cast<std::uint32_t>(atr.size());
				if (!atr.empty())
					memcpy(command.atr, atr.data(), atr.size());
				break;
			}

			case card_command::Code::RESET:
			{
				if (!card_) {
					SetError(command, -ENODEV);
					break;
				}
				std::vector<std::uint8_t> atr;
				std::lock_guard<std::recursive_mutex> lock(card_->io_mutex);
				int ret = card_->card.Reset(atr);
				if (ret) {
					SetError(command, ret);
					break;
				}
				command.atr_length = static_cast<std::uint32_t>(atr.size());
				if (!atr.empty())
					memcpy(command.atr, atr.data(), atr.size());
				command.card_present = true;
				command.card_initialized = true;
				break;
			}

			case card_command::Code::TRANSMIT:
			{
				if (!card_ || command.data_length > card_command::MAX_DATA_SIZE ||
					command.data_capacity > card_command::MAX_DATA_SIZE) {
					SetError(command, -EINVAL);
					break;
				}
				/* 必要サイズを返せるよう、共有コマンド領域の全容量で受信する */
				std::size_t recv_length = card_command::MAX_DATA_SIZE;
				std::lock_guard<std::recursive_mutex> lock(card_->io_mutex);
				int ret = card_->card.Transmit(command.data, command.data_length,
					command.data, recv_length);
				command.data_length = static_cast<std::uint32_t>(recv_length);
				if (!ret && recv_length > command.data_capacity)
					ret = -ENOBUFS;
				if (ret)
					SetError(command, ret);
				break;
			}

			case card_command::Code::BEGIN_TRANSACTION:
				if (!card_ || transaction_) {
					SetError(command, -EINVAL);
					break;
				}
				card_->io_mutex.lock();
				transaction_ = true;
				break;

			case card_command::Code::END_TRANSACTION:
				if (!card_ || !transaction_) {
					SetError(command, -EINVAL);
					break;
				}
				{
					auto disposition = static_cast<card_command::Disposition>(
						command.disposition);
					int ret = 0;
					if (disposition != card_command::Disposition::LEAVE &&
						disposition != card_command::Disposition::RESET) {
						ret = -EINVAL;
					} else if (disposition == card_command::Disposition::RESET) {
						try {
							std::vector<std::uint8_t> atr;
							ret = card_->card.Reset(atr);
							if (!ret) {
								command.atr_length = static_cast<std::uint32_t>(atr.size());
								if (!atr.empty())
									memcpy(command.atr, atr.data(), atr.size());
							}
						} catch (...) {
							ret = -EIO;
						}
					}
					/* disposition の成否にかかわらず、終了要求で排他状態を解除する */
					card_->io_mutex.unlock();
					transaction_ = false;
					if (ret)
						SetError(command, ret);
				}
				break;

			default:
				SetError(command, -EINVAL);
				break;
			}
		} catch (...) {
			SetError(command, -EIO);
		}

		std::size_t written = 0;
		if (!conn_->Write(&command, sizeof(command), written) || written != sizeof(command))
			break;
	}

	ReleaseCard();
	delete this;
}

} // namespace px4
