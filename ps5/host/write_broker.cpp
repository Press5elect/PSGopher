// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// PSGopher: from OGBox db63d46 (ps5/src/platform/ps5_broker.cpp: the write broker's client, without
// the storage write test), in namespace psgopher::broker.

#include "write_broker.h"

#include "../broker/posix.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace psgopher::broker
{
namespace
{
using ::broker::now_ns;

std::string Format(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Format(const char* format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	return line;
}
} // namespace

// ------------------------------------------------------------------------------------------ client

Client::~Client()
{
	if (m_fd >= 0)
		close(m_fd);
}

bool Client::Fail(const char* what)
{
	const int e = errno;
	m_error = Format("%s (errno %d: %s)", what, e, std::strerror(e));
	return false;
}

bool Client::Send(Op op, uint32_t handle, uint64_t offset, uint64_t length, uint32_t flags, const void* body, size_t size,
	uint32_t* seq)
{
	if (m_fd < 0)
	{
		errno = ENOTCONN;
		return Fail("no connection");
	}
	::broker::Request q;
	q.op = static_cast<uint16_t>(op);
	q.seq = m_seq++;
	q.handle = handle;
	q.offset = offset;
	q.length = length;
	q.size = static_cast<uint32_t>(size);
	q.flags = flags;
	if (!::broker::send_all(m_fd, &q, sizeof(q), timeout_ms) || (size != 0 && !::broker::send_all(m_fd, body, size, timeout_ms)))
		return Fail("a request could not be sent");
	m_pending.emplace_back(q.seq, q.op);
	if (seq)
		*seq = q.seq;
	return true;
}

bool Client::SendV(Op op, uint32_t handle, uint64_t offset, const struct iovec* iov, int n)
{
	if (m_fd < 0)
	{
		errno = ENOTCONN;
		return Fail("no connection");
	}
	size_t size = 0;
	for (int i = 0; i < n; i++)
		size += iov[i].iov_len;
	if (size > ::broker::kMaxBody)
	{
		errno = EMSGSIZE;
		return Fail("a request too large");
	}
	::broker::Request q;
	q.op = static_cast<uint16_t>(op);
	q.seq = m_seq++;
	q.handle = handle;
	q.offset = offset;
	q.size = static_cast<uint32_t>(size);
	bool ok = ::broker::send_all(m_fd, &q, sizeof(q), timeout_ms);
	for (int i = 0; ok && i < n; i++)
		ok = iov[i].iov_len == 0 || ::broker::send_all(m_fd, iov[i].iov_base, iov[i].iov_len, timeout_ms);
	if (!ok)
		return Fail("a request could not be sent");
	m_pending.emplace_back(q.seq, q.op);
	return true;
}

bool Client::Finish(int wait_ms)
{
	if (m_fd < 0)
		return true;
	shutdown(m_fd, SHUT_WR);
	const uint64_t until = now_ns() + static_cast<uint64_t>(wait_ms) * 1000000ull;
	bool ended = false;
	unsigned char sink[4096];
	while (!ended)
	{
		const uint64_t now = now_ns();
		if (now >= until)
			break;
		pollfd p{m_fd, POLLIN, 0};
		const int ready = poll(&p, 1, static_cast<int>(std::min<uint64_t>((until - now) / 1000000ull + 1, 1000)));
		if (ready < 0 && errno != EINTR)
			break;
		if (ready <= 0)
			continue;
		const ssize_t got = read(m_fd, sink, sizeof(sink));
		if (got < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		ended = got <= 0;   // EOF (or the connection reset: it is gone either way)
	}
	close(m_fd);
	m_fd = -1;
	m_pending.clear();
	return ended;
}

bool Client::Receive(Reply& reply, std::vector<uint8_t>* body, int wait_ms)
{
	if (m_pending.empty())
	{
		errno = EINVAL;
		return Fail("no request is waiting for its reply");
	}
	if (!::broker::recv_exact(m_fd, &reply, sizeof(reply), wait_ms))
		return Fail("no reply from the broker");
	const auto expected = m_pending.front();
	m_pending.pop_front();
	if (reply.magic != ::broker::kMagic || reply.version != ::broker::kVersion || reply.seq != expected.first ||
		reply.op != expected.second || reply.size > ::broker::kMaxBody + 4096)
	{
		m_error = Format("a reply out of order or not one (seq %u for %u, op %u for %u, size %u)", reply.seq, expected.first,
			reply.op, expected.second, reply.size);
		return false;
	}
	std::vector<uint8_t> sink;
	std::vector<uint8_t>& into = body ? *body : sink;
	into.resize(reply.size);
	if (reply.size != 0 && !::broker::recv_exact(m_fd, into.data(), reply.size, timeout_ms))
		return Fail("a reply's body broke off");
	return true;
}

int Client::Call(Op op, uint32_t handle, uint64_t offset, uint64_t length, uint32_t flags, const void* body, size_t size,
	Reply& reply, std::vector<uint8_t>* out, int wait_ms)
{
	if (!Send(op, handle, offset, length, flags, body, size) || !Receive(reply, out, wait_ms))
		return -1;
	return reply.error;
}

int Client::Hello(std::string& text, uint32_t& version, int wait_ms)
{
	Reply r;
	std::vector<uint8_t> body;
	const int e = Call(Op::hello, 0, 0, ::broker::kVersion, 0, nullptr, 0, r, &body, wait_ms);
	version = static_cast<uint32_t>(r.value);
	text.assign(body.begin(), body.end());
	return e;
}

int Client::Open(const std::string& path, uint32_t flags, uint32_t& handle, bool& direct)
{
	Reply r;
	const int e = Call(Op::open, 0, 0, 0, flags, path.data(), path.size(), r, nullptr, timeout_ms);
	handle = static_cast<uint32_t>(r.value);
	direct = r.flags & 1;
	return e;
}

int Client::Write(uint32_t handle, uint64_t offset, const void* data, size_t size, uint64_t& written)
{
	Reply r;
	const int e = Call(Op::write, handle, offset, 0, 0, data, size, r, nullptr, timeout_ms);
	written = r.value;
	return e;
}

int Client::Read(uint32_t handle, uint64_t offset, size_t size, std::vector<uint8_t>& out)
{
	Reply r;
	return Call(Op::read, handle, offset, size, 0, nullptr, 0, r, &out, timeout_ms);
}

int Client::Truncate(uint32_t handle, uint64_t size)
{
	Reply r;
	return Call(Op::truncate, handle, 0, size, 0, nullptr, 0, r, nullptr, timeout_ms);
}

int Client::Sync(uint32_t handle, int wait_ms)
{
	Reply r;
	return Call(Op::sync, handle, 0, 0, 0, nullptr, 0, r, nullptr, wait_ms);
}

int Client::Close(uint32_t handle)
{
	Reply r;
	return Call(Op::close, handle, 0, 0, 0, nullptr, 0, r, nullptr, timeout_ms);
}

int Client::Unlink(const std::string& path)
{
	Reply r;
	return Call(Op::unlink, 0, 0, 0, 0, path.data(), path.size(), r, nullptr, timeout_ms);
}

int Client::Bench(const std::string& path, const BenchArgs& args, BenchResult& result, std::vector<BenchStep>& steps)
{
	std::vector<uint8_t> body(sizeof(args) + path.size());
	std::memcpy(body.data(), &args, sizeof(args));
	std::memcpy(body.data() + sizeof(args), path.data(), path.size());
	Reply r;
	std::vector<uint8_t> out;
	// the broker answers when it is done: its time limit, its fsync, and room
	const int e = Call(Op::bench, 0, 0, 0, 0, body.data(), body.size(), r, &out, (static_cast<int>(args.seconds) + 300) * 1000);
	result = BenchResult{};
	steps.clear();
	if (e != 0)
		return e;
	if (out.size() < sizeof(result))
	{
		m_error = "a BENCH reply too short";
		return -1;
	}
	std::memcpy(&result, out.data(), sizeof(result));
	const size_t n = std::min<size_t>(result.steps, (out.size() - sizeof(result)) / sizeof(BenchStep));
	steps.resize(n);
	if (n)
		std::memcpy(steps.data(), out.data() + sizeof(result), n * sizeof(BenchStep));
	return 0;
}

int Client::Quit()
{
	Reply r;
	return Call(Op::quit, 0, 0, 0, 0, nullptr, 0, r, nullptr, timeout_ms);
}

int Client::Copy(uint32_t from, uint64_t fromOffset, uint32_t to, uint64_t toOffset, uint64_t length, uint64_t& copied,
	int wait_ms)
{
	::broker::CopyArgs a;
	a.from = from;
	a.from_offset = fromOffset;
	Reply r;
	const int e = Call(Op::copy, to, toOffset, length, 0, &a, sizeof(a), r, nullptr, wait_ms);
	copied = e < 0 ? 0 : r.value;
	return e;
}

int Launch(const std::string& elf, std::string& why, unsigned port)
{
	const int file = open(elf.c_str(), O_RDONLY);
	if (file < 0)
	{
		why = Format("%s can't be opened (errno %d: %s)", elf.c_str(), errno, std::strerror(errno));
		return -1;
	}
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		why = Format("no socket (errno %d: %s)", errno, std::strerror(errno));
		close(file);
		return -1;
	}
	const struct timeval timeout = {5, 0};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	struct sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<unsigned short>(port));
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address)) != 0)
	{
		why = Format("no payload loader at 127.0.0.1:%u (errno %d: %s)", port, errno, std::strerror(errno));
		close(fd);
		close(file);
		return -1;
	}
	const int one = 1, buffer = 4 << 20;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
	std::vector<uint8_t> chunk(64 << 10);
	size_t sent = 0;
	for (;;)
	{
		const ssize_t got = read(file, chunk.data(), chunk.size());
		if (got == 0)
			break;
		if (got < 0 || !::broker::send_all(fd, chunk.data(), static_cast<size_t>(got), 5000))
		{
			why = Format("sending %s to the payload loader broke off after %zu bytes (errno %d: %s)", elf.c_str(), sent, errno,
				std::strerror(errno));
			close(fd);
			close(file);
			return -1;
		}
		sent += static_cast<size_t>(got);
	}
	close(file);
	why = Format("%s (%zu bytes) sent to the payload loader at 127.0.0.1:%u", elf.c_str(), sent, port);
	return fd;
}

} // namespace psgopher::broker

// ------------------------------------------------------------------ the copy's use of the broker

#include <mutex>
#include <sys/stat.h>
#include <thread>

extern "C" void say(const char* format, ...) __attribute__((format(printf, 1, 2)));

namespace psgopher
{
namespace
{
constexpr const char* kBrokerElf = "/app0/write-broker.elf";
constexpr int kHelloMs = 8000;     // the loader starts the payload: its first answer
constexpr int kReplyMs = 120000;   // one message's answer (16 MiB at the held-back rate takes about 1.5 s)
constexpr int kSyncMs = 120000;
constexpr int kFinishMs = 30000;   // a failed connection: what the broker was sent is carried out first
constexpr size_t kMessage = 16u << 20;
constexpr unsigned kMaxLaunches = 3; // a run: the first, and again if it ended (idle for 10 minutes)

struct State
{
	std::mutex mutex;
	std::unique_ptr<broker::Client> client;
	unsigned launches = 0;
	std::string logged;   // the last "not through the helper" reason said
	HelperConnector connector;
};

State& state()
{
	static State* s = new State; // (never destroyed: a thread may be in it at the title's end)
	return *s;
}

void notThrough(State& s, const std::string& why)
{
	if (s.logged == why)
		return;
	s.logged = why;
	say("storage: not through the helper: %s", why.c_str());
}

// The path the broker is given: the console's /data (a host test's folder stands for it).
std::string brokerPath(const std::string& root, const std::string& target)
{
	if (!root.empty() && root != "/data" && target.compare(0, root.size(), root) == 0)
		return "/data" + target.substr(root.size());
	return target;
}

// A connection that works: the one kept (asked for a sign of life when it has been idle), or a new one.
bool connect(State& s, std::string& why)
{
	if (s.client)
	{
		std::string text;
		uint32_t version = 0;
		if (s.client->Hello(text, version, 5000) == 0)
			return true;
		s.client->Finish(kFinishMs);
		s.client.reset();
	}
	if (s.launches >= kMaxLaunches)
	{
		why = "the helper ended more than once this run: not started again";
		return false;
	}
	if (!s.connector && access(kBrokerElf, R_OK) != 0)
	{
		why = std::string(kBrokerElf) + " is missing from the title folder";
		return false;
	}
	s.launches++;
	std::string launched;
	int fd = -1;
	for (int attempt = 0; attempt < 2 && fd < 0; attempt++)
	{
		if (attempt != 0)
			std::this_thread::sleep_for(std::chrono::seconds(1)); // (the loader may be busy)
		fd = s.connector ? s.connector(launched) : broker::Launch(kBrokerElf, launched);
	}
	if (fd < 0)
	{
		why = "the helper could not be started: " + launched;
		return false;
	}
	auto client = std::make_unique<broker::Client>(fd);
	client->timeout_ms = kReplyMs;
	std::string text;
	uint32_t version = 0;
	const int e = client->Hello(text, version, kHelloMs);
	if (e != 0 || version != ::broker::kVersion)
	{
		why = e != 0 ? "the helper didn't answer (" + (e < 0 ? client->Error() : std::string(std::strerror(e))) + ")"
		             : "the helper is of another version (" + std::to_string(version) + ")";
		return false;
	}
	s.client = std::move(client);
	say("storage: copies through the helper (%s; %s)", launched.c_str(), text.c_str());
	s.logged.clear();
	return true;
}

int writeThrough(broker::Client& c, const std::string& path, const unsigned char* data, size_t size)
{
	uint32_t handle = 0;
	bool direct = false;
	int e = c.Open(path, ::broker::open_write | ::broker::open_create | ::broker::open_truncate, handle, direct);
	if (e != 0)
		return e;
	for (size_t at = 0; at < size && e == 0;)
	{
		const size_t n = std::min(kMessage, size - at);
		uint64_t written = 0;
		e = c.Write(handle, at, data + at, n, written);
		if (e == 0 && written != n)
			e = EIO;
		at += n;
	}
	if (e == 0)
		e = c.Sync(handle, kSyncMs);
	if (e < 0)
		return e; // the connection is gone: the caller ends it
	const int closed = c.Close(handle);
	return e != 0 ? e : closed;
}
} // namespace

bool helperEnabled()
{
	const char* v = getenv("PSGOPHER_WRITE_HELPER");
	return !v || strcmp(v, "0") != 0;
}

void helperUseConnector(HelperConnector connector)
{
	State& s = state();
	std::lock_guard lock(s.mutex);
	s.connector = std::move(connector);
}

void helperReset()
{
	State& s = state();
	std::lock_guard lock(s.mutex);
	if (s.client)
		s.client->Finish(kFinishMs);
	s.client.reset();
	s.launches = 0;
	s.logged.clear();
}

bool helperWriteFile(const std::string& root, const std::string& target, const unsigned char* data, size_t size,
	std::string* why)
{
	State& s = state();
	std::lock_guard lock(s.mutex);
	std::string reason;
	const std::string path = brokerPath(root, target);
	if (!helperEnabled())
		reason = "write_helper is off (psgopher.ini)";
	else if (path.compare(0, 6, "/data/") != 0)
		reason = "the destination (" + target + ") isn't under /data/";
	else if (!::broker::path_allowed(path.data(), path.size()))
		reason = "the destination (" + target + ") isn't a path the helper takes";
	else if (connect(s, reason))
	{
		const int e = writeThrough(*s.client, path, data, size);
		struct stat st;
		if (e == 0 && stat(target.c_str(), &st) == 0 && static_cast<uint64_t>(st.st_size) == size)
			return true;
		if (e != 0)
			reason = e < 0 ? "the helper's connection failed (" + s.client->Error() + ")"
			               : std::string("the helper's write failed: ") + std::strerror(e);
		else
			reason = "the file written by the helper is not " + std::to_string(size) + " bytes";
		// it carries out what it was sent before it ends: nothing of it writes the file once we do
		s.client->Finish(kFinishMs);
		s.client.reset();
	}
	notThrough(s, reason);
	if (why)
		*why = reason;
	return false;
}
} // namespace psgopher
