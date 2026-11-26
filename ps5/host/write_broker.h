// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// PSGopher: from OGBox db63d46 (ps5/src/platform/ps5_broker.h, ps5_wbremote.h), without the storage
// write test and the cache drives' write-back; in namespace psgopher.
//
// The console holds a title's writes to its storage back after a burst of about 1.3 GiB (OGBox's
// measurements on the console: the title's own writes 192 MiB/s for 1.3 GiB, then 12 to 32 MiB/s; a
// payload of its own 200 MiB/s past 4 GiB). The write broker (ps5/broker, write-broker.elf in the
// title folder) is such a payload: the title sends it to the payload loader on 127.0.0.1:9021, as it
// sends the sandbox elevation helper, and then speaks ps5/broker/protocol.hpp with it over that
// connection. PSGopher uses it for one thing: the copy of a ROM from a share into the play cache
// (library.cpp's localCopy), at the first copy of a run, only for a destination under /data/ and
// only while the `write_helper` setting is on; when it can't be used or fails the title writes the
// copy itself, as before.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <sys/uio.h>
#include <vector>

#include "../broker/protocol.hpp"

namespace psgopher::broker
{
using ::broker::BenchArgs;
using ::broker::BenchResult;
using ::broker::BenchStep;
using ::broker::Op;
using ::broker::Reply;

/// One connection to the broker. Requests may be sent ahead of their replies (pipelined); the
/// replies are taken in the order the requests went, and one out of that order is a protocol error.
/// The calls return 0, the broker's errno, or -1 when the connection failed (`Error()` says how).
class Client
{
public:
	explicit Client(int fd = -1) : m_fd(fd) {}
	~Client();
	Client(const Client&) = delete;
	Client& operator=(const Client&) = delete;

	int Fd() const { return m_fd; }
	const std::string& Error() const { return m_error; }
	size_t InFlight() const { return m_pending.size(); }

	bool Send(Op op, uint32_t handle, uint64_t offset, uint64_t length, uint32_t flags, const void* body, size_t size,
		uint32_t* seq = nullptr);
	bool SendV(Op op, uint32_t handle, uint64_t offset, const struct iovec* iov, int n);
	/// No more requests: the sending side shut, the broker's answers let go until it ends (it carries
	/// out every request it got whole first), for at most wait_ms; then closed. false: not ended by then.
	bool Finish(int wait_ms);
	bool Receive(Reply& reply, std::vector<uint8_t>* body, int timeout_ms);
	int Call(Op op, uint32_t handle, uint64_t offset, uint64_t length, uint32_t flags, const void* body, size_t size,
		Reply& reply, std::vector<uint8_t>* out, int timeout_ms);

	int Hello(std::string& text, uint32_t& version, int timeout_ms);
	int Open(const std::string& path, uint32_t flags, uint32_t& handle, bool& direct);
	int Write(uint32_t handle, uint64_t offset, const void* data, size_t size, uint64_t& written);
	int Read(uint32_t handle, uint64_t offset, size_t size, std::vector<uint8_t>& out);
	int Truncate(uint32_t handle, uint64_t size);
	int Sync(uint32_t handle, int timeout_ms);
	int Close(uint32_t handle);
	int Unlink(const std::string& path);
	int Bench(const std::string& path, const BenchArgs& args, BenchResult& result, std::vector<BenchStep>& steps);
	int Quit();
	/// The broker copies length bytes (at most kMaxBody) of the file `from` at fromOffset into `to`
	/// at toOffset itself (protocol.hpp COPY). ENOSYS: a broker without COPY.
	int Copy(uint32_t from, uint64_t fromOffset, uint32_t to, uint64_t toOffset, uint64_t length, uint64_t& copied,
		int wait_ms);

	int timeout_ms = 30000; // a reply's wait, for the calls without their own

private:
	int m_fd;
	uint32_t m_seq = 1;
	std::deque<std::pair<uint32_t, uint16_t>> m_pending; // seq, op
	std::string m_error;
	bool Fail(const char* what);
};

/// Connects to the payload loader on 127.0.0.1:port and sends it the broker's ELF: the connection
/// (the broker's once the loader has started it), or -1 with why.
int Launch(const std::string& elf, std::string& why, unsigned port = 9021);
} // namespace psgopher::broker

namespace psgopher
{
/// The broker's connection (broker::Launch's, or a test's), or -1 with why.
using HelperConnector = std::function<int(std::string& why)>;

/// Whether the copies may use the helper: the `write_helper` setting (title_settings.cpp puts it in
/// PSGOPHER_WRITE_HELPER, "0" or "1"); on when the variable is not there.
// The title folder (main.cpp, once the sandbox is left): the helper is <folder>/write-broker.elf.
void helperSetTitleFolder(const char* title_dir);
bool helperEnabled();

/// Writes `size` bytes of `data` as the file `target` through the write broker, launching it at the
/// first call of a run (and again, a few times at most, if it ended). `root` is the folder the
/// console's /data stands for ("/data" on the console; the host tests' folder, which their broker
/// opens as <its root>/data). true: the file is there, whole (its size read back). false: not done
/// (`why` says why; the file may be partly written: the caller writes it itself and starts it anew).
/// Logs once "storage: copies through the helper (...)" or "storage: not through the helper: <why>".
bool helperWriteFile(const std::string& root, const std::string& target, const unsigned char* data, size_t size,
	std::string* why);

/// The host tests': another way to reach a broker than the payload loader; null: the loader again.
void helperUseConnector(HelperConnector connector);
/// The host tests': forget the connection and what was logged (a new run).
void helperReset();
} // namespace psgopher
