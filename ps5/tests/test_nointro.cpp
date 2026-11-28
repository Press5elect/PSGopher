/*
 * PSGopher - host test: a cartridge's No-Intro name by its game ID and revision (nointro.cpp), on the table
 * made from the No-Intro database Gopher64 carries (make-nointro-ids.py).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include "nointro.h"

#include <cstdarg>
#include <cstdio>

extern "C" void say(const char *format, ...)
{
	va_list a;
	va_start(a, format);
	vprintf(format, a);
	va_end(a);
	puts("");
}

static int g_failures;
#define CHECK(x)                                                                    \
	do {                                                                            \
		if (!(x)) {                                                                 \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #x);              \
			g_failures++;                                                           \
		}                                                                           \
	} while (0)

int main(int argc, char **argv)
{
	if (argc < 2)
		return 2;
	psgopher::noIntroUseFile(argv[1]);
	CHECK(psgopher::noIntroName("NSME", 0) == "Super Mario 64 (USA)");
	CHECK(psgopher::noIntroName("NSMJ", 3) == "Super Mario 64 (Japan) (Rev 3) (Shindou Edition)");
	CHECK(psgopher::noIntroName("NSMJ", 0) == "Super Mario 64 (Japan)");
	CHECK(psgopher::noIntroName("NREE", 1) == "Resident Evil 2 (USA) (Rev 1)");
	CHECK(psgopher::noIntroName("NREE", -1) == "Resident Evil 2 (USA)");
	CHECK(psgopher::noIntroName("NCFE", 0) == "Clay Fighter 63 1-3 (USA)"); /* the retail one, not its beta */
	CHECK(psgopher::noIntroName("XXXX", 0).empty());
	CHECK(psgopher::noIntroName("", 0).empty());
	psgopher::noIntroUseFile("/nonexistent/nointro-ids.tsv");
	CHECK(psgopher::noIntroName("NSME", 0).empty());
	std::printf(g_failures ? "test_nointro: %d failure(s)\n" : "test_nointro: all checks passed\n", g_failures);
	return g_failures ? 1 : 0;
}
