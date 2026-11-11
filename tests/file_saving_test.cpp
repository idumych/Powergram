// SPDX-License-Identifier: GPL-3.0-or-later
// Saving received files on Haiku: the copy is whole, replaces what was
// there, and gets a type, sniffed or else the sender's.
#include <NodeInfo.h>
#include <Node.h>
#include <File.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../src/app/FileSaving.h"

static int sFailures = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
	sFailures++; } } while (0)

static std::string
TypeOf(const char* path)
{
	BNode node(path);
	BNodeInfo info(&node);
	char type[B_MIME_TYPE_LENGTH] = "";
	info.GetType(type);
	return type;
}


static void
Write(const char* path, const std::string& data)
{
	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	file.Write(data.data(), data.size());
}


static std::string
Read(const char* path)
{
	BFile file(path, B_READ_ONLY);
	off_t size = 0;
	file.GetSize(&size);
	std::string data(size, '\0');
	file.Read(&data[0], size);
	return data;
}


int
main()
{
	const char* dir = "/tmp/file_saving_test";
	mkdir(dir, 0755);
	std::string src = std::string(dir) + "/src", dst = std::string(dir) + "/out";

	// Bigger than the copy buffer, and not a multiple of it.
	std::string big;
	for (int i = 0; i < 700000; i++)
		big += (char)(i * 31 % 251);
	Write(src.c_str(), big);
	Write(dst.c_str(), std::string(2000000, 'x'));  // longer: must be cut
	CHECK(SaveCopy(src, dst, "application/x-made-up", true) == B_OK);
	CHECK(Read(dst.c_str()) == big);
	// Nothing to sniff: the sender's type.
	CHECK(TypeOf(dst.c_str()) == "application/x-made-up");

	// A PDF is sniffed as one, whatever the sender said.
	std::string pdf = std::string(dir) + "/doc.pdf";
	Write(src.c_str(), "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n1 0 obj\n<<>>\nendobj\ntrailer\n<<>>\n%%EOF\n");
	CHECK(SaveCopy(src, pdf, "application/octet-stream", true) == B_OK);
	CHECK(TypeOf(pdf.c_str()) == "application/pdf");

	// A TDLib download in place, typed by its name.
	std::string text = std::string(dir) + "/notes.txt";
	Write(text.c_str(), "plain words\n");
	GiveFileType(text.c_str(), "");
	CHECK(TypeOf(text.c_str()) == "text/plain");

	CHECK(SaveCopy(std::string(dir) + "/missing", dst, "", true) != B_OK);

	printf(sFailures == 0 ? "file saving tests passed\n" : "%d failures\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
