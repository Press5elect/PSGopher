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
#include <string>

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
	/* (games are not named here: the checks go by IDs and No-Intro's tags) */
	auto has = [](const std::string &name, const char *tag) { return name.find(tag) != std::string::npos; };
	const std::string usa = psgopher::noIntroName("NSME", 0), japan = psgopher::noIntroName("NSMJ", 0),
	                  japan3 = psgopher::noIntroName("NSMJ", 3), rev0 = psgopher::noIntroName("NREE", 0),
	                  rev1 = psgopher::noIntroName("NREE", 1), unknown = psgopher::noIntroName("NREE", -1),
	                  retail = psgopher::noIntroName("NCFE", 0);
	CHECK(has(usa, "(USA)") && !has(usa, "(Rev"));               /* the USA release, no revision */
	CHECK(has(japan, "(Japan)") && !has(japan, "(Rev"));         /* the destination letter picks the region */
	CHECK(has(japan3, "(Japan)") && has(japan3, "(Rev 3)"));     /* the revision byte picks the revision */
	CHECK(has(rev1, "(Rev 1)") && !has(rev0, "(Rev") && rev0 == unknown);
	CHECK(rev0.substr(0, rev0.find(" (")) == rev1.substr(0, rev1.find(" ("))); /* the same game */
	CHECK(!retail.empty() && !has(retail, "(Beta)"));            /* the retail release, not its beta */
	CHECK(psgopher::noIntroName("XXXX", 0).empty());
	CHECK(psgopher::noIntroName("", 0).empty());
	psgopher::noIntroUseFile("/nonexistent/nointro-ids.tsv");
	CHECK(psgopher::noIntroName("NSME", 0).empty());
	std::printf(g_failures ? "test_nointro: %d failure(s)\n" : "test_nointro: all checks passed\n", g_failures);
	return g_failures ? 1 : 0;
}
